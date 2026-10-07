// AC Customs native runtime
//
// This x86 DLL is loaded by the managed Decal plugin and contains the low-level
// Asheron's Call texture/runtime hooks used by theme application and Live Mirror.
// Many addresses and structure offsets below are reverse-engineered against the
// supported 32-bit client. Keep address-dependent changes isolated and document
// new discoveries in docs/REVERSE_ENGINEERING.md.
//
// IMPORTANT: Build x86. The managed bridge and target client are 32-bit.

#include <Windows.h>
#include <intrin.h>

#include <algorithm>

#include <atomic>

#include <cstdint>

#include <cstring>

#include <fstream>

#include <iomanip>

#include <mutex>

#include <sstream>

#include <string>

#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>



#include "MinHook.h"



static std::string ACCustomsLocalAppDataRoot()
{
    char localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA(
        "LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(sizeof(localAppData)));

    if (length == 0 || length >= sizeof(localAppData))
        return std::string(".\\ACCustoms");

    return std::string(localAppData) + "\\ACCustoms";
}

// Runtime directories are configurable by the managed Decal plugin. These
// LOCALAPPDATA defaults keep standalone/developer use off machine-specific
// source paths even before the managed layer supplies explicit directories.
static std::mutex g_ACCustomsPathMutex;
static std::string g_ACCustomsCaptureDirectory =
    ACCustomsLocalAppDataRoot() + "\\Runtime\\Captured";
static std::string g_ACCustomsReplacementDirectory =
    ACCustomsLocalAppDataRoot() + "\\User\\textures";

static std::string ACCustomsGetCaptureDirectory()
{
    std::lock_guard<std::mutex> lock(g_ACCustomsPathMutex);
    return g_ACCustomsCaptureDirectory;
}

static std::string ACCustomsGetReplacementDirectory()
{
    std::lock_guard<std::mutex> lock(g_ACCustomsPathMutex);
    return g_ACCustomsReplacementDirectory;
}

// Theme Apply/Restore diagnostics are intentionally independent of the
// Developer Tests log. A normal user should always get a useful failure
// report when a theme operation is incomplete.
static std::mutex g_ThemeDiagnosticMutex;
static std::string g_LastThemeOperationReport;

static std::string ACCustomsThemeDiagnosticPath()
{
    char localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA(
        "LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(sizeof(localAppData)));

    if (length == 0 || length >= sizeof(localAppData))
        return std::string();

    const std::string root = std::string(localAppData) + "\\ACCustoms";
    const std::string logs = root + "\\Logs";

    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(logs.c_str(), nullptr);

    return logs + "\\theme_changes.log";
}

static std::string ACCustomsThemeTimestamp()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    char buffer[64] = {};
    sprintf_s(
        buffer,
        sizeof(buffer),
        "%04u-%02u-%02u %02u:%02u:%02u",
        static_cast<unsigned>(st.wYear),
        static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay),
        static_cast<unsigned>(st.wHour),
        static_cast<unsigned>(st.wMinute),
        static_cast<unsigned>(st.wSecond));
    return buffer;
}

static void ACCustomsRecordThemeReport(const std::string& report)
{
    std::lock_guard<std::mutex> lock(g_ThemeDiagnosticMutex);
    g_LastThemeOperationReport = report;

    const std::string path = ACCustomsThemeDiagnosticPath();
    if (path.empty())
        return;

    std::ofstream out(path, std::ios::out | std::ios::app);
    if (!out.is_open())
        return;

    out << "[" << ACCustomsThemeTimestamp() << "] " << report << std::endl;
}



static std::string ACCustomsMetadataPath()
{
    return ACCustomsGetCaptureDirectory() + "\\textures.csv";
}

static std::string ACCustomsEncounteredPath()
{
    return ACCustomsLocalAppDataRoot() + "\\Runtime\\encountered_textures.csv";
}

static std::string ACCustomsDeveloperLogPath()
{
    const std::string root = ACCustomsLocalAppDataRoot();
    const std::string logs = root + "\\Logs";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(logs.c_str(), nullptr);
    return logs + "\\ACModernUI.log";
}

static std::mutex g_LogMutex;

static std::mutex g_MetadataMutex;
static std::mutex g_EncounteredMutex;
static std::unordered_set<std::uint32_t> g_EncounteredDIDs;
static bool g_EncounteredLoaded = false;

static constexpr std::uint32_t LIVE_SWAP_TEST_DID = 0x06001119u;
static std::mutex g_LiveSwapMutex;

struct LiveTextureEntry
{
    void* renderSurface;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t imageSize;
    std::uint32_t pixelFormat;
    std::uint32_t formatInfo;
};

static std::mutex g_LiveTextureRegistryMutex;
static std::unordered_map<std::uint32_t, LiveTextureEntry> g_LiveTextureRegistry;

// DIDs whose currently-live surfaces were actually modified by the active
// theme. Restore operates on this set rather than every file in the pack.
// This prevents false "missing original" failures for textures that loaded
// after the engine had already switched back to Vanilla and were never themed.
static std::mutex g_ActiveAppliedDidsMutex;
static std::unordered_set<std::uint32_t> g_ActiveAppliedDids;

static void MarkActiveAppliedDid(std::uint32_t did)
{
    std::lock_guard<std::mutex> lock(g_ActiveAppliedDidsMutex);
    g_ActiveAppliedDids.insert(did);
}

static void ForgetActiveAppliedDid(std::uint32_t did)
{
    std::lock_guard<std::mutex> lock(g_ActiveAppliedDidsMutex);
    g_ActiveAppliedDids.erase(did);
}

static std::vector<std::uint32_t> SnapshotActiveAppliedDids()
{
    std::lock_guard<std::mutex> lock(g_ActiveAppliedDidsMutex);
    return std::vector<std::uint32_t>(
        g_ActiveAppliedDids.begin(),
        g_ActiveAppliedDids.end());
}

static void ClearActiveAppliedDids()
{
    std::lock_guard<std::mutex> lock(g_ActiveAppliedDidsMutex);
    g_ActiveAppliedDids.clear();
}

static void* g_LiveSwapRenderSurface = nullptr;

// Probe #46: follow the known War Magic replacement RenderSurface through
// renderer lock/upload activity during the natural close -> open rebuild.
static constexpr std::uint32_t PROBE46_TARGET_DID = 0x06001365u;
static std::atomic<std::uint32_t> g_Probe46LockCount(0);
static std::atomic<std::uint32_t> g_Probe46UploadCount(0);
static std::atomic<bool> g_Probe53Captured(false);
static std::uint32_t g_LiveSwapImageSize = 0;
static bool g_LiveSwapShowingReplacement = true;
static std::atomic<bool> g_LiveSwapStop(false);

// Probe #88: persistent desired UI state. This is intentionally an enum rather
// than a boolean so the same decision point can later become a real ActivePack
// resolver without changing the load-time interception architecture.
enum class ActiveThemeMode : std::uint32_t
{
    Vanilla = 0,
    TestReplacement = 1
};

// AC Customs safe-mode rule: every process starts VANILLA.  The Decal plugin
// may remember which pack was selected, but native replacement state is never
// persisted across launches.  Apply must be explicitly requested each session.
static std::atomic<ActiveThemeMode> g_ActiveThemeMode(
    ActiveThemeMode::Vanilla);

// Decal proof-of-concept control state. Native initialization is explicit and
// idempotent; DllMain does not install hooks. The managed plugin calls
// ACCustoms_Initialize during Startup and then drives Apply/Restore by exports.
static std::mutex g_ACCustomsControlMutex;
static std::atomic<bool> g_ACCustomsInitialized(false);
static std::atomic<bool> g_ACCustomsInitializationFailed(false);
static std::atomic<bool> g_ACCustomsApplyInProgress(false);
static std::atomic<bool> g_DeveloperTestsEnabled(false);
static void* g_CoreDrawHookTarget = nullptr;
static std::atomic<bool> g_CoreDrawDisableScheduled(false);

// Probe #92: every explicit theme selection advances a generation. Previously
// known item caches are rebuilt in bulk during F8/F9, while never-before-seen
// item widgets lazily repair their own cache at 0x004E2D10 before AC binds the
// generated icon. Generation 0 means no explicit F8/F9 selection has occurred.
static std::atomic<std::uint32_t> g_Probe92ThemeGeneration(0);
static std::atomic<std::uint32_t> g_Probe92LazyItemChecks(0);
static std::atomic<std::uint32_t> g_Probe92LazyItemRebuilds(0);

// Probe #90: retain every item object ID learned from item widgets even when
// the client recycles those widgets while changing bags. F8/F9 rebuild caches
// for the persistent learned-item registry, then refresh currently materialized
// owners before invalidating backed UI roots. F10 is intentionally unused;
// F12 remains a manual cache-refresh diagnostic.
// Probe #92 additionally repairs never-before-seen item caches lazily when their
// item widget first runs the common refresh path after a theme generation change.






// ------------------------------------------------------------

// AsyncCache::SerializeFromCachePack

// RVA 0x17AC0

// ------------------------------------------------------------



using SerializeFromCachePackFn =

    bool (__thiscall*)(

        void* thisPtr,

        void* arg1,

        void* arg2

    );



static SerializeFromCachePackFn

    g_OriginalSerializeFromCachePack = nullptr;





// Forward declarations used by the probe #8 hook. Their definitions
// appear later in this translation unit.
static bool ProbeReadableRange(const void* address, std::size_t size);
static std::uint32_t ReadUInt32(const void* base, std::size_t offset);
static std::string Hex32(std::uint32_t value);
static void WriteLog(const std::string& message);
static void RegisterLiveTexture(std::uint32_t did, void* renderSurface);


// Probe #21: UI lock/unlock path discovered in this exact acclient.exe.
// @lockui handler RVA 0x171180 calls UI-lock setter RVA 0x1D4340,
// then calls RVA 0x5B4C0 with (0x0D, 0).
using UiLockSetterFn = void (__thiscall*)(void* thisPtr, bool locked);
static UiLockSetterFn g_OriginalUiLockSetter = nullptr;

using UiPostLockFn = void (__thiscall*)(void* thisPtr, std::uint32_t eventId, std::uint32_t arg);
static UiPostLockFn g_OriginalUiPostLock = nullptr;

static std::atomic<std::uint32_t> g_Probe21LockSequence(0);

// Probe #24: armed observation of VA 0x0059B8A0 (RVA 0x19B8A0).
// Static analysis shows this __thiscall function takes two stack arguments,
// calls RVA 0x7A0B0, then sets [this+0x1A4]=1 and snapshots two globals
// into +0x1AC/+0x1B0. This is a stronger dirty/invalidation candidate.
using UiDirtyCandidateFn = void (__thiscall*)(void* thisPtr, std::uint32_t arg1, std::uint32_t arg2);
static UiDirtyCandidateFn g_OriginalUiDirtyCandidate = nullptr;
static std::atomic<DWORD> g_Probe23ArmedUntil(0);
static std::atomic<std::uint32_t> g_Probe23EventCount(0);

static void __fastcall HookedUiDirtyCandidate(void* thisPtr, void*, std::uint32_t arg1, std::uint32_t arg2)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    const std::uint32_t n = armed ? ++g_Probe23EventCount : 0;
    std::uint8_t dirtyBefore = 0xFF;
    if (armed && thisPtr && ProbeReadableRange(static_cast<unsigned char*>(thisPtr) + 0x1A4, 1))
        dirtyBefore = *(static_cast<unsigned char*>(thisPtr) + 0x1A4);
    void* caller = armed ? _ReturnAddress() : nullptr;
    g_OriginalUiDirtyCandidate(thisPtr, arg1, arg2);
    if (armed && n <= 300)
    {
        std::uint8_t dirtyAfter = 0xFF;
        if (thisPtr && ProbeReadableRange(static_cast<unsigned char*>(thisPtr) + 0x1A4, 1))
            dirtyAfter = *(static_cast<unsigned char*>(thisPtr) + 0x1A4);
        WriteLog(
            "PROBE #24 DIRTY n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " arg1=" + Hex32(arg1) + " arg2=" + Hex32(arg2) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))) +
            " dirty=" + std::to_string(dirtyBefore) + "->" + std::to_string(dirtyAfter));
    }
}



// Probe #26: inspect the listener list used by RVA 0x7A0B0 before dispatch.
// Exact client disassembly shows 0x47A0B0 obtains a registry via RVA 0x7A810,
// looks up key 0x004DD26E through registry vtable +0x10, then walks the list
// at result+4. Eligible listeners receive a virtual callback at vtable +0x22C.
using UiRegistryGetterFn = void* (__cdecl*)();
static UiRegistryGetterFn g_UiRegistryGetter = nullptr;
static std::atomic<std::uint32_t> g_Probe26SnapshotCount(0);

static void Probe26SnapshotListeners(std::uint32_t arg1, std::uint32_t arg2)
{
    if (!g_UiRegistryGetter)
        return;

    void* registry = g_UiRegistryGetter();
    if (!registry || !ProbeReadableRange(registry, sizeof(void*)))
        return;

    void** registryVtable = *reinterpret_cast<void***>(registry);
    if (!registryVtable || !ProbeReadableRange(registryVtable, 0x14))
        return;

    using LookupFn = void* (__thiscall*)(void*, std::uint32_t);
    LookupFn lookup = reinterpret_cast<LookupFn>(registryVtable[4]); // +0x10
    if (!lookup)
        return;

    void* bucket = lookup(registry, 0x004DD26Eu);
    if (!bucket || !ProbeReadableRange(static_cast<unsigned char*>(bucket) + 4, sizeof(void*)))
        return;

    void* node = *reinterpret_cast<void**>(static_cast<unsigned char*>(bucket) + 4);
    std::uint32_t index = 0;
    while (node && index < 64)
    {
        if (!ProbeReadableRange(node, 8))
            break;

        void* listener = *reinterpret_cast<void**>(node);
        void* next = *reinterpret_cast<void**>(static_cast<unsigned char*>(node) + 4);
        if (listener && ProbeReadableRange(listener, sizeof(void*)))
        {
            void** vt = *reinterpret_cast<void***>(listener);
            std::uintptr_t callback = 0;
            std::uintptr_t filter = 0;
            if (vt && ProbeReadableRange(vt, 0x230))
            {
                filter = reinterpret_cast<std::uintptr_t>(vt[0]);
                callback = reinterpret_cast<std::uintptr_t>(vt[0x22C / 4]);
            }
            WriteLog(
                "PROBE #26 LISTENER i=" + std::to_string(index) +
                " listener=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(listener))) +
                " vtable=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(vt))) +
                " filter=" + Hex32(static_cast<std::uint32_t>(filter)) +
                " callback22C=" + Hex32(static_cast<std::uint32_t>(callback)) +
                " arg1=" + Hex32(arg1) + " arg2=" + Hex32(arg2));
        }
        node = next;
        ++index;
    }
}

// Probe #25: verified dispatcher called by the dirty/invalidation virtual at 0x59B8A0.
// Static disassembly: RVA 0x7A0B0 enumerates registered UI listeners and invokes
// listener vtable slot +0x22C with these same two arguments.
using UiDispatchFn = bool (__cdecl*)(std::uint32_t arg1, std::uint32_t arg2);
static UiDispatchFn g_OriginalUiDispatch = nullptr;
static std::atomic<std::uint32_t> g_Probe25DispatchCount(0);

static bool __cdecl HookedUiDispatch(std::uint32_t arg1, std::uint32_t arg2)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    const std::uint32_t n = armed ? ++g_Probe25DispatchCount : 0;
    void* caller = armed ? _ReturnAddress() : nullptr;
    if (armed && n <= 32)
        Probe26SnapshotListeners(arg1, arg2);
    const bool result = g_OriginalUiDispatch(arg1, arg2);
    if (armed && n <= 300)
    {
        WriteLog(
            "PROBE #25 DISPATCH n=" + std::to_string(n) +
            " arg1=" + Hex32(arg1) + " arg2=" + Hex32(arg2) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))) +
            " result=" + std::to_string(result ? 1 : 0));
    }
    return result;
}



// Probe #27: listener callback implementations observed by Probe #26.
// Both are __thiscall and return with RET 8, so they receive the same two
// 32-bit arguments dispatched by RVA 0x7A0B0. Observational only.
using UiListenerCallbackFn = void (__thiscall*)(void* thisPtr, std::uint32_t arg1, std::uint32_t arg2);
static UiListenerCallbackFn g_OriginalUiListenerCommon = nullptr;
static UiListenerCallbackFn g_OriginalUiListenerSpecial = nullptr;
static std::atomic<std::uint32_t> g_Probe27CommonCount(0);
static std::atomic<std::uint32_t> g_Probe27SpecialCount(0);

static void Probe27LogCallback(const char* kind, std::uint32_t n, void* thisPtr,
    std::uint32_t arg1, std::uint32_t arg2, void* caller, bool before)
{
    if (n > 300) return;
    WriteLog(
        std::string("PROBE #27 ") + kind + (before ? " ENTER" : " EXIT") +
        " n=" + std::to_string(n) +
        " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
        " arg1=" + Hex32(arg1) +
        " arg2=" + Hex32(arg2) +
        " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))));
}

static void __fastcall HookedUiListenerCommon(void* thisPtr, void*, std::uint32_t arg1, std::uint32_t arg2)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    const std::uint32_t n = armed ? ++g_Probe27CommonCount : 0;
    void* caller = armed ? _ReturnAddress() : nullptr;
    if (armed) Probe27LogCallback("COMMON", n, thisPtr, arg1, arg2, caller, true);
    g_OriginalUiListenerCommon(thisPtr, arg1, arg2);
    if (armed) Probe27LogCallback("COMMON", n, thisPtr, arg1, arg2, caller, false);
}

static void __fastcall HookedUiListenerSpecial(void* thisPtr, void*, std::uint32_t arg1, std::uint32_t arg2)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    const std::uint32_t n = armed ? ++g_Probe27SpecialCount : 0;
    void* caller = armed ? _ReturnAddress() : nullptr;
    if (armed) Probe27LogCallback("SPECIAL", n, thisPtr, arg1, arg2, caller, true);
    g_OriginalUiListenerSpecial(thisPtr, arg1, arg2);
    if (armed) Probe27LogCallback("SPECIAL", n, thisPtr, arg1, arg2, caller, false);
}

// Probe #28: broad UI-event type observation.
// Exact client VA 0x00429A00 / RVA 0x29A00 is a tiny __thiscall getter:
//   event object -> descriptor -> type at descriptor+0x08.
// We only log while explicitly armed so we can compare Stats/Abilities open/close
// against the previously verified chat path without guessing another redraw target.
using UiEventTypeFn = std::uint32_t (__thiscall*)(void* eventPtr);
static UiEventTypeFn g_OriginalUiEventType = nullptr;
static std::atomic<std::uint32_t> g_Probe28EventTypeCount(0);

static std::uint32_t __fastcall HookedUiEventType(void* eventPtr, void*)
{
    const std::uint32_t result = g_OriginalUiEventType(eventPtr);

    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    if (!armed)
        return result;

    const std::uint32_t n = ++g_Probe28EventTypeCount;
    if (n <= 600)
    {
        void* caller = _ReturnAddress();
        std::uint32_t descriptor = 0;
        if (eventPtr && ProbeReadableRange(eventPtr, sizeof(void*)))
            descriptor = ReadUInt32(eventPtr, 0x00);

        WriteLog(
            "PROBE #28 EVENTTYPE n=" + std::to_string(n) +
            " event=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(eventPtr))) +
            " descriptor=" + Hex32(descriptor) +
            " type=" + Hex32(result) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))));
    }

    return result;
}







// Probe #36: identify the class-specific consumer of the 0x10000199 event.
// Exact-client static analysis:
//   vtable 0x0079E280 + 0x0C -> 0x004725D0
//   0x4725D0 checks event+4 == this and event+8 == 1.
//   If state key 0x0D is false, it calls 0x00472210(this, event).
// We observe both functions only; no calls are forced.
using UiEventConsumerFn = std::uint32_t (__thiscall*)(void* thisPtr, void* eventRecord);
using UiEventActionFn   = bool (__thiscall*)(void* thisPtr, void* eventRecord);

static UiEventConsumerFn g_OriginalUiEventConsumer = nullptr;
static UiEventActionFn g_OriginalUiEventAction = nullptr;
static std::atomic<std::uint32_t> g_Probe36ConsumerCount(0);
static std::atomic<std::uint32_t> g_Probe36ActionCount(0);
// Probe #37 controlled active test.
// Static disassembly of 0x472210 confirms its stack event argument is never read.
// The routine uses ECX (the captured 0x79E280 object), queries key 0x12, and if
// eligible constructs/dispatches its own downstream event. We retain only the
// live object pointer from a naturally observed successful 0x472210 call.
static std::atomic<std::uintptr_t> g_Probe37CapturedTarget(0);
static std::atomic<bool> g_Probe37RefreshRequested(false);


static std::string Probe36EventSummary(void* eventRecord)
{
    if (!eventRecord || !ProbeReadableRange(eventRecord, 0x14))
        return " event=<unreadable>";

    return
        " event=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(eventRecord))) +
        " field0=" + Hex32(ReadUInt32(eventRecord, 0x00)) +
        " owner=" + Hex32(ReadUInt32(eventRecord, 0x04)) +
        " a=" + Hex32(ReadUInt32(eventRecord, 0x08)) +
        " b=" + Hex32(ReadUInt32(eventRecord, 0x0C)) +
        " c=" + Hex32(ReadUInt32(eventRecord, 0x10));
}

static std::uint32_t __fastcall HookedUiEventConsumer(void* thisPtr, void*, void* eventRecord)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    std::uint32_t n = 0;

    if (armed)
    {
        n = ++g_Probe36ConsumerCount;
        WriteLog(
            "PROBE #36 CONSUMER4725D0 ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            Probe36EventSummary(eventRecord) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const std::uint32_t result = g_OriginalUiEventConsumer(thisPtr, eventRecord);

    if (armed)
    {
        WriteLog(
            "PROBE #36 CONSUMER4725D0 EXIT n=" + std::to_string(n) +
            " result=" + Hex32(result));
    }

    return result;
}

static bool __fastcall HookedUiEventAction(void* thisPtr, void*, void* eventRecord)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    std::uint32_t n = 0;

    if (armed)
    {
        n = ++g_Probe36ActionCount;
        WriteLog(
            "PROBE #36 ACTION472210 ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            Probe36EventSummary(eventRecord) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const bool result = g_OriginalUiEventAction(thisPtr, eventRecord);

    if (armed && result && thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
    {
        const std::uint32_t vt = ReadUInt32(thisPtr, 0x00);
        if (vt == 0x0079E280)
        {
            g_Probe37CapturedTarget.store(reinterpret_cast<std::uintptr_t>(thisPtr));
            WriteLog("PROBE #37 CAPTURE target=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                     " from natural successful 0x472210 call.");
        }
    }

    if (armed)
    {
        WriteLog(
            "PROBE #36 ACTION472210 EXIT n=" + std::to_string(n) +
            " result=" + std::to_string(result ? 1 : 0));
    }

    return result;
}

// Probe #35: observe the dispatcher used by 0x460410.
// Exact-client static disassembly shows 0x460410 builds a 0x28-byte event record:
//   +0x00 = this+0x2E4
//   +0x04 = source/owner this
//   +0x08 = arg1
//   +0x0C = arg2
//   +0x10 = arg3
// then calls 0x45AC50 with ECX = global dispatcher at [0x0083E03C].
// 0x45AC50 is bool __thiscall(dispatcher, eventRecord*), RET 4.
// Observational only.
using UiEventDispatchFn = bool (__thiscall*)(void* dispatcher, void* eventRecord);
static UiEventDispatchFn g_OriginalUiEventDispatch = nullptr;
static std::atomic<std::uint32_t> g_Probe35DispatchCount(0);

static bool __fastcall HookedUiEventDispatch(void* dispatcher, void*, void* eventRecord)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);

    std::uint32_t n = 0;
    std::uint32_t owner = 0;
    std::uint32_t ownerVtable = 0;
    std::uint32_t field0 = 0;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;

    if (armed)
    {
        n = ++g_Probe35DispatchCount;

        if (eventRecord && ProbeReadableRange(eventRecord, 0x14))
        {
            field0 = ReadUInt32(eventRecord, 0x00);
            owner  = ReadUInt32(eventRecord, 0x04);
            a      = ReadUInt32(eventRecord, 0x08);
            b      = ReadUInt32(eventRecord, 0x0C);
            c      = ReadUInt32(eventRecord, 0x10);

            void* ownerPtr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(owner));
            if (ownerPtr && ProbeReadableRange(ownerPtr, sizeof(std::uint32_t)))
                ownerVtable = ReadUInt32(ownerPtr, 0x00);
        }

        WriteLog(
            "PROBE #35 DISPATCH ENTER n=" + std::to_string(n) +
            " dispatcher=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dispatcher))) +
            " event=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(eventRecord))) +
            " field0=" + Hex32(field0) +
            " owner=" + Hex32(owner) +
            " ownerVtable=" + Hex32(ownerVtable) +
            " a=" + Hex32(a) +
            " b=" + Hex32(b) +
            " c=" + Hex32(c) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const bool result = g_OriginalUiEventDispatch(dispatcher, eventRecord);

    if (armed)
    {
        WriteLog(
            "PROBE #35 DISPATCH EXIT n=" + std::to_string(n) +
            " owner=" + Hex32(owner) +
            " a=" + Hex32(a) +
            " b=" + Hex32(b) +
            " c=" + Hex32(c) +
            " result=" + std::to_string(result ? 1 : 0));
    }

    return result;
}

// Probe #41: clean correlation of the confirmed 0x464090 -> 0x463830
// propagation chain. No 0x462390 hook.
//
// We log every armed 0x464090/0x463830 call, but classify whether it is the
// confirmed Abilities root vtable (0x79E280) or one of the dependent classes.
// We also capture caller and parent nesting depth. This is observational only.

static thread_local std::uint32_t g_Probe41PropDepth = 0;
static std::atomic<std::uint32_t> g_Probe41Count(0);

static const char* Probe41ClassName(std::uint32_t vt)
{
    switch (vt)
    {
    case 0x0079E280: return "ABILITIES79E280";
    case 0x0079DA08: return "DEP79DA08";
    case 0x007A8E48: return "DEP7A8E48";
    case 0x007A8718: return "DEP7A8718";
    case 0x007BC450: return "DEP7BC450";
    default: return "OTHER";
    }
}

static void* g_Probe44Abilities = nullptr;

static volatile LONG g_Probe45AbilitiesTransitions = 0;
static std::uint32_t g_Probe45LastAbilitiesArg = 0xFFFFFFFFu;

static void Probe45MarkAbilitiesTransition(
    void* thisPtr,
    std::uint32_t arg,
    std::uint32_t flagsA4)
{
    const LONG seq = InterlockedIncrement(&g_Probe45AbilitiesTransitions);
    const char* phase = "OTHER";
    const std::uint32_t low = arg & 0xFFu;

    // 0x1900 has low byte zero in 0x463830; arg 1 is the complementary path.
    if (low == 0)
        phase = "LOWBYTE_ZERO";
    else if (low == 1)
        phase = "LOWBYTE_ONE";

    WriteLog(
        "PROBE #45 DIFF ABILITIES transition=" + std::to_string(seq) +
        " phase=" + phase +
        " this=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(thisPtr))) +
        " arg=" + Hex32(arg) +
        " flagsA4=" + Hex32(flagsA4) +
        " previousArg=" + Hex32(g_Probe45LastAbilitiesArg));

    g_Probe45LastAbilitiesArg = arg;
}

// Probe #42: resolve the concrete virtual targets used inside 0x463830
// for the confirmed Abilities object. Observational only; no new function is
// called and no widget state is modified.
//
// Exact-client static analysis of 0x463830 showed calls through virtual slots
// +0xFC, +0x100, +0x104, +0x118 and +0x11C.  We snapshot those entries from
// the live 0x79E280 vtable during a natural Abilities transition.

static std::uint32_t Probe42ReadVfunc(std::uint32_t vtable, std::uint32_t slot)
{
    const void* p = reinterpret_cast<const void*>(
        static_cast<std::uintptr_t>(vtable + slot));
    if (!ProbeReadableRange(p, sizeof(std::uint32_t)))
        return 0;
    return *reinterpret_cast<const std::uint32_t*>(p);
}

static void Probe42LogAbilitiesVirtuals(
    void* thisPtr,
    std::uint32_t vtable,
    std::uint32_t arg,
    std::uint32_t flagsA4)
{
    if (vtable == 0x0079E280)
    {
        g_Probe44Abilities = thisPtr;
        Probe45MarkAbilitiesTransition(thisPtr, arg, flagsA4);

        const DWORD p49Until = g_Probe23ArmedUntil.load();
        if ((arg & 0xFFu) != 0 && p49Until != 0 &&
            static_cast<LONG>(p49Until - GetTickCount()) > 0 &&
            ProbeReadableRange(thisPtr, 0x558))
        {
            const std::uint32_t f554 = ReadUInt32(thisPtr, 0x554);
            WriteLog(
                "PROBE #49 OPEN PATH this=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(thisPtr))) +
                " arg=" + Hex32(arg) +
                " flags554=" + Hex32(f554) +
                " bit3_FC=" + std::string((f554 & 0x8u) ? "1" : "0") +
                " bit14_MAKE_ATTACH=" + std::string((f554 & 0x4000u) ? "1" : "0"));
        }
    }
    WriteLog(
        "PROBE #42 ABILITIES VIRTUALS"
        " this=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(thisPtr))) +
        " arg=" + Hex32(arg) +
        " flagsA4=" + Hex32(flagsA4) +
        " slotFC=" + Hex32(Probe42ReadVfunc(vtable, 0xFC)) +
        " slot100=" + Hex32(Probe42ReadVfunc(vtable, 0x100)) +
        " slot104=" + Hex32(Probe42ReadVfunc(vtable, 0x104)) +
        " slot118=" + Hex32(Probe42ReadVfunc(vtable, 0x118)) +
        " slot11C=" + Hex32(Probe42ReadVfunc(vtable, 0x11C)) +
        " caller=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
}

// Probe #40: trace the generic state/visibility propagation used by the
// dependent widgets when the Abilities panel changes state.
//
// Static correction after Probe #39:
//   0x4723B0 and 0x4724A0 are input-event handlers (their first two values are
//   coordinates; the third is an input/event code), not panel lifecycle methods.
// The more useful downstream path is:
//   0x462390 -> mutates generic widget state via 0x6A0D50
//            -> if bit 1 of this+0xA4 changes, emits 0x460410(0x18,newState,0)
//            -> invokes virtual +0xF8
// For the Abilities object and the dependent widget vtables observed so far,
// virtual +0xF8 resolves to 0x464090. 0x464090 then invokes virtual +0x130
// (0x463830 on these classes) and recursively propagates +0xF8 to children.
//
// This probe is observational only. It records 0x462390, 0x464090 and 0x463830
// for the Abilities object (vtable 0x79E280) and the dependent vtables already
// observed in the natural toggle fan-out.

using Probe40State462390Fn = void (__thiscall*)(void*, std::uint32_t);
using Probe40Prop464090Fn = void (__thiscall*)(void*, std::uint32_t);
using Probe40Leaf463830Fn = void (__thiscall*)(void*, std::uint32_t);

static Probe40State462390Fn g_OriginalProbe40State462390 = nullptr;
static Probe40Prop464090Fn g_OriginalProbe40Prop464090 = nullptr;
static Probe40Leaf463830Fn g_OriginalProbe40Leaf463830 = nullptr;
static std::atomic<std::uint32_t> g_Probe40Count(0);

static bool Probe40InterestingVtable(std::uint32_t vt)
{
    return vt == 0x0079E280 ||
           vt == 0x0079DA08 ||
           vt == 0x007A8E48 ||
           vt == 0x007A8718 ||
           vt == 0x007BC450;
}

static bool Probe40ReadWidget(void* thisPtr, std::uint32_t& vt, std::uint32_t& flagsA4)
{
    vt = 0;
    flagsA4 = 0;
    if (!thisPtr || !ProbeReadableRange(thisPtr, 0xA8))
        return false;
    vt = ReadUInt32(thisPtr, 0x00);
    flagsA4 = ReadUInt32(thisPtr, 0xA4);
    return Probe40InterestingVtable(vt);
}

static void __fastcall HookedProbe40State462390(void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    std::uint32_t vt = 0, before = 0;
    const bool logIt = (until != 0 && static_cast<LONG>(until - now) > 0) &&
                       Probe40ReadWidget(thisPtr, vt, before);
    std::uint32_t n = 0;
    if (logIt)
    {
        n = ++g_Probe40Count;
        WriteLog("PROBE #40 STATE462390 ENTER n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " vtable=" + Hex32(vt) +
                 " arg=" + Hex32(value) +
                 " flagsA4=" + Hex32(before) +
                 " bit1=" + Hex32((before >> 1) & 1) +
                 " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalProbe40State462390(thisPtr, value);

    if (logIt)
    {
        std::uint32_t vt2 = 0, after = 0;
        Probe40ReadWidget(thisPtr, vt2, after);
        WriteLog("PROBE #40 STATE462390 EXIT n=" + std::to_string(n) +
                 " flagsA4=" + Hex32(after) +
                 " bit1=" + Hex32((after >> 1) & 1));
    }
}

static void __fastcall HookedProbe40Prop464090(void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    std::uint32_t vt = 0, flags = 0;
    const bool logIt = (until != 0 && static_cast<LONG>(until - now) > 0) &&
                       Probe40ReadWidget(thisPtr, vt, flags);
    std::uint32_t n = 0;
    const std::uint32_t depth = g_Probe41PropDepth;
    if (logIt)
    {
        n = ++g_Probe41Count;
        WriteLog("PROBE #41 PROP464090 ENTER n=" + std::to_string(n) +
                 " depth=" + std::to_string(depth) +
                 " class=" + Probe41ClassName(vt) +
                 " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " vtable=" + Hex32(vt) +
                 " arg=" + Hex32(value) +
                 " flagsA4=" + Hex32(flags) +
                 " bit1=" + Hex32((flags >> 1) & 1) +
                 " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    ++g_Probe41PropDepth;
    g_OriginalProbe40Prop464090(thisPtr, value);
    --g_Probe41PropDepth;

    if (logIt)
        WriteLog("PROBE #41 PROP464090 EXIT n=" + std::to_string(n) +
                 " depth=" + std::to_string(depth) +
                 " class=" + Probe41ClassName(vt));
}

static void __fastcall HookedProbe40Leaf463830(void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    std::uint32_t vt = 0, flags = 0;
    const bool logIt = (until != 0 && static_cast<LONG>(until - now) > 0) &&
                       Probe40ReadWidget(thisPtr, vt, flags);
    std::uint32_t n = 0;
    if (logIt)
    {
        n = ++g_Probe41Count;
        if (vt == 0x0079E280)
            Probe42LogAbilitiesVirtuals(thisPtr, vt, value, flags);
        WriteLog("PROBE #41 LEAF463830 ENTER n=" + std::to_string(n) +
                 " depth=" + std::to_string(g_Probe41PropDepth) +
                 " class=" + Probe41ClassName(vt) +
                 " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " vtable=" + Hex32(vt) +
                 " arg=" + Hex32(value) +
                 " flagsA4=" + Hex32(flags) +
                 " bit1=" + Hex32((flags >> 1) & 1) +
                 " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalProbe40Leaf463830(thisPtr, value);

    if (logIt)
        WriteLog("PROBE #41 LEAF463830 EXIT n=" + std::to_string(n) +
                 " class=" + Probe41ClassName(vt));
}

// Probe #39: stay inside the confirmed 0x79E280 Abilities class.
// Exact-client static analysis identifies three enclosing routines around the
// 0x4720B0 call sites we observed:
//   0x472300 -> contains the 0x47234A refresh call (event handler, one arg)
//   0x4723B0 -> contains the 0x472415 refresh call (three args)
//   0x4724A0 -> contains the 0x47258B refresh and 0x47259C (1,7,0) notify (three args)
// Observational only. We log only when this->vtable == 0x0079E280.

using Probe39Event472300Fn = std::uint32_t (__thiscall*)(void*, void*);
using Probe39Path4723B0Fn = void (__thiscall*)(void*, std::uint32_t, std::uint32_t, std::uint32_t);
using Probe39Path4724A0Fn = void (__thiscall*)(void*, std::uint32_t, std::uint32_t, std::uint32_t);

static Probe39Event472300Fn g_OriginalProbe39Event472300 = nullptr;
static Probe39Path4723B0Fn g_OriginalProbe39Path4723B0 = nullptr;
static Probe39Path4724A0Fn g_OriginalProbe39Path4724A0 = nullptr;
static std::atomic<std::uint32_t> g_Probe39Count(0);

static bool Probe39Target(void* thisPtr)
{
    return thisPtr &&
           ProbeReadableRange(thisPtr, 0x712) &&
           ReadUInt32(thisPtr, 0x00) == 0x0079E280;
}

static std::string Probe39State(void* thisPtr)
{
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(thisPtr);
    return " flag710=" + Hex32(p[0x710]) +
           " flag711=" + Hex32(p[0x711]) +
           " byteA4=" + Hex32(p[0xA4]);
}

static std::uint32_t __fastcall HookedProbe39Event472300(void* thisPtr, void*, void* eventRecord)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool logIt = (until != 0 && static_cast<LONG>(until - now) > 0) && Probe39Target(thisPtr);
    std::uint32_t n = 0;

    if (logIt)
    {
        n = ++g_Probe39Count;
        std::uint32_t field0 = 0, owner = 0, a = 0, b = 0, c = 0;
        if (eventRecord && ProbeReadableRange(eventRecord, 0x14))
        {
            field0 = ReadUInt32(eventRecord, 0x00);
            owner  = ReadUInt32(eventRecord, 0x04);
            a      = ReadUInt32(eventRecord, 0x08);
            b      = ReadUInt32(eventRecord, 0x0C);
            c      = ReadUInt32(eventRecord, 0x10);
        }
        WriteLog("PROBE #39 EVENT472300 ENTER n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 Probe39State(thisPtr) +
                 " event=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(eventRecord))) +
                 " field0=" + Hex32(field0) + " owner=" + Hex32(owner) +
                 " a=" + Hex32(a) + " b=" + Hex32(b) + " c=" + Hex32(c) +
                 " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const std::uint32_t result = g_OriginalProbe39Event472300(thisPtr, eventRecord);

    if (logIt)
        WriteLog("PROBE #39 EVENT472300 EXIT n=" + std::to_string(n) +
                 Probe39State(thisPtr) + " result=" + Hex32(result));
    return result;
}

static void __fastcall HookedProbe39Path4723B0(
    void* thisPtr, void*, std::uint32_t arg1, std::uint32_t arg2, std::uint32_t arg3)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool logIt = (until != 0 && static_cast<LONG>(until - now) > 0) && Probe39Target(thisPtr);
    std::uint32_t n = 0;

    if (logIt)
    {
        n = ++g_Probe39Count;
        WriteLog("PROBE #39 PATH4723B0 ENTER n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 Probe39State(thisPtr) +
                 " arg1=" + Hex32(arg1) + " arg2=" + Hex32(arg2) + " arg3=" + Hex32(arg3) +
                 " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalProbe39Path4723B0(thisPtr, arg1, arg2, arg3);

    if (logIt)
        WriteLog("PROBE #39 PATH4723B0 EXIT n=" + std::to_string(n) + Probe39State(thisPtr));
}

static void __fastcall HookedProbe39Path4724A0(
    void* thisPtr, void*, std::uint32_t arg1, std::uint32_t arg2, std::uint32_t arg3)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool logIt = (until != 0 && static_cast<LONG>(until - now) > 0) && Probe39Target(thisPtr);
    std::uint32_t n = 0;

    if (logIt)
    {
        n = ++g_Probe39Count;
        WriteLog("PROBE #39 PATH4724A0 ENTER n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 Probe39State(thisPtr) +
                 " arg1=" + Hex32(arg1) + " arg2=" + Hex32(arg2) + " arg3=" + Hex32(arg3) +
                 " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalProbe39Path4724A0(thisPtr, arg1, arg2, arg3);

    if (logIt)
        WriteLog("PROBE #39 PATH4724A0 EXIT n=" + std::to_string(n) + Probe39State(thisPtr));
}

// Probe #38: distinguish the panel OPEN/activation path from CLOSE/deactivation.
// Exact-client static disassembly shows the 0x4723B0 state-transition routine:
//   OPEN:  this+0x711 = 1; 0x465F90(this, 3)
//   CLOSE: this+0x711 = 0; 0x465FB0(this, 3)
// 0x465F90 forwards to dispatcher routine 0x45E760, while 0x465FB0 forwards
// to 0x45C1F0. This hook is observational only; Probe #34 already observes
// the close-side 0x465FB0 call.
using UiOpen465F90Fn = void (__thiscall*)(void* thisPtr, std::uint32_t value);
static UiOpen465F90Fn g_OriginalUiOpen465F90 = nullptr;
static std::atomic<std::uint32_t> g_Probe38OpenCount(0);

static void __fastcall HookedUiOpen465F90(void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    std::uint32_t n = 0;

    if (armed)
    {
        n = ++g_Probe38OpenCount;
        std::uint32_t vtable = 0;
        std::uint32_t flag710 = 0, flag711 = 0;
        if (thisPtr && ProbeReadableRange(thisPtr, 0x712))
        {
            vtable = ReadUInt32(thisPtr, 0x00);
            flag710 = *reinterpret_cast<std::uint8_t*>(
                reinterpret_cast<std::uint8_t*>(thisPtr) + 0x710);
            flag711 = *reinterpret_cast<std::uint8_t*>(
                reinterpret_cast<std::uint8_t*>(thisPtr) + 0x711);
        }

        WriteLog(
            "PROBE #38 OPEN465F90 ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " vtable=" + Hex32(vtable) +
            " value=" + Hex32(value) +
            " flag710=" + Hex32(flag710) +
            " flag711=" + Hex32(flag711) +
            " caller=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalUiOpen465F90(thisPtr, value);

    if (armed)
        WriteLog("PROBE #38 OPEN465F90 EXIT n=" + std::to_string(n));
}

// Probe #34: observe the two side paths surrounding 0x4720B0 in the
// 0x4724E0-0x4725A0 state-transition routine.
// Exact-client static disassembly shows:
//   0x465FB0: __thiscall(this, uint32_t value), RET 4
//   0x460410: __thiscall(this, uint32_t a, uint32_t b, uint32_t c), RET 0xC
// The 0x4725xx caller invokes 0x465FB0(3) when this+0x711 was set,
// then always calls 0x4720B0, and conditionally calls 0x460410(1,7,0).
// Observational only.
using UiSide465FB0Fn = void (__thiscall*)(void* thisPtr, std::uint32_t value);
using UiSide460410Fn = void (__thiscall*)(void* thisPtr, std::uint32_t a, std::uint32_t b, std::uint32_t c);

static UiSide465FB0Fn g_OriginalUiSide465FB0 = nullptr;
static UiSide460410Fn g_OriginalUiSide460410 = nullptr;
static std::atomic<std::uint32_t> g_Probe34Side465FB0Count(0);
static std::atomic<std::uint32_t> g_Probe34Side460410Count(0);

static void __fastcall HookedUiSide465FB0(void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    std::uint32_t n = 0;

    if (armed)
    {
        n = ++g_Probe34Side465FB0Count;
        std::uint32_t vtable = 0;
        std::uint32_t flag710 = 0, flag711 = 0;
        if (thisPtr && ProbeReadableRange(thisPtr, 0x712))
        {
            vtable = ReadUInt32(thisPtr, 0x00);
            flag710 = *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uint8_t*>(thisPtr) + 0x710);
            flag711 = *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uint8_t*>(thisPtr) + 0x711);
        }
        WriteLog(
            "PROBE #34 SIDE465FB0 ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " vtable=" + Hex32(vtable) +
            " value=" + Hex32(value) +
            " flag710=" + Hex32(flag710) +
            " flag711=" + Hex32(flag711) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalUiSide465FB0(thisPtr, value);

    if (armed)
        WriteLog("PROBE #34 SIDE465FB0 EXIT n=" + std::to_string(n));
}

static void __fastcall HookedUiSide460410(
    void* thisPtr, void*, std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    std::uint32_t n = 0;

    if (armed)
    {
        n = ++g_Probe34Side460410Count;
        std::uint32_t vtable = 0;
        if (thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
            vtable = ReadUInt32(thisPtr, 0x00);

        WriteLog(
            "PROBE #34 SIDE460410 ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " vtable=" + Hex32(vtable) +
            " a=" + Hex32(a) +
            " b=" + Hex32(b) +
            " c=" + Hex32(c) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    g_OriginalUiSide460410(thisPtr, a, b, c);

    if (armed)
        WriteLog("PROBE #34 SIDE460410 EXIT n=" + std::to_string(n));
}

// Probe #33: observe the two downstream paths used by 0x00472160.
// Exact-client static disassembly:
//   0x00460820: bool __thiscall Fn(this, uint32_t key, uint32_t value), RET 8
//   0x00464FA0: bool __thiscall Fn(this, uint32_t value), RET 4
// 0x472160 calls 0x460820 for state keys 0x0D/0x0E when a state bit changes;
// otherwise it can forward the requested value to 0x464FA0.
// Observational only: no forced calls or state mutation.
using UiStateUpdateFn = bool (__thiscall*)(void* thisPtr, std::uint32_t key, std::uint32_t value);
using UiStateForwardFn = bool (__thiscall*)(void* thisPtr, std::uint32_t value);

static UiStateUpdateFn g_OriginalUiStateUpdate = nullptr;
static UiStateForwardFn g_OriginalUiStateForward = nullptr;
static std::atomic<std::uint32_t> g_Probe33StateUpdateCount(0);
static std::atomic<std::uint32_t> g_Probe33StateForwardCount(0);

static bool __fastcall HookedUiStateUpdate(
    void* thisPtr, void*, std::uint32_t key, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);

    std::uint32_t n = 0;
    if (armed)
    {
        n = ++g_Probe33StateUpdateCount;
        std::uint32_t vtable = 0;
        if (thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
            vtable = ReadUInt32(thisPtr, 0x00);

        WriteLog(
            "PROBE #33 STATEUPDATE ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " vtable=" + Hex32(vtable) +
            " key=" + Hex32(key) +
            " value=" + Hex32(value) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const bool result = g_OriginalUiStateUpdate(thisPtr, key, value);

    if (armed)
    {
        WriteLog(
            "PROBE #33 STATEUPDATE EXIT n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " key=" + Hex32(key) +
            " value=" + Hex32(value) +
            " result=" + std::to_string(result ? 1 : 0));
    }
    return result;
}

static bool __fastcall HookedUiStateForward(
    void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);

    std::uint32_t n = 0;
    if (armed)
    {
        n = ++g_Probe33StateForwardCount;
        std::uint32_t vtable = 0;
        if (thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
            vtable = ReadUInt32(thisPtr, 0x00);

        WriteLog(
            "PROBE #33 STATEFORWARD ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " vtable=" + Hex32(vtable) +
            " value=" + Hex32(value) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const bool result = g_OriginalUiStateForward(thisPtr, value);

    if (armed)
    {
        WriteLog(
            "PROBE #33 STATEFORWARD EXIT n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " value=" + Hex32(value) +
            " result=" + std::to_string(result ? 1 : 0));
    }
    return result;
}

// Probe #32: observe the live virtual +0x9C target resolved by Probe #31.
// Static disassembly of the exact client shows 0x00472160 is:
//   bool __thiscall Fn(void* thisPtr, uint32_t value)
// and returns with RET 4. It queries UI state through 0x00460CC0 and may
// update state through 0x00460820 or forward the value to 0x00464FA0.
// This probe is observational only.
using UiVirtual9CFn = bool (__thiscall*)(void* thisPtr, std::uint32_t value);
static UiVirtual9CFn g_OriginalUiVirtual9C = nullptr;
static std::atomic<std::uint32_t> g_Probe32Virtual9CCount(0);

static bool __fastcall HookedUiVirtual9C(void* thisPtr, void*, std::uint32_t value)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);

    std::uint32_t n = 0;
    if (armed)
    {
        n = ++g_Probe32Virtual9CCount;

        std::uint32_t vtable = 0;
        if (thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
            vtable = ReadUInt32(thisPtr, 0x00);

        WriteLog(
            "PROBE #32 VIRTUAL9C ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " vtable=" + Hex32(vtable) +
            " value=" + Hex32(value) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const bool result = g_OriginalUiVirtual9C(thisPtr, value);

    if (armed)
    {
        WriteLog(
            "PROBE #32 VIRTUAL9C EXIT n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " value=" + Hex32(value) +
            " result=" + std::to_string(result ? 1 : 0));
    }

    return result;
}

// Probe #31: observational hooks selected from the class-specific handlers.
// 0x00471490 is repeatedly called by the 0x00471890 handler after several
// type-specific state changes. 0x004720B0 is repeatedly called by 0x00472300
// for its 0x0D-0x13 event family and may in turn invoke this->vtable+0x9C.
// Both are __thiscall routines with no stack arguments. We only observe them.
using UiRefresh1490Fn = void (__thiscall*)(void* thisPtr);
using UiRefresh20B0Fn = void (__thiscall*)(void* thisPtr);

static UiRefresh1490Fn g_OriginalUiRefresh1490 = nullptr;
static UiRefresh20B0Fn g_OriginalUiRefresh20B0 = nullptr;
static std::atomic<std::uint32_t> g_Probe31Refresh1490Count(0);
static std::atomic<std::uint32_t> g_Probe31Refresh20B0Count(0);

static void __fastcall HookedUiRefresh1490(void* thisPtr, void*)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);

    if (armed)
    {
        const std::uint32_t n = ++g_Probe31Refresh1490Count;
        if (n <= 400)
        {
            void* caller = _ReturnAddress();
            std::uint32_t vtable = 0;
            if (thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
                vtable = ReadUInt32(thisPtr, 0x00);

            WriteLog(
                "PROBE #31 REFRESH1490 n=" + std::to_string(n) +
                " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                " vtable=" + Hex32(vtable) +
                " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))));
        }
    }

    g_OriginalUiRefresh1490(thisPtr);
}

static void __fastcall HookedUiRefresh20B0(void* thisPtr, void*)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);

    if (armed)
    {
        const std::uint32_t n = ++g_Probe31Refresh20B0Count;
        if (n <= 400)
        {
            void* caller = _ReturnAddress();
            std::uint32_t vtable = 0;
            std::uint32_t callback9C = 0;

            if (thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
            {
                vtable = ReadUInt32(thisPtr, 0x00);
                void* vt = reinterpret_cast<void*>(static_cast<std::uintptr_t>(vtable));
                if (vtable && ProbeReadableRange(vt, 0xA0))
                    callback9C = ReadUInt32(vt, 0x9C);
            }

            WriteLog(
                "PROBE #31 REFRESH20B0 n=" + std::to_string(n) +
                " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
                " vtable=" + Hex32(vtable) +
                " callback9C=" + Hex32(callback9C) +
                " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))));
        }
    }

    g_OriginalUiRefresh20B0(thisPtr);
}

// Probe #30: observational hook for exact client VA 0x00464C90 / RVA 0x64C90.
// Exact disassembly shows: bool __thiscall(this, eventPtr), RET 4.
// This routine calls UiEventType(eventPtr), maps the event through this+0x40C,
// and, when the map succeeds, invokes this->vtable[0xC8/4](eventPtr).
// We log the parent object and the resolved +0xC8 virtual target, but do not
// invoke or alter any UI behavior ourselves.
using UiEventRouterFn = bool (__thiscall*)(void* thisPtr, void* eventPtr);
static UiEventRouterFn g_OriginalUiEventRouter = nullptr;
static std::atomic<std::uint32_t> g_Probe30RouterCount(0);

static bool __fastcall HookedUiEventRouter(void* thisPtr, void*, void* eventPtr)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    const std::uint32_t n = armed ? ++g_Probe30RouterCount : 0;

    std::uint32_t descriptor = 0;
    std::uint32_t vtable = 0;
    std::uint32_t callbackC8 = 0;

    if (armed && eventPtr && ProbeReadableRange(eventPtr, sizeof(std::uint32_t)))
        descriptor = ReadUInt32(eventPtr, 0x00);

    if (armed && thisPtr && ProbeReadableRange(thisPtr, sizeof(std::uint32_t)))
    {
        vtable = ReadUInt32(thisPtr, 0x00);
        void* vt = reinterpret_cast<void*>(static_cast<std::uintptr_t>(vtable));
        if (vtable && ProbeReadableRange(vt, 0xCC))
            callbackC8 = ReadUInt32(vt, 0xC8);
    }

    if (armed && n <= 400)
    {
        WriteLog(
            "PROBE #30 ROUTER ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " event=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(eventPtr))) +
            " descriptor=" + Hex32(descriptor) +
            " vtable=" + Hex32(vtable) +
            " callbackC8=" + Hex32(callbackC8));
    }

    const bool result = g_OriginalUiEventRouter(thisPtr, eventPtr);

    if (armed && n <= 400)
    {
        WriteLog(
            "PROBE #30 ROUTER EXIT n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " result=" + std::to_string(result ? 1 : 0));
    }

    return result;
}

// Probe #29: observational hook for exact client VA 0x00429120 / RVA 0x29120.
// Exact disassembly: __thiscall, two 32-bit stack arguments, RET 8, boolean AL result.
// We intentionally log the raw arguments rather than assigning UI semantics prematurely.
using UiEventMapFn = bool (__thiscall*)(void* thisPtr, void* arg1, std::uint32_t arg2);
static UiEventMapFn g_OriginalUiEventMap = nullptr;
static std::atomic<std::uint32_t> g_Probe29EventMapCount(0);

static bool __fastcall HookedUiEventMap(void* thisPtr, void*, void* arg1, std::uint32_t arg2)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed = (until != 0 && static_cast<LONG>(until - now) > 0);
    const std::uint32_t n = armed ? ++g_Probe29EventMapCount : 0;
    void* caller = armed ? _ReturnAddress() : nullptr;

    std::uint32_t arg1First = 0;
    if (armed && arg1 && ProbeReadableRange(arg1, sizeof(std::uint32_t)))
        arg1First = ReadUInt32(arg1, 0x00);

    if (armed && n <= 600)
    {
        WriteLog(
            "PROBE #29 MAP ENTER n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " arg1=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(arg1))) +
            " arg1[0]=" + Hex32(arg1First) +
            " arg2=" + Hex32(arg2) +
            " caller=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(caller))));
    }

    const bool result = g_OriginalUiEventMap(thisPtr, arg1, arg2);

    if (armed && n <= 600)
    {
        WriteLog(
            "PROBE #29 MAP EXIT n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " result=" + std::to_string(result ? 1 : 0));
    }
    return result;
}

static void __fastcall HookedUiLockSetter(void* thisPtr, void*, bool locked)
{
    const std::uint32_t seq = ++g_Probe21LockSequence;
    std::uintptr_t notifyTarget = 0;
    if (thisPtr && ProbeReadableRange(thisPtr, sizeof(void*)))
    {
        void** vtable = *reinterpret_cast<void***>(thisPtr);
        if (vtable && ProbeReadableRange(vtable, 0x18))
            notifyTarget = reinterpret_cast<std::uintptr_t>(vtable[5]); // vtable + 0x14
    }

    WriteLog(
        "PROBE #22 LOCK SETTER ENTER seq=" + std::to_string(seq) +
        " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
        " locked=" + std::to_string(locked ? 1 : 0) +
        " notify_vtbl14=" + Hex32(static_cast<std::uint32_t>(notifyTarget)));

    g_OriginalUiLockSetter(thisPtr, locked);

    WriteLog("PROBE #22 LOCK SETTER EXIT seq=" + std::to_string(seq));
}

static void __fastcall HookedUiPostLock(
    void* thisPtr, void*, std::uint32_t eventId, std::uint32_t arg)
{
    WriteLog(
        "PROBE #21 POST-LOCK CALL this=" +
        Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
        " eventId=" + Hex32(eventId) +
        " arg=" + Hex32(arg) +
        " lockSeq=" + std::to_string(g_Probe21LockSequence.load()));

    g_OriginalUiPostLock(thisPtr, eventId, arg);

    WriteLog("PROBE #21 POST-LOCK RETURN eventId=" + Hex32(eventId));
}


// Probe #9 target-upload context. 0x00444200 calls the renderer's
// virtual +0x60 lock method. For the observed RenderSurface vtable
// (0x00801AA8), that slot resolves to VA 0x00696F10.
static thread_local bool
    g_Probe9InsideTargetUpload = false;

using RendererLockFn =
    bool (__thiscall*)(
        void* thisPtr,
        void* lockState,
        std::uint32_t level,
        std::uint32_t* pitchOut,
        void** bitsOut);

static RendererLockFn
    g_OriginalRendererLock = nullptr;

// ------------------------------------------------------------
// Probe #68: exact War Magic RenderSurface lock/unlock path.
//
// Probe #67 proved the higher-level 0x00443290 surface-copy routine is not
// the path used by DID 0x06001365.  However, during the successful F9 ->
// close/open refresh the game itself entered RenderSurface::Lock (0x696F10)
// for the exact target.  F8/F9 call g_OriginalRendererLock directly, so they
// intentionally bypass this observation hook.
//
// Probe #68 records the real game caller, the enclosing 0x4410C0 acquire
// caller (when present), and hashes the mapped 20x20 BGRA bytes at lock and
// immediately before the matching 0x696FB0 unlock.
// ------------------------------------------------------------
static std::atomic<std::uint32_t> g_Probe68Phase(0);
static std::atomic<DWORD> g_Probe68ArmedUntil(0);
static std::atomic<std::uint32_t> g_Probe68TargetLockCount(0);
static std::atomic<std::uint32_t> g_Probe68TargetUnlockCount(0);
static std::atomic<std::uint32_t> g_Probe68TargetChangedCount(0);

static const char* Probe68PhaseName(std::uint32_t phase)
{
    switch (phase)
    {
    case 1: return "NATURAL";
    case 2: return "F8_POST";
    case 3: return "F9_POST";
    default: return "IDLE";
    }
}

static bool Probe68Armed()
{
    const DWORD until = g_Probe68ArmedUntil.load();
    return g_Probe68Phase.load() != 0 &&
           until != 0 &&
           static_cast<LONG>(until - GetTickCount()) > 0;
}

static void Probe68ResetCounters()
{
    g_Probe68TargetLockCount.store(0);
    g_Probe68TargetUnlockCount.store(0);
    g_Probe68TargetChangedCount.store(0);
}

static void Probe68ArmPhase(std::uint32_t phase, DWORD milliseconds)
{
    Probe68ResetCounters();
    g_Probe68Phase.store(phase);
    g_Probe68ArmedUntil.store(GetTickCount() + milliseconds);
}

static void Probe68LogAndStopPhase(const char* reason)
{
    const std::uint32_t phase = g_Probe68Phase.load();
    if (phase == 0)
        return;

    WriteLog(
        "PROBE #68 SUMMARY phase=" + std::string(Probe68PhaseName(phase)) +
        " reason=" + reason +
        " targetLocks=" + std::to_string(g_Probe68TargetLockCount.load()) +
        " targetUnlocks=" + std::to_string(g_Probe68TargetUnlockCount.load()) +
        " mappedBytesChanged=" + std::to_string(g_Probe68TargetChangedCount.load()));

    g_Probe68Phase.store(0);
    g_Probe68ArmedUntil.store(0);
}

static void Probe68FinishExpiredPhase()
{
    if (g_Probe68Phase.load() == 0)
        return;
    const DWORD until = g_Probe68ArmedUntil.load();
    if (until != 0 && static_cast<LONG>(until - GetTickCount()) <= 0)
        Probe68LogAndStopPhase("expired");
}

static std::uint32_t Probe68HashMappedTarget(
    void* thisPtr,
    void* bits,
    std::uint32_t pitch)
{
    if (!thisPtr || !bits || !ProbeReadableRange(thisPtr, 0x60))
        return 0;

    const std::uint32_t width = ReadUInt32(thisPtr, 0x58);
    const std::uint32_t height = ReadUInt32(thisPtr, 0x5C);
    if (width == 0 || height == 0 || width > 4096 || height > 4096)
        return 0;

    const std::uint32_t rowBytes = width * 4u;
    if (pitch < rowBytes)
        return 0;

    std::uint32_t hash = 2166136261u;
    const unsigned char* base = static_cast<const unsigned char*>(bits);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const unsigned char* row = base + static_cast<std::size_t>(y) * pitch;
        if (!ProbeReadableRange(row, rowBytes))
            return 0;
        for (std::uint32_t x = 0; x < rowBytes; ++x)
        {
            hash ^= row[x];
            hash *= 16777619u;
        }
    }
    return hash;
}

struct Probe68TargetLockFrame
{
    void* thisPtr = nullptr;
    void* bits = nullptr;
    std::uint32_t pitch = 0;
    std::uint32_t level = 0;
    std::uint32_t lockCaller = 0;
    std::uint32_t acquireCaller = 0;
    std::uint32_t hashAtLock = 0;
    bool active = false;
};

static thread_local Probe68TargetLockFrame g_Probe68TargetLockFrame;


// Probe #48: observe the Abilities backing-object lifecycle.
// The Abilities vtable is 0x0079E280. Its +0x11C target (0x461920)
// produces a rendering-related object, and +0x118 (0x461BA0) attaches/
// replaces the object at widget+0xB0. Do not call either function here;
// only observe natural calls during the F11 window.
static constexpr std::uint32_t PROBE48_ABILITIES_VTABLE = 0x0079E280u;

using Probe48MakeBackingFn =
    void (__thiscall*)(void* thisPtr, void** outObject);
using Probe48AttachBackingFn =
    void (__thiscall*)(void* thisPtr, void* object);

static Probe48MakeBackingFn g_OriginalProbe48MakeBacking = nullptr;
static Probe48AttachBackingFn g_OriginalProbe48AttachBacking = nullptr;
static std::atomic<std::uint32_t> g_Probe48MakeCount(0);
static std::atomic<std::uint32_t> g_Probe48AttachCount(0);

static bool Probe48Armed()
{
    const DWORD until = g_Probe23ArmedUntil.load();
    return until != 0 &&
        static_cast<LONG>(until - GetTickCount()) > 0;
}

static bool Probe48IsAbilities(void* p)
{
    return p &&
        ProbeReadableRange(p, 0xB4) &&
        ReadUInt32(p, 0) == PROBE48_ABILITIES_VTABLE;
}


// Probe #58: bounded D3D9 draw-submission trace during the known-good
// Abilities close -> reopen transition. We observe IDirect3DDevice9::SetTexture
// and the principal indexed/non-indexed draw calls, recording AC return addresses.
// This probe deliberately does NOT assume the +0x120 offscreen surface is itself
// the sampled texture; Probe #57 showed it is not re-read during the refresh.
using Probe58SetTextureFn = long (__stdcall*)(void*, unsigned long, void*);
using Probe58DrawPrimitiveFn = long (__stdcall*)(void*, int, unsigned int, unsigned int);
using Probe58DrawIndexedPrimitiveFn = long (__stdcall*)(
    void*, int, int, unsigned int, unsigned int, unsigned int, unsigned int);

static Probe58SetTextureFn g_Probe58OriginalSetTexture = nullptr;
static Probe58DrawPrimitiveFn g_Probe58OriginalDrawPrimitive = nullptr;
static Probe58DrawIndexedPrimitiveFn g_Probe58OriginalDrawIndexedPrimitive = nullptr;

static std::atomic<bool> g_Probe58HooksInstalled(false);
static std::atomic<std::uint32_t> g_Probe58SetTextureCount(0);
static std::atomic<std::uint32_t> g_Probe58DrawCount(0);
static std::atomic<std::uint32_t> g_Probe58LastTexture0(0);

static bool Probe58Armed()
{
    return GetTickCount() <= g_Probe23ArmedUntil.load();
}

static long __stdcall HookedProbe58SetTexture(
    void* device, unsigned long stage, void* texture)
{
    if (Probe58Armed())
    {
        const std::uint32_t tex = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(texture));

        if (stage == 0)
            g_Probe58LastTexture0.store(tex);

        const std::uint32_t n = g_Probe58SetTextureCount.fetch_add(1) + 1;
        // Bound logging aggressively: enough to identify the submission path
        // without turning every frame into a huge log.
        if (n <= 250)
        {
            WriteLog(
                "PROBE #58 SetTexture n=" + std::to_string(n) +
                " stage=" + std::to_string(static_cast<unsigned long long>(stage)) +
                " texture=" + Hex32(tex) +
                " caller=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
        }
        else if (n == 251)
        {
            WriteLog("PROBE #58 SetTexture logging capped at 250 calls.");
        }
    }

    return g_Probe58OriginalSetTexture(device, stage, texture);
}

static long __stdcall HookedProbe58DrawPrimitive(
    void* device, int primitiveType, unsigned int startVertex,
    unsigned int primitiveCount)
{
    if (Probe58Armed())
    {
        const std::uint32_t n = g_Probe58DrawCount.fetch_add(1) + 1;
        if (n <= 250)
        {
            WriteLog(
                "PROBE #58 DrawPrimitive n=" + std::to_string(n) +
                " type=" + std::to_string(primitiveType) +
                " primCount=" + std::to_string(primitiveCount) +
                " tex0=" + Hex32(g_Probe58LastTexture0.load()) +
                " caller=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
        }
        else if (n == 251)
        {
            WriteLog("PROBE #58 draw logging capped at 250 calls.");
        }
    }

    return g_Probe58OriginalDrawPrimitive(
        device, primitiveType, startVertex, primitiveCount);
}

static long __stdcall HookedProbe58DrawIndexedPrimitive(
    void* device, int primitiveType, int baseVertexIndex,
    unsigned int minVertexIndex, unsigned int numVertices,
    unsigned int startIndex, unsigned int primitiveCount)
{
    if (Probe58Armed())
    {
        const std::uint32_t n = g_Probe58DrawCount.fetch_add(1) + 1;
        if (n <= 250)
        {
            WriteLog(
                "PROBE #58 DrawIndexedPrimitive n=" + std::to_string(n) +
                " type=" + std::to_string(primitiveType) +
                " primCount=" + std::to_string(primitiveCount) +
                " tex0=" + Hex32(g_Probe58LastTexture0.load()) +
                " caller=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
        }
        else if (n == 251)
        {
            WriteLog("PROBE #58 draw logging capped at 250 calls.");
        }
    }

    return g_Probe58OriginalDrawIndexedPrimitive(
        device, primitiveType, baseVertexIndex, minVertexIndex, numVertices,
        startIndex, primitiveCount);
}

static bool Probe58Install()
{
    // Same exact live IDirect3DDevice9 source established in Probe #56.
    void* owner = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x00870340u)), 0)));
    if (!owner || !ProbeReadableRange(owner, 0x46C))
    {
        WriteLog("PROBE #58 ERROR: graphics owner at 0x00870340 unavailable.");
        return false;
    }

    const std::uint32_t deviceAddress = ReadUInt32(owner, 0x468);
    void* device = reinterpret_cast<void*>(static_cast<std::uintptr_t>(deviceAddress));
    if (!device || !ProbeReadableRange(device, sizeof(void*)))
    {
        WriteLog("PROBE #58 ERROR: IDirect3DDevice9 pointer unavailable.");
        return false;
    }

    const std::uint32_t vtAddress = ReadUInt32(device, 0);
    void* vt = reinterpret_cast<void*>(static_cast<std::uintptr_t>(vtAddress));
    // IDirect3DDevice9:
    // SetTexture             index 65 = +0x104
    // DrawPrimitive          index 81 = +0x144
    // DrawIndexedPrimitive   index 82 = +0x148
    if (!vt || !ProbeReadableRange(vt, 0x14C))
    {
        WriteLog("PROBE #58 ERROR: IDirect3DDevice9 vtable unavailable.");
        return false;
    }

    void* setTextureFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(vt, 0x104)));
    void* drawPrimitiveFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(vt, 0x144)));
    void* drawIndexedPrimitiveFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(vt, 0x148)));

    WriteLog(
        "PROBE #58 DEVICE device=" + Hex32(deviceAddress) +
        " vtable=" + Hex32(vtAddress) +
        " SetTexture104=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(setTextureFn))) +
        " DrawPrimitive144=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(drawPrimitiveFn))) +
        " DrawIndexedPrimitive148=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(drawIndexedPrimitiveFn))));

    if (g_Probe58HooksInstalled.load())
    {
        WriteLog("PROBE #58 hooks already installed.");
        return true;
    }

    MH_STATUS st = MH_CreateHook(
        setTextureFn,
        reinterpret_cast<LPVOID>(&HookedProbe58SetTexture),
        reinterpret_cast<LPVOID*>(&g_Probe58OriginalSetTexture));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #58 ERROR SetTexture MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook(
        drawPrimitiveFn,
        reinterpret_cast<LPVOID>(&HookedProbe58DrawPrimitive),
        reinterpret_cast<LPVOID*>(&g_Probe58OriginalDrawPrimitive));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #58 ERROR DrawPrimitive MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook(
        drawIndexedPrimitiveFn,
        reinterpret_cast<LPVOID>(&HookedProbe58DrawIndexedPrimitive),
        reinterpret_cast<LPVOID*>(&g_Probe58OriginalDrawIndexedPrimitive));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #58 ERROR DrawIndexedPrimitive MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(setTextureFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #58 ERROR SetTexture MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(drawPrimitiveFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #58 ERROR DrawPrimitive MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(drawIndexedPrimitiveFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #58 ERROR DrawIndexedPrimitive MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    g_Probe58HooksInstalled.store(true);
    WriteLog("PROBE #58 SUCCESS: SetTexture/DrawPrimitive/DrawIndexedPrimitive hooks enabled.");
    return true;
}

// Probe #57: catch DIRECT IDirect3DSurface9::LockRect/UnlockRect calls on the
// exact War Magic backing surface. This covers D3DX/internal consumers that bypass
// AC's RenderSurface::Lock wrapper at 0x00696F10.
using Probe57LockRectFn = long (__stdcall*)(void*, void*, const RECT*, unsigned long);
using Probe57UnlockRectFn = long (__stdcall*)(void*);

static Probe57LockRectFn g_Probe57OriginalLockRect = nullptr;
static Probe57UnlockRectFn g_Probe57OriginalUnlockRect = nullptr;
static std::atomic<std::uint32_t> g_Probe57TargetSurface(0);
static std::atomic<std::uint32_t> g_Probe57LockCount(0);
static std::atomic<std::uint32_t> g_Probe57UnlockCount(0);
static std::atomic<bool> g_Probe57HooksInstalled(false);

static long __stdcall HookedProbe57LockRect(
    void* surface, void* lockedRect, const RECT* rect, unsigned long flags)
{
    const bool target =
        surface &&
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(surface)) ==
            g_Probe57TargetSurface.load() &&
        (GetTickCount() <= g_Probe23ArmedUntil.load());

    if (target)
    {
        const std::uint32_t n = g_Probe57LockCount.fetch_add(1) + 1;
        WriteLog(
            "PROBE #57 TARGET LockRect ENTER n=" + std::to_string(n) +
            " surface=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(surface))) +
            " flags=" + Hex32(static_cast<std::uint32_t>(flags)) +
            " caller=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    const long hr = g_Probe57OriginalLockRect(surface, lockedRect, rect, flags);

    if (target)
    {
        std::uint32_t pitch = 0;
        std::uint32_t bits = 0;
        if (lockedRect && ProbeReadableRange(lockedRect, 8))
        {
            pitch = ReadUInt32(lockedRect, 0);
            bits = ReadUInt32(lockedRect, 4);
        }
        WriteLog(
            "PROBE #57 TARGET LockRect EXIT hr=" + Hex32(
                static_cast<std::uint32_t>(hr)) +
            " pitch=" + Hex32(pitch) +
            " bits=" + Hex32(bits));
    }

    return hr;
}

static long __stdcall HookedProbe57UnlockRect(void* surface)
{
    if (surface &&
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(surface)) ==
            g_Probe57TargetSurface.load() &&
        (GetTickCount() <= g_Probe23ArmedUntil.load()))
    {
        const std::uint32_t n = g_Probe57UnlockCount.fetch_add(1) + 1;
        WriteLog(
            "PROBE #57 TARGET UnlockRect n=" + std::to_string(n) +
            " surface=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(surface))) +
            " caller=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    return g_Probe57OriginalUnlockRect(surface);
}

static bool Probe57InstallAndTarget()
{
    void* rs = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
        auto it = g_LiveTextureRegistry.find(PROBE46_TARGET_DID);
        if (it != g_LiveTextureRegistry.end())
            rs = it->second.renderSurface;
    }

    if (!rs || !ProbeReadableRange(rs, 0x124))
    {
        WriteLog("PROBE #57 ERROR: target DID 0x06001365 RenderSurface unavailable.");
        return false;
    }

    const std::uint32_t backing = ReadUInt32(rs, 0x120);
    void* surface = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(backing));
    if (!backing || !ProbeReadableRange(surface, sizeof(void*)))
    {
        WriteLog("PROBE #57 ERROR: target RenderSurface+0x120 unavailable.");
        return false;
    }

    const std::uint32_t vtAddress = ReadUInt32(surface, 0);
    void* vt = reinterpret_cast<void*>(static_cast<std::uintptr_t>(vtAddress));
    if (!vt || !ProbeReadableRange(vt, 0x3c))
    {
        WriteLog("PROBE #57 ERROR: target IDirect3DSurface9 vtable unavailable.");
        return false;
    }

    void* lockFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(vt, 0x34)));
    void* unlockFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(vt, 0x38)));

    g_Probe57TargetSurface.store(backing);

    WriteLog(
        "PROBE #57 TARGET backing=" + Hex32(backing) +
        " vtable=" + Hex32(vtAddress) +
        " LockRect34=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(lockFn))) +
        " UnlockRect38=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(unlockFn))));

    if (g_Probe57HooksInstalled.load())
    {
        WriteLog("PROBE #57 TARGET refreshed; direct D3D9 LockRect/UnlockRect hooks already installed.");
        return true;
    }

    MH_STATUS st = MH_CreateHook(
        lockFn,
        reinterpret_cast<LPVOID>(&HookedProbe57LockRect),
        reinterpret_cast<LPVOID*>(&g_Probe57OriginalLockRect));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #57 ERROR LockRect MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook(
        unlockFn,
        reinterpret_cast<LPVOID>(&HookedProbe57UnlockRect),
        reinterpret_cast<LPVOID*>(&g_Probe57OriginalUnlockRect));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #57 ERROR UnlockRect MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(lockFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #57 ERROR LockRect MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(unlockFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #57 ERROR UnlockRect MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    g_Probe57HooksInstalled.store(true);
    WriteLog("PROBE #57 SUCCESS: exact D3D9 LockRect/UnlockRect implementations hooked.");
    return true;
}

// Probe #56: observe whether the exact War Magic offscreen surface is consumed
// through IDirect3DDevice9::UpdateSurface (+0x78) or StretchRect (+0x88).
// Static analysis proves RenderSurface+0x120 is an IDirect3DSurface9 created by
// IDirect3DDevice9::CreateOffscreenPlainSurface (+0x90).
using Probe56UpdateSurfaceFn = long (__stdcall*)(
    void*, void*, const RECT*, void*, const POINT*);
using Probe56StretchRectFn = long (__stdcall*)(
    void*, void*, const RECT*, void*, const RECT*, unsigned int);

static Probe56UpdateSurfaceFn g_Probe56OriginalUpdateSurface = nullptr;
static Probe56StretchRectFn g_Probe56OriginalStretchRect = nullptr;
static std::atomic<std::uint32_t> g_Probe56TargetSurface(0);
static std::atomic<std::uint32_t> g_Probe56UpdateCount(0);
static std::atomic<std::uint32_t> g_Probe56StretchCount(0);
static std::atomic<bool> g_Probe56HooksInstalled(false);

static long __stdcall HookedProbe56UpdateSurface(
    void* device, void* sourceSurface, const RECT* sourceRect,
    void* destSurface, const POINT* destPoint)
{
    if (sourceSurface &&
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sourceSurface)) ==
            g_Probe56TargetSurface.load() &&
        (GetTickCount() <= g_Probe23ArmedUntil.load()))
    {
        const std::uint32_t n = g_Probe56UpdateCount.fetch_add(1) + 1;
        WriteLog(
            "PROBE #56 TARGET UpdateSurface n=" + std::to_string(n) +
            " src=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sourceSurface))) +
            " dst=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(destSurface))) +
            " caller=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    return g_Probe56OriginalUpdateSurface(
        device, sourceSurface, sourceRect, destSurface, destPoint);
}

static long __stdcall HookedProbe56StretchRect(
    void* device, void* sourceSurface, const RECT* sourceRect,
    void* destSurface, const RECT* destRect, unsigned int filter)
{
    if (sourceSurface &&
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sourceSurface)) ==
            g_Probe56TargetSurface.load() &&
        (GetTickCount() <= g_Probe23ArmedUntil.load()))
    {
        const std::uint32_t n = g_Probe56StretchCount.fetch_add(1) + 1;
        WriteLog(
            "PROBE #56 TARGET StretchRect n=" + std::to_string(n) +
            " src=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sourceSurface))) +
            " dst=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(destSurface))) +
            " filter=" + Hex32(filter) +
            " caller=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    return g_Probe56OriginalStretchRect(
        device, sourceSurface, sourceRect, destSurface, destRect, filter);
}

static bool Probe56InstallAndTarget()
{
    void* surface = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
        auto it = g_LiveTextureRegistry.find(PROBE46_TARGET_DID);
        if (it != g_LiveTextureRegistry.end())
            surface = it->second.renderSurface;
    }

    if (!surface || !ProbeReadableRange(surface, 0x124))
    {
        WriteLog("PROBE #56 ERROR: target DID 0x06001365 RenderSurface is unavailable.");
        return false;
    }

    const std::uint32_t backing = ReadUInt32(surface, 0x120);
    if (!backing)
    {
        WriteLog("PROBE #56 ERROR: target RenderSurface+0x120 is null.");
        return false;
    }
    g_Probe56TargetSurface.store(backing);

    if (g_Probe56HooksInstalled.load())
    {
        WriteLog("PROBE #56 TARGET refreshed backing120=" + Hex32(backing) +
                 "; D3D9 hooks already installed.");
        return true;
    }

    // acclient.exe static global: *(0x00870340) is the graphics owner;
    // owner+0x468 is the live IDirect3DDevice9* used by CreateOffscreenPlainSurface.
    void* owner = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x00870340u)), 0)));
    if (!owner || !ProbeReadableRange(owner, 0x46C))
    {
        WriteLog("PROBE #56 ERROR: graphics owner at 0x00870340 is unavailable.");
        return false;
    }

    const std::uint32_t deviceAddress = ReadUInt32(owner, 0x468);
    void* device = reinterpret_cast<void*>(static_cast<std::uintptr_t>(deviceAddress));
    if (!device || !ProbeReadableRange(device, sizeof(void*)))
    {
        WriteLog("PROBE #56 ERROR: IDirect3DDevice9 pointer is unavailable.");
        return false;
    }

    const std::uint32_t deviceVtableAddress = ReadUInt32(device, 0);
    void* deviceVtable =
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(deviceVtableAddress));
    if (!deviceVtable || !ProbeReadableRange(deviceVtable, 0x94))
    {
        WriteLog("PROBE #56 ERROR: IDirect3DDevice9 vtable is unavailable.");
        return false;
    }

    void* updateFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(deviceVtable, 0x78)));
    void* stretchFn = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(ReadUInt32(deviceVtable, 0x88)));

    WriteLog(
        "PROBE #56 DEVICE device=" + Hex32(deviceAddress) +
        " vtable=" + Hex32(deviceVtableAddress) +
        " UpdateSurface78=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(updateFn))) +
        " StretchRect88=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(stretchFn))) +
        " targetBacking=" + Hex32(backing));

    MH_STATUS st = MH_CreateHook(
        updateFn,
        reinterpret_cast<LPVOID>(&HookedProbe56UpdateSurface),
        reinterpret_cast<LPVOID*>(&g_Probe56OriginalUpdateSurface));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #56 ERROR UpdateSurface MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_CreateHook(
        stretchFn,
        reinterpret_cast<LPVOID>(&HookedProbe56StretchRect),
        reinterpret_cast<LPVOID*>(&g_Probe56OriginalStretchRect));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED)
    {
        WriteLog(std::string("PROBE #56 ERROR StretchRect MH_CreateHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(updateFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #56 ERROR UpdateSurface MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    st = MH_EnableHook(stretchFn);
    if (st != MH_OK && st != MH_ERROR_ENABLED)
    {
        WriteLog(std::string("PROBE #56 ERROR StretchRect MH_EnableHook: ") +
                 MH_StatusToString(st));
        return false;
    }

    g_Probe56HooksInstalled.store(true);
    WriteLog("PROBE #56 SUCCESS: UpdateSurface +0x78 and StretchRect +0x88 hooks enabled.");
    return true;
}

// Probe #55: identify the loaded module that owns the exact backing-resource
// vtable/functions discovered by Probe #54. Observational only.
static void Probe55LogModuleForAddress(const char* label, std::uint32_t address)
{
    if (!address)
    {
        WriteLog(std::string("PROBE #55 MODULE ") + label + " address=0x00000000");
        return;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(address)), &mbi, sizeof(mbi)) != sizeof(mbi))
    {
        WriteLog(std::string("PROBE #55 MODULE ") + label +
                 " address=" + Hex32(address) + " VirtualQuery failed.");
        return;
    }

    HMODULE module = reinterpret_cast<HMODULE>(mbi.AllocationBase);
    char path[MAX_PATH] = {};
    const DWORD pathLen = GetModuleFileNameA(module, path, MAX_PATH);

    std::uint32_t imageSize = 0;
    if (module && ProbeReadableRange(module, sizeof(IMAGE_DOS_HEADER)))
    {
        const IMAGE_DOS_HEADER* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            const unsigned char* base =
                reinterpret_cast<const unsigned char*>(module);
            const IMAGE_NT_HEADERS32* nt =
                reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
            if (ProbeReadableRange(nt, sizeof(IMAGE_NT_HEADERS32)) &&
                nt->Signature == IMAGE_NT_SIGNATURE)
                imageSize = nt->OptionalHeader.SizeOfImage;
        }
    }

    const std::uint32_t baseAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(module));
    const std::uint32_t rva =
        (baseAddress && address >= baseAddress) ? address - baseAddress : 0;

    WriteLog(
        std::string("PROBE #55 MODULE ") + label +
        " address=" + Hex32(address) +
        " moduleBase=" + Hex32(baseAddress) +
        " moduleSize=" + Hex32(imageSize) +
        " RVA=" + Hex32(rva) +
        " path=" + (pathLen ? std::string(path) : std::string("<unknown>")));
}

static void Probe55IdentifyBackingModule()
{
    void* surface = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
        auto it = g_LiveTextureRegistry.find(PROBE46_TARGET_DID);
        if (it != g_LiveTextureRegistry.end())
            surface = it->second.renderSurface;
    }

    if (!surface || !ProbeReadableRange(surface, 0x124))
    {
        WriteLog("PROBE #55 SNAPSHOT: target DID 0x06001365 is not currently registered/readable.");
        return;
    }

    const std::uint32_t resourceAddress = ReadUInt32(surface, 0x120);
    void* resource = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(resourceAddress));
    if (!resourceAddress || !ProbeReadableRange(resource, sizeof(void*)))
    {
        WriteLog("PROBE #55 SNAPSHOT: backing120 is null/unreadable.");
        return;
    }

    const std::uint32_t vtableAddress = ReadUInt32(resource, 0x00);
    void* vtable = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(vtableAddress));
    if (!vtableAddress || !ProbeReadableRange(vtable, 0x3C))
    {
        WriteLog("PROBE #55 SNAPSHOT: backing-resource vtable is null/unreadable.");
        return;
    }

    const std::uint32_t lockFn = ReadUInt32(vtable, 0x34);
    const std::uint32_t unlockFn = ReadUInt32(vtable, 0x38);

    WriteLog(
        "PROBE #55 SNAPSHOT DID=" + Hex32(PROBE46_TARGET_DID) +
        " surface=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(surface))) +
        " backing120=" + Hex32(resourceAddress) +
        " vtable=" + Hex32(vtableAddress) +
        " lock34=" + Hex32(lockFn) +
        " unlock38=" + Hex32(unlockFn));

    Probe55LogModuleForAddress("VTABLE", vtableAddress);
    Probe55LogModuleForAddress("LOCK34", lockFn);
    Probe55LogModuleForAddress("UNLOCK38", unlockFn);
}

// Probe #54: snapshot the exact War Magic RenderSurface backing graphics
// resource at +0x120. Static analysis of the exact client shows 0x696F10 uses
// this object for virtual +0x34 (lock), while 0x696FB0 uses virtual +0x38
// (unlock). Observational only: no backing-resource virtual is invoked here.
static void Probe54DumpTargetBackingResource()
{
    void* surface = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
        auto it = g_LiveTextureRegistry.find(PROBE46_TARGET_DID);
        if (it != g_LiveTextureRegistry.end())
            surface = it->second.renderSurface;
    }

    if (!surface || !ProbeReadableRange(surface, 0x124))
    {
        WriteLog("PROBE #54 SNAPSHOT: target DID 0x06001365 is not currently registered/readable.");
        return;
    }

    const std::uint32_t surfaceAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(surface));
    const std::uint32_t surfaceVtable = ReadUInt32(surface, 0x00);
    const std::uint32_t resourceAddress = ReadUInt32(surface, 0x120);

    WriteLog(
        "PROBE #54 SNAPSHOT DID=" + Hex32(PROBE46_TARGET_DID) +
        " surface=" + Hex32(surfaceAddress) +
        " surfaceVtable=" + Hex32(surfaceVtable) +
        " backing120=" + Hex32(resourceAddress));

    void* resource = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(resourceAddress));

    if (!resourceAddress || !ProbeReadableRange(resource, sizeof(void*)))
    {
        WriteLog("PROBE #54 SNAPSHOT: backing120 is null/unreadable.");
        return;
    }

    const std::uint32_t vtableAddress = ReadUInt32(resource, 0x00);
    WriteLog(
        "PROBE #54 BACKING OBJECT backing120=" + Hex32(resourceAddress) +
        " vtable=" + Hex32(vtableAddress));

    void* vtable = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(vtableAddress));

    if (!vtableAddress || !ProbeReadableRange(vtable, 0x84))
    {
        WriteLog("PROBE #54 SNAPSHOT: backing-resource vtable is null/unreadable.");
        return;
    }

    for (std::uint32_t offset = 0; offset <= 0x80; offset += 4)
    {
        const std::uint32_t fn = ReadUInt32(vtable, offset);
        std::string tag;
        if (offset == 0x34) tag = " CONFIRMED_LOCK";
        if (offset == 0x38) tag = " CONFIRMED_UNLOCK";
        WriteLog(
            "PROBE #54 VSLOT +" + Hex32(offset).substr(2) +
            " fn=" + Hex32(fn) + tag);
    }
}

// Probe #52: identify the renderer-side object owned by the exact War Magic
// RenderSurface.  At F11, resolve DID 0x06001365 through our live registry,
// read RenderSurface+0x118, then dump its vtable targets.  This is deliberately
// observational: the resulting addresses tell us which exact renderer methods
// are safe/relevant to disassemble before Probe #53.
static void Probe52DumpTargetRendererObject()
{
    void* surface = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
        auto it = g_LiveTextureRegistry.find(PROBE46_TARGET_DID);
        if (it != g_LiveTextureRegistry.end())
            surface = it->second.renderSurface;
    }

    if (!surface || !ProbeReadableRange(surface, 0x11C))
    {
        WriteLog("PROBE #52 SNAPSHOT: target DID 0x06001365 is not currently registered/readable.");
        return;
    }

    const std::uint32_t surfaceAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(surface));
    const std::uint32_t rendererAddress = ReadUInt32(surface, 0x118);

    WriteLog(
        "PROBE #52 SNAPSHOT DID=" + Hex32(PROBE46_TARGET_DID) +
        " surface=" + Hex32(surfaceAddress) +
        " surfaceVtable=" + Hex32(ReadUInt32(surface, 0x00)) +
        " rendererObj=" + Hex32(rendererAddress));

    void* renderer = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(rendererAddress));

    if (!rendererAddress || !ProbeReadableRange(renderer, sizeof(void*)))
    {
        WriteLog("PROBE #52 SNAPSHOT: rendererObj is null/unreadable.");
        return;
    }

    const std::uint32_t vtableAddress = ReadUInt32(renderer, 0x00);
    void* vtable = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(vtableAddress));

    WriteLog(
        "PROBE #52 RENDERER OBJECT rendererObj=" + Hex32(rendererAddress) +
        " vtable=" + Hex32(vtableAddress));

    if (!vtableAddress || !ProbeReadableRange(vtable, 0x84))
    {
        WriteLog("PROBE #52 SNAPSHOT: renderer vtable is null/unreadable.");
        return;
    }

    for (std::uint32_t offset = 0; offset <= 0x80; offset += 4)
    {
        const std::uint32_t fn = ReadUInt32(vtable, offset);
        WriteLog(
            "PROBE #52 VSLOT +" + Hex32(offset).substr(2) +
            " fn=" + Hex32(fn));
    }
}

// Probe #75: active targeted repaint test for the exact persistent War Magic
// draw node identified by Probe #73.
//
// Probe #87: #85 isolated the stale per-stack layer to DID-less 32x32 item
// sources. #86 then identified the native inventory image update chain. The
// common item refresh method is 0x004E2D10; it owns image node [this+0x668] and
// obtains the item's cached icon through item lookup 0x005583F0 -> 0x0058F480.
//
// Static analysis of 0x0058F480/0x0058EC60 shows the actual stale cache is a
// global per-item cache entry whose generated 32x32 surfaces live at +0x20/+0x24.
// 0x0058E800 only rebuilds the entry when ordinary item-state fields change, so
// a theme texture swap does not invalidate it. 0x0058DFB0 is the real generator:
// it releases the old +0x20/+0x24 surfaces and recomposes fresh 32x32 item images
// through 0x00442C70 using the CURRENT theme textures.
//
// Probe #87 keeps F8/F9 unchanged. It passively registers live item-widget owners
// whenever 0x004E2D10 naturally runs. F10 executes on the UI thread and:
//   1) looks up each registered item object with 0x005583F0,
//   2) gets its cache entry with 0x0058EC60,
//   3) force-rebuilds each unique cache entry with 0x0058DFB0,
//   4) calls the original 0x004E2D10 on every still-valid widget owner,
//   5) invalidates the normal backed UI roots.
//
// If the stale grape changes immediately on F10, this closes the inventory-cache
// gap and gives us the exact extra stage to integrate into F8/F9.
using Probe87BlitFn = bool (__thiscall*)(
    void* destHelper,
    void* sourceHelper,
    std::uint32_t blendMode,
    std::uint32_t alphaBits);

using Probe87DrawFn = void (__thiscall*)(
    void* thisPtr,
    void* arg1,
    void* arg2,
    void* arg3,
    void* arg4);

using Probe87InvalidateSelfFn = void (__thiscall*)(void* thisPtr);


using Probe87ImageSetFn = void (__thiscall*)(
    void* thisPtr,
    void* imageResource);

using Probe87ImageClearFn = void (__thiscall*)(
    void* thisPtr);


using Probe87ItemRefreshFn = bool (__thiscall*)(
    void* owner);

using Probe87LookupItemFn = void* (__cdecl*)(
    std::uint32_t itemId);

using Probe87CacheGetFn = void* (__thiscall*)(
    void* itemObject);

using Probe87CacheRebuildFn = void (__thiscall*)(
    void* cacheEntry,
    void* itemObject);

using Probe87CompositeFn = bool (__thiscall*)(
    void* destHelper,
    void* arg1Helper,
    std::uint32_t arg2,
    std::uint32_t arg3,
    std::uint32_t arg4Helper,
    std::uint32_t arg5,
    std::uint32_t arg6);

using Probe87CopyFn = bool (__thiscall*)(
    void* destHelper,
    void* sourceHelper);


using Probe87ObjectBindFn = void (__thiscall*)(
    void* renderer,
    std::uint32_t stage,
    void* textureObject);

using Probe87RawBindFn = void (__thiscall*)(
    void* renderer,
    std::uint32_t stage,
    void* d3dTexture);

static Probe87BlitFn g_OriginalProbe87Blit = nullptr;
static Probe87DrawFn g_OriginalProbe87Draw = nullptr;
static Probe87InvalidateSelfFn g_Probe87InvalidateSelf = nullptr;

static Probe87ImageSetFn g_OriginalProbe87ImageSet = nullptr;
static Probe87ImageClearFn g_OriginalProbe87ImageClear = nullptr;

static Probe87ItemRefreshFn g_OriginalProbe87ItemRefresh = nullptr;
static Probe87LookupItemFn g_Probe87LookupItem = nullptr;
static Probe87CacheGetFn g_Probe87CacheGet = nullptr;
static Probe87CacheRebuildFn g_Probe87CacheRebuild = nullptr;

static Probe87CompositeFn g_OriginalProbe87Composite = nullptr;
static Probe87CopyFn g_OriginalProbe87Copy = nullptr;

static Probe87ObjectBindFn g_OriginalProbe87ObjectBind = nullptr;
static Probe87RawBindFn g_OriginalProbe87RawBind = nullptr;

static std::atomic<DWORD> g_Probe87BindTraceUntil(0);
static std::atomic<std::uint32_t> g_Probe87ObjectBindCount(0);
static std::atomic<std::uint32_t> g_Probe87RawBindCount(0);

static std::atomic<DWORD> g_Probe87SurfaceTraceUntil(0);
static std::atomic<std::uint32_t> g_Probe87InventoryBlitCalls(0);
static std::atomic<std::uint32_t> g_Probe87InventoryCompositeCalls(0);

static std::atomic<std::uint32_t> g_Probe87CursorTraceSeq(0);
static std::atomic<std::uint32_t> g_Probe87CursorTargetRoot(0);
static std::atomic<std::int32_t> g_Probe87CursorScreenX(0);
static std::atomic<std::int32_t> g_Probe87CursorScreenY(0);
static std::atomic<std::int32_t> g_Probe87CursorClientX(0);
static std::atomic<std::int32_t> g_Probe87CursorClientY(0);
static std::atomic<std::uint32_t> g_Probe87CursorHwnd(0);
static std::atomic<std::int32_t> g_Probe87CursorLocalX(0);
static std::atomic<std::int32_t> g_Probe87CursorLocalY(0);
static std::atomic<std::uint32_t> g_Probe87CursorHitCalls(0);


static std::atomic<DWORD> g_Probe87SetterTraceUntil(0);
static std::atomic<std::uint32_t> g_Probe87ImageSetCalls(0);
static std::atomic<std::uint32_t> g_Probe87ImageClearCalls(0);
static std::atomic<std::uint32_t> g_Probe87SetterNodeDrawCalls(0);

static std::atomic<std::uint32_t> g_Probe87OwnerLearnCount(0);
static std::atomic<std::uint32_t> g_Probe87ForcedCacheRebuildCount(0);
static std::atomic<std::uint32_t> g_Probe87ForcedOwnerRefreshCount(0);
static std::atomic<std::uint32_t> g_Probe87ForcedOwnerRefreshSuccess(0);

static std::atomic<std::uint32_t> g_Probe90KnownItemLearnCount(0);

static std::atomic<DWORD> g_Probe87ItemCaptureUntil(0);
static std::atomic<std::uint32_t> g_Probe87ItemCompositeCalls(0);
static std::atomic<std::uint32_t> g_Probe87ItemCopyCalls(0);
static std::atomic<std::uint32_t> g_Probe87ItemCacheDrawCalls(0);
static std::atomic<std::uint32_t> g_Probe87ItemGeneratedUseCalls(0);

static std::atomic<bool> g_Probe87CaptureArmed(false);
static std::atomic<std::uint32_t> g_Probe87DesktopRoot(0);
static std::atomic<std::uint32_t> g_Probe87DesktopTid(0);
static std::atomic<std::uint32_t> g_Probe87TreeEnumerations(0);
static std::atomic<std::uint32_t> g_Probe87AwaitRedrawMode(0);
static std::atomic<DWORD> g_Probe87AwaitRedrawUntil(0);
static std::atomic<std::uint32_t> g_Probe87ForceCount(0);
static std::atomic<std::uint32_t> g_Probe87RedrawCalls(0);
static std::atomic<std::uint32_t> g_Probe87InvalidationAttempts(0);
static std::atomic<std::uint32_t> g_Probe87InvalidationCompleted(0);

static constexpr UINT kProbe87UiMessage = WM_APP + 0x278u;

struct Probe87DrawFrame
{
    void* thisPtr;
    void* arg1;
    void* arg2;
    void* arg3;
    void* arg4;
    std::uint32_t caller;
};

struct Probe87RootEntry
{
    std::uint32_t node;
    std::uint32_t vtable;
    std::uint32_t backing;
    std::uint32_t parent;
    std::uint32_t tid;
    std::uint32_t depth;
    DWORD lastSeenTick;
};


struct Probe87ItemOwnerEntry
{
    std::uint32_t owner;
    std::uint32_t vtable;
    std::uint32_t itemId;
    std::uint32_t source600;
    std::uint32_t imageNode;
    std::uint32_t imageNodeVtable;
    std::uint32_t tid;
    DWORD lastSeenTick;
};

static thread_local Probe87DrawFrame g_Probe87DrawStack[8] = {};
static thread_local int g_Probe87DrawDepth = 0;
static thread_local bool g_Probe92LazyItemRebuildActive = false;

// ------------------------------------------------------------
// AC Customs diagnostic UI snapshot capture.
//
// This is intentionally developer-only.  It samples the live UI tree plus the
// 0x00442C70 texture blit stream for a short window and writes a replay-oriented
// JSON document.  Normal theme application does not depend on these structures.
// ------------------------------------------------------------

struct ACCustomsSnapshotNode
{
    std::uint32_t traversalIndex;
    std::uint32_t depth;
    std::uint32_t siblingIndex;
    std::uint32_t address;
    std::uint32_t vtable;
    std::uint32_t parent;
    std::uint32_t backing;
    std::uint32_t flagsA4;
    std::uint32_t childCount;
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
    std::uint32_t resource;
    std::uint32_t resourceVtable;
    std::uint32_t underlying;
    std::uint32_t did;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t underlyingA0;
    std::uint32_t underlyingA4;
    std::uint32_t backedRoot;
};

struct ACCustomsSnapshotBlit
{
    std::uint32_t firstSequence;
    std::uint32_t callCount;
    std::uint32_t threadId;
    std::uint32_t node;
    std::uint32_t nodeVtable;
    std::uint32_t control;
    std::uint32_t itemId;
    std::uint32_t itemPrimaryDid;
    std::int32_t nodeLeft;
    std::int32_t nodeTop;
    std::int32_t nodeRight;
    std::int32_t nodeBottom;
    std::uint32_t root;
    std::int32_t rootLeft;
    std::int32_t rootTop;
    std::int32_t rootRight;
    std::int32_t rootBottom;
    std::uint32_t sourceSurface;
    std::uint32_t sourceDid;
    std::uint32_t sourceWidth;
    std::uint32_t sourceHeight;
    std::int32_t sourceLeft;
    std::int32_t sourceTop;
    std::int32_t sourceRight;
    std::int32_t sourceBottom;
    std::uint32_t destSurface;
    std::uint32_t destDid;
    std::uint32_t destWidth;
    std::uint32_t destHeight;
    std::int32_t destLeft;
    std::int32_t destTop;
    std::int32_t destRight;
    std::int32_t destBottom;
    std::int32_t estimatedScreenLeft;
    std::int32_t estimatedScreenTop;
    std::int32_t estimatedScreenRight;
    std::int32_t estimatedScreenBottom;
    std::uint32_t blendMode;
    std::uint32_t alphaBits;
    std::uint32_t drawCaller;
    std::uint32_t blitCaller;
};

struct ACCustomsSnapshotTextureAsset
{
    std::uint32_t did;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t imageSize;
    std::uint32_t pixelFormat;
    std::uint32_t formatInfo;
    std::string fileName;
    std::string status;
};

struct ACCustomsSnapshotGeneratedAsset
{
    std::uint32_t surface;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t imageSize;
    std::uint32_t pixelFormat;
    std::string fileName;
    std::string status;
};

struct ACCustomsSnapshotControlState
{
    std::uint32_t control;
    std::uint32_t root;
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
    std::int32_t rootLeft;
    std::int32_t rootTop;
    std::int32_t rootRight;
    std::int32_t rootBottom;
    std::uint32_t currentState;
    std::uint32_t presentMask;
    std::uint32_t did1;
    std::uint32_t did2;
    std::uint32_t did3;
    std::uint32_t did6;
    std::uint32_t did7;
    std::uint32_t did8;
    std::uint32_t did13;
};

// Live Mirror v1.2.6: state metadata is learned only while AC naturally
// transitions a live control. We never sweep every control's state table during
// the 500 ms snapshot window. This keeps state reads on a known-live object and
// removes the long UI-thread scan that destabilized earlier builds.
struct ACCustomsLearnedControlState
{
    std::uint32_t currentState = 0;
    std::uint32_t presentMask = 0;
    std::uint32_t did1 = 0;
    std::uint32_t did2 = 0;
    std::uint32_t did3 = 0;
    std::uint32_t did6 = 0;
    std::uint32_t did7 = 0;
    std::uint32_t did8 = 0;
    std::uint32_t did13 = 0;
};
static std::mutex g_ACCustomsLearnedStateMutex;
static std::unordered_map<std::uint32_t, ACCustomsLearnedControlState>
    g_ACCustomsLearnedStates;

static std::vector<ACCustomsSnapshotTextureAsset>
ACCustomsSnapshotDumpTextures(
    const std::string& outputPath,
    const std::unordered_set<std::uint32_t>& dids);
static std::vector<ACCustomsSnapshotGeneratedAsset>
ACCustomsSnapshotDumpGeneratedSurfaces(
    const std::string& outputPath,
    const std::vector<ACCustomsSnapshotBlit>& blits);

static std::mutex g_ACCustomsSnapshotMutex;
static std::vector<ACCustomsSnapshotNode> g_ACCustomsSnapshotNodes;
static std::vector<ACCustomsSnapshotBlit> g_ACCustomsSnapshotBlits;
static std::vector<ACCustomsSnapshotControlState> g_ACCustomsSnapshotControlStates;
static std::unordered_map<std::string, std::size_t> g_ACCustomsSnapshotBlitIndex;
static std::atomic<bool> g_ACCustomsSnapshotActive(false);
static std::atomic<bool> g_ACCustomsSnapshotTreeCaptured(false);
static std::atomic<bool> g_ACCustomsSnapshotWriterRunning(false);
static std::atomic<std::uint32_t> g_ACCustomsSnapshotSequence(0);
static std::atomic<std::uint32_t> g_ACCustomsSnapshotTotalBlitCalls(0);
static std::atomic<std::uint32_t> g_ACCustomsSnapshotDroppedBlits(0);
static std::string g_ACCustomsSnapshotOutputPath;
static DWORD g_ACCustomsSnapshotStartedTick = 0;
static constexpr DWORD kACCustomsSnapshotCaptureMs = 750;
static constexpr DWORD kACCustomsLiveMirrorCaptureMs = 500;
static std::atomic<DWORD> g_ACCustomsSnapshotCaptureDurationMs(kACCustomsSnapshotCaptureMs);
static std::atomic<bool> g_ACCustomsSnapshotLiveMirror(false);
static std::atomic<bool> g_ACCustomsSnapshotLastWriteSucceeded(false);
static std::atomic<std::uint32_t> g_ACCustomsSnapshotLastNodeCount(0);
static std::atomic<std::uint32_t> g_ACCustomsSnapshotLastBlitCount(0);
static std::atomic<std::uint32_t> g_ACCustomsSnapshotLastFileDidCount(0);
static constexpr std::size_t kACCustomsSnapshotMaxNodes = 32768;
static constexpr std::size_t kACCustomsSnapshotMaxUniqueBlits = 20000;
static void* g_ACCustomsSnapshotBlitHookTarget = nullptr;
static Probe87BlitFn g_OriginalACCustomsSnapshotBlit = nullptr;
static thread_local Probe87DrawFrame g_ACCustomsSnapshotDrawStack[64] = {};
static thread_local int g_ACCustomsSnapshotDrawDepth = 0;


// AC Customs control-state linkage diagnostic. These hooks are created during
// production initialization but remain disabled unless the user explicitly
// starts a Developer Tests state-link capture.
enum class ACCustomsStateLinkEventKind : std::uint32_t
{
    StateTransition = 1,
    ImageSet = 2
};

struct ACCustomsStateLinkEvent
{
    std::uint32_t sequence;
    std::uint32_t elapsedMs;
    std::uint32_t threadId;
    ACCustomsStateLinkEventKind kind;
    std::uint32_t object;
    std::uint32_t vtable;
    std::uint32_t caller;
    std::uint32_t requestedState;
    std::uint32_t oldDid;
    std::uint32_t newDid;
    std::uint32_t resource;
    std::uint32_t parent;
    std::uint32_t root;
    std::int32_t rectLeft;
    std::int32_t rectTop;
    std::int32_t rectRight;
    std::int32_t rectBottom;
    std::int32_t rootLeft;
    std::int32_t rootTop;
    std::int32_t rootRight;
    std::int32_t rootBottom;
    std::int32_t cursorX;
    std::int32_t cursorY;
    std::uint32_t leftButtonDown;
    std::vector<std::uint32_t> nearbyDids;
};

// Probe 2: read-only inspection of the per-control state table at +0x23C.
// 0x0069BAD0 proves this is a hash map keyed by visual-state number. We
// reproduce that lookup without applying/changing any visual state.
struct ACCustomsStateTablePropertyProbe
{
    std::uint32_t key;
    std::uint32_t node;
    std::uint32_t values[4];
    std::vector<std::uint32_t> candidateDids;
};

struct ACCustomsStateTableAttachmentProbe
{
    std::uint32_t index;
    std::uint32_t object;
    std::uint32_t vtable;
    std::vector<std::uint32_t> rawDwords;
    std::vector<std::uint32_t> candidateDids;
};

struct ACCustomsStateTableStateProbe
{
    std::uint32_t state;
    std::uint32_t record;
    std::uint32_t recordVtable;
    std::uint32_t flag0D;
    std::uint32_t propertyBuckets;
    std::uint32_t propertyBucketCount;
    std::uint32_t attachmentArray;
    std::uint32_t attachmentCapacityRaw;
    std::uint32_t attachmentCount;
    std::vector<std::uint32_t> rawDwords;
    std::vector<std::uint32_t> candidateDids;
    std::vector<ACCustomsStateTablePropertyProbe> properties;
    std::vector<ACCustomsStateTableAttachmentProbe> attachments;
};

struct ACCustomsStateTableControlProbe
{
    std::uint32_t object;
    std::uint32_t vtable;
    std::uint32_t parent;
    std::uint32_t root;
    std::int32_t rectLeft;
    std::int32_t rectTop;
    std::int32_t rectRight;
    std::int32_t rectBottom;
    std::int32_t rootLeft;
    std::int32_t rootTop;
    std::int32_t rootRight;
    std::int32_t rootBottom;
    std::uint32_t currentState;
    std::uint32_t currentRecord;
    std::uint32_t tableBuckets;
    std::uint32_t tableBucketCount;
    std::vector<std::uint32_t> observedRequestedStates;
    std::vector<std::uint32_t> observedDids;
    std::vector<ACCustomsStateTableStateProbe> states;
};

static ACCustomsStateTableStateProbe ACCustomsStateTableCaptureState(
    void* control, std::uint32_t state);
static std::uint32_t ACCustomsStateTableDirectTextureDid(
    const ACCustomsStateTableStateProbe& state);

static std::mutex g_ACCustomsStateLinkMutex;
static std::vector<ACCustomsStateLinkEvent> g_ACCustomsStateLinkEvents;
static std::atomic<bool> g_ACCustomsStateLinkActive(false);
static std::atomic<bool> g_ACCustomsStateLinkWriterRunning(false);
static std::atomic<std::uint32_t> g_ACCustomsStateLinkSequence(0);
static std::atomic<std::uint32_t> g_ACCustomsStateLinkDroppedEvents(0);
static std::string g_ACCustomsStateLinkOutputPath;
static DWORD g_ACCustomsStateLinkStartedTick = 0;
static constexpr DWORD kACCustomsStateLinkCaptureMs = 15000;
static constexpr std::size_t kACCustomsStateLinkMaxEvents = 12000;
static void* g_ACCustomsStateLinkTransitionHookTarget = nullptr;
static void* g_ACCustomsStateLinkImageSetHookTarget = nullptr;
static UiVirtual9CFn g_OriginalACCustomsStateLinkTransition = nullptr;
static Probe87ImageSetFn g_OriginalACCustomsStateLinkImageSet = nullptr;
static std::unordered_map<std::uint32_t, ACCustomsStateTableControlProbe>
    g_ACCustomsStateTableControls;
static constexpr std::size_t kACCustomsStateTableMaxControls = 256;
static constexpr std::size_t kACCustomsStateTableRawBytes = 0xA8;
static constexpr std::size_t kACCustomsStateTableMaxProperties = 128;
static constexpr std::size_t kACCustomsStateTableMaxAttachments = 32;
static constexpr std::size_t kACCustomsStateTableAttachmentRawBytes = 0x80;
static constexpr std::size_t kACCustomsStateTableNestedScanBytes = 0x60;

static std::mutex g_Probe87Mutex;
static std::unordered_map<std::uint32_t, Probe87RootEntry> g_Probe87Roots;
static std::unordered_set<std::uint32_t> g_Probe87SeenDrawNodes;
static std::unordered_map<std::uint32_t, std::uintptr_t> g_Probe87MessageHooks;
static std::unordered_set<std::uint32_t> g_Probe87RedrawnDids;
static std::unordered_set<std::uint32_t> g_Probe87RedrawnNodes;
static std::unordered_set<std::uint32_t> g_Probe87RedrawnDestSurfaces;
static std::unordered_set<std::uint32_t> g_Probe87ReplacementDids;
static std::unordered_set<std::uint32_t> g_Probe87ItemCandidateSurfaces;
static std::unordered_set<std::uint64_t> g_Probe87SeenInventoryBinds;
static std::unordered_set<std::uint64_t> g_Probe87SeenInventorySources;
static std::unordered_set<std::uint64_t> g_Probe87SeenCursorHits;
static std::unordered_set<std::uint32_t> g_Probe87SetterNodes;
static std::unordered_set<std::uint64_t> g_Probe87SeenSetterNodeDraws;
static std::unordered_map<std::uint32_t, Probe87ItemOwnerEntry> g_Probe87ItemOwners;
static std::unordered_set<std::uint32_t> g_Probe90KnownItemIds;
// Cache-entry address -> most recent theme generation for which we explicitly
// rebuilt that generated item appearance. Protected by g_Probe87Mutex.
static std::unordered_map<std::uint32_t, std::uint32_t> g_Probe92CacheThemeGeneration;

// Live Mirror item provenance. AC composes inventory/vendor icons into DID-less
// cache surfaces. During 0x0058DFB0 we record the real file-backed source DIDs
// used by that composition so the editor can map the final generated icon back
// to an editable DAT texture. Protected by g_Probe87Mutex.
struct ACCustomsItemTextureProvenance
{
    std::uint32_t primaryDid = 0;
    std::vector<std::uint32_t> componentDids;
};
static std::unordered_map<std::uint32_t, ACCustomsItemTextureProvenance>
    g_ACCustomsItemTextureProvenance;
static std::unordered_map<std::uint32_t, std::uint32_t>
    g_ACCustomsItemObjectToId;
// Stable Live Mirror correlation: during item-cache composition, remember the
// actual generated RenderSurface address that AC draws into. Later, when that
// DID-less surface is used as a normal UI blit source, this map recovers the
// owning runtime item without relying on the draw-node identity.
static std::unordered_map<std::uint32_t, std::uint32_t>
    g_ACCustomsGeneratedSurfaceToItemId;
static thread_local std::uint32_t g_ACCustomsCurrentItemRefreshId = 0;
static thread_local bool g_ACCustomsItemRebuildCaptureActive = false;
static thread_local std::uint32_t g_ACCustomsItemRebuildId = 0;
static thread_local std::vector<std::uint32_t> g_ACCustomsItemRebuildDids;
static Probe87CacheRebuildFn g_OriginalACCustomsObservedCacheRebuild = nullptr;

static std::uint32_t ACCustomsFindItemIdForImageNodeFast(void* node)
{
    if (!node)
        return 0;

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));

    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    for (const auto& kv : g_Probe87ItemOwners)
    {
        const Probe87ItemOwnerEntry& entry = kv.second;
        if (entry.imageNode != address || !entry.owner)
            continue;

        // Item-owner records can survive an AC logout/login so that owners
        // learned while the new UI is being built are not discarded by the
        // first post-login snapshot. Validate every matching record against
        // the live owner object before trusting it; stale old-session addresses
        // then become harmless even if the allocator later reuses memory.
        void* owner = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(entry.owner));
        if (!ProbeReadableRange(owner, 0x66C))
            continue;
        if (ReadUInt32(owner, 0x00) != entry.vtable)
            continue;
        if (ReadUInt32(owner, 0x668) != address)
            continue;
        if (entry.itemId != 0 && ReadUInt32(owner, 0x5FC) != entry.itemId)
            continue;

        return entry.itemId;
    }
    return 0;
}

static std::uint32_t ACCustomsItemPrimaryDid(std::uint32_t itemId)
{
    if (!itemId)
        return 0;
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    const auto it = g_ACCustomsItemTextureProvenance.find(itemId);
    return it == g_ACCustomsItemTextureProvenance.end()
        ? 0u : it->second.primaryDid;
}

static std::uint32_t ACCustomsItemIdForGeneratedSurface(
    std::uint32_t surface)
{
    if (!surface)
        return 0;
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    const auto it = g_ACCustomsGeneratedSurfaceToItemId.find(surface);
    return it == g_ACCustomsGeneratedSurfaceToItemId.end()
        ? 0u : it->second;
}

static bool Probe87SetterTraceArmed()
{
    const DWORD until = g_Probe87SetterTraceUntil.load();
    return until != 0 && static_cast<LONG>(until - GetTickCount()) > 0;
}

static bool Probe87IsImageNode(void* node)
{
    return node &&
        ProbeReadableRange(node, 0x9Cu) &&
        ReadUInt32(node, 0x00) == 0x0079E3C0u;
}

static std::string Probe87ImageResourceSummary(void* resource)
{
    if (!resource || !ProbeReadableRange(resource, 0x0C))
        return "resource=0x00000000";

    const std::uint32_t resourceAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(resource));
    const std::uint32_t resourceVtable = ReadUInt32(resource, 0x00);
    const std::uint32_t resource4 = ReadUInt32(resource, 0x04);
    const std::uint32_t underlyingAddress = ReadUInt32(resource, 0x08);

    std::string out =
        "resource=" + Hex32(resourceAddress) +
        " resourceVtable=" + Hex32(resourceVtable) +
        " resource4=" + Hex32(resource4) +
        " underlying=" + Hex32(underlyingAddress);

    if (underlyingAddress != 0)
    {
        void* underlying = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(underlyingAddress));
        if (ProbeReadableRange(underlying, 0x124))
        {
            out +=
                " underlyingVtable=" + Hex32(ReadUInt32(underlying, 0x00)) +
                " underlyingDID28=" + Hex32(ReadUInt32(underlying, 0x28)) +
                " underlyingA0=" + std::to_string(ReadUInt32(underlying, 0xA0)) +
                " underlyingA4=" + std::to_string(ReadUInt32(underlying, 0xA4)) +
                " underlyingD8=" + Hex32(ReadUInt32(underlying, 0xD8)) +
                " underlyingDC=" + Hex32(ReadUInt32(underlying, 0xDC)) +
                " underlying118=" + Hex32(ReadUInt32(underlying, 0x118)) +
                " underlying11C=" + Hex32(ReadUInt32(underlying, 0x11C)) +
                " underlying120=" + Hex32(ReadUInt32(underlying, 0x120));
        }
        else
        {
            out += " underlyingReadable=0";
        }
    }

    return out;
}

static void Probe87FinishSetterTraceIfExpired()
{
    const DWORD until = g_Probe87SetterTraceUntil.load();
    if (until == 0 || static_cast<LONG>(until - GetTickCount()) > 0)
        return;

    if (g_Probe87SetterTraceUntil.exchange(0) == 0)
        return;

    std::size_t nodes = 0;
    std::size_t draws = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        nodes = g_Probe87SetterNodes.size();
        draws = g_Probe87SeenSetterNodeDraws.size();
    }

    WriteLog(
        "PROBE #87 IMAGE_SETTER_SUMMARY reason=expired"
        " imageSetCalls=" + std::to_string(g_Probe87ImageSetCalls.load()) +
        " imageClearCalls=" + std::to_string(g_Probe87ImageClearCalls.load()) +
        " touchedImageNodes=" + std::to_string(nodes) +
        " touchedNodeDraws=" + std::to_string(g_Probe87SetterNodeDrawCalls.load()) +
        " uniqueDrawSources=" + std::to_string(draws));
}


static std::string Probe87NodeRect(void* node);

static std::uint32_t Probe87SurfaceLikeWidth(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress)
        return 0;
    void* p = reinterpret_cast<void*>(static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(p, 0xA8))
        return 0;
    return ReadUInt32(p, 0xA0);
}

static std::uint32_t Probe87SurfaceLikeHeight(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress)
        return 0;
    void* p = reinterpret_cast<void*>(static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(p, 0xA8))
        return 0;
    return ReadUInt32(p, 0xA4);
}

static bool Probe87OwnerLooksValid(
    const Probe87ItemOwnerEntry& entry,
    void*& ownerOut,
    void*& imageNodeOut)
{
    ownerOut = nullptr;
    imageNodeOut = nullptr;

    if (!entry.owner)
        return false;

    void* owner = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(entry.owner));
    if (!ProbeReadableRange(owner, 0x66C))
        return false;

    if (ReadUInt32(owner, 0x00) != entry.vtable)
        return false;

    if (entry.itemId != 0 && ReadUInt32(owner, 0x5FC) != entry.itemId)
        return false;

    const std::uint32_t imageNodeAddress = ReadUInt32(owner, 0x668);
    if (!imageNodeAddress)
        return false;

    void* imageNode = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(imageNodeAddress));
    if (!ProbeReadableRange(imageNode, 0x9C))
        return false;

    if (ReadUInt32(imageNode, 0x00) != 0x0079E3C0u)
        return false;

    ownerOut = owner;
    imageNodeOut = imageNode;
    return true;
}

static void Probe87RegisterItemOwner(void* owner)
{
    if (!owner || !ProbeReadableRange(owner, 0x66C))
        return;

    const std::uint32_t imageNodeAddress = ReadUInt32(owner, 0x668);
    if (!imageNodeAddress)
        return;

    void* imageNode = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(imageNodeAddress));
    if (!ProbeReadableRange(imageNode, 0x9C) ||
        ReadUInt32(imageNode, 0x00) != 0x0079E3C0u)
        return;

    Probe87ItemOwnerEntry entry = {};
    entry.owner = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(owner));
    entry.vtable = ReadUInt32(owner, 0x00);
    entry.itemId = ReadUInt32(owner, 0x5FC);
    entry.source600 = ReadUInt32(owner, 0x600);
    entry.imageNode = imageNodeAddress;
    entry.imageNodeVtable = ReadUInt32(imageNode, 0x00);
    entry.tid = GetCurrentThreadId();
    entry.lastSeenTick = GetTickCount();

    bool isNew = false;
    bool isNewItemId = false;
    std::size_t ownerCount = 0;
    std::size_t knownItemCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        auto it = g_Probe87ItemOwners.find(entry.owner);
        if (it == g_Probe87ItemOwners.end())
        {
            g_Probe87ItemOwners.emplace(entry.owner, entry);
            isNew = true;
        }
        else
        {
            it->second = entry;
        }

        if (entry.itemId != 0)
            isNewItemId = g_Probe90KnownItemIds.insert(entry.itemId).second;

        ownerCount = g_Probe87ItemOwners.size();
        knownItemCount = g_Probe90KnownItemIds.size();
    }

    if (isNew)
    {
        const std::uint32_t n = ++g_Probe87OwnerLearnCount;
        if (n <= 120)
        {
            WriteLog(
                "PROBE #87 ITEM_OWNER_LEARN n=" + std::to_string(n) +
                " total=" + std::to_string(ownerCount) +
                " tid=" + Hex32(entry.tid) +
                " owner=" + Hex32(entry.owner) +
                " vtable=" + Hex32(entry.vtable) +
                " itemId=" + Hex32(entry.itemId) +
                " source600=" + Hex32(entry.source600) +
                " imageNode=" + Hex32(entry.imageNode) +
                " imageRect=" + Probe87NodeRect(imageNode));
        }
    }

    if (isNewItemId)
    {
        const std::uint32_t n = ++g_Probe90KnownItemLearnCount;
        if (n <= 400)
        {
            WriteLog(
                "PROBE #90 ITEM_ID_LEARN n=" + std::to_string(n) +
                " knownItems=" + std::to_string(knownItemCount) +
                " itemId=" + Hex32(entry.itemId) +
                " owner=" + Hex32(entry.owner));
        }
    }
}

static bool Probe87CallCacheRebuildSafely(
    void* cacheEntry,
    void* itemObject)
{
    if (!cacheEntry || !itemObject ||
        !g_Probe87CacheRebuild)
        return false;

    __try
    {
        g_Probe87CacheRebuild(cacheEntry, itemObject);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool Probe87CallItemRefreshSafely(void* owner)
{
    if (!owner || !g_OriginalProbe87ItemRefresh)
        return false;

    __try
    {
        return g_OriginalProbe87ItemRefresh(owner);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool Probe87LookupCacheRawSafely(
    std::uint32_t itemId,
    void** itemObjectOut,
    void** cacheEntryOut)
{
    if (!itemObjectOut || !cacheEntryOut)
        return false;

    *itemObjectOut = nullptr;
    *cacheEntryOut = nullptr;

    if (itemId == 0 || !g_Probe87LookupItem || !g_Probe87CacheGet)
        return false;

    __try
    {
        void* itemObject = g_Probe87LookupItem(itemId);
        if (!itemObject)
            return false;

        void* cacheEntry = g_Probe87CacheGet(itemObject);
        if (!cacheEntry)
            return false;

        *itemObjectOut = itemObject;
        *cacheEntryOut = cacheEntry;
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        *itemObjectOut = nullptr;
        *cacheEntryOut = nullptr;
        return false;
    }
}

static bool Probe87LookupCacheSafely(
    std::uint32_t itemId,
    void** itemObjectOut,
    void** cacheEntryOut)
{
    if (!itemObjectOut || !cacheEntryOut)
        return false;

    *itemObjectOut = nullptr;
    *cacheEntryOut = nullptr;

    void* itemObject = nullptr;
    void* cacheEntry = nullptr;
    if (!Probe87LookupCacheRawSafely(
            itemId,
            &itemObject,
            &cacheEntry))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        g_ACCustomsItemObjectToId[static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(itemObject))] = itemId;
    }

    *itemObjectOut = itemObject;
    *cacheEntryOut = cacheEntry;
    return true;
}

static void Probe92MarkCacheCurrent(
    std::uint32_t cacheAddress,
    std::uint32_t generation)
{
    if (!cacheAddress || generation == 0)
        return;

    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    g_Probe92CacheThemeGeneration[cacheAddress] = generation;
}

static bool Probe92EnsureItemCacheCurrent(
    std::uint32_t itemId,
    const char* reason)
{
    const std::uint32_t generation =
        g_Probe92ThemeGeneration.load(std::memory_order_acquire);
    if (generation == 0 || itemId == 0 || g_Probe92LazyItemRebuildActive)
        return false;

    ++g_Probe92LazyItemChecks;

    void* itemObject = nullptr;
    void* cacheEntry = nullptr;
    if (!Probe87LookupCacheSafely(itemId, &itemObject, &cacheEntry) ||
        !ProbeReadableRange(cacheEntry, 0x28))
    {
        WriteLog(
            "PROBE #92 LAZY_ITEM_CACHE itemId=" + Hex32(itemId) +
            " generation=" + std::to_string(generation) +
            " action=LOOKUP_FAILED reason=" +
            (reason ? std::string(reason) : std::string("unspecified")));
        return false;
    }

    const std::uint32_t cacheAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(cacheEntry));

    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        const auto it = g_Probe92CacheThemeGeneration.find(cacheAddress);
        if (it != g_Probe92CacheThemeGeneration.end() &&
            it->second == generation)
        {
            return false;
        }
    }

    const std::uint32_t before20 = ReadUInt32(cacheEntry, 0x20);
    const std::uint32_t before24 = ReadUInt32(cacheEntry, 0x24);

    g_Probe92LazyItemRebuildActive = true;
    const bool ok = Probe87CallCacheRebuildSafely(cacheEntry, itemObject);
    g_Probe92LazyItemRebuildActive = false;

    const std::uint32_t after20 =
        ProbeReadableRange(cacheEntry, 0x28) ? ReadUInt32(cacheEntry, 0x20) : 0;
    const std::uint32_t after24 =
        ProbeReadableRange(cacheEntry, 0x28) ? ReadUInt32(cacheEntry, 0x24) : 0;

    if (ok)
    {
        Probe92MarkCacheCurrent(cacheAddress, generation);
        ++g_Probe92LazyItemRebuilds;
        ++g_Probe87ForcedCacheRebuildCount;
    }

    WriteLog(
        "PROBE #92 LAZY_ITEM_CACHE itemId=" + Hex32(itemId) +
        " cache=" + Hex32(cacheAddress) +
        " generation=" + std::to_string(generation) +
        " action=" + std::string(ok ? "REBUILT" : "REBUILD_FAILED") +
        " reason=" + (reason ? std::string(reason) : std::string("unspecified")) +
        " before20=" + Hex32(before20) +
        " after20=" + Hex32(after20) +
        " before24=" + Hex32(before24) +
        " after24=" + Hex32(after24));

    return ok;
}

static void Probe87ForceItemCachesOnUiThread(std::uint32_t tid)
{
    std::vector<Probe87ItemOwnerEntry> owners;
    std::vector<std::uint32_t> knownItemIds;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        for (const auto& kv : g_Probe87ItemOwners)
        {
            if (kv.second.tid == tid)
                owners.push_back(kv.second);
        }
        knownItemIds.reserve(g_Probe90KnownItemIds.size());
        for (std::uint32_t itemId : g_Probe90KnownItemIds)
            knownItemIds.push_back(itemId);
    }

    const std::uint32_t themeGeneration =
        g_Probe92ThemeGeneration.load(std::memory_order_acquire);

    WriteLog(
        "PROBE #92 ITEM_FORCE_BEGIN tid=" + Hex32(tid) +
        " generation=" + std::to_string(themeGeneration) +
        " materializedOwners=" + std::to_string(owners.size()) +
        " knownItemIds=" + std::to_string(knownItemIds.size()));

    struct CacheWork
    {
        void* cacheEntry;
        void* itemObject;
        std::uint32_t itemId;
    };

    std::vector<CacheWork> cacheWork;
    std::unordered_set<std::uint32_t> seenCacheEntries;
    std::uint32_t lookupFailures = 0;

    // Probe #90: cache rebuild is keyed from the persistent item-ID registry,
    // not from currently-live item widgets. This is the important bag-switch
    // change: recycled/hidden widgets no longer make a previously visited bag
    // disappear from the theme-switch transaction.
    for (std::uint32_t itemId : knownItemIds)
    {
        void* itemObject = nullptr;
        void* cacheEntry = nullptr;
        if (!Probe87LookupCacheSafely(itemId, &itemObject, &cacheEntry) ||
            !ProbeReadableRange(cacheEntry, 0x28))
        {
            ++lookupFailures;
            continue;
        }

        const std::uint32_t cacheAddress = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(cacheEntry));
        if (!seenCacheEntries.insert(cacheAddress).second)
            continue;

        cacheWork.push_back({cacheEntry, itemObject, itemId});
    }

    std::uint32_t rebuilt = 0;
    for (const CacheWork& work : cacheWork)
    {
        const std::uint32_t cacheAddress = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(work.cacheEntry));

        const std::uint32_t before20 =
            ProbeReadableRange(work.cacheEntry, 0x28)
            ? ReadUInt32(work.cacheEntry, 0x20) : 0;
        const std::uint32_t before24 =
            ProbeReadableRange(work.cacheEntry, 0x28)
            ? ReadUInt32(work.cacheEntry, 0x24) : 0;

        const bool ok =
            Probe87CallCacheRebuildSafely(work.cacheEntry, work.itemObject);

        const std::uint32_t after20 =
            ProbeReadableRange(work.cacheEntry, 0x28)
            ? ReadUInt32(work.cacheEntry, 0x20) : 0;
        const std::uint32_t after24 =
            ProbeReadableRange(work.cacheEntry, 0x28)
            ? ReadUInt32(work.cacheEntry, 0x24) : 0;

        if (ok)
        {
            ++rebuilt;
            ++g_Probe87ForcedCacheRebuildCount;
            Probe92MarkCacheCurrent(cacheAddress, themeGeneration);
        }

        if (rebuilt <= 120)
        {
            WriteLog(
                "PROBE #90 CACHE_REBUILD itemId=" + Hex32(work.itemId) +
                " cache=" + Hex32(cacheAddress) +
                " ok=" + std::string(ok ? "1" : "0") +
                " before20=" + Hex32(before20) +
                " after20=" + Hex32(after20) +
                " before24=" + Hex32(before24) +
                " after24=" + Hex32(after24));
        }
    }

    // Only currently materialized widgets need their image-resource pointer
    // rebound immediately. Hidden bags have no visible widget to refresh; their
    // global per-item caches have already been rebuilt above and will be used
    // when that bag is materialized later.
    std::uint32_t validOwners = 0;
    std::uint32_t refreshed = 0;
    std::uint32_t refreshOk = 0;
    for (const Probe87ItemOwnerEntry& entry : owners)
    {
        void* owner = nullptr;
        void* imageNode = nullptr;
        if (!Probe87OwnerLooksValid(entry, owner, imageNode))
            continue;

        ++validOwners;
        ++refreshed;
        ++g_Probe87ForcedOwnerRefreshCount;

        const std::uint32_t beforeResource =
            ProbeReadableRange(imageNode, 0x9C)
            ? ReadUInt32(imageNode, 0x98) : 0;

        const bool ok = Probe87CallItemRefreshSafely(owner);

        const std::uint32_t afterResource =
            ProbeReadableRange(imageNode, 0x9C)
            ? ReadUInt32(imageNode, 0x98) : 0;

        if (ok)
        {
            ++refreshOk;
            ++g_Probe87ForcedOwnerRefreshSuccess;
        }

        if (refreshed <= 120)
        {
            WriteLog(
                "PROBE #90 OWNER_REFRESH owner=" + Hex32(entry.owner) +
                " itemId=" + Hex32(entry.itemId) +
                " imageNode=" + Hex32(entry.imageNode) +
                " imageRect=" + Probe87NodeRect(imageNode) +
                " ok=" + std::string(ok ? "1" : "0") +
                " beforeResource=" + Hex32(beforeResource) +
                " afterResource=" + Hex32(afterResource));
        }
    }

    WriteLog(
        "PROBE #92 ITEM_FORCE_SUMMARY tid=" + Hex32(tid) +
        " generation=" + std::to_string(themeGeneration) +
        " knownItemIds=" + std::to_string(knownItemIds.size()) +
        " lookupFailures=" + std::to_string(lookupFailures) +
        " uniqueCaches=" + std::to_string(cacheWork.size()) +
        " cachesRebuilt=" + std::to_string(rebuilt) +
        " materializedOwners=" + std::to_string(owners.size()) +
        " validOwners=" + std::to_string(validOwners) +
        " ownersRefreshed=" + std::to_string(refreshed) +
        " ownerRefreshSuccess=" + std::to_string(refreshOk));
}

static const char* Probe87ModeName(std::uint32_t mode)
{
    switch (mode)
    {
    case 8: return "F8_ORIGINAL";
    case 9: return "F9_REPLACEMENT";
    case 12: return "ITEM_CACHE_FORCE_REFRESH";
    default: return "NONE";
    }
}

static std::uint32_t Probe87HelperSurface(void* helper)
{
    if (!helper || !ProbeReadableRange(helper, 0x08)) return 0;
    return ReadUInt32(helper, 0x04);
}

static std::uint32_t Probe87SurfaceDid(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress) return 0;
    void* surface = reinterpret_cast<void*>(static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(surface, 0x2C)) return 0;
    return ReadUInt32(surface, 0x28);
}

static std::uint32_t Probe87SurfaceField(
    std::uint32_t surfaceAddress,
    std::size_t offset)
{
    if (!surfaceAddress) return 0;
    void* surface = reinterpret_cast<void*>(static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(surface, offset + sizeof(std::uint32_t))) return 0;
    return ReadUInt32(surface, offset);
}

static std::string Probe87HelperRect(void* helper)
{
    if (!helper || !ProbeReadableRange(helper, 0x1C))
        return "(unreadable)";
    return "(" +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(helper, 0x0C))) + "," +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(helper, 0x10))) + ")-(" +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(helper, 0x14))) + "," +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(helper, 0x18))) + ")";
}


static bool Probe87HelperRectContainsPoint(
    void* helper,
    std::int32_t x,
    std::int32_t y)
{
    if (!helper || !ProbeReadableRange(helper, 0x1C))
        return false;

    const std::int32_t l =
        static_cast<std::int32_t>(ReadUInt32(helper, 0x0C));
    const std::int32_t t =
        static_cast<std::int32_t>(ReadUInt32(helper, 0x10));
    const std::int32_t r =
        static_cast<std::int32_t>(ReadUInt32(helper, 0x14));
    const std::int32_t b =
        static_cast<std::int32_t>(ReadUInt32(helper, 0x18));

    return x >= l && x < r && y >= t && y < b;
}

static bool Probe87SmallSurface(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress) return false;
    const std::uint32_t w = Probe87SurfaceField(surfaceAddress, 0x58);
    const std::uint32_t h = Probe87SurfaceField(surfaceAddress, 0x5C);
    return w >= 8 && h >= 8 && w <= 160 && h <= 160;
}

static bool Probe87ItemTraceArmed()
{
    const DWORD until = g_Probe87ItemCaptureUntil.load();
    return until != 0 && static_cast<LONG>(until - GetTickCount()) > 0;
}

static bool Probe87CandidateContains(std::uint32_t surfaceAddress)
{
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    return g_Probe87ItemCandidateSurfaces.find(surfaceAddress) !=
        g_Probe87ItemCandidateSurfaces.end();
}

static void Probe87RememberCandidate(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress) return;
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    g_Probe87ItemCandidateSurfaces.insert(surfaceAddress);
}

static void Probe87FinishItemTraceIfExpired()
{
    const DWORD until = g_Probe87ItemCaptureUntil.load();
    if (until == 0 || static_cast<LONG>(until - GetTickCount()) > 0)
        return;
    if (g_Probe87ItemCaptureUntil.exchange(0) == 0)
        return;

    std::size_t candidates = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        candidates = g_Probe87ItemCandidateSurfaces.size();
    }
    WriteLog(
        "PROBE #87 ITEM_TRACE_SUMMARY reason=expired"
        " compositeCalls=" + std::to_string(g_Probe87ItemCompositeCalls.load()) +
        " copyCalls=" + std::to_string(g_Probe87ItemCopyCalls.load()) +
        " cacheDrawCalls=" + std::to_string(g_Probe87ItemCacheDrawCalls.load()) +
        " generatedUseCalls=" + std::to_string(g_Probe87ItemGeneratedUseCalls.load()) +
        " candidateSurfaces=" + std::to_string(candidates));
}


static bool Probe87BindTraceArmed()
{
    const DWORD until = g_Probe87BindTraceUntil.load();
    return until != 0 && static_cast<LONG>(until - GetTickCount()) > 0;
}


static bool Probe87SurfaceTraceArmed()
{
    const DWORD until = g_Probe87SurfaceTraceUntil.load();
    return until != 0 && static_cast<LONG>(until - GetTickCount()) > 0;
}

static bool Probe87SmallSourceSurface(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress)
        return false;
    const std::uint32_t w = Probe87SurfaceField(surfaceAddress, 0x58);
    const std::uint32_t h = Probe87SurfaceField(surfaceAddress, 0x5C);
    return w >= 4 && h >= 4 && w <= 192 && h <= 192;
}

static bool Probe87RememberInventorySource(
    std::uint32_t nodeAddress,
    std::uint32_t sourceSurface)
{
    const std::uint64_t key =
        (static_cast<std::uint64_t>(nodeAddress) << 32) |
        static_cast<std::uint64_t>(sourceSurface);
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    return g_Probe87SeenInventorySources.insert(key).second;
}

static void Probe87FinishSurfaceTraceIfExpired()
{
    const DWORD until = g_Probe87SurfaceTraceUntil.load();
    if (until == 0 || static_cast<LONG>(until - GetTickCount()) > 0)
        return;
    if (g_Probe87SurfaceTraceUntil.exchange(0) == 0)
        return;

    std::size_t uniqueHits = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        uniqueHits = g_Probe87SeenCursorHits.size();
    }

    WriteLog(
        "PROBE #87 CURSOR_TRACE_SUMMARY reason=expired"
        " targetSeq=" + std::to_string(g_Probe87CursorTraceSeq.load()) +
        " targetRoot=" + Hex32(g_Probe87CursorTargetRoot.load()) +
        " hwnd=" + Hex32(g_Probe87CursorHwnd.load()) +
        " screen=(" + std::to_string(g_Probe87CursorScreenX.load()) + "," +
            std::to_string(g_Probe87CursorScreenY.load()) + ")" +
        " client=(" + std::to_string(g_Probe87CursorClientX.load()) + "," +
            std::to_string(g_Probe87CursorClientY.load()) + ")" +
        " local=(" + std::to_string(g_Probe87CursorLocalX.load()) + "," +
            std::to_string(g_Probe87CursorLocalY.load()) + ")" +
        " hitCalls=" + std::to_string(g_Probe87CursorHitCalls.load()) +
        " uniqueHits=" + std::to_string(uniqueHits));
}

static bool Probe87ReadNodeRect(
    void* node,
    std::int32_t& l,
    std::int32_t& t,
    std::int32_t& r,
    std::int32_t& b)
{
    if (!node || !ProbeReadableRange(node, 0x8C))
        return false;
    l = static_cast<std::int32_t>(ReadUInt32(node, 0x7C));
    t = static_cast<std::int32_t>(ReadUInt32(node, 0x80));
    r = static_cast<std::int32_t>(ReadUInt32(node, 0x84));
    b = static_cast<std::int32_t>(ReadUInt32(node, 0x88));
    return true;
}

static bool Probe87ReadNodeRect(
    void* node,
    std::int32_t& l,
    std::int32_t& t,
    std::int32_t& r,
    std::int32_t& b);

static bool Probe87LooksLikeRightPanelRoot(const Probe87RootEntry& root)
{
    void* node = reinterpret_cast<void*>(static_cast<std::uintptr_t>(root.node));
    std::int32_t l = 0, t = 0, r = 0, b = 0;
    if (!Probe87ReadNodeRect(node, l, t, r, b))
        return false;

    const std::int32_t w = r - l + 1;
    const std::int32_t h = b - t + 1;

    if (root.vtable == 0x007BC450u)
        return true;

    return l >= 1000 && w >= 250 && w <= 420 && h >= 600 && h <= 900;
}

static bool Probe87SelectRightPanelAtCursor(
    const POINT& pt,
    Probe87RootEntry& selected,
    std::int32_t& localX,
    std::int32_t& localY)
{
    bool found = false;
    std::int64_t bestArea = 0;

    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    for (const auto& kv : g_Probe87Roots)
    {
        const Probe87RootEntry& root = kv.second;
        if (!Probe87LooksLikeRightPanelRoot(root))
            continue;

        void* node = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(root.node));
        std::int32_t l = 0, t = 0, r = 0, b = 0;
        if (!Probe87ReadNodeRect(node, l, t, r, b))
            continue;

        if (pt.x < l || pt.x > r || pt.y < t || pt.y > b)
            continue;

        const std::int64_t area =
            static_cast<std::int64_t>(r - l + 1) *
            static_cast<std::int64_t>(b - t + 1);

        if (!found || area < bestArea)
        {
            found = true;
            bestArea = area;
            selected = root;
            localX = pt.x - l;
            localY = pt.y - t;
        }
    }

    return found;
}


static bool Probe87FindLikelyDidInTextureObject(
    void* textureObject,
    std::uint32_t& did,
    std::uint32_t& offset)
{
    did = 0;
    offset = 0xFFFFFFFFu;
    if (!textureObject || !ProbeReadableRange(textureObject, 0xA0))
        return false;

    for (std::uint32_t off = 0; off + 4 <= 0xA0; off += 4)
    {
        const std::uint32_t value = ReadUInt32(textureObject, off);
        if ((value & 0xFF000000u) == 0x06000000u)
        {
            did = value;
            offset = off;
            return true;
        }
    }
    return false;
}

static void Probe87FinishBindTraceIfExpired()
{
    const DWORD until = g_Probe87BindTraceUntil.load();
    if (until == 0 || static_cast<LONG>(until - GetTickCount()) > 0)
        return;
    if (g_Probe87BindTraceUntil.exchange(0) == 0)
        return;

    std::size_t unique = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        unique = g_Probe87SeenInventoryBinds.size();
    }

    WriteLog(
        "PROBE #87 INVENTORY_BIND_SUMMARY reason=expired"
        " objectBinds=" + std::to_string(g_Probe87ObjectBindCount.load()) +
        " rawBinds=" + std::to_string(g_Probe87RawBindCount.load()) +
        " uniqueBinds=" + std::to_string(unique));
}

static std::string Probe87NodeRect(void* node)
{
    if (!node || !ProbeReadableRange(node, 0x8C)) return "(unreadable)";
    return "(" + std::to_string(static_cast<std::int32_t>(ReadUInt32(node, 0x7C))) + "," +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(node, 0x80))) + ")-(" +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(node, 0x84))) + "," +
        std::to_string(static_cast<std::int32_t>(ReadUInt32(node, 0x88))) + ")";
}

static int Probe87HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool Probe87ParseReplacementName(const char* name, std::uint32_t& did)
{
    if (!name) return false;
    const std::string n(name);
    if (n.size() != 12 || n[8] != '.' ||
        !(n[9] == 'r' || n[9] == 'R') ||
        !(n[10] == 'g' || n[10] == 'G') ||
        !(n[11] == 'b' || n[11] == 'B'))
        return false;

    std::uint32_t value = 0;
    for (int i = 0; i < 8; ++i)
    {
        const int h = Probe87HexNibble(n[static_cast<std::size_t>(i)]);
        if (h < 0) return false;
        value = (value << 4) | static_cast<std::uint32_t>(h);
    }
    if ((value & 0xFF000000u) != 0x06000000u)
        return false;
    did = value;
    return true;
}

static void Probe87LoadReplacementDidSet()
{
    std::unordered_set<std::uint32_t> found;
    const std::string pattern = ACCustomsGetReplacementDirectory() + R"(\*.rgb)";
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;
            std::uint32_t did = 0;
            if (Probe87ParseReplacementName(fd.cFileName, did))
                found.insert(did);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        g_Probe87ReplacementDids.swap(found);
    }
    WriteLog("PROBE #87 REPLACEMENT_SET count=" + std::to_string(g_Probe87ReplacementDids.size()));
}

static bool Probe87IsReplacementDid(std::uint32_t did)
{
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    return g_Probe87ReplacementDids.find(did) != g_Probe87ReplacementDids.end();
}

static bool Probe87CallInvalidateSelfSafely(void* node)
{
    __try
    {
        g_Probe87InvalidateSelf(node);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static LRESULT CALLBACK Probe87GetMessageHookProc(int code, WPARAM wParam, LPARAM lParam);

static bool Probe87InstallScheduler(std::uint32_t tid)
{
    if (!tid) return false;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        if (g_Probe87MessageHooks.find(tid) != g_Probe87MessageHooks.end())
            return true;
    }

    SetLastError(ERROR_SUCCESS);
    HHOOK hook = SetWindowsHookExA(WH_GETMESSAGE, &Probe87GetMessageHookProc, nullptr, tid);
    if (!hook)
    {
        WriteLog("PROBE #87 ERROR: UI scheduler install failed tid=" + Hex32(tid) +
            " GetLastError=" + std::to_string(GetLastError()));
        return false;
    }

    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        inserted = g_Probe87MessageHooks.emplace(
            tid, reinterpret_cast<std::uintptr_t>(hook)).second;
    }
    if (!inserted)
    {
        UnhookWindowsHookEx(hook);
        return true;
    }

    WriteLog("PROBE #87 UI_THREAD_SCHEDULER installed tid=" + Hex32(tid) +
        " hook=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(hook))) +
        " message=" + Hex32(kProbe87UiMessage));
    return true;
}

static void Probe87UninstallSchedulers()
{
    std::vector<HHOOK> hooks;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        for (const auto& kv : g_Probe87MessageHooks)
            hooks.push_back(reinterpret_cast<HHOOK>(kv.second));
        g_Probe87MessageHooks.clear();
    }
    for (HHOOK hook : hooks)
        if (hook) UnhookWindowsHookEx(hook);
}

static bool Probe87FindBackedRoot(void* start, Probe87RootEntry& out)
{
    if (!start) return false;
    std::unordered_set<std::uint32_t> seen;
    void* node = start;
    for (std::uint32_t depth = 0; depth < 24; ++depth)
    {
        if (!node || !ProbeReadableRange(node, 0xB4))
            return false;
        const std::uint32_t address = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(node));
        if (!seen.insert(address).second)
            return false;

        const std::uint32_t backing = ReadUInt32(node, 0xB0);
        const std::uint32_t parent = ReadUInt32(node, 0xAC);
        if (backing != 0)
        {
            out.node = address;
            out.vtable = ReadUInt32(node, 0x00);
            out.backing = backing;
            out.parent = parent;
            out.tid = GetCurrentThreadId();
            out.depth = depth;
            out.lastSeenTick = GetTickCount();
            return true;
        }
        if (!parent)
            return false;
        node = reinterpret_cast<void*>(static_cast<std::uintptr_t>(parent));
    }
    return false;
}


static void ACCustomsSnapshotCaptureTreeRecursive(
    void* node,
    std::uint32_t depth,
    std::uint32_t siblingIndex,
    std::unordered_set<std::uint32_t>& visited,
    std::vector<ACCustomsSnapshotNode>& out)
{
    if (!node || depth > 64 || out.size() >= kACCustomsSnapshotMaxNodes)
        return;
    if (!ProbeReadableRange(node, 0x128))
        return;

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));
    if (!address || !visited.insert(address).second)
        return;

    ACCustomsSnapshotNode entry = {};
    entry.traversalIndex = static_cast<std::uint32_t>(out.size());
    entry.depth = depth;
    entry.siblingIndex = siblingIndex;
    entry.address = address;
    entry.vtable = ReadUInt32(node, 0x00);
    entry.parent = ReadUInt32(node, 0xAC);
    entry.backing = ReadUInt32(node, 0xB0);
    entry.flagsA4 = ProbeReadableRange(node, 0xA8) ? ReadUInt32(node, 0xA4) : 0;
    entry.childCount = ReadUInt32(node, 0x120);
    Probe87ReadNodeRect(node, entry.left, entry.top, entry.right, entry.bottom);

    if (ProbeReadableRange(node, 0x9C))
    {
        entry.resource = ReadUInt32(node, 0x98);
        if (entry.resource)
        {
            void* resource = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(entry.resource));
            if (ProbeReadableRange(resource, 0x0C))
            {
                entry.resourceVtable = ReadUInt32(resource, 0x00);
                entry.underlying = ReadUInt32(resource, 0x08);
                if (entry.underlying)
                {
                    void* underlying = reinterpret_cast<void*>(
                        static_cast<std::uintptr_t>(entry.underlying));
                    if (ProbeReadableRange(underlying, 0xA8))
                    {
                        const std::uint32_t candidateDid = ReadUInt32(underlying, 0x28);
                        if ((candidateDid & 0xFF000000u) == 0x06000000u)
                            entry.did = candidateDid;
                        entry.width = ReadUInt32(underlying, 0x58);
                        entry.height = ReadUInt32(underlying, 0x5C);
                        entry.underlyingA0 = ReadUInt32(underlying, 0xA0);
                        entry.underlyingA4 = ReadUInt32(underlying, 0xA4);
                    }
                }
            }
        }
    }

    Probe87RootEntry root = {};
    if (Probe87FindBackedRoot(node, root))
        entry.backedRoot = root.node;

    out.push_back(entry);

    const std::uint32_t childCount = entry.childCount;
    if (childCount == 0 || childCount > 4096)
        return;

    const std::uint32_t headRaw = ReadUInt32(node, 0x124);
    std::uint32_t link = headRaw >= 8 ? (headRaw - 8) : 0;
    std::uint32_t iter = 0;
    std::uint32_t childIndex = 0;

    while (link && iter < childCount + 8 && iter < 4096)
    {
        void* linkPtr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link));
        if (!ProbeReadableRange(linkPtr, 0x14))
            break;

        const std::uint32_t childAddress = ReadUInt32(linkPtr, 0x10);
        const std::uint32_t nextRaw = ReadUInt32(linkPtr, 0x08);
        if (childAddress)
        {
            ACCustomsSnapshotCaptureTreeRecursive(
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(childAddress)),
                depth + 1,
                childIndex,
                visited,
                out);
            ++childIndex;
        }

        link = nextRaw >= 8 ? (nextRaw - 8) : 0;
        ++iter;
    }
}

static std::vector<ACCustomsSnapshotControlState>
ACCustomsSnapshotCaptureControlStates(
    const std::vector<ACCustomsSnapshotNode>& nodes)
{
    std::unordered_map<std::uint32_t, ACCustomsLearnedControlState> learned;
    {
        std::lock_guard<std::mutex> lock(g_ACCustomsLearnedStateMutex);
        learned = g_ACCustomsLearnedStates;
    }

    std::unordered_map<std::uint32_t, const ACCustomsSnapshotNode*> nodeByAddress;
    nodeByAddress.reserve(nodes.size());
    for (const auto& node : nodes)
        if (node.address)
            nodeByAddress[node.address] = &node;

    std::vector<ACCustomsSnapshotControlState> result;
    result.reserve(learned.size());
    for (const auto& node : nodes)
    {
        if (node.vtable != 0x0079E280u || node.address == 0)
            continue;

        const auto found = learned.find(node.address);
        if (found == learned.end())
            continue;

        const ACCustomsLearnedControlState& learnedState = found->second;
        ACCustomsSnapshotControlState c = {};
        c.control = node.address;
        c.root = node.backedRoot;
        c.left = node.left;
        c.top = node.top;
        c.right = node.right;
        c.bottom = node.bottom;
        c.currentState = learnedState.currentState;
        c.presentMask = learnedState.presentMask;
        c.did1 = learnedState.did1;
        c.did2 = learnedState.did2;
        c.did3 = learnedState.did3;
        c.did6 = learnedState.did6;
        c.did7 = learnedState.did7;
        c.did8 = learnedState.did8;
        c.did13 = learnedState.did13;

        const auto rootIt = nodeByAddress.find(c.root);
        if (rootIt != nodeByAddress.end() && rootIt->second)
        {
            c.rootLeft = rootIt->second->left;
            c.rootTop = rootIt->second->top;
            c.rootRight = rootIt->second->right;
            c.rootBottom = rootIt->second->bottom;
        }
        result.push_back(c);
    }
    return result;
}

static void ACCustomsSnapshotCaptureTreeOnUiThread()
{
    if (!g_ACCustomsSnapshotActive.load(std::memory_order_acquire))
        return;

    bool expected = false;
    if (!g_ACCustomsSnapshotTreeCaptured.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    const std::uint32_t desktopAddress = g_Probe87DesktopRoot.load();
    if (!desktopAddress)
    {
        g_ACCustomsSnapshotTreeCaptured.store(false, std::memory_order_release);
        return;
    }

    void* desktop = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(desktopAddress));
    if (!ProbeReadableRange(desktop, 0x128))
    {
        g_ACCustomsSnapshotTreeCaptured.store(false, std::memory_order_release);
        return;
    }

    std::vector<ACCustomsSnapshotNode> nodes;
    nodes.reserve(1200);
    std::unordered_set<std::uint32_t> visited;
    ACCustomsSnapshotCaptureTreeRecursive(desktop, 0, 0, visited, nodes);

    // Live Mirror v1.2.6: serialize only mappings learned during natural
    // control transitions. No live state-table sweep occurs here.
    std::vector<ACCustomsSnapshotControlState> controlStates =
        ACCustomsSnapshotCaptureControlStates(nodes);

    // Snapshot capture needs a real redraw, not just whatever happened to be
    // dirty already. Mark each discovered backed root dirty once while the
    // blit recorder is armed so the next frame exposes its draw recipe.
    {
        std::unordered_set<std::uint32_t> invalidatedRoots;
        for (const ACCustomsSnapshotNode& n : nodes)
        {
            if (n.backedRoot == 0 ||
                !invalidatedRoots.insert(n.backedRoot).second)
            {
                continue;
            }

            void* root = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(n.backedRoot));
            Probe87CallInvalidateSelfSafely(root);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsSnapshotMutex);
        g_ACCustomsSnapshotNodes.swap(nodes);
        g_ACCustomsSnapshotControlStates.swap(controlStates);
    }
}

static bool ACCustomsSnapshotReadHelperRect(
    void* helper,
    std::int32_t& l,
    std::int32_t& t,
    std::int32_t& r,
    std::int32_t& b)
{
    if (!helper || !ProbeReadableRange(helper, 0x1C))
        return false;
    l = static_cast<std::int32_t>(ReadUInt32(helper, 0x0C));
    t = static_cast<std::int32_t>(ReadUInt32(helper, 0x10));
    r = static_cast<std::int32_t>(ReadUInt32(helper, 0x14));
    b = static_cast<std::int32_t>(ReadUInt32(helper, 0x18));
    return true;
}

static std::uint32_t ACCustomsSnapshotFindOwningControl(void* node)
{
    std::unordered_set<std::uint32_t> seen;
    for (std::uint32_t depth = 0; node && depth < 24; ++depth)
    {
        if (!ProbeReadableRange(node, 0xB0))
            return 0;
        const std::uint32_t address = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(node));
        if (!address || !seen.insert(address).second)
            return 0;
        if (ReadUInt32(node, 0x00) == 0x0079E280u)
            return address;
        const std::uint32_t parent = ReadUInt32(node, 0xAC);
        node = parent ? reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(parent)) : nullptr;
    }
    return 0;
}

static std::string ACCustomsSnapshotBlitKey(const ACCustomsSnapshotBlit& b)
{
    std::ostringstream out;
    out << b.node << '|'
        << b.root << '|'
        << b.sourceSurface << '|'
        << b.sourceDid << '|'
        << b.sourceLeft << ',' << b.sourceTop << ',' << b.sourceRight << ',' << b.sourceBottom << '|'
        << b.destSurface << '|'
        << b.destDid << '|'
        << b.destLeft << ',' << b.destTop << ',' << b.destRight << ',' << b.destBottom << '|'
        << b.blendMode << '|'
        << b.alphaBits;
    return out.str();
}

static void ACCustomsSnapshotRecordBlit(
    void* destHelper,
    void* sourceHelper,
    std::uint32_t blendMode,
    std::uint32_t alphaBits,
    std::uint32_t blitCaller)
{
    if (!g_ACCustomsSnapshotActive.load(std::memory_order_acquire))
        return;

    ACCustomsSnapshotBlit b = {};
    b.firstSequence = ++g_ACCustomsSnapshotSequence;
    b.callCount = 1;
    b.threadId = GetCurrentThreadId();
    b.blendMode = blendMode;
    b.alphaBits = alphaBits;
    b.blitCaller = blitCaller;

    Probe87DrawFrame* frame = nullptr;
    if (g_ACCustomsSnapshotDrawDepth > 0)
        frame = &g_ACCustomsSnapshotDrawStack[g_ACCustomsSnapshotDrawDepth - 1];

    if (frame && frame->thisPtr)
    {
        b.node = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(frame->thisPtr));
        if (ProbeReadableRange(frame->thisPtr, 4))
            b.nodeVtable = ReadUInt32(frame->thisPtr, 0x00);
        // Associate this currently-drawing node with the nearest live
        // interactive control. This is a shallow parent walk on the UI thread;
        // it does not touch the control's state table.
        b.control = ACCustomsSnapshotFindOwningControl(frame->thisPtr);
        b.itemId = ACCustomsFindItemIdForImageNodeFast(frame->thisPtr);
        b.itemPrimaryDid = ACCustomsItemPrimaryDid(b.itemId);
        Probe87ReadNodeRect(
            frame->thisPtr,
            b.nodeLeft, b.nodeTop, b.nodeRight, b.nodeBottom);
        b.drawCaller = frame->caller;

        Probe87RootEntry root = {};
        if (Probe87FindBackedRoot(frame->thisPtr, root))
        {
            b.root = root.node;
            void* rootNode = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(root.node));
            Probe87ReadNodeRect(
                rootNode,
                b.rootLeft, b.rootTop, b.rootRight, b.rootBottom);
        }
    }

    b.sourceSurface = Probe87HelperSurface(sourceHelper);
    b.sourceDid = Probe87SurfaceDid(b.sourceSurface);
    if (b.itemId == 0 && b.sourceSurface != 0 &&
        (b.sourceDid & 0xFF000000u) != 0x06000000u)
    {
        b.itemId = ACCustomsItemIdForGeneratedSurface(b.sourceSurface);
        if (b.itemId != 0)
            b.itemPrimaryDid = ACCustomsItemPrimaryDid(b.itemId);
    }
    b.sourceWidth = Probe87SurfaceField(b.sourceSurface, 0x58);
    b.sourceHeight = Probe87SurfaceField(b.sourceSurface, 0x5C);

    // Generated/surface-like objects use +0xA0/+0xA4 for geometry.
    if ((b.sourceDid & 0xFF000000u) != 0x06000000u &&
        (b.sourceWidth == 0 || b.sourceHeight == 0 ||
         b.sourceWidth > 8192 || b.sourceHeight > 8192))
    {
        const std::uint32_t generatedWidth =
            Probe87SurfaceField(b.sourceSurface, 0xA0);
        const std::uint32_t generatedHeight =
            Probe87SurfaceField(b.sourceSurface, 0xA4);

        if (generatedWidth != 0 && generatedHeight != 0 &&
            generatedWidth <= 8192 && generatedHeight <= 8192)
        {
            b.sourceWidth = generatedWidth;
            b.sourceHeight = generatedHeight;
        }
    }

    ACCustomsSnapshotReadHelperRect(
        sourceHelper,
        b.sourceLeft, b.sourceTop, b.sourceRight, b.sourceBottom);

    b.destSurface = Probe87HelperSurface(destHelper);
    b.destDid = Probe87SurfaceDid(b.destSurface);
    b.destWidth = Probe87SurfaceField(b.destSurface, 0x58);
    b.destHeight = Probe87SurfaceField(b.destSurface, 0x5C);
    ACCustomsSnapshotReadHelperRect(
        destHelper,
        b.destLeft, b.destTop, b.destRight, b.destBottom);

    // 0x00442C70 usually draws into a backed-root-local surface.  This estimate
    // is useful for the editor even before we have proven every compositor case.
    b.estimatedScreenLeft = b.destLeft + b.rootLeft;
    b.estimatedScreenTop = b.destTop + b.rootTop;
    b.estimatedScreenRight = b.destRight + b.rootLeft;
    b.estimatedScreenBottom = b.destBottom + b.rootTop;

    ++g_ACCustomsSnapshotTotalBlitCalls;
    const std::string key = ACCustomsSnapshotBlitKey(b);

    std::lock_guard<std::mutex> lock(g_ACCustomsSnapshotMutex);
    auto it = g_ACCustomsSnapshotBlitIndex.find(key);
    if (it != g_ACCustomsSnapshotBlitIndex.end())
    {
        if (it->second < g_ACCustomsSnapshotBlits.size())
            ++g_ACCustomsSnapshotBlits[it->second].callCount;
        return;
    }

    if (g_ACCustomsSnapshotBlits.size() >= kACCustomsSnapshotMaxUniqueBlits)
    {
        ++g_ACCustomsSnapshotDroppedBlits;
        return;
    }

    const std::size_t index = g_ACCustomsSnapshotBlits.size();
    g_ACCustomsSnapshotBlits.push_back(b);
    g_ACCustomsSnapshotBlitIndex.emplace(key, index);
}

static bool __fastcall HookedACCustomsSnapshotBlit(
    void* destHelper,
    void* /*edx*/,
    void* sourceHelper,
    std::uint32_t blendMode,
    std::uint32_t alphaBits)
{
    if (g_ACCustomsSnapshotActive.load(std::memory_order_acquire))
    {
        ACCustomsSnapshotRecordBlit(
            destHelper,
            sourceHelper,
            blendMode,
            alphaBits,
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress())));
    }

    return g_OriginalACCustomsSnapshotBlit(
        destHelper, sourceHelper, blendMode, alphaBits);
}

static void ACCustomsSnapshotWriteRect(
    std::ostream& out,
    std::int32_t l,
    std::int32_t t,
    std::int32_t r,
    std::int32_t b)
{
    out << '[' << l << ',' << t << ',' << r << ',' << b << ']';
}

static void ACCustomsSnapshotWriteJson()
{
    g_ACCustomsSnapshotLastWriteSucceeded.store(false, std::memory_order_release);
    g_ACCustomsSnapshotLastNodeCount.store(0, std::memory_order_release);
    g_ACCustomsSnapshotLastBlitCount.store(0, std::memory_order_release);
    g_ACCustomsSnapshotLastFileDidCount.store(0, std::memory_order_release);

    std::vector<ACCustomsSnapshotNode> nodes;
    std::vector<ACCustomsSnapshotBlit> blits;
    std::vector<ACCustomsSnapshotControlState> controlStates;
    std::unordered_map<std::uint32_t, ACCustomsItemTextureProvenance> itemProvenance;
    std::string outputPath;
    {
        std::lock_guard<std::mutex> lock(g_ACCustomsSnapshotMutex);
        nodes = g_ACCustomsSnapshotNodes;
        blits = g_ACCustomsSnapshotBlits;
        controlStates = g_ACCustomsSnapshotControlStates;
        outputPath = g_ACCustomsSnapshotOutputPath;
    }
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        itemProvenance = g_ACCustomsItemTextureProvenance;
    }

    if (outputPath.empty())
        return;

    std::unordered_set<std::uint32_t> fileDids;
    std::uint32_t generatedBlits = 0;
    std::uint32_t itemCorrelatedGeneratedBlits = 0;
    std::uint32_t imageNodes = 0;
    std::uint32_t nodesWithResourceDid = 0;
    for (const auto& n : nodes)
    {
        if (n.vtable == 0x0079E3C0u)
            ++imageNodes;
        if (n.did)
            ++nodesWithResourceDid;
    }
    for (const auto& b : blits)
    {
        if ((b.sourceDid & 0xFF000000u) == 0x06000000u)
            fileDids.insert(b.sourceDid);
        else
        {
            ++generatedBlits;
            if (b.itemId != 0)
                ++itemCorrelatedGeneratedBlits;
        }
    }
    // Learned off-screen state textures (hover/pressed/selected) must be
    // available to the Manager even when they were not drawn in this frame.
    for (const auto& c : controlStates)
    {
        const std::uint32_t dids[] =
            { c.did1, c.did2, c.did3, c.did6, c.did7, c.did8, c.did13 };
        for (const std::uint32_t did : dids)
            if ((did & 0xFF000000u) == 0x06000000u)
                fileDids.insert(did);
    }
    // Item provenance DIDs are likewise first-class editable texture assets.
    for (const auto& kv : itemProvenance)
    {
        if ((kv.second.primaryDid & 0xFF000000u) == 0x06000000u)
            fileDids.insert(kv.second.primaryDid);
        for (const std::uint32_t did : kv.second.componentDids)
            if ((did & 0xFF000000u) == 0x06000000u)
                fileDids.insert(did);
    }

    const std::vector<ACCustomsSnapshotTextureAsset> textureAssets =
        ACCustomsSnapshotDumpTextures(outputPath, fileDids);
    const std::vector<ACCustomsSnapshotGeneratedAsset> generatedAssets =
        ACCustomsSnapshotDumpGeneratedSurfaces(outputPath, blits);
    std::uint32_t dumpedTextures = 0;
    std::uint32_t failedTextureDumps = 0;
    for (const auto& asset : textureAssets)
    {
        if (asset.status == "dumped" || asset.status == "cached")
            ++dumpedTextures;
        else
            ++failedTextureDumps;
    }

    const std::string tempPath = outputPath + ".tmp";
    std::ofstream out(tempPath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out)
    {
        WriteLog("ACCUSTOMS SNAPSHOT write failed path=" + tempPath);
        return;
    }

    const DWORD elapsed = GetTickCount() - g_ACCustomsSnapshotStartedTick;
    out << "{\n";
    out << "  \"formatVersion\": 3,\n";
    out << "  \"kind\": \"ac-customs-ui-diagnostic-snapshot\",\n";
    out << "  \"activeTheme\": \"VANILLA\",\n";
    out << "  \"captureWindowMs\": " << elapsed << ",\n";
    out << "  \"desktopAddress\": \"" << Hex32(g_Probe87DesktopRoot.load()) << "\",\n";
    out << "  \"desktopThreadId\": \"" << Hex32(g_Probe87DesktopTid.load()) << "\",\n";
    out << "  \"summary\": {\n";
    out << "    \"nodeCount\": " << nodes.size() << ",\n";
    out << "    \"imageNodeCount\": " << imageNodes << ",\n";
    out << "    \"nodesWithFileTextureDid\": " << nodesWithResourceDid << ",\n";
    out << "    \"totalBlitCalls\": " << g_ACCustomsSnapshotTotalBlitCalls.load() << ",\n";
    out << "    \"uniqueBlitCount\": " << blits.size() << ",\n";
    out << "    \"uniqueFileTextureDidsInBlits\": " << fileDids.size() << ",\n";
    out << "    \"uniqueGeneratedOrDidlessBlits\": " << generatedBlits << ",\n";
    out << "    \"itemCorrelatedGeneratedBlits\": " << itemCorrelatedGeneratedBlits << ",\n";
    out << "    \"dumpedTextureCount\": " << dumpedTextures << ",\n";
    out << "    \"failedTextureDumpCount\": " << failedTextureDumps << ",\n";
    out << "    \"generatedAssetCount\": " << generatedAssets.size() << ",\n";
    out << "    \"controlStateCount\": " << controlStates.size() << ",\n";
    out << "    \"itemProvenanceCount\": " << itemProvenance.size() << ",\n";
    out << "    \"droppedUniqueBlits\": " << g_ACCustomsSnapshotDroppedBlits.load() << "\n";
    out << "  },\n";
    out << "  \"notes\": [\n";
    out << "    \"node rects are raw +0x7C/+0x80/+0x84/+0x88 values\",\n";
    out << "    \"blit source/destination rects come from 0x00442C70 helper objects\",\n";
    out << "    \"estimatedScreenDestRect adds the backed-root origin and must be validated against captures\",\n";
    out << "    \"sourceDid 0 means a generated/intermediate surface or an unresolved file texture\",\n";
    out << "    \"texture files live in <snapshot-name>_assets\\\\textures and are captured only from Vanilla\"\n";
    out << "  ],\n";

    out << "  \"textureAssets\": [\n";
    for (std::size_t i = 0; i < textureAssets.size(); ++i)
    {
        const auto& a = textureAssets[i];
        out << "    {";
        out << "\"did\":\"" << Hex32(a.did) << "\"";
        out << ",\"width\":" << a.width;
        out << ",\"height\":" << a.height;
        out << ",\"imageSize\":" << a.imageSize;
        out << ",\"pixelFormat\":\"" << Hex32(a.pixelFormat) << "\"";
        out << ",\"formatInfo\":\"" << Hex32(a.formatInfo) << "\"";
        out << ",\"file\":\"" << a.fileName << "\"";
        out << ",\"dumpStatus\":\"" << a.status << "\"";
        out << '}';
        if (i + 1 != textureAssets.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"generatedAssets\": [\n";
    for (std::size_t i = 0; i < generatedAssets.size(); ++i)
    {
        const auto& a = generatedAssets[i];
        out << "    {";
        out << "\"surface\":\"" << Hex32(a.surface) << "\"";
        out << ",\"width\":" << a.width;
        out << ",\"height\":" << a.height;
        out << ",\"imageSize\":" << a.imageSize;
        out << ",\"pixelFormat\":\"" << Hex32(a.pixelFormat) << "\"";
        out << ",\"file\":\"" << a.fileName << "\"";
        out << ",\"dumpStatus\":\"" << a.status << "\"";
        out << '}';
        if (i + 1 != generatedAssets.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"controlStates\": [\n";
    for (std::size_t i = 0; i < controlStates.size(); ++i)
    {
        const auto& c = controlStates[i];
        out << "    {\"control\":\"" << Hex32(c.control) << "\"";
        out << ",\"root\":\"" << Hex32(c.root) << "\"";
        out << ",\"rect\":";
        ACCustomsSnapshotWriteRect(out, c.left, c.top, c.right, c.bottom);
        out << ",\"rootRect\":";
        ACCustomsSnapshotWriteRect(out, c.rootLeft, c.rootTop, c.rootRight, c.rootBottom);
        out << ",\"currentState\":" << c.currentState;
        out << ",\"presentMask\":" << c.presentMask;
        out << ",\"did1\":\"" << Hex32(c.did1) << "\"";
        out << ",\"did2\":\"" << Hex32(c.did2) << "\"";
        out << ",\"did3\":\"" << Hex32(c.did3) << "\"";
        out << ",\"did6\":\"" << Hex32(c.did6) << "\"";
        out << ",\"did7\":\"" << Hex32(c.did7) << "\"";
        out << ",\"did8\":\"" << Hex32(c.did8) << "\"";
        out << ",\"did13\":\"" << Hex32(c.did13) << "\"}";
        if (i + 1 != controlStates.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"itemProvenance\": [\n";
    std::size_t itemIndex = 0;
    for (const auto& kv : itemProvenance)
    {
        out << "    {\"itemId\":\"" << Hex32(kv.first) << "\"";
        out << ",\"primaryDid\":\"" << Hex32(kv.second.primaryDid) << "\"";
        out << ",\"componentDids\":[";
        for (std::size_t n = 0; n < kv.second.componentDids.size(); ++n)
        {
            if (n) out << ',';
            out << '\"' << Hex32(kv.second.componentDids[n]) << '\"';
        }
        out << "]}";
        if (++itemIndex != itemProvenance.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"nodes\": [\n";
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        out << "    {";
        out << "\"index\":" << n.traversalIndex;
        out << ",\"depth\":" << n.depth;
        out << ",\"siblingIndex\":" << n.siblingIndex;
        out << ",\"address\":\"" << Hex32(n.address) << "\"";
        out << ",\"vtable\":\"" << Hex32(n.vtable) << "\"";
        out << ",\"parent\":\"" << Hex32(n.parent) << "\"";
        out << ",\"backing\":\"" << Hex32(n.backing) << "\"";
        out << ",\"flagsA4\":\"" << Hex32(n.flagsA4) << "\"";
        out << ",\"childCount\":" << n.childCount;
        out << ",\"rect\":";
        ACCustomsSnapshotWriteRect(out, n.left, n.top, n.right, n.bottom);
        out << ",\"resource\":\"" << Hex32(n.resource) << "\"";
        out << ",\"resourceVtable\":\"" << Hex32(n.resourceVtable) << "\"";
        out << ",\"underlying\":\"" << Hex32(n.underlying) << "\"";
        out << ",\"did\":\"" << Hex32(n.did) << "\"";
        out << ",\"surfaceWidth58\":" << n.width;
        out << ",\"surfaceHeight5C\":" << n.height;
        out << ",\"underlyingA0\":" << n.underlyingA0;
        out << ",\"underlyingA4\":" << n.underlyingA4;
        out << ",\"backedRoot\":\"" << Hex32(n.backedRoot) << "\"";
        out << '}';
        if (i + 1 != nodes.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";

    out << "  \"blits\": [\n";
    for (std::size_t i = 0; i < blits.size(); ++i)
    {
        const auto& b = blits[i];
        out << "    {";
        out << "\"firstSequence\":" << b.firstSequence;
        out << ",\"callCount\":" << b.callCount;
        out << ",\"threadId\":\"" << Hex32(b.threadId) << "\"";
        out << ",\"node\":\"" << Hex32(b.node) << "\"";
        out << ",\"nodeVtable\":\"" << Hex32(b.nodeVtable) << "\"";
        out << ",\"control\":\"" << Hex32(b.control) << "\"";
        out << ",\"itemId\":\"" << Hex32(b.itemId) << "\"";
        out << ",\"itemPrimaryDid\":\"" << Hex32(b.itemPrimaryDid) << "\"";
        out << ",\"nodeRect\":";
        ACCustomsSnapshotWriteRect(out, b.nodeLeft, b.nodeTop, b.nodeRight, b.nodeBottom);
        out << ",\"root\":\"" << Hex32(b.root) << "\"";
        out << ",\"rootRect\":";
        ACCustomsSnapshotWriteRect(out, b.rootLeft, b.rootTop, b.rootRight, b.rootBottom);
        out << ",\"sourceSurface\":\"" << Hex32(b.sourceSurface) << "\"";
        out << ",\"sourceDid\":\"" << Hex32(b.sourceDid) << "\"";
        out << ",\"sourceSize\":[" << b.sourceWidth << ',' << b.sourceHeight << ']';
        out << ",\"sourceRect\":";
        ACCustomsSnapshotWriteRect(out, b.sourceLeft, b.sourceTop, b.sourceRight, b.sourceBottom);
        out << ",\"destSurface\":\"" << Hex32(b.destSurface) << "\"";
        out << ",\"destDid\":\"" << Hex32(b.destDid) << "\"";
        out << ",\"destSize\":[" << b.destWidth << ',' << b.destHeight << ']';
        out << ",\"destRect\":";
        ACCustomsSnapshotWriteRect(out, b.destLeft, b.destTop, b.destRight, b.destBottom);
        out << ",\"estimatedScreenDestRect\":";
        ACCustomsSnapshotWriteRect(
            out,
            b.estimatedScreenLeft, b.estimatedScreenTop,
            b.estimatedScreenRight, b.estimatedScreenBottom);
        out << ",\"blendMode\":\"" << Hex32(b.blendMode) << "\"";
        out << ",\"alphaBits\":\"" << Hex32(b.alphaBits) << "\"";
        out << ",\"drawCaller\":\"" << Hex32(b.drawCaller) << "\"";
        out << ",\"blitCaller\":\"" << Hex32(b.blitCaller) << "\"";
        out << '}';
        if (i + 1 != blits.size()) out << ',';
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    out.close();

    if (!MoveFileExA(
            tempPath.c_str(),
            outputPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        WriteLog(
            "ACCUSTOMS SNAPSHOT finalize failed temp=" + tempPath +
            " final=" + outputPath +
            " GetLastError=" + std::to_string(GetLastError()));
        DeleteFileA(tempPath.c_str());
        return;
    }

    g_ACCustomsSnapshotLastNodeCount.store(
        static_cast<std::uint32_t>(nodes.size()), std::memory_order_release);
    g_ACCustomsSnapshotLastBlitCount.store(
        static_cast<std::uint32_t>(blits.size()), std::memory_order_release);
    g_ACCustomsSnapshotLastFileDidCount.store(
        static_cast<std::uint32_t>(fileDids.size()), std::memory_order_release);
    g_ACCustomsSnapshotLastWriteSucceeded.store(true, std::memory_order_release);

    WriteLog(
        "ACCUSTOMS SNAPSHOT saved path=" + outputPath +
        " nodes=" + std::to_string(nodes.size()) +
        " uniqueBlits=" + std::to_string(blits.size()) +
        " totalBlits=" + std::to_string(g_ACCustomsSnapshotTotalBlitCalls.load()) +
        " fileDids=" + std::to_string(fileDids.size()) +
        " dumpedTextures=" + std::to_string(dumpedTextures) +
        " generatedAssets=" + std::to_string(generatedAssets.size()) +
        " controlStates=" + std::to_string(controlStates.size()) +
        " itemProvenance=" + std::to_string(itemProvenance.size()) +
        " failedTextureDumps=" + std::to_string(failedTextureDumps));
}

static DWORD WINAPI ACCustomsSnapshotWriterThread(LPVOID)
{
    Sleep(g_ACCustomsSnapshotCaptureDurationMs.load(std::memory_order_acquire));
    g_ACCustomsSnapshotActive.store(false, std::memory_order_release);

    // Do not rewrite a detour prologue while a captured call may still be
    // returning through it.
    Sleep(60);
    ACCustomsSnapshotWriteJson();

    if (g_ACCustomsSnapshotBlitHookTarget)
        MH_DisableHook(g_ACCustomsSnapshotBlitHookTarget);
    if (g_CoreDrawHookTarget)
        MH_DisableHook(g_CoreDrawHookTarget);

    g_ACCustomsSnapshotLiveMirror.store(false, std::memory_order_release);
    g_ACCustomsSnapshotCaptureDurationMs.store(
        kACCustomsSnapshotCaptureMs, std::memory_order_release);
    g_ACCustomsSnapshotWriterRunning.store(false, std::memory_order_release);
    return 0;
}

static bool ACCustomsStartUiSnapshotInternal(
    const char* outputPath,
    bool requireDeveloperTests,
    DWORD captureDurationMs,
    bool liveMirror)
{
    if (!outputPath || !*outputPath ||
        !g_ACCustomsInitialized.load(std::memory_order_acquire) ||
        (requireDeveloperTests &&
         !g_DeveloperTestsEnabled.load(std::memory_order_acquire)) ||
        !g_ACCustomsSnapshotBlitHookTarget ||
        !g_CoreDrawHookTarget)
    {
        return false;
    }

    // Serialize the start of a snapshot with theme Apply/Restore. Once the
    // writer-running flag is published, Apply/Restore will reject until the
    // snapshot hooks have been disabled and the writer has finished.
    std::lock_guard<std::mutex> controlLock(
        g_ACCustomsControlMutex);

    if (g_ACCustomsApplyInProgress.load(
            std::memory_order_acquire))
    {
        WriteLog(
            "ACCUSTOMS SNAPSHOT refused: theme apply/restore in progress.");
        return false;
    }

    // Authoring snapshots must contain the real vanilla source pixels.
    // Refuse capture while a custom theme is active so the scene cannot
    // accidentally bake replacement artwork into its replay assets.
    if (g_ActiveThemeMode.load(std::memory_order_acquire) !=
        ActiveThemeMode::Vanilla)
    {
        WriteLog("ACCUSTOMS SNAPSHOT refused: Restore Vanilla before capture.");
        return false;
    }

    bool expected = false;
    if (!g_ACCustomsSnapshotWriterRunning.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsSnapshotMutex);
        g_ACCustomsSnapshotNodes.clear();
        g_ACCustomsSnapshotBlits.clear();
        g_ACCustomsSnapshotControlStates.clear();
        g_ACCustomsSnapshotBlitIndex.clear();
        g_ACCustomsSnapshotOutputPath = outputPath;
    }

    g_ACCustomsSnapshotCaptureDurationMs.store(
        captureDurationMs == 0 ? kACCustomsSnapshotCaptureMs : captureDurationMs,
        std::memory_order_release);
    g_ACCustomsSnapshotLastWriteSucceeded.store(false, std::memory_order_release);
    g_ACCustomsSnapshotLastNodeCount.store(0, std::memory_order_release);
    g_ACCustomsSnapshotLastBlitCount.store(0, std::memory_order_release);
    g_ACCustomsSnapshotLastFileDidCount.store(0, std::memory_order_release);
    g_ACCustomsSnapshotLiveMirror.store(liveMirror, std::memory_order_release);

    g_ACCustomsSnapshotSequence.store(0, std::memory_order_release);
    g_ACCustomsSnapshotTotalBlitCalls.store(0, std::memory_order_release);
    g_ACCustomsSnapshotDroppedBlits.store(0, std::memory_order_release);
    g_ACCustomsSnapshotTreeCaptured.store(false, std::memory_order_release);
    g_ACCustomsSnapshotStartedTick = GetTickCount();

    MH_STATUS status = MH_EnableHook(g_ACCustomsSnapshotBlitHookTarget);
    if (status != MH_OK && status != MH_ERROR_ENABLED)
    {
        g_ACCustomsSnapshotLiveMirror.store(false, std::memory_order_release);
        g_ACCustomsSnapshotWriterRunning.store(false, std::memory_order_release);
        return false;
    }

    status = MH_EnableHook(g_CoreDrawHookTarget);
    if (status != MH_OK && status != MH_ERROR_ENABLED)
    {
        MH_DisableHook(g_ACCustomsSnapshotBlitHookTarget);
        g_ACCustomsSnapshotLiveMirror.store(false, std::memory_order_release);
        g_ACCustomsSnapshotWriterRunning.store(false, std::memory_order_release);
        return false;
    }

    g_ACCustomsSnapshotActive.store(true, std::memory_order_release);

    HANDLE worker = CreateThread(
        nullptr, 0, &ACCustomsSnapshotWriterThread, nullptr, 0, nullptr);
    if (!worker)
    {
        g_ACCustomsSnapshotActive.store(false, std::memory_order_release);
        MH_DisableHook(g_ACCustomsSnapshotBlitHookTarget);
        MH_DisableHook(g_CoreDrawHookTarget);
        g_ACCustomsSnapshotLiveMirror.store(false, std::memory_order_release);
        g_ACCustomsSnapshotWriterRunning.store(false, std::memory_order_release);
        return false;
    }
    CloseHandle(worker);

    WriteLog("ACCUSTOMS SNAPSHOT armed path=" + std::string(outputPath));
    return true;
}

static bool ACCustomsStartUiSnapshot(const char* outputPath)
{
    return ACCustomsStartUiSnapshotInternal(
        outputPath,
        true,
        kACCustomsSnapshotCaptureMs,
        false);
}

static bool ACCustomsStartLiveMirrorSnapshot(const char* outputPath)
{
    return ACCustomsStartUiSnapshotInternal(
        outputPath,
        false,
        kACCustomsLiveMirrorCaptureMs,
        true);
}

// -----------------------------------------------------------------------------
// Live UI Manager bridge v1.
//
// The render/UI hooks never perform pipe I/O.  This independent worker accepts
// low-rate capture requests from ACModernUIManager, starts the existing short
// snapshot transaction, waits for its background writer, then acknowledges only
// after the atomically-renamed JSON and assets are ready.
// -----------------------------------------------------------------------------
static const char* kACCustomsLiveMirrorPipeName =
    R"(\\.\pipe\ACCustoms.LiveUI.v1)";
static std::atomic<bool> g_ACCustomsLiveMirrorBridgeStop(false);
static std::atomic<bool> g_ACCustomsLiveMirrorBridgeStarted(false);
static std::atomic<std::uint32_t> g_ACCustomsLiveMirrorGeneration(0);

static bool ACCustomsLiveMirrorEnsureDirectories(std::string& snapshotPath)
{
    char localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA(
        "LOCALAPPDATA", localAppData, static_cast<DWORD>(sizeof(localAppData)));
    if (length == 0 || length >= sizeof(localAppData))
        return false;

    const std::string root = std::string(localAppData) + "\\ACCustoms";
    const std::string runtime = root + "\\Runtime";
    const std::string live = runtime + "\\LiveMirror";

    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(runtime.c_str(), nullptr);
    CreateDirectoryA(live.c_str(), nullptr);

    const DWORD attrs = GetFileAttributesA(live.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return false;
    }

    snapshotPath = live + "\\live_scene.json";
    return true;
}

static bool ACCustomsLiveMirrorPipeWriteLine(HANDLE pipe, const std::string& line)
{
    const std::string wire = line + "\n";
    DWORD written = 0;
    return WriteFile(
               pipe,
               wire.data(),
               static_cast<DWORD>(wire.size()),
               &written,
               nullptr) != FALSE &&
           written == wire.size();
}

static bool ACCustomsLiveMirrorPipeReadLine(HANDLE pipe, std::string& line)
{
    line.clear();
    char ch = 0;
    while (line.size() < 1024)
    {
        DWORD read = 0;
        if (!ReadFile(pipe, &ch, 1, &read, nullptr) || read != 1)
            return false;
        if (ch == '\n')
            return true;
        if (ch != '\r')
            line.push_back(ch);
    }
    return false;
}

static DWORD WINAPI ACCustomsLiveMirrorBridgeThread(LPVOID)
{
    WriteLog("ACCUSTOMS LIVE_MIRROR bridge thread started");

    while (!g_ACCustomsLiveMirrorBridgeStop.load(std::memory_order_acquire))
    {
        HANDLE pipe = CreateNamedPipeA(
            kACCustomsLiveMirrorPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            Sleep(1000);
            continue;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!connected)
        {
            CloseHandle(pipe);
            continue;
        }

        WriteLog("ACCUSTOMS LIVE_MIRROR manager connected");
        if (!ACCustomsLiveMirrorPipeWriteLine(pipe, "HELLO 1"))
        {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            continue;
        }

        std::string command;
        while (!g_ACCustomsLiveMirrorBridgeStop.load(std::memory_order_acquire) &&
               ACCustomsLiveMirrorPipeReadLine(pipe, command))
        {
            if (command == "PING")
            {
                if (!ACCustomsLiveMirrorPipeWriteLine(pipe, "PONG"))
                    break;
                continue;
            }

            if (command == "CAPTURE")
            {
                if (g_ActiveThemeMode.load(std::memory_order_acquire) !=
                    ActiveThemeMode::Vanilla)
                {
                    if (!ACCustomsLiveMirrorPipeWriteLine(
                            pipe, "VANILLA_REQUIRED"))
                    {
                        break;
                    }
                    continue;
                }

                std::string snapshotPath;
                if (!ACCustomsLiveMirrorEnsureDirectories(snapshotPath))
                {
                    if (!ACCustomsLiveMirrorPipeWriteLine(pipe, "PATH_ERROR"))
                        break;
                    continue;
                }

                if (!ACCustomsStartLiveMirrorSnapshot(snapshotPath.c_str()))
                {
                    if (!ACCustomsLiveMirrorPipeWriteLine(pipe, "BUSY"))
                        break;
                    continue;
                }

                while (g_ACCustomsSnapshotWriterRunning.load(
                           std::memory_order_acquire) &&
                       !g_ACCustomsLiveMirrorBridgeStop.load(
                           std::memory_order_acquire))
                {
                    Sleep(20);
                }

                if (g_ACCustomsLiveMirrorBridgeStop.load(
                        std::memory_order_acquire))
                {
                    break;
                }

                const bool writeOk =
                    g_ACCustomsSnapshotLastWriteSucceeded.load(
                        std::memory_order_acquire);
                const std::uint32_t nodeCount =
                    g_ACCustomsSnapshotLastNodeCount.load(
                        std::memory_order_acquire);
                const std::uint32_t blitCount =
                    g_ACCustomsSnapshotLastBlitCount.load(
                        std::memory_order_acquire);
                const std::uint32_t fileDidCount =
                    g_ACCustomsSnapshotLastFileDidCount.load(
                        std::memory_order_acquire);

                if (!writeOk)
                {
                    WriteLog(
                        "ACCUSTOMS LIVE_MIRROR capture finalize failed");
                    if (!ACCustomsLiveMirrorPipeWriteLine(
                            pipe, "CAPTURE_ERROR"))
                    {
                        break;
                    }
                    continue;
                }

                if (nodeCount == 0 || blitCount == 0 || fileDidCount == 0)
                {
                    WriteLog(
                        "ACCUSTOMS LIVE_MIRROR empty capture nodes=" +
                        std::to_string(nodeCount) +
                        " blits=" + std::to_string(blitCount) +
                        " fileDids=" + std::to_string(fileDidCount));
                    if (!ACCustomsLiveMirrorPipeWriteLine(pipe, "EMPTY"))
                        break;
                    continue;
                }

                const std::uint32_t generation =
                    ++g_ACCustomsLiveMirrorGeneration;
                if (!ACCustomsLiveMirrorPipeWriteLine(
                        pipe,
                        "READY " + std::to_string(generation)))
                {
                    break;
                }
                continue;
            }

            if (!ACCustomsLiveMirrorPipeWriteLine(pipe, "UNKNOWN"))
                break;
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        WriteLog("ACCUSTOMS LIVE_MIRROR manager disconnected");
    }

    g_ACCustomsLiveMirrorBridgeStarted.store(false, std::memory_order_release);
    return 0;
}

static void ACCustomsStartLiveMirrorBridge()
{
    bool expected = false;
    if (!g_ACCustomsLiveMirrorBridgeStarted.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return;
    }

    g_ACCustomsLiveMirrorBridgeStop.store(false, std::memory_order_release);
    HANDLE thread = CreateThread(
        nullptr, 0, &ACCustomsLiveMirrorBridgeThread, nullptr, 0, nullptr);
    if (!thread)
    {
        g_ACCustomsLiveMirrorBridgeStarted.store(false, std::memory_order_release);
        WriteLog("ACCUSTOMS LIVE_MIRROR bridge thread create failed");
        return;
    }
    CloseHandle(thread);
}


static std::uint32_t ACCustomsStateLinkResourceDid(void* resource)
{
    if (!resource || !ProbeReadableRange(resource, 0x0C))
        return 0;

    const std::uint32_t underlyingAddress = ReadUInt32(resource, 0x08);
    if (!underlyingAddress)
        return 0;

    void* underlying = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(underlyingAddress));
    if (!ProbeReadableRange(underlying, 0x2C))
        return 0;

    const std::uint32_t did = ReadUInt32(underlying, 0x28);
    return (did & 0xFF000000u) == 0x06000000u ? did : 0;
}

static std::uint32_t ACCustomsStateLinkNodeDid(void* node)
{
    if (!node || !ProbeReadableRange(node, 0x9C))
        return 0;

    const std::uint32_t resourceAddress = ReadUInt32(node, 0x98);
    if (!resourceAddress)
        return 0;

    return ACCustomsStateLinkResourceDid(reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(resourceAddress)));
}

static void ACCustomsStateLinkCollectNearbyDidsRecursive(
    void* node,
    std::uint32_t depth,
    std::unordered_set<std::uint32_t>& visited,
    std::vector<std::uint32_t>& dids)
{
    if (!node || depth > 3 || dids.size() >= 16 ||
        !ProbeReadableRange(node, 0x128))
    {
        return;
    }

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));
    if (!address || !visited.insert(address).second)
        return;

    // This diagnostic follows the UI-node child-list layout. Do not interpret
    // arbitrary state objects as nodes unless their vtable is in the known
    // acclient UI-class range.
    const std::uint32_t vtable = ReadUInt32(node, 0x00);
    if (vtable < 0x00700000u || vtable > 0x00810000u)
        return;

    const std::uint32_t did = ACCustomsStateLinkNodeDid(node);
    if (did != 0 && std::find(dids.begin(), dids.end(), did) == dids.end())
        dids.push_back(did);

    const std::uint32_t childCount = ReadUInt32(node, 0x120);
    if (childCount == 0 || childCount > 256)
        return;

    const std::uint32_t headRaw = ReadUInt32(node, 0x124);
    std::uint32_t link = headRaw >= 8 ? (headRaw - 8) : 0;
    std::uint32_t iter = 0;
    while (link && iter < childCount + 4 && iter < 256 && dids.size() < 16)
    {
        void* linkPtr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link));
        if (!ProbeReadableRange(linkPtr, 0x14))
            break;

        const std::uint32_t childAddress = ReadUInt32(linkPtr, 0x10);
        const std::uint32_t nextRaw = ReadUInt32(linkPtr, 0x08);
        if (childAddress)
        {
            ACCustomsStateLinkCollectNearbyDidsRecursive(
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(childAddress)),
                depth + 1,
                visited,
                dids);
        }

        link = nextRaw >= 8 ? (nextRaw - 8) : 0;
        ++iter;
    }
}

static void ACCustomsStateLinkFillCommon(
    ACCustomsStateLinkEvent& event,
    void* object)
{
    event.object = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(object));
    event.threadId = GetCurrentThreadId();
    event.elapsedMs = GetTickCount() - g_ACCustomsStateLinkStartedTick;
    event.cursorX = -1;
    event.cursorY = -1;
    event.leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ? 1u : 0u;

    if (object && ProbeReadableRange(object, 4))
        event.vtable = ReadUInt32(object, 0x00);
    if (object && ProbeReadableRange(object, 0xB0))
        event.parent = ReadUInt32(object, 0xAC);

    Probe87ReadNodeRect(
        object,
        event.rectLeft, event.rectTop,
        event.rectRight, event.rectBottom);

    Probe87RootEntry root = {};
    if (Probe87FindBackedRoot(object, root))
    {
        event.root = root.node;
        void* rootNode = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(root.node));
        Probe87ReadNodeRect(
            rootNode,
            event.rootLeft, event.rootTop,
            event.rootRight, event.rootBottom);
    }

    POINT screen = {};
    if (GetCursorPos(&screen))
    {
        HWND hwnd = GetForegroundWindow();
        if (hwnd)
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(hwnd, &processId);
            POINT client = screen;
            if (processId == GetCurrentProcessId() && ScreenToClient(hwnd, &client))
            {
                event.cursorX = client.x;
                event.cursorY = client.y;
            }
        }
    }
}

static void ACCustomsStateTableAddUnique(
    std::vector<std::uint32_t>& values,
    std::uint32_t value)
{
    if (value == 0)
        return;
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

static bool ACCustomsStateTableInterestingState(std::uint32_t state)
{
    switch (state)
    {
    case 1: case 2: case 3:
    case 6: case 7: case 8:
        return true;
    default:
        return false;
    }
}

static const char* ACCustomsStateTableStateName(std::uint32_t state)
{
    switch (state)
    {
    case 1: return "normal";
    case 2: return "hover";
    case 3: return "pressed";
    case 6: return "selected";
    case 7: return "selectedHover";
    case 8: return "selectedPressed";
    case 13: return "special13";
    default: return "unknown";
    }
}

static void* ACCustomsStateTableLookup(void* control, std::uint32_t state)
{
    if (!control || !ProbeReadableRange(control, 0x37C))
        return nullptr;

    // 0x0069BAD0 receives ECX = control + 0x23C. Relative to that map,
    // buckets are +0x134 and bucket count is +0x13C. Therefore they are
    // control +0x370 and +0x378 respectively.
    const std::uint32_t bucketsAddress = ReadUInt32(control, 0x370);
    const std::uint32_t bucketCount = ReadUInt32(control, 0x378);
    if (!bucketsAddress || bucketCount == 0 || bucketCount > 4096)
        return nullptr;

    void* buckets = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(bucketsAddress));
    if (!ProbeReadableRange(
            buckets, static_cast<std::size_t>(bucketCount) * 4u))
    {
        return nullptr;
    }

    const std::uint32_t bucketIndex = state % bucketCount;
    std::uint32_t node = ReadUInt32(buckets, bucketIndex * 4u);
    for (std::uint32_t guard = 0; node && guard < 256; ++guard)
    {
        void* nodePtr = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(node));
        if (!ProbeReadableRange(nodePtr, 0x0C))
            return nullptr;

        if (ReadUInt32(nodePtr, 0x00) == state)
        {
            return reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(node + 8u));
        }

        node = ReadUInt32(nodePtr, 0x04);
    }

    return nullptr;
}

static void ACCustomsStateTableCollectCandidateFromWord(
    std::uint32_t word,
    std::vector<std::uint32_t>& dids)
{
    if ((word & 0xFF000000u) == 0x06000000u)
        ACCustomsStateTableAddUnique(dids, word);

    // Also recognize the two texture/resource layouts already proven by the
    // snapshot and replacement engine. This is diagnostic-only; values are
    // deliberately labeled candidateDids in the output.
    if (word < 0x00010000u)
        return;

    void* ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(word));
    if (!ProbeReadableRange(ptr, 0x0C))
        return;

    const std::uint32_t wrapperDid = ACCustomsStateLinkResourceDid(ptr);
    if (wrapperDid != 0)
        ACCustomsStateTableAddUnique(dids, wrapperDid);

    if (ProbeReadableRange(ptr, 0x2C))
    {
        const std::uint32_t directDid = ReadUInt32(ptr, 0x28);
        if ((directDid & 0xFF000000u) == 0x06000000u)
            ACCustomsStateTableAddUnique(dids, directDid);
    }
}

static void ACCustomsStateTableCollectAttachmentCandidates(
    void* object,
    std::vector<std::uint32_t>& dids)
{
    if (!object || !ProbeReadableRange(object, 4))
        return;

    for (std::size_t offset = 0;
         offset < kACCustomsStateTableAttachmentRawBytes;
         offset += 4)
    {
        void* field = static_cast<void*>(
            static_cast<char*>(object) + offset);
        if (!ProbeReadableRange(field, 4))
            break;

        const std::uint32_t word = ReadUInt32(object, offset);
        ACCustomsStateTableCollectCandidateFromWord(word, dids);

        // Probe 3: 0x0069D070/0x0069D268 prove that stateRecord+0x9C
        // is an array of polymorphic attachment objects.  Their payload can
        // itself be one pointer deeper, so scan only a small, bounded prefix
        // of readable pointed-to objects for candidate Type 0x06 DIDs.
        if (word < 0x00010000u)
            continue;

        void* nested = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(word));
        if (!ProbeReadableRange(nested, 4))
            continue;

        for (std::size_t nestedOffset = 0;
             nestedOffset < kACCustomsStateTableNestedScanBytes;
             nestedOffset += 4)
        {
            void* nestedField = static_cast<void*>(
                static_cast<char*>(nested) + nestedOffset);
            if (!ProbeReadableRange(nestedField, 4))
                break;
            ACCustomsStateTableCollectCandidateFromWord(
                ReadUInt32(nested, nestedOffset), dids);
        }
    }
}

static void ACCustomsStateTableCollectAttachments(
    void* stateRecord,
    ACCustomsStateTableStateProbe& outState)
{
    if (!stateRecord || !ProbeReadableRange(stateRecord, 0xA8))
        return;

    outState.attachmentArray = ReadUInt32(stateRecord, 0x9C);
    outState.attachmentCapacityRaw = ReadUInt32(stateRecord, 0xA0);
    outState.attachmentCount = ReadUInt32(stateRecord, 0xA4);

    if (!outState.attachmentArray ||
        outState.attachmentCount == 0 ||
        outState.attachmentCount > kACCustomsStateTableMaxAttachments)
    {
        return;
    }

    void* array = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(outState.attachmentArray));
    if (!ProbeReadableRange(
            array,
            static_cast<std::size_t>(outState.attachmentCount) * 4u))
    {
        return;
    }

    for (std::uint32_t i = 0; i < outState.attachmentCount; ++i)
    {
        const std::uint32_t objectAddress = ReadUInt32(array, i * 4u);
        if (!objectAddress)
            continue;

        void* object = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(objectAddress));
        if (!ProbeReadableRange(object, 4))
            continue;

        ACCustomsStateTableAttachmentProbe attachment = {};
        attachment.index = i;
        attachment.object = objectAddress;
        attachment.vtable = ReadUInt32(object, 0x00);

        for (std::size_t offset = 0;
             offset < kACCustomsStateTableAttachmentRawBytes;
             offset += 4)
        {
            void* field = static_cast<void*>(
                static_cast<char*>(object) + offset);
            if (!ProbeReadableRange(field, 4))
                break;
            attachment.rawDwords.push_back(ReadUInt32(object, offset));
        }

        ACCustomsStateTableCollectAttachmentCandidates(
            object, attachment.candidateDids);
        std::sort(
            attachment.candidateDids.begin(),
            attachment.candidateDids.end());

        for (std::uint32_t did : attachment.candidateDids)
            ACCustomsStateTableAddUnique(outState.candidateDids, did);

        outState.attachments.push_back(std::move(attachment));
    }
}

static void ACCustomsStateTableCollectPropertyMap(
    void* stateRecord,
    ACCustomsStateTableStateProbe& outState)
{
    if (!stateRecord || !ProbeReadableRange(stateRecord, 0x98))
        return;

    // State-record construction initializes a property hash map at +0x24.
    // 0x004643E0 shows its bucket pointer/count at +0x68/+0x70 relative to
    // that subobject => stateRecord +0x8C / +0x94.
    const std::uint32_t bucketsAddress = ReadUInt32(stateRecord, 0x8C);
    const std::uint32_t bucketCount = ReadUInt32(stateRecord, 0x94);
    outState.propertyBuckets = bucketsAddress;
    outState.propertyBucketCount = bucketCount;
    if (!bucketsAddress || bucketCount == 0 || bucketCount > 4096)
        return;

    void* buckets = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(bucketsAddress));
    if (!ProbeReadableRange(
            buckets, static_cast<std::size_t>(bucketCount) * 4u))
    {
        return;
    }

    std::unordered_set<std::uint32_t> visitedNodes;
    for (std::uint32_t bucket = 0; bucket < bucketCount; ++bucket)
    {
        std::uint32_t node = ReadUInt32(buckets, bucket * 4u);
        for (std::uint32_t guard = 0; node && guard < 512; ++guard)
        {
            if (!visitedNodes.insert(node).second)
                break;
            if (outState.properties.size() >= kACCustomsStateTableMaxProperties)
                return;

            void* nodePtr = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(node));
            if (!ProbeReadableRange(nodePtr, 0x0C))
                break;

            ACCustomsStateTablePropertyProbe property = {};
            property.key = ReadUInt32(nodePtr, 0x00);
            property.node = node;
            for (std::size_t i = 0; i < 4; ++i)
            {
                const std::size_t valueOffset = 0x08 + i * 4;
                void* valueField = static_cast<void*>(
                    static_cast<char*>(nodePtr) + valueOffset);
                if (!ProbeReadableRange(valueField, 4))
                    break;

                property.values[i] = ReadUInt32(nodePtr, valueOffset);
                ACCustomsStateTableCollectCandidateFromWord(
                    property.values[i], property.candidateDids);
                ACCustomsStateTableCollectCandidateFromWord(
                    property.values[i], outState.candidateDids);
            }
            outState.properties.push_back(std::move(property));
            node = ReadUInt32(nodePtr, 0x04);
        }
    }

    std::sort(
        outState.properties.begin(), outState.properties.end(),
        [](const ACCustomsStateTablePropertyProbe& a,
           const ACCustomsStateTablePropertyProbe& b)
        {
            return a.key < b.key;
        });
}

static ACCustomsStateTableStateProbe ACCustomsStateTableCaptureState(
    void* control,
    std::uint32_t state)
{
    ACCustomsStateTableStateProbe probe = {};
    probe.state = state;

    void* record = ACCustomsStateTableLookup(control, state);
    if (!record)
        return probe;

    probe.record = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(record));
    if (ProbeReadableRange(record, 0x10))
    {
        probe.recordVtable = ReadUInt32(record, 0x00);
        probe.flag0D = static_cast<std::uint32_t>(
            *reinterpret_cast<const std::uint8_t*>(
                static_cast<const char*>(record) + 0x0D));
    }

    for (std::size_t offset = 0;
         offset < kACCustomsStateTableRawBytes;
         offset += 4)
    {
        void* field = static_cast<void*>(
            static_cast<char*>(record) + offset);
        if (!ProbeReadableRange(field, 4))
            break;

        const std::uint32_t word = ReadUInt32(record, offset);
        probe.rawDwords.push_back(word);
        ACCustomsStateTableCollectCandidateFromWord(
            word, probe.candidateDids);
    }

    ACCustomsStateTableCollectPropertyMap(record, probe);
    ACCustomsStateTableCollectAttachments(record, probe);
    std::sort(probe.candidateDids.begin(), probe.candidateDids.end());
    return probe;
}

static std::uint32_t ACCustomsStateTableDirectTextureDid(
    const ACCustomsStateTableStateProbe& state)
{
    for (const auto& attachment : state.attachments)
    {
        // Probe 3 proved this attachment class is the direct image/style
        // assignment. word[1] == 5 and word[2] is the actual texture DID.
        if (attachment.vtable == 0x008020E8u &&
            attachment.rawDwords.size() >= 3 &&
            attachment.rawDwords[1] == 5u)
        {
            const std::uint32_t did = attachment.rawDwords[2];
            if ((did & 0xFF000000u) == 0x06000000u)
                return did;
        }
    }
    return 0;
}

static void ACCustomsStateTableObserveControl(
    void* control,
    std::uint32_t observedState)
{
    if (!control ||
        !ACCustomsStateTableInterestingState(observedState) ||
        !ProbeReadableRange(control, 0x408))
    {
        return;
    }

    const std::uint32_t object = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(control));
    const std::uint32_t vtable = ReadUInt32(control, 0x00);

    // Probe 2 deliberately targets the exact button/control class proven by
    // Stats and the scrollbar in Probe 1. Broaden only after this structure
    // is understood.
    if (vtable != 0x0079E280u)
        return;

    std::vector<std::uint32_t> visibleDids;
    std::unordered_set<std::uint32_t> visited;
    ACCustomsStateLinkCollectNearbyDidsRecursive(
        control, 0, visited, visibleDids);

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsStateLinkMutex);
        auto it = g_ACCustomsStateTableControls.find(object);
        if (it != g_ACCustomsStateTableControls.end())
        {
            ACCustomsStateTableAddUnique(
                it->second.observedRequestedStates, observedState);
            for (std::uint32_t did : visibleDids)
                ACCustomsStateTableAddUnique(it->second.observedDids, did);
            return;
        }

        if (g_ACCustomsStateTableControls.size() >=
            kACCustomsStateTableMaxControls)
        {
            return;
        }
    }

    ACCustomsStateTableControlProbe capture = {};
    capture.object = object;
    capture.vtable = vtable;
    capture.currentState = ReadUInt32(control, 0x400);
    capture.currentRecord = ReadUInt32(control, 0x404);
    capture.tableBuckets = ReadUInt32(control, 0x370);
    capture.tableBucketCount = ReadUInt32(control, 0x378);
    ACCustomsStateTableAddUnique(
        capture.observedRequestedStates, observedState);
    for (std::uint32_t did : visibleDids)
        ACCustomsStateTableAddUnique(capture.observedDids, did);

    ACCustomsStateLinkEvent common = {};
    ACCustomsStateLinkFillCommon(common, control);
    capture.parent = common.parent;
    capture.root = common.root;
    capture.rectLeft = common.rectLeft;
    capture.rectTop = common.rectTop;
    capture.rectRight = common.rectRight;
    capture.rectBottom = common.rectBottom;
    capture.rootLeft = common.rootLeft;
    capture.rootTop = common.rootTop;
    capture.rootRight = common.rootRight;
    capture.rootBottom = common.rootBottom;

    static const std::uint32_t statesToProbe[] =
        { 1u, 2u, 3u, 6u, 7u, 8u, 13u };
    for (std::uint32_t state : statesToProbe)
        capture.states.push_back(
            ACCustomsStateTableCaptureState(control, state));

    std::sort(capture.observedDids.begin(), capture.observedDids.end());

    std::lock_guard<std::mutex> lock(g_ACCustomsStateLinkMutex);
    auto inserted = g_ACCustomsStateTableControls.emplace(object, capture);
    if (!inserted.second)
    {
        ACCustomsStateTableAddUnique(
            inserted.first->second.observedRequestedStates, observedState);
        for (std::uint32_t did : visibleDids)
            ACCustomsStateTableAddUnique(
                inserted.first->second.observedDids, did);
    }
}

static void ACCustomsStateLinkRecord(ACCustomsStateLinkEvent&& event)
{
    if (!g_ACCustomsStateLinkActive.load(std::memory_order_acquire))
        return;

    event.sequence = ++g_ACCustomsStateLinkSequence;

    std::lock_guard<std::mutex> lock(g_ACCustomsStateLinkMutex);
    if (g_ACCustomsStateLinkEvents.size() >= kACCustomsStateLinkMaxEvents)
    {
        ++g_ACCustomsStateLinkDroppedEvents;
        return;
    }
    g_ACCustomsStateLinkEvents.push_back(std::move(event));
}

static bool ACCustomsLearnDirectStateSafe(
    void* control,
    std::uint32_t stateNumber,
    bool* presentOut,
    std::uint32_t* didOut)
{
    if (presentOut) *presentOut = false;
    if (didOut) *didOut = 0;
    if (!control || !presentOut || !didOut)
        return false;

    __try
    {
        if (!ProbeReadableRange(control, 0x37Cu) ||
            ReadUInt32(control, 0x00) != 0x0079E280u)
            return false;

        const std::uint32_t bucketsAddress = ReadUInt32(control, 0x370);
        const std::uint32_t bucketCount = ReadUInt32(control, 0x378);
        if (!bucketsAddress || bucketCount == 0 || bucketCount > 256)
            return true;

        void* buckets = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(bucketsAddress));
        if (!ProbeReadableRange(
                buckets, static_cast<std::size_t>(bucketCount) * 4u))
            return true;

        std::uint32_t node = ReadUInt32(
            buckets, (stateNumber % bucketCount) * 4u);
        void* record = nullptr;
        for (std::uint32_t guard = 0; node && guard < 32; ++guard)
        {
            void* nodePtr = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(node));
            if (!ProbeReadableRange(nodePtr, 0x0Cu))
                return true;
            if (ReadUInt32(nodePtr, 0x00) == stateNumber)
            {
                record = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(node + 8u));
                break;
            }
            node = ReadUInt32(nodePtr, 0x04);
        }
        if (!record || !ProbeReadableRange(record, 0xA8u))
            return true;

        // Probe 3 established this state-record class. Refuse to interpret any
        // other value type as an image-bearing state record.
        if (ReadUInt32(record, 0x00) != 0x00801F98u)
            return true;

        *presentOut = true;
        const std::uint32_t attachmentArray = ReadUInt32(record, 0x9C);
        const std::uint32_t attachmentCount = ReadUInt32(record, 0xA4);
        if (!attachmentArray || attachmentCount == 0 || attachmentCount > 8)
            return true;

        void* array = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(attachmentArray));
        if (!ProbeReadableRange(
                array, static_cast<std::size_t>(attachmentCount) * 4u))
            return true;

        for (std::uint32_t i = 0; i < attachmentCount; ++i)
        {
            const std::uint32_t objectAddress = ReadUInt32(array, i * 4u);
            if (!objectAddress)
                continue;
            void* object = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(objectAddress));
            if (!ProbeReadableRange(object, 0x0Cu))
                continue;
            if (ReadUInt32(object, 0x00) != 0x008020E8u ||
                ReadUInt32(object, 0x04) != 5u)
                continue;
            const std::uint32_t did = ReadUInt32(object, 0x08);
            if ((did & 0xFF000000u) == 0x06000000u)
            {
                *didOut = did;
                break;
            }
        }
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        *presentOut = false;
        *didOut = 0;
        return false;
    }
}

static void ACCustomsLearnStateTransition(
    void* control,
    std::uint32_t stateNumber)
{
    if (!control)
        return;
    switch (stateNumber)
    {
    case 1u: case 2u: case 3u: case 6u: case 7u: case 8u: case 13u:
        break;
    default:
        return;
    }

    bool present = false;
    std::uint32_t did = 0;
    if (!ACCustomsLearnDirectStateSafe(control, stateNumber, &present, &did))
        return;

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(control));
    std::lock_guard<std::mutex> lock(g_ACCustomsLearnedStateMutex);
    ACCustomsLearnedControlState& learned = g_ACCustomsLearnedStates[address];
    learned.currentState = stateNumber;
    if (present && stateNumber < 32)
        learned.presentMask |= (1u << stateNumber);
    switch (stateNumber)
    {
    case 1u: learned.did1 = did; break;
    case 2u: learned.did2 = did; break;
    case 3u: learned.did3 = did; break;
    case 6u: learned.did6 = did; break;
    case 7u: learned.did7 = did; break;
    case 8u: learned.did8 = did; break;
    case 13u: learned.did13 = did; break;
    default: break;
    }
}

static bool __fastcall HookedACCustomsStateLinkTransition(
    void* thisPtr,
    void*,
    std::uint32_t value)
{
    if (g_ACCustomsStateLinkActive.load(std::memory_order_acquire))
    {
        ACCustomsStateLinkEvent event = {};
        event.kind = ACCustomsStateLinkEventKind::StateTransition;
        event.requestedState = value;
        event.caller = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        ACCustomsStateLinkFillCommon(event, thisPtr);

        std::unordered_set<std::uint32_t> visited;
        ACCustomsStateLinkCollectNearbyDidsRecursive(
            thisPtr, 0, visited, event.nearbyDids);

        // Probe 2: inspect the control's state table read-only. This does not
        // invoke vtable+0x9C and does not change the visual state.
        ACCustomsStateTableObserveControl(thisPtr, value);

        ACCustomsStateLinkRecord(std::move(event));
    }

    const bool result = g_OriginalACCustomsStateLinkTransition(thisPtr, value);
    // Learn only the state AC just naturally applied to this known-live control.
    // This is intentionally outside the snapshot tree walk.
    ACCustomsLearnStateTransition(thisPtr, value);
    return result;
}

static void __fastcall HookedACCustomsStateLinkImageSet(
    void* thisPtr,
    void*,
    void* imageResource)
{
    if (g_ACCustomsStateLinkActive.load(std::memory_order_acquire))
    {
        ACCustomsStateLinkEvent event = {};
        event.kind = ACCustomsStateLinkEventKind::ImageSet;
        event.caller = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        event.oldDid = ACCustomsStateLinkNodeDid(thisPtr);
        event.newDid = ACCustomsStateLinkResourceDid(imageResource);
        event.resource = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(imageResource));
        ACCustomsStateLinkFillCommon(event, thisPtr);
        if (event.oldDid != 0)
            event.nearbyDids.push_back(event.oldDid);
        if (event.newDid != 0 && event.newDid != event.oldDid)
            event.nearbyDids.push_back(event.newDid);
        ACCustomsStateLinkRecord(std::move(event));
    }

    g_OriginalACCustomsStateLinkImageSet(thisPtr, imageResource);
}

static const char* ACCustomsStateLinkKindName(ACCustomsStateLinkEventKind kind)
{
    return kind == ACCustomsStateLinkEventKind::StateTransition
        ? "state" : "imageSet";
}

static void ACCustomsStateLinkWriteJson()
{
    std::vector<ACCustomsStateLinkEvent> events;
    std::vector<ACCustomsStateTableControlProbe> stateTables;
    std::string outputPath;
    {
        std::lock_guard<std::mutex> lock(g_ACCustomsStateLinkMutex);
        events = g_ACCustomsStateLinkEvents;
        outputPath = g_ACCustomsStateLinkOutputPath;
        stateTables.reserve(g_ACCustomsStateTableControls.size());
        for (const auto& kv : g_ACCustomsStateTableControls)
            stateTables.push_back(kv.second);
    }

    std::sort(
        stateTables.begin(), stateTables.end(),
        [](const ACCustomsStateTableControlProbe& a,
           const ACCustomsStateTableControlProbe& b)
        {
            if (a.root != b.root) return a.root < b.root;
            if (a.rectTop != b.rectTop) return a.rectTop < b.rectTop;
            if (a.rectLeft != b.rectLeft) return a.rectLeft < b.rectLeft;
            return a.object < b.object;
        });

    if (outputPath.empty())
        return;

    const std::string tempPath = outputPath + ".tmp";
    std::ofstream out(tempPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        WriteLog("ACCUSTOMS STATE_LINK could not open " + tempPath);
        return;
    }

    std::unordered_map<std::uint64_t, std::uint32_t> transitionCounts;
    for (const auto& e : events)
    {
        if (e.kind != ACCustomsStateLinkEventKind::ImageSet ||
            e.oldDid == 0 || e.newDid == 0 || e.oldDid == e.newDid)
        {
            continue;
        }
        const std::uint64_t key =
            (static_cast<std::uint64_t>(e.oldDid) << 32) | e.newDid;
        ++transitionCounts[key];
    }

    out << "{\n";
    out << "  \"formatVersion\": 3,\n";
    out << "  \"kind\": \"ac-customs-control-state-links\",\n";
    out << "  \"captureWindowMs\": " << kACCustomsStateLinkCaptureMs << ",\n";
    out << "  \"summary\": {\n";
    out << "    \"eventCount\": " << events.size() << ",\n";
    out << "    \"droppedEvents\": " << g_ACCustomsStateLinkDroppedEvents.load() << ",\n";
    out << "    \"observedDidTransitionCount\": " << transitionCounts.size() << ",\n";
    out << "    \"stateTableControlCount\": " << stateTables.size() << "\n";
    out << "  },\n";
    out << "  \"observedDidTransitions\": [\n";
    std::size_t transitionIndex = 0;
    for (const auto& kv : transitionCounts)
    {
        const std::uint32_t fromDid = static_cast<std::uint32_t>(kv.first >> 32);
        const std::uint32_t toDid = static_cast<std::uint32_t>(kv.first & 0xFFFFFFFFu);
        out << "    {\"from\":\"" << Hex32(fromDid)
            << "\",\"to\":\"" << Hex32(toDid)
            << "\",\"count\":" << kv.second << "}";
        if (++transitionIndex != transitionCounts.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"stateTables\": [\n";
    for (std::size_t i = 0; i < stateTables.size(); ++i)
    {
        const auto& control = stateTables[i];
        out << "    {\n";
        out << "      \"object\":\"" << Hex32(control.object) << "\",\n";
        out << "      \"vtable\":\"" << Hex32(control.vtable) << "\",\n";
        out << "      \"parent\":\"" << Hex32(control.parent) << "\",\n";
        out << "      \"root\":\"" << Hex32(control.root) << "\",\n";
        out << "      \"rect\":[" << control.rectLeft << ',' << control.rectTop << ','
            << control.rectRight << ',' << control.rectBottom << "],\n";
        out << "      \"rootRect\":[" << control.rootLeft << ',' << control.rootTop << ','
            << control.rootRight << ',' << control.rootBottom << "],\n";
        out << "      \"currentState\":" << control.currentState << ",\n";
        out << "      \"currentRecord\":\"" << Hex32(control.currentRecord) << "\",\n";
        out << "      \"tableBuckets\":\"" << Hex32(control.tableBuckets) << "\",\n";
        out << "      \"tableBucketCount\":" << control.tableBucketCount << ",\n";

        out << "      \"observedRequestedStates\":[";
        for (std::size_t j = 0;
             j < control.observedRequestedStates.size(); ++j)
        {
            if (j) out << ',';
            out << control.observedRequestedStates[j];
        }
        out << "],\n";

        out << "      \"observedDids\":[";
        for (std::size_t j = 0; j < control.observedDids.size(); ++j)
        {
            if (j) out << ',';
            out << '"' << Hex32(control.observedDids[j]) << '"';
        }
        out << "],\n";

        out << "      \"states\":[\n";
        for (std::size_t j = 0; j < control.states.size(); ++j)
        {
            const auto& state = control.states[j];
            out << "        {\"state\":" << state.state
                << ",\"name\":\""
                << ACCustomsStateTableStateName(state.state) << "\""
                << ",\"present\":" << (state.record != 0 ? 1 : 0)
                << ",\"record\":\"" << Hex32(state.record) << "\""
                << ",\"recordVtable\":\""
                << Hex32(state.recordVtable) << "\""
                << ",\"flag0D\":" << state.flag0D
                << ",\"propertyBuckets\":\""
                << Hex32(state.propertyBuckets) << "\""
                << ",\"propertyBucketCount\":"
                << state.propertyBucketCount
                << ",\"attachmentArray\":\""
                << Hex32(state.attachmentArray) << "\""
                << ",\"attachmentCapacityRaw\":\""
                << Hex32(state.attachmentCapacityRaw) << "\""
                << ",\"attachmentCount\":"
                << state.attachmentCount;

            out << ",\"candidateDids\":[";
            for (std::size_t k = 0; k < state.candidateDids.size(); ++k)
            {
                if (k) out << ',';
                out << '"' << Hex32(state.candidateDids[k]) << '"';
            }
            out << ']';

            out << ",\"rawDwords\":[";
            for (std::size_t k = 0; k < state.rawDwords.size(); ++k)
            {
                if (k) out << ',';
                out << '"' << Hex32(state.rawDwords[k]) << '"';
            }
            out << ']';

            out << ",\"properties\":[";
            for (std::size_t k = 0; k < state.properties.size(); ++k)
            {
                if (k) out << ',';
                const auto& property = state.properties[k];
                out << "{\"key\":\"" << Hex32(property.key)
                    << "\",\"node\":\"" << Hex32(property.node)
                    << "\",\"values\":[";
                for (std::size_t n = 0; n < 4; ++n)
                {
                    if (n) out << ',';
                    out << '"' << Hex32(property.values[n]) << '"';
                }
                out << "],\"candidateDids\":[";
                for (std::size_t n = 0;
                     n < property.candidateDids.size(); ++n)
                {
                    if (n) out << ',';
                    out << '"' << Hex32(property.candidateDids[n]) << '"';
                }
                out << "]}";
            }
            out << ']';

            out << ",\"attachments\":[";
            for (std::size_t k = 0; k < state.attachments.size(); ++k)
            {
                if (k) out << ',';
                const auto& attachment = state.attachments[k];
                out << "{\"index\":" << attachment.index
                    << ",\"object\":\"" << Hex32(attachment.object) << "\""
                    << ",\"vtable\":\"" << Hex32(attachment.vtable) << "\"";

                out << ",\"candidateDids\":[";
                for (std::size_t n = 0;
                     n < attachment.candidateDids.size(); ++n)
                {
                    if (n) out << ',';
                    out << '\"' << Hex32(attachment.candidateDids[n]) << '\"';
                }
                out << ']';

                out << ",\"rawDwords\":[";
                for (std::size_t n = 0;
                     n < attachment.rawDwords.size(); ++n)
                {
                    if (n) out << ',';
                    out << '\"' << Hex32(attachment.rawDwords[n]) << '\"';
                }
                out << "]}";
            }
            out << "]}";
            if (j + 1 != control.states.size()) out << ',';
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (i + 1 != stateTables.size()) out << ',';
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"events\": [\n";
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        out << "    {";
        out << "\"sequence\":" << e.sequence;
        out << ",\"elapsedMs\":" << e.elapsedMs;
        out << ",\"type\":\"" << ACCustomsStateLinkKindName(e.kind) << "\"";
        out << ",\"threadId\":\"" << Hex32(e.threadId) << "\"";
        out << ",\"object\":\"" << Hex32(e.object) << "\"";
        out << ",\"vtable\":\"" << Hex32(e.vtable) << "\"";
        out << ",\"caller\":\"" << Hex32(e.caller) << "\"";
        out << ",\"requestedState\":" << e.requestedState;
        out << ",\"oldDid\":\"" << Hex32(e.oldDid) << "\"";
        out << ",\"newDid\":\"" << Hex32(e.newDid) << "\"";
        out << ",\"resource\":\"" << Hex32(e.resource) << "\"";
        out << ",\"parent\":\"" << Hex32(e.parent) << "\"";
        out << ",\"root\":\"" << Hex32(e.root) << "\"";
        out << ",\"rect\": [" << e.rectLeft << ',' << e.rectTop << ','
            << e.rectRight << ',' << e.rectBottom << ']';
        out << ",\"rootRect\": [" << e.rootLeft << ',' << e.rootTop << ','
            << e.rootRight << ',' << e.rootBottom << ']';
        out << ",\"cursor\": [" << e.cursorX << ',' << e.cursorY << ']';
        out << ",\"leftButtonDown\":" << e.leftButtonDown;
        out << ",\"nearbyDids\": [";
        for (std::size_t j = 0; j < e.nearbyDids.size(); ++j)
        {
            if (j) out << ',';
            out << '\"' << Hex32(e.nearbyDids[j]) << '\"';
        }
        out << "]}";
        if (i + 1 != events.size()) out << ',';
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    out.close();

    if (!MoveFileExA(
            tempPath.c_str(), outputPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        WriteLog(
            "ACCUSTOMS STATE_LINK finalize failed temp=" + tempPath +
            " final=" + outputPath +
            " GetLastError=" + std::to_string(GetLastError()));
        DeleteFileA(tempPath.c_str());
        return;
    }

    WriteLog(
        "ACCUSTOMS STATE_LINK saved path=" + outputPath +
        " events=" + std::to_string(events.size()) +
        " transitions=" + std::to_string(transitionCounts.size()));
}

static DWORD WINAPI ACCustomsStateLinkWriterThread(LPVOID)
{
    Sleep(kACCustomsStateLinkCaptureMs);
    g_ACCustomsStateLinkActive.store(false, std::memory_order_release);
    Sleep(60);

    // Transition hook remains enabled for Live Mirror state learning.
    if (g_ACCustomsStateLinkImageSetHookTarget)
        MH_DisableHook(g_ACCustomsStateLinkImageSetHookTarget);

    ACCustomsStateLinkWriteJson();
    g_ACCustomsStateLinkWriterRunning.store(false, std::memory_order_release);
    return 0;
}

static bool ACCustomsStartStateLinkCapture(const char* outputPath)
{
    if (!outputPath || !*outputPath ||
        !g_ACCustomsInitialized.load(std::memory_order_acquire) ||
        !g_DeveloperTestsEnabled.load(std::memory_order_acquire) ||
        !g_ACCustomsStateLinkTransitionHookTarget ||
        !g_ACCustomsStateLinkImageSetHookTarget ||
        g_ACCustomsSnapshotWriterRunning.load(std::memory_order_acquire))
    {
        return false;
    }

    bool expected = false;
    if (!g_ACCustomsStateLinkWriterRunning.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsStateLinkMutex);
        g_ACCustomsStateLinkEvents.clear();
        g_ACCustomsStateTableControls.clear();
        g_ACCustomsStateLinkOutputPath = outputPath;
    }
    g_ACCustomsStateLinkSequence.store(0, std::memory_order_release);
    g_ACCustomsStateLinkDroppedEvents.store(0, std::memory_order_release);
    g_ACCustomsStateLinkStartedTick = GetTickCount();

    MH_STATUS status = MH_EnableHook(g_ACCustomsStateLinkTransitionHookTarget);
    if (status != MH_OK && status != MH_ERROR_ENABLED)
    {
        g_ACCustomsStateLinkWriterRunning.store(false, std::memory_order_release);
        return false;
    }

    status = MH_EnableHook(g_ACCustomsStateLinkImageSetHookTarget);
    if (status != MH_OK && status != MH_ERROR_ENABLED)
    {
        g_ACCustomsStateLinkWriterRunning.store(false, std::memory_order_release);
        return false;
    }

    g_ACCustomsStateLinkActive.store(true, std::memory_order_release);

    HANDLE worker = CreateThread(
        nullptr, 0, &ACCustomsStateLinkWriterThread, nullptr, 0, nullptr);
    if (!worker)
    {
        g_ACCustomsStateLinkActive.store(false, std::memory_order_release);
        MH_DisableHook(g_ACCustomsStateLinkImageSetHookTarget);
        g_ACCustomsStateLinkWriterRunning.store(false, std::memory_order_release);
        return false;
    }
    CloseHandle(worker);

    WriteLog(
        "ACCUSTOMS STATE_LINK armed path=" + std::string(outputPath) +
        " durationMs=" + std::to_string(kACCustomsStateLinkCaptureMs));
    return true;
}


static bool Probe87RegisterTreeBackedRoot(
    void* node,
    std::uint32_t tid,
    std::uint32_t depth,
    const char* source)
{
    if (!node || !ProbeReadableRange(node, 0xB4))
        return false;

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));
    const std::uint32_t backing = ReadUInt32(node, 0xB0);
    if (!address || !backing)
        return false;

    Probe87RootEntry entry = {};
    entry.node = address;
    entry.vtable = ReadUInt32(node, 0x00);
    entry.backing = backing;
    entry.parent = ReadUInt32(node, 0xAC);
    entry.tid = tid;
    entry.depth = depth;
    entry.lastSeenTick = GetTickCount();

    bool isNew = false;
    std::size_t rootCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        auto it = g_Probe87Roots.find(entry.node);
        if (it == g_Probe87Roots.end())
        {
            g_Probe87Roots.emplace(entry.node, entry);
            isNew = true;
        }
        else
        {
            it->second.lastSeenTick = entry.lastSeenTick;
            it->second.tid = entry.tid;
            it->second.backing = entry.backing;
            it->second.parent = entry.parent;
            it->second.vtable = entry.vtable;
        }
        rootCount = g_Probe87Roots.size();
    }

    if (isNew)
    {
        const std::uint32_t flagsA4 =
            ProbeReadableRange(node, 0xA8) ? ReadUInt32(node, 0xA4) : 0;
        WriteLog(
            "PROBE #87 ROOT_CAPTURE n=" + std::to_string(rootCount) +
            " source=" + std::string(source ? source : "TREE") +
            " tid=" + Hex32(entry.tid) +
            " root=" + Hex32(entry.node) +
            " vtable=" + Hex32(entry.vtable) +
            " depth=" + std::to_string(entry.depth) +
            " rect=" + Probe87NodeRect(node) +
            " A4=" + Hex32(flagsA4) +
            " AC=" + Hex32(entry.parent) +
            " B0=" + Hex32(entry.backing));
        Probe87InstallScheduler(entry.tid);
    }
    return isNew;
}

static void Probe87EnumerateBackedTreeRecursive(
    void* node,
    std::uint32_t tid,
    std::uint32_t depth,
    std::unordered_set<std::uint32_t>& visited,
    std::uint32_t& visitedNodes,
    std::uint32_t& backedNodes)
{
    if (!node || depth > 32 || visitedNodes >= 4096)
        return;
    if (!ProbeReadableRange(node, 0x128))
        return;

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));
    if (!address || !visited.insert(address).second)
        return;
    ++visitedNodes;

    const std::uint32_t backing = ReadUInt32(node, 0xB0);
    if (depth != 0 && backing != 0)
    {
        ++backedNodes;
        Probe87RegisterTreeBackedRoot(node, tid, depth, "DESKTOP_TREE");
        return; // Native 0x006A0B20 also stops descending at a B0-backed child.
    }

    const std::uint32_t childCount = ReadUInt32(node, 0x120);
    if (childCount == 0 || childCount > 2048)
        return;

    const std::uint32_t headRaw = ReadUInt32(node, 0x124);
    std::uint32_t link = headRaw >= 8 ? (headRaw - 8) : 0;
    std::uint32_t iter = 0;

    while (link && iter < childCount + 8 && iter < 4096)
    {
        void* linkPtr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link));
        if (!ProbeReadableRange(linkPtr, 0x14))
            break;

        const std::uint32_t childAddress = ReadUInt32(linkPtr, 0x10);
        const std::uint32_t nextRaw = ReadUInt32(linkPtr, 0x08);

        if (childAddress)
        {
            void* child = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(childAddress));
            Probe87EnumerateBackedTreeRecursive(
                child, tid, depth + 1, visited, visitedNodes, backedNodes);
        }

        link = nextRaw >= 8 ? (nextRaw - 8) : 0;
        ++iter;
    }
}

static void Probe87EnumerateDesktopTree(std::uint32_t desktopAddress, std::uint32_t tid)
{
    if (!desktopAddress || !tid)
        return;

    void* desktop = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(desktopAddress));
    if (!ProbeReadableRange(desktop, 0x128))
        return;

    std::unordered_set<std::uint32_t> visited;
    std::uint32_t visitedNodes = 0;
    std::uint32_t backedNodes = 0;
    Probe87EnumerateBackedTreeRecursive(
        desktop, tid, 0, visited, visitedNodes, backedNodes);

    const std::uint32_t pass = ++g_Probe87TreeEnumerations;
    std::size_t registered = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        registered = g_Probe87Roots.size();
    }

    if (pass <= 12)
    {
        WriteLog(
            "PROBE #87 TREE_ENUM pass=" + std::to_string(pass) +
            " tid=" + Hex32(tid) +
            " desktop=" + Hex32(desktopAddress) +
            " childCount=" + std::to_string(ReadUInt32(desktop, 0x120)) +
            " visitedNodes=" + std::to_string(visitedNodes) +
            " backedBoundaries=" + std::to_string(backedNodes) +
            " registeredRoots=" + std::to_string(registered));
    }
}

static void Probe87DiscoverRootFromDraw(void* drawThis)
{
    if (!g_Probe87CaptureArmed.load() || !drawThis)
        return;

    const std::uint32_t drawAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(drawThis));
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        if (!g_Probe87SeenDrawNodes.insert(drawAddress).second)
            return;
    }

    Probe87RootEntry entry = {};
    if (!Probe87FindBackedRoot(drawThis, entry))
        return;

    if (entry.parent != 0)
    {
        std::uint32_t expected = 0;
        if (g_Probe87DesktopRoot.compare_exchange_strong(expected, entry.parent))
        {
            g_Probe87DesktopTid.store(entry.tid);
            WriteLog(
                "PROBE #87 DESKTOP_CAPTURE tid=" + Hex32(entry.tid) +
                " desktop=" + Hex32(entry.parent) +
                " fromBackedRoot=" + Hex32(entry.node) +
                " rect=" + Probe87NodeRect(reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(entry.parent))));
        }
        if (g_Probe87DesktopRoot.load() == entry.parent)
        {
            Probe87EnumerateDesktopTree(entry.parent, entry.tid);
            // Production mode only needs the desktop pointer/TID once. Every
            // Apply/Restore re-enumerates the current tree on the owning UI thread,
            // so keeping the broad draw discovery active forever is unnecessary.
            g_Probe87CaptureArmed.store(false, std::memory_order_release);
        }
    }

    bool isNew = false;
    std::size_t rootCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        auto it = g_Probe87Roots.find(entry.node);
        if (it == g_Probe87Roots.end())
        {
            g_Probe87Roots.emplace(entry.node, entry);
            isNew = true;
        }
        else
        {
            it->second.lastSeenTick = entry.lastSeenTick;
            it->second.tid = entry.tid;
            it->second.backing = entry.backing;
            it->second.parent = entry.parent;
        }
        rootCount = g_Probe87Roots.size();
    }

    if (isNew)
    {
        void* root = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.node));
        WriteLog(
            "PROBE #87 ROOT_CAPTURE n=" + std::to_string(rootCount) +
            " tid=" + Hex32(entry.tid) +
            " root=" + Hex32(entry.node) +
            " vtable=" + Hex32(entry.vtable) +
            " depth=" + std::to_string(entry.depth) +
            " rect=" + Probe87NodeRect(root) +
            " AC=" + Hex32(entry.parent) +
            " B0=" + Hex32(entry.backing) +
            " fromDraw=" + Hex32(drawAddress));
        Probe87InstallScheduler(entry.tid);
    }
}

static void Probe87ExecuteThreadInvalidations(std::uint32_t mode)
{
    const std::uint32_t tid = GetCurrentThreadId();

    const std::uint32_t desktop = g_Probe87DesktopRoot.load();
    const std::uint32_t desktopTid = g_Probe87DesktopTid.load();
    if (desktop != 0 && (desktopTid == 0 || desktopTid == tid))
        Probe87EnumerateDesktopTree(desktop, tid);

    std::vector<Probe87RootEntry> roots;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        for (const auto& kv : g_Probe87Roots)
            if (kv.second.tid == tid)
                roots.push_back(kv.second);
    }

    WriteLog("PROBE #87 UI_MESSAGE_DISPATCH mode=" + std::string(Probe87ModeName(mode)) +
        " tid=" + Hex32(tid) + " roots=" + std::to_string(roots.size()));

    // F8/F9 rebuild every cache reachable through the persistent learned-item
    // registry. F12 keeps a manual diagnostic rebuild available. F10 is unused.
    if (mode == 8 || mode == 9 || mode == 12)
    {
        WriteLog(
            "PROBE #92 ITEM_CACHE_STAGE_BEGIN mode=" +
            std::string(Probe87ModeName(mode)) +
            " tid=" + Hex32(tid));

        Probe87ForceItemCachesOnUiThread(tid);

        WriteLog(
            "PROBE #92 ITEM_CACHE_STAGE_END mode=" +
            std::string(Probe87ModeName(mode)) +
            " tid=" + Hex32(tid));
    }

    std::uint32_t localAttempt = 0;
    std::uint32_t localComplete = 0;
    for (const Probe87RootEntry& entry : roots)
    {
        void* root = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.node));
        if (!root || !ProbeReadableRange(root, 0xB4) ||
            ReadUInt32(root, 0x00) != entry.vtable ||
            ReadUInt32(root, 0xB0) == 0)
        {
            WriteLog("PROBE #87 ROOT_SKIP mode=" + std::string(Probe87ModeName(mode)) +
                " root=" + Hex32(entry.node) + " reason=stale_or_unbacked");
            continue;
        }

        ++localAttempt;
        ++g_Probe87InvalidationAttempts;
        if (localAttempt <= 24)
        {
            WriteLog("PROBE #87 ROOT_INVALIDATE ENTER mode=" + std::string(Probe87ModeName(mode)) +
                " tid=" + Hex32(tid) +
                " root=" + Hex32(entry.node) +
                " depth=" + std::to_string(entry.depth) +
                " rect=" + Probe87NodeRect(root) +
                " AC=" + Hex32(ReadUInt32(root, 0xAC)) +
                " B0=" + Hex32(ReadUInt32(root, 0xB0)));
        }

        const bool completed = Probe87CallInvalidateSelfSafely(root);
        if (completed)
        {
            ++localComplete;
            ++g_Probe87InvalidationCompleted;
        }
        if (localAttempt <= 24)
        {
            WriteLog("PROBE #87 ROOT_INVALIDATE EXIT mode=" + std::string(Probe87ModeName(mode)) +
                " root=" + Hex32(entry.node) +
                " completed=" + std::string(completed ? "1" : "0"));
        }
    }

    WriteLog("PROBE #87 THREAD_INVALIDATE_SUMMARY mode=" + std::string(Probe87ModeName(mode)) +
        " tid=" + Hex32(tid) +
        " attempted=" + std::to_string(localAttempt) +
        " completed=" + std::to_string(localComplete));
}

static LRESULT CALLBACK Probe87GetMessageHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && lParam != 0)
    {
        MSG* msg = reinterpret_cast<MSG*>(lParam);
        if (msg->message == kProbe87UiMessage)
        {
            const std::uint32_t mode = static_cast<std::uint32_t>(msg->wParam);
            msg->message = WM_NULL;
            msg->wParam = 0;
            msg->lParam = 0;
            Probe87ExecuteThreadInvalidations(mode);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static bool Probe87NeedCorrelation()
{
    return g_Probe87CaptureArmed.load() ||
        g_Probe87AwaitRedrawMode.load() != 0 ||
        Probe87SurfaceTraceArmed() ||
        Probe87SetterTraceArmed() ||
        g_ACCustomsItemRebuildCaptureActive;
}



static void __fastcall HookedACCustomsObservedCacheRebuild(
    void* cacheEntry,
    void*,
    void* itemObject)
{
    std::uint32_t itemId = g_ACCustomsCurrentItemRefreshId;
    if (!itemId && itemObject)
    {
        const std::uint32_t objectAddress = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(itemObject));
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        const auto it = g_ACCustomsItemObjectToId.find(objectAddress);
        if (it != g_ACCustomsItemObjectToId.end())
            itemId = it->second;
    }

    const bool previousActive = g_ACCustomsItemRebuildCaptureActive;
    const std::uint32_t previousId = g_ACCustomsItemRebuildId;
    std::vector<std::uint32_t> previousDids;
    previousDids.swap(g_ACCustomsItemRebuildDids);

    g_ACCustomsItemRebuildCaptureActive = true;
    g_ACCustomsItemRebuildId = itemId;
    g_ACCustomsItemRebuildDids.clear();

    g_OriginalACCustomsObservedCacheRebuild(cacheEntry, itemObject);

    // Learn the generated item surfaces directly from the completed cache
    // entry. +0x20/+0x24 are 12-byte wrappers; wrapper+0x08 is the actual
    // generated 32x32 RenderSurface. This production path does not depend on
    // the old Probe87 blit detour.
    if (itemId && cacheEntry && ProbeReadableRange(cacheEntry, 0x28))
    {
        const std::uint32_t wrapper20 = ReadUInt32(cacheEntry, 0x20);
        const std::uint32_t wrapper24 = ReadUInt32(cacheEntry, 0x24);

        // cacheEntry+0x20/+0x24 hold 12-byte texture wrappers, not the
        // RenderSurface pointers themselves. AC's wrapper constructor at
        // 0x00694D80 stores the actual generated surface at wrapper+0x08.
        const std::uint32_t surface20 =
            wrapper20 && ProbeReadableRange(
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(wrapper20)),
                0x0C)
            ? ReadUInt32(
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(wrapper20)),
                0x08)
            : 0;
        const std::uint32_t surface24 =
            wrapper24 && ProbeReadableRange(
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(wrapper24)),
                0x0C)
            ? ReadUInt32(
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(wrapper24)),
                0x08)
            : 0;

        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        if (surface20)
            g_ACCustomsGeneratedSurfaceToItemId[surface20] = itemId;
        if (surface24)
            g_ACCustomsGeneratedSurfaceToItemId[surface24] = itemId;
    }

    if (itemId)
    {
        ACCustomsItemTextureProvenance p;
        p.componentDids = g_ACCustomsItemRebuildDids;

        // If no source-DID trace was available, preserve any direct texture
        // DIDs carried by the cache entry as conservative provenance.
        if (cacheEntry && ProbeReadableRange(cacheEntry, 0x20))
        {
            static const std::uint32_t candidateOffsets[] =
                { 0x0Cu, 0x10u, 0x14u, 0x18u, 0x1Cu };
            for (const std::uint32_t offset : candidateOffsets)
            {
                const std::uint32_t value = ReadUInt32(cacheEntry, offset);
                if ((value & 0xFF000000u) != 0x06000000u)
                    continue;
                if (std::find(p.componentDids.begin(), p.componentDids.end(), value) ==
                    p.componentDids.end())
                {
                    p.componentDids.push_back(value);
                }
            }
        }

        if (!p.componentDids.empty())
            p.primaryDid = p.componentDids.front();

        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        if (p.primaryDid || !p.componentDids.empty())
            g_ACCustomsItemTextureProvenance[itemId] = std::move(p);
        if (itemObject)
        {
            g_ACCustomsItemObjectToId[static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(itemObject))] = itemId;
        }
    }

    g_ACCustomsItemRebuildDids.clear();
    g_ACCustomsItemRebuildDids.swap(previousDids);
    g_ACCustomsItemRebuildId = previousId;
    g_ACCustomsItemRebuildCaptureActive = previousActive;
}

static bool __fastcall HookedProbe87ItemRefresh(
    void* owner,
    void* /*edx*/)
{
    Probe87RegisterItemOwner(owner);

    const std::uint32_t previousItemId = g_ACCustomsCurrentItemRefreshId;
    g_ACCustomsCurrentItemRefreshId =
        (owner && ProbeReadableRange(owner, 0x600))
            ? ReadUInt32(owner, 0x5FC) : 0;

    // Probe #92: this closes the untouched-inventory gap. If this widget is
    // being materialized for the first time after F8/F9, its item ID was not
    // present during the bulk transaction. Repair the per-item generated cache
    // NOW, before the original 0x004E2D10 binds that cache to [owner+0x668].
    if (owner && ProbeReadableRange(owner, 0x600))
    {
        const std::uint32_t itemId = ReadUInt32(owner, 0x5FC);
        Probe92EnsureItemCacheCurrent(itemId, "ITEM_REFRESH");
    }

    const bool result = g_OriginalProbe87ItemRefresh(owner);
    g_ACCustomsCurrentItemRefreshId = previousItemId;
    return result;
}

static void __fastcall HookedProbe87ImageSet(
    void* thisPtr,
    void* /*edx*/,
    void* imageResource)
{
    const bool armed = Probe87SetterTraceArmed();
    const bool imageNode = armed && Probe87IsImageNode(thisPtr);

    std::uint32_t callN = 0;
    std::uint32_t caller = 0;
    std::uint32_t nodeAddress = 0;
    std::uint32_t oldResourceAddress = 0;

    if (imageNode)
    {
        callN = ++g_Probe87ImageSetCalls;
        caller = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        nodeAddress = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(thisPtr));
        oldResourceAddress = ReadUInt32(thisPtr, 0x98);

        {
            std::lock_guard<std::mutex> lock(g_Probe87Mutex);
            g_Probe87SetterNodes.insert(nodeAddress);
        }

        if (callN <= 300)
        {
            Probe87RootEntry root = {};
            const bool haveRoot = Probe87FindBackedRoot(thisPtr, root);

            WriteLog(
                "PROBE #87 IMAGE_SET ENTER n=" + std::to_string(callN) +
                " tid=" + Hex32(GetCurrentThreadId()) +
                " caller=" + Hex32(caller) +
                " node=" + Hex32(nodeAddress) +
                " nodeRect=" + Probe87NodeRect(thisPtr) +
                " parent=" + Hex32(ReadUInt32(thisPtr, 0xAC)) +
                " root=" + Hex32(haveRoot ? root.node : 0) +
                " rootRect=" + (haveRoot
                    ? Probe87NodeRect(reinterpret_cast<void*>(
                        static_cast<std::uintptr_t>(root.node)))
                    : std::string("(none)")) +
                " oldResource=" + Hex32(oldResourceAddress) +
                " " + Probe87ImageResourceSummary(imageResource));
        }
    }

    g_OriginalProbe87ImageSet(thisPtr, imageResource);

    if (imageNode && callN <= 300)
    {
        WriteLog(
            "PROBE #87 IMAGE_SET EXIT n=" + std::to_string(callN) +
            " node=" + Hex32(nodeAddress) +
            " node98=" + Hex32(
                ProbeReadableRange(thisPtr, 0x9C) ?
                ReadUInt32(thisPtr, 0x98) : 0));
    }
}

static void __fastcall HookedProbe87ImageClear(
    void* thisPtr,
    void* /*edx*/)
{
    const bool imageNode =
        Probe87SetterTraceArmed() && Probe87IsImageNode(thisPtr);

    std::uint32_t callN = 0;
    std::uint32_t nodeAddress = 0;

    if (imageNode)
    {
        callN = ++g_Probe87ImageClearCalls;
        nodeAddress = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(thisPtr));

        {
            std::lock_guard<std::mutex> lock(g_Probe87Mutex);
            g_Probe87SetterNodes.insert(nodeAddress);
        }

        if (callN <= 200)
        {
            void* oldResource = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(ReadUInt32(thisPtr, 0x98)));

            WriteLog(
                "PROBE #87 IMAGE_CLEAR ENTER n=" + std::to_string(callN) +
                " tid=" + Hex32(GetCurrentThreadId()) +
                " caller=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(_ReturnAddress()))) +
                " node=" + Hex32(nodeAddress) +
                " nodeRect=" + Probe87NodeRect(thisPtr) +
                " parent=" + Hex32(ReadUInt32(thisPtr, 0xAC)) +
                " " + Probe87ImageResourceSummary(oldResource));
        }
    }

    g_OriginalProbe87ImageClear(thisPtr);

    if (imageNode && callN <= 200)
    {
        WriteLog(
            "PROBE #87 IMAGE_CLEAR EXIT n=" + std::to_string(callN) +
            " node=" + Hex32(nodeAddress) +
            " node98=" + Hex32(
                ProbeReadableRange(thisPtr, 0x9C) ?
                ReadUInt32(thisPtr, 0x98) : 0));
    }
}

static void __fastcall HookedProbe87Draw(
    void* thisPtr, void* /*edx*/, void* arg1, void* arg2, void* arg3, void* arg4)
{
    if (g_Probe87CaptureArmed.load())
        Probe87DiscoverRootFromDraw(thisPtr);

    bool pushed = false;
    if (Probe87NeedCorrelation() && g_Probe87DrawDepth < 8)
    {
        Probe87DrawFrame& frame = g_Probe87DrawStack[g_Probe87DrawDepth++];
        frame.thisPtr = thisPtr;
        frame.arg1 = arg1;
        frame.arg2 = arg2;
        frame.arg3 = arg3;
        frame.arg4 = arg4;
        frame.caller = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        pushed = true;
    }

    g_OriginalProbe87Draw(thisPtr, arg1, arg2, arg3, arg4);

    if (pushed && g_Probe87DrawDepth > 0)
        --g_Probe87DrawDepth;
}



static void __fastcall HookedProbe87ObjectBind(
    void* renderer,
    void* /*edx*/,
    std::uint32_t stage,
    void* textureObject)
{
    if (Probe87BindTraceArmed() && stage == 0 && textureObject &&
        g_Probe87DrawDepth > 0)
    {
        Probe87DrawFrame& frame = g_Probe87DrawStack[g_Probe87DrawDepth - 1];
        Probe87RootEntry root = {};
        if (frame.thisPtr &&
            Probe87FindBackedRoot(frame.thisPtr, root) &&
            Probe87LooksLikeRightPanelRoot(root) &&
            ProbeReadableRange(textureObject, 0xA0))
        {
            ++g_Probe87ObjectBindCount;

            const std::uint32_t nodeAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(frame.thisPtr));
            const std::uint32_t texAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(textureObject));
            const std::uint64_t key =
                (static_cast<std::uint64_t>(nodeAddress) << 32) |
                static_cast<std::uint64_t>(texAddress);

            bool first = false;
            std::size_t uniqueN = 0;
            {
                std::lock_guard<std::mutex> lock(g_Probe87Mutex);
                first = g_Probe87SeenInventoryBinds.insert(key).second;
                uniqueN = g_Probe87SeenInventoryBinds.size();
            }

            if (first && uniqueN <= 600)
            {
                std::uint32_t possibleDid = 0;
                std::uint32_t possibleDidOffset = 0xFFFFFFFFu;
                Probe87FindLikelyDidInTextureObject(
                    textureObject, possibleDid, possibleDidOffset);

                const std::uint32_t texType = ReadUInt32(textureObject, 0x58);
                const std::uint32_t levels = ReadUInt32(textureObject, 0x5C);
                const std::uint32_t width = ReadUInt32(textureObject, 0x88);
                const std::uint32_t height = ReadUInt32(textureObject, 0x8C);
                const std::uint32_t d3d98 = ReadUInt32(textureObject, 0x98);
                const std::uint32_t d3d9C = ReadUInt32(textureObject, 0x9C);

                WriteLog(
                    "PROBE #87 INVENTORY_OBJECT_BIND uniqueN=" +
                    std::to_string(uniqueN) +
                    " tid=" + Hex32(GetCurrentThreadId()) +
                    " node=" + Hex32(nodeAddress) +
                    " nodeVtable=" + Hex32(
                        ProbeReadableRange(frame.thisPtr, 4) ?
                        ReadUInt32(frame.thisPtr, 0x00) : 0) +
                    " nodeRect=" + Probe87NodeRect(frame.thisPtr) +
                    " drawCaller=" + Hex32(frame.caller) +
                    " root=" + Hex32(root.node) +
                    " rootRect=" + Probe87NodeRect(
                        reinterpret_cast<void*>(
                            static_cast<std::uintptr_t>(root.node))) +
                    " textureObject=" + Hex32(texAddress) +
                    " type=" + Hex32(texType) +
                    " levels=" + std::to_string(levels) +
                    " wh=" + std::to_string(width) + "x" + std::to_string(height) +
                    " d3d98=" + Hex32(d3d98) +
                    " d3d9C=" + Hex32(d3d9C) +
                    " possibleDID=" + Hex32(possibleDid) +
                    " possibleDIDOffset=" + Hex32(possibleDidOffset) +
                    " bindCaller=" + Hex32(static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
            }
        }
    }

    g_OriginalProbe87ObjectBind(renderer, stage, textureObject);
}

static void __fastcall HookedProbe87RawBind(
    void* renderer,
    void* /*edx*/,
    std::uint32_t stage,
    void* d3dTexture)
{
    if (Probe87BindTraceArmed() && stage == 0 && d3dTexture &&
        g_Probe87DrawDepth > 0)
    {
        Probe87DrawFrame& frame = g_Probe87DrawStack[g_Probe87DrawDepth - 1];
        Probe87RootEntry root = {};
        if (frame.thisPtr &&
            Probe87FindBackedRoot(frame.thisPtr, root) &&
            Probe87LooksLikeRightPanelRoot(root))
        {
            ++g_Probe87RawBindCount;

            const std::uint32_t nodeAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(frame.thisPtr));
            const std::uint32_t texAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(d3dTexture));
            const std::uint64_t key =
                0x8000000000000000ull ^
                (static_cast<std::uint64_t>(nodeAddress) << 32) ^
                static_cast<std::uint64_t>(texAddress);

            bool first = false;
            std::size_t uniqueN = 0;
            {
                std::lock_guard<std::mutex> lock(g_Probe87Mutex);
                first = g_Probe87SeenInventoryBinds.insert(key).second;
                uniqueN = g_Probe87SeenInventoryBinds.size();
            }

            if (first && uniqueN <= 600)
            {
                WriteLog(
                    "PROBE #87 INVENTORY_RAW_BIND uniqueN=" +
                    std::to_string(uniqueN) +
                    " tid=" + Hex32(GetCurrentThreadId()) +
                    " node=" + Hex32(nodeAddress) +
                    " nodeVtable=" + Hex32(
                        ProbeReadableRange(frame.thisPtr, 4) ?
                        ReadUInt32(frame.thisPtr, 0x00) : 0) +
                    " nodeRect=" + Probe87NodeRect(frame.thisPtr) +
                    " drawCaller=" + Hex32(frame.caller) +
                    " root=" + Hex32(root.node) +
                    " rootRect=" + Probe87NodeRect(
                        reinterpret_cast<void*>(
                            static_cast<std::uintptr_t>(root.node))) +
                    " d3dTexture=" + Hex32(texAddress) +
                    " bindCaller=" + Hex32(static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
            }
        }
    }

    g_OriginalProbe87RawBind(renderer, stage, d3dTexture);
}

static bool __fastcall HookedProbe87Composite(
    void* destHelper,
    void* /*edx*/,
    void* arg1Helper,
    std::uint32_t arg2,
    std::uint32_t arg3,
    std::uint32_t arg4Helper,
    std::uint32_t arg5,
    std::uint32_t arg6)
{
    if (Probe87SurfaceTraceArmed() && g_Probe87DrawDepth > 0)
    {
        Probe87DrawFrame& frame = g_Probe87DrawStack[g_Probe87DrawDepth - 1];
        Probe87RootEntry root = {};
        if (frame.thisPtr &&
            Probe87FindBackedRoot(frame.thisPtr, root) &&
            Probe87LooksLikeRightPanelRoot(root))
        {
            const std::uint32_t destSurface = Probe87HelperSurface(destHelper);
            const std::uint32_t arg1Surface = Probe87HelperSurface(arg1Helper);
            void* arg4Ptr = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(arg4Helper));
            const std::uint32_t arg4Surface = Probe87HelperSurface(arg4Ptr);

            const bool interesting =
                root.node == g_Probe87CursorTargetRoot.load() &&
                Probe87HelperRectContainsPoint(
                    destHelper,
                    g_Probe87CursorLocalX.load(),
                    g_Probe87CursorLocalY.load());

            if (interesting)
            {
                const std::uint32_t n = ++g_Probe87InventoryCompositeCalls;
                if (n <= 500)
                {
                    WriteLog(
                        "PROBE #87 CURSOR_HIT_COMPOSITE targetSeq=" + std::to_string(g_Probe87CursorTraceSeq.load()) + " n=" +
                        std::to_string(n) +
                        " tid=" + Hex32(GetCurrentThreadId()) +
                        " caller=" + Hex32(static_cast<std::uint32_t>(
                            reinterpret_cast<std::uintptr_t>(_ReturnAddress()))) +
                        " node=" + Hex32(static_cast<std::uint32_t>(
                            reinterpret_cast<std::uintptr_t>(frame.thisPtr))) +
                        " nodeVtable=" + Hex32(
                            ProbeReadableRange(frame.thisPtr, 4) ?
                            ReadUInt32(frame.thisPtr, 0) : 0) +
                        " nodeRect=" + Probe87NodeRect(frame.thisPtr) +
                        " root=" + Hex32(root.node) +
                        " rootRect=" + Probe87NodeRect(
                            reinterpret_cast<void*>(
                                static_cast<std::uintptr_t>(root.node))) +
                        " destSurface=" + Hex32(destSurface) +
                        " destDID=" + Hex32(Probe87SurfaceDid(destSurface)) +
                        " destWH=" + std::to_string(Probe87SurfaceField(destSurface, 0x58)) +
                            "x" + std::to_string(Probe87SurfaceField(destSurface, 0x5C)) +
                        " destRect=" + Probe87HelperRect(destHelper) +
                        " arg1Surface=" + Hex32(arg1Surface) +
                        " arg1DID=" + Hex32(Probe87SurfaceDid(arg1Surface)) +
                        " arg1WH=" + std::to_string(Probe87SurfaceField(arg1Surface, 0x58)) +
                            "x" + std::to_string(Probe87SurfaceField(arg1Surface, 0x5C)) +
                        " arg1Rect=" + Probe87HelperRect(arg1Helper) +
                        " arg1_118=" + Hex32(Probe87SurfaceField(arg1Surface, 0x118)) +
                        " arg1_120=" + Hex32(Probe87SurfaceField(arg1Surface, 0x120)) +
                        " arg4Surface=" + Hex32(arg4Surface) +
                        " arg4DID=" + Hex32(Probe87SurfaceDid(arg4Surface)) +
                        " arg4WH=" + std::to_string(Probe87SurfaceField(arg4Surface, 0x58)) +
                            "x" + std::to_string(Probe87SurfaceField(arg4Surface, 0x5C)) +
                        " a2=" + Hex32(arg2) +
                        " a3=" + Hex32(arg3) +
                        " a5=" + Hex32(arg5) +
                        " a6=" + Hex32(arg6));
                }
            }
        }
    }

    return g_OriginalProbe87Composite(
        destHelper, arg1Helper, arg2, arg3, arg4Helper, arg5, arg6);
}

static bool __fastcall HookedProbe87Copy(
    void* destHelper,
    void* /*edx*/,
    void* sourceHelper)
{
    const bool armed = Probe87ItemTraceArmed();
    std::uint32_t destSurface = 0;
    std::uint32_t sourceSurface = 0;
    bool trace = false;
    std::uint32_t n = 0;

    if (armed)
    {
        destSurface = Probe87HelperSurface(destHelper);
        sourceSurface = Probe87HelperSurface(sourceHelper);
        trace = Probe87SmallSurface(destSurface) &&
            (Probe87SmallSurface(sourceSurface) ||
             Probe87IsReplacementDid(Probe87SurfaceDid(sourceSurface)));

        if (trace)
        {
            n = ++g_Probe87ItemCopyCalls;
            Probe87RememberCandidate(destSurface);
            if (n <= 300)
            {
                WriteLog(
                    "PROBE #87 ITEM_COPY ENTER n=" + std::to_string(n) +
                    " tid=" + Hex32(GetCurrentThreadId()) +
                    " caller=" + Hex32(static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(_ReturnAddress()))) +
                    " destSurface=" + Hex32(destSurface) +
                    " destDID=" + Hex32(Probe87SurfaceDid(destSurface)) +
                    " destWH=" + std::to_string(Probe87SurfaceField(destSurface, 0x58)) +
                        "x" + std::to_string(Probe87SurfaceField(destSurface, 0x5C)) +
                    " sourceSurface=" + Hex32(sourceSurface) +
                    " sourceDID=" + Hex32(Probe87SurfaceDid(sourceSurface)) +
                    " sourceWH=" + std::to_string(Probe87SurfaceField(sourceSurface, 0x58)) +
                        "x" + std::to_string(Probe87SurfaceField(sourceSurface, 0x5C)));
            }
        }
    }

    const bool result = g_OriginalProbe87Copy(destHelper, sourceHelper);

    if (trace && n <= 300)
    {
        WriteLog(
            "PROBE #87 ITEM_COPY EXIT n=" + std::to_string(n) +
            " result=" + std::string(result ? "1" : "0") +
            " destSurface=" + Hex32(destSurface));
    }
    return result;
}

static bool __fastcall HookedProbe87Blit(
    void* destHelper, void* /*edx*/, void* sourceHelper,
    std::uint32_t blendMode, std::uint32_t alphaBits)
{
    if (Probe87SetterTraceArmed() && g_Probe87DrawDepth > 0)
    {
        Probe87DrawFrame& frame =
            g_Probe87DrawStack[g_Probe87DrawDepth - 1];

        const std::uint32_t nodeAddress = frame.thisPtr
            ? static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(frame.thisPtr))
            : 0;

        bool touched = false;
        if (nodeAddress)
        {
            std::lock_guard<std::mutex> lock(g_Probe87Mutex);
            touched = g_Probe87SetterNodes.find(nodeAddress) !=
                g_Probe87SetterNodes.end();
        }

        if (touched)
        {
            const std::uint32_t sourceSurface =
                Probe87HelperSurface(sourceHelper);
            const std::uint64_t key =
                (static_cast<std::uint64_t>(nodeAddress) << 32) |
                static_cast<std::uint64_t>(sourceSurface);

            bool first = false;
            std::size_t uniqueN = 0;
            {
                std::lock_guard<std::mutex> lock(g_Probe87Mutex);
                first = g_Probe87SeenSetterNodeDraws.insert(key).second;
                uniqueN = g_Probe87SeenSetterNodeDraws.size();
            }

            ++g_Probe87SetterNodeDrawCalls;

            if (first && uniqueN <= 300)
            {
                const std::uint32_t nodeResource =
                    ProbeReadableRange(frame.thisPtr, 0x9C)
                    ? ReadUInt32(frame.thisPtr, 0x98)
                    : 0;
                void* nodeResourcePtr = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(nodeResource));

                WriteLog(
                    "PROBE #87 SETTER_NODE_DRAW uniqueN=" +
                    std::to_string(uniqueN) +
                    " tid=" + Hex32(GetCurrentThreadId()) +
                    " node=" + Hex32(nodeAddress) +
                    " nodeRect=" + Probe87NodeRect(frame.thisPtr) +
                    " node98=" + Hex32(nodeResource) +
                    " " + Probe87ImageResourceSummary(nodeResourcePtr) +
                    " sourceSurface=" + Hex32(sourceSurface) +
                    " sourceDID=" + Hex32(Probe87SurfaceDid(sourceSurface)) +
                    " sourceRect=" + Probe87HelperRect(sourceHelper) +
                    " destRect=" + Probe87HelperRect(destHelper) +
                    " blitCaller=" + Hex32(static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(_ReturnAddress()))) +
                    " drawCaller=" + Hex32(frame.caller));
            }
        }
    }

    if (Probe87SurfaceTraceArmed() && g_Probe87DrawDepth > 0)
    {
        Probe87DrawFrame& frame = g_Probe87DrawStack[g_Probe87DrawDepth - 1];
        Probe87RootEntry root = {};
        const std::uint32_t targetRoot = g_Probe87CursorTargetRoot.load();
        const std::int32_t targetX = g_Probe87CursorLocalX.load();
        const std::int32_t targetY = g_Probe87CursorLocalY.load();

        if (frame.thisPtr &&
            targetRoot != 0 &&
            Probe87FindBackedRoot(frame.thisPtr, root) &&
            root.node == targetRoot &&
            Probe87HelperRectContainsPoint(destHelper, targetX, targetY))
        {
            const std::uint32_t sourceSurface =
                Probe87HelperSurface(sourceHelper);
            const std::uint32_t destSurface =
                Probe87HelperSurface(destHelper);
            const std::uint32_t nodeAddress = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(frame.thisPtr));

            ++g_Probe87CursorHitCalls;

            const std::uint64_t key =
                (static_cast<std::uint64_t>(nodeAddress) << 32) ^
                static_cast<std::uint64_t>(sourceSurface) ^
                (static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(ReadUInt32(destHelper, 0x0C))) << 1) ^
                (static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(ReadUInt32(destHelper, 0x10))) << 17);

            bool first = false;
            std::size_t uniqueN = 0;
            {
                std::lock_guard<std::mutex> lock(g_Probe87Mutex);
                first = g_Probe87SeenCursorHits.insert(key).second;
                uniqueN = g_Probe87SeenCursorHits.size();
            }

            if (first && uniqueN <= 64)
            {
                WriteLog(
                    "PROBE #87 CURSOR_HIT_SOURCE targetSeq=" +
                    std::to_string(g_Probe87CursorTraceSeq.load()) +
                    " uniqueN=" + std::to_string(uniqueN) +
                    " tid=" + Hex32(GetCurrentThreadId()) +
                    " hwnd=" + Hex32(g_Probe87CursorHwnd.load()) +
                    " screen=(" +
                        std::to_string(g_Probe87CursorScreenX.load()) + "," +
                        std::to_string(g_Probe87CursorScreenY.load()) + ")" +
                    " client=(" +
                        std::to_string(g_Probe87CursorClientX.load()) + "," +
                        std::to_string(g_Probe87CursorClientY.load()) + ")" +
                    " local=(" + std::to_string(targetX) + "," +
                        std::to_string(targetY) + ")" +
                    " node=" + Hex32(nodeAddress) +
                    " nodeVtable=" + Hex32(
                        ProbeReadableRange(frame.thisPtr, 4) ?
                        ReadUInt32(frame.thisPtr, 0) : 0) +
                    " nodeRect=" + Probe87NodeRect(frame.thisPtr) +
                    " drawCaller=" + Hex32(frame.caller) +
                    " root=" + Hex32(root.node) +
                    " rootRect=" + Probe87NodeRect(
                        reinterpret_cast<void*>(
                            static_cast<std::uintptr_t>(root.node))) +
                    " sourceSurface=" + Hex32(sourceSurface) +
                    " sourceDID=" + Hex32(Probe87SurfaceDid(sourceSurface)) +
                    " sourceWH=" +
                        std::to_string(Probe87SurfaceField(sourceSurface, 0x58)) +
                        "x" +
                        std::to_string(Probe87SurfaceField(sourceSurface, 0x5C)) +
                    " sourceRect=" + Probe87HelperRect(sourceHelper) +
                    " source118=" +
                        Hex32(Probe87SurfaceField(sourceSurface, 0x118)) +
                    " source120=" +
                        Hex32(Probe87SurfaceField(sourceSurface, 0x120)) +
                    " destSurface=" + Hex32(destSurface) +
                    " destDID=" + Hex32(Probe87SurfaceDid(destSurface)) +
                    " destRect=" + Probe87HelperRect(destHelper) +
                    " blend=" + Hex32(blendMode) +
                    " alphaBits=" + Hex32(alphaBits));
            }
        }
    }

    if (!Probe87NeedCorrelation())
        return g_OriginalProbe87Blit(destHelper, sourceHelper, blendMode, alphaBits);

    const std::uint32_t sourceSurface = Probe87HelperSurface(sourceHelper);
    const std::uint32_t sourceDid = Probe87SurfaceDid(sourceSurface);
    if (g_ACCustomsItemRebuildCaptureActive &&
        g_ACCustomsItemRebuildId != 0)
    {
        const std::uint32_t generatedDest =
            Probe87HelperSurface(destHelper);
        if (generatedDest != 0 &&
            (Probe87SurfaceDid(generatedDest) & 0xFF000000u) != 0x06000000u)
        {
            std::lock_guard<std::mutex> lock(g_Probe87Mutex);
            g_ACCustomsGeneratedSurfaceToItemId[generatedDest] =
                g_ACCustomsItemRebuildId;
        }
    }
    if (g_ACCustomsItemRebuildCaptureActive &&
        (sourceDid & 0xFF000000u) == 0x06000000u &&
        std::find(
            g_ACCustomsItemRebuildDids.begin(),
            g_ACCustomsItemRebuildDids.end(),
            sourceDid) == g_ACCustomsItemRebuildDids.end())
    {
        g_ACCustomsItemRebuildDids.push_back(sourceDid);
    }
    Probe87DrawFrame* outer = nullptr;
    if (g_Probe87DrawDepth > 0)
        outer = &g_Probe87DrawStack[g_Probe87DrawDepth - 1];

    const bool result = g_OriginalProbe87Blit(destHelper, sourceHelper, blendMode, alphaBits);

    if (Probe87ItemTraceArmed() && sourceSurface != 0)
    {
        const std::uint32_t sourceW = Probe87SurfaceField(sourceSurface, 0x58);
        const std::uint32_t sourceH = Probe87SurfaceField(sourceSurface, 0x5C);
        const std::uint32_t itemDestSurface = Probe87HelperSurface(destHelper);
        const std::uint32_t destW = Probe87SurfaceField(itemDestSurface, 0x58);
        const std::uint32_t destH = Probe87SurfaceField(itemDestSurface, 0x5C);
        const bool nonFileSource =
            sourceDid == 0 || (sourceDid & 0xFF000000u) != 0x06000000u;
        const bool cacheShape =
            nonFileSource &&
            sourceW >= 8 && sourceH >= 8 &&
            sourceW <= 160 && sourceH <= 160 &&
            (destW > sourceW || destH > sourceH || destW > 160 || destH > 160);

        if (cacheShape)
        {
            const std::uint32_t n = ++g_Probe87ItemCacheDrawCalls;
            const bool generated = Probe87CandidateContains(sourceSurface);
            if (generated)
                ++g_Probe87ItemGeneratedUseCalls;

            if (n <= 500)
            {
                WriteLog(
                    "PROBE #87 ITEM_CACHE_DRAW n=" + std::to_string(n) +
                    " tid=" + Hex32(GetCurrentThreadId()) +
                    " caller=" + Hex32(static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(_ReturnAddress()))) +
                    " sourceSurface=" + Hex32(sourceSurface) +
                    " sourceDID=" + Hex32(sourceDid) +
                    " sourceWH=" + std::to_string(sourceW) + "x" + std::to_string(sourceH) +
                    " generatedDuringTrace=" + std::string(generated ? "1" : "0") +
                    " destSurface=" + Hex32(itemDestSurface) +
                    " destDID=" + Hex32(Probe87SurfaceDid(itemDestSurface)) +
                    " destWH=" + std::to_string(destW) + "x" + std::to_string(destH) +
                    " blend=" + Hex32(blendMode) +
                    " alphaBits=" + Hex32(alphaBits));
            }
        }
    }

    const std::uint32_t mode = g_Probe87AwaitRedrawMode.load();
    if (mode != 0 && outer && outer->thisPtr && sourceDid != 0 && Probe87IsReplacementDid(sourceDid))
    {
        const std::uint32_t drawThisAddress = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(outer->thisPtr));
        const std::uint32_t destSurface = Probe87HelperSurface(destHelper);
        bool firstDid = false;
        std::size_t didCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_Probe87Mutex);
            firstDid = g_Probe87RedrawnDids.insert(sourceDid).second;
            g_Probe87RedrawnNodes.insert(drawThisAddress);
            g_Probe87RedrawnDestSurfaces.insert(destSurface);
            didCount = g_Probe87RedrawnDids.size();
        }
        ++g_Probe87RedrawCalls;
        if (firstDid && didCount <= 60)
        {
            WriteLog(
                "PROBE #87 REDRAW_DID mode=" + std::string(Probe87ModeName(mode)) +
                " uniqueN=" + std::to_string(didCount) +
                " DID=" + Hex32(sourceDid) +
                " drawThis=" + Hex32(drawThisAddress) +
                " sourceSurface=" + Hex32(sourceSurface) +
                " destSurface=" + Hex32(destSurface));
        }
    }

    return result;
}

static void Probe87ResetState()
{
    Probe87UninstallSchedulers();
    g_Probe87CaptureArmed.store(false);
    g_Probe87DesktopRoot.store(0);
    g_Probe87DesktopTid.store(0);
    g_Probe87TreeEnumerations.store(0);
    g_Probe87AwaitRedrawMode.store(0);
    g_Probe87AwaitRedrawUntil.store(0);
    g_Probe87ForceCount.store(0);
    g_Probe87RedrawCalls.store(0);
    g_Probe87InvalidationAttempts.store(0);
    g_Probe87InvalidationCompleted.store(0);
    g_Probe87ItemCaptureUntil.store(0);
    g_Probe87ItemCompositeCalls.store(0);
    g_Probe87ItemCopyCalls.store(0);
    g_Probe87ItemCacheDrawCalls.store(0);
    g_Probe87ItemGeneratedUseCalls.store(0);
    g_Probe87BindTraceUntil.store(0);
    g_Probe87ObjectBindCount.store(0);
    g_Probe87RawBindCount.store(0);
    g_Probe87SurfaceTraceUntil.store(0);
    g_Probe87InventoryBlitCalls.store(0);
    g_Probe87InventoryCompositeCalls.store(0);
    g_Probe87CursorTraceSeq.store(0);
    g_Probe87CursorTargetRoot.store(0);
    g_Probe87CursorScreenX.store(0);
    g_Probe87CursorScreenY.store(0);
    g_Probe87CursorClientX.store(0);
    g_Probe87CursorClientY.store(0);
    g_Probe87CursorHwnd.store(0);
    g_Probe87CursorLocalX.store(0);
    g_Probe87CursorLocalY.store(0);
    g_Probe87CursorHitCalls.store(0);
    g_Probe87SetterTraceUntil.store(0);
    g_Probe87ImageSetCalls.store(0);
    g_Probe87ImageClearCalls.store(0);
    g_Probe87SetterNodeDrawCalls.store(0);
    g_Probe87OwnerLearnCount.store(0);
    g_Probe87ForcedCacheRebuildCount.store(0);
    g_Probe87ForcedOwnerRefreshCount.store(0);
    g_Probe87ForcedOwnerRefreshSuccess.store(0);
    g_Probe90KnownItemLearnCount.store(0);
    g_Probe92LazyItemChecks.store(0);
    g_Probe92LazyItemRebuilds.store(0);
    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    g_Probe87Roots.clear();
    g_Probe87SeenDrawNodes.clear();
    g_Probe87RedrawnDids.clear();
    g_Probe87RedrawnNodes.clear();
    g_Probe87RedrawnDestSurfaces.clear();
    g_Probe87ReplacementDids.clear();
    g_Probe87ItemCandidateSurfaces.clear();
    g_Probe87SeenInventoryBinds.clear();
    g_Probe87SeenInventorySources.clear();
    g_Probe87SeenCursorHits.clear();
    g_Probe87SetterNodes.clear();
    g_Probe87SeenSetterNodeDraws.clear();
    g_Probe87ItemOwners.clear();
    g_Probe90KnownItemIds.clear();
    g_Probe92CacheThemeGeneration.clear();
}

// Return true when a currently-drawing node still belongs to the cached
// desktop. Backed roots can be nested, so comparing only the nearest backed
// root's direct parent to the desktop produces false session-change detections.
static bool Probe87NodeBelongsToDesktop(
    std::uint32_t nodeAddress,
    std::uint32_t desktopAddress)
{
    if (nodeAddress == 0 || desktopAddress == 0)
        return false;

    std::uint32_t current = nodeAddress;
    for (std::uint32_t depth = 0; depth < 96 && current != 0; ++depth)
    {
        if (current == desktopAddress)
            return true;

        void* node = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(current));
        if (!ProbeReadableRange(node, 0xB0))
            return false;

        const std::uint32_t parent = ReadUInt32(node, 0xAC);
        if (parent == current)
            return false;
        current = parent;
    }

    return false;
}

// Clear only UI-object-address state that becomes invalid when AC destroys and
// rebuilds the desktop tree (for example, logout -> login in the same process).
// Theme/replacement state intentionally survives this reset. Item provenance
// and generated-surface correlation are preserved: the permanent item/cache
// hooks may already have learned the NEW login session before the first
// post-login snapshot notices that the desktop changed. Clearing those maps at
// snapshot start makes DID-less icons disappear for that capture.
static void Probe87ResetUiSessionDiscovery(
    const char* reason,
    std::uint32_t oldDesktop,
    std::uint32_t newDesktop)
{
    g_Probe87CaptureArmed.store(true, std::memory_order_release);
    g_Probe87DesktopRoot.store(0, std::memory_order_release);
    g_Probe87DesktopTid.store(0, std::memory_order_release);
    g_Probe87TreeEnumerations.store(0, std::memory_order_release);
    g_Probe87CursorTargetRoot.store(0, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        g_Probe87Roots.clear();
        g_Probe87SeenDrawNodes.clear();
        g_Probe87RedrawnNodes.clear();
        g_Probe87RedrawnDestSurfaces.clear();
        g_Probe87ItemCandidateSurfaces.clear();
        g_Probe87SeenInventoryBinds.clear();
        g_Probe87SeenInventorySources.clear();
        g_Probe87SeenCursorHits.clear();
        g_Probe87SetterNodes.clear();
        g_Probe87SeenSetterNodeDraws.clear();

        // Do not clear g_Probe87ItemOwners or g_Probe90KnownItemIds here. The
        // always-on item-refresh hook may already have learned owners from the
        // NEW login session before Live UI notices that the desktop changed.
        // Clearing these maps at first post-login snapshot is what made
        // generated/DID-less icons disappear after logout -> login. Snapshot
        // lookup validates each retained owner against the live object before
        // using it, so stale old-session entries cannot be trusted blindly.
        g_Probe92CacheThemeGeneration.clear();

        // Object addresses are session-local and must be discarded. Do NOT
        // clear g_ACCustomsItemTextureProvenance or
        // g_ACCustomsGeneratedSurfaceToItemId here. Those are learned by the
        // always-on item/cache hooks, including while the new login UI is being
        // constructed before Live UI's one-shot draw hook is re-enabled.
        g_ACCustomsItemObjectToId.clear();
    }

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsLearnedStateMutex);
        g_ACCustomsLearnedStates.clear();
    }

    WriteLog(
        "ACCUSTOMS UI_SESSION_RESET reason=" +
        std::string(reason ? reason : "UNKNOWN") +
        " oldDesktop=" + Hex32(oldDesktop) +
        " newDesktop=" + Hex32(newDesktop));
}

static void Probe87QueueRootInvalidation(std::uint32_t mode)
{
    g_Probe87CaptureArmed.store(false);

    std::vector<std::uint32_t> tids;
    std::size_t rootCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_Probe87Mutex);
        rootCount = g_Probe87Roots.size();
        std::unordered_set<std::uint32_t> seenTids;
        for (const auto& kv : g_Probe87Roots)
        {
            const std::uint32_t tid = kv.second.tid;
            if (tid && g_Probe87MessageHooks.find(tid) != g_Probe87MessageHooks.end() &&
                seenTids.insert(tid).second)
                tids.push_back(tid);
        }
        g_Probe87RedrawnDids.clear();
        g_Probe87RedrawnNodes.clear();
        g_Probe87RedrawnDestSurfaces.clear();
    }

    if (rootCount == 0 || tids.empty())
    {
        WriteLog("PROBE #87 BULK_QUEUE FAILED mode=" + std::string(Probe87ModeName(mode)) +
            " roots=" + std::to_string(rootCount) +
            " threads=" + std::to_string(tids.size()) +
            " reason=no_captured_backed_roots");
        return;
    }

    g_Probe87RedrawCalls.store(0);
    g_Probe87InvalidationAttempts.store(0);
    g_Probe87InvalidationCompleted.store(0);
    g_Probe87AwaitRedrawMode.store(mode);
    g_Probe87AwaitRedrawUntil.store(GetTickCount() + 2000);
    ++g_Probe87ForceCount;

    std::uint32_t postedCount = 0;
    for (std::uint32_t tid : tids)
    {
        SetLastError(ERROR_SUCCESS);
        const BOOL posted = PostThreadMessageA(tid, kProbe87UiMessage, static_cast<WPARAM>(mode), 0);
        if (posted)
            ++postedCount;
        else
            WriteLog("PROBE #87 BULK_QUEUE THREAD_FAILED mode=" + std::string(Probe87ModeName(mode)) +
                " tid=" + Hex32(tid) + " GetLastError=" + std::to_string(GetLastError()));
    }

    WriteLog("PROBE #87 BULK_QUEUE mode=" + std::string(Probe87ModeName(mode)) +
        " roots=" + std::to_string(rootCount) +
        " threads=" + std::to_string(tids.size()) +
        " postedThreads=" + std::to_string(postedCount) +
        " instruction=NO_UI_INTERACTION");
}

static void Probe87CheckTimeouts()
{
    const std::uint32_t mode = g_Probe87AwaitRedrawMode.load();
    const DWORD until = g_Probe87AwaitRedrawUntil.load();
    if (mode != 0 && until != 0 && static_cast<LONG>(until - GetTickCount()) <= 0)
    {
        g_Probe87AwaitRedrawMode.store(0);
        g_Probe87AwaitRedrawUntil.store(0);
        std::size_t dids = 0;
        std::size_t nodes = 0;
        std::size_t dests = 0;
        std::size_t roots = 0;
        std::size_t threads = 0;
        {
            std::lock_guard<std::mutex> lock(g_Probe87Mutex);
            dids = g_Probe87RedrawnDids.size();
            nodes = g_Probe87RedrawnNodes.size();
            dests = g_Probe87RedrawnDestSurfaces.size();
            roots = g_Probe87Roots.size();
            std::unordered_set<std::uint32_t> tids;
            for (const auto& kv : g_Probe87Roots) tids.insert(kv.second.tid);
            threads = tids.size();
        }
        WriteLog(
            "PROBE #87 REDRAW_SUMMARY mode=" + std::string(Probe87ModeName(mode)) +
            " capturedRoots=" + std::to_string(roots) +
            " rootThreads=" + std::to_string(threads) +
            " invalidationAttempts=" + std::to_string(g_Probe87InvalidationAttempts.load()) +
            " invalidationCompleted=" + std::to_string(g_Probe87InvalidationCompleted.load()) +
            " redrawCalls=" + std::to_string(g_Probe87RedrawCalls.load()) +
            " uniqueReplacementDIDs=" + std::to_string(dids) +
            " uniqueDrawNodes=" + std::to_string(nodes) +
            " uniqueDestSurfaces=" + std::to_string(dests));
    }
}

// Probe #50: follow the actual state mutation and its redraw/invalidation helper.
// 0x462390 calls 0x6A0D50 between its OLD and NEW state queries.
// When bit 1 changes, 0x6A0D50 obtains the widget rect via virtual +0x34 and
// calls 0x69FF00(this, &rect).  Observe both naturally; invoke neither manually.
using Probe50MutateFn = void (__thiscall*)(void*, std::uint32_t);
using Probe50InvalidateFn = void (__thiscall*)(void*, void*);

static Probe50MutateFn g_OriginalProbe50Mutate = nullptr;
static Probe50InvalidateFn g_OriginalProbe50Invalidate = nullptr;
static std::atomic<std::uint32_t> g_Probe50MutateCount(0);
static std::atomic<std::uint32_t> g_Probe50InvalidateCount(0);

static void __fastcall HookedProbe50Mutate(
    void* thisPtr, void*, std::uint32_t value)
{
    const bool trace = Probe48Armed() && Probe48IsAbilities(thisPtr);
    std::uint32_t beforeA4 = 0xFFFFFFFFu;
    std::uint32_t beforeB0 = 0;
    std::uint32_t beforeAC = 0;
    std::uint32_t caller = 0;

    if (trace) {
        if (ProbeReadableRange(thisPtr, 0xB4)) {
            beforeA4 = ReadUInt32(thisPtr, 0xA4);
            beforeAC = ReadUInt32(thisPtr, 0xAC);
            beforeB0 = ReadUInt32(thisPtr, 0xB0);
        }
        caller = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        const auto n = ++g_Probe50MutateCount;
        WriteLog("PROBE #50 MUTATE ENTER n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " arg=" + Hex32(value) +
                 " A4=" + Hex32(beforeA4) +
                 " bit1=" + Hex32((beforeA4 >> 1) & 1u) +
                 " AC=" + Hex32(beforeAC) +
                 " B0=" + Hex32(beforeB0) +
                 " caller=" + Hex32(caller));
    }

    g_OriginalProbe50Mutate(thisPtr, value);

    if (trace) {
        std::uint32_t afterA4 = 0xFFFFFFFFu;
        std::uint32_t afterAC = 0;
        std::uint32_t afterB0 = 0;
        if (ProbeReadableRange(thisPtr, 0xB4)) {
            afterA4 = ReadUInt32(thisPtr, 0xA4);
            afterAC = ReadUInt32(thisPtr, 0xAC);
            afterB0 = ReadUInt32(thisPtr, 0xB0);
        }
        WriteLog("PROBE #50 MUTATE EXIT"
                 " this=" + Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " A4=" + Hex32(afterA4) +
                 " bit1=" + Hex32((afterA4 >> 1) & 1u) +
                 " AC=" + Hex32(afterAC) +
                 " B0=" + Hex32(afterB0));
    }
}

static void __fastcall HookedProbe50Invalidate(
    void* thisPtr, void*, void* rectPtr)
{
    const bool trace = Probe48Armed() && Probe48IsAbilities(thisPtr);
    std::uint32_t caller = 0;
    std::int32_t l = 0, top = 0, r = 0, b = 0;

    if (trace) {
        caller = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        if (rectPtr && ProbeReadableRange(rectPtr, 16)) {
            l = *reinterpret_cast<std::int32_t*>(
                static_cast<unsigned char*>(rectPtr) + 0);
            top = *reinterpret_cast<std::int32_t*>(
                static_cast<unsigned char*>(rectPtr) + 4);
            r = *reinterpret_cast<std::int32_t*>(
                static_cast<unsigned char*>(rectPtr) + 8);
            b = *reinterpret_cast<std::int32_t*>(
                static_cast<unsigned char*>(rectPtr) + 12);
        }
        const auto n = ++g_Probe50InvalidateCount;
        WriteLog("PROBE #50 INVALIDATE ENTER n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " rect=(" + std::to_string(l) + "," + std::to_string(top) +
                 ")-(" + std::to_string(r) + "," + std::to_string(b) + ")" +
                 " B0=" + Hex32(ProbeReadableRange(thisPtr, 0xB4)
                     ? ReadUInt32(thisPtr, 0xB0) : 0u) +
                 " AC=" + Hex32(ProbeReadableRange(thisPtr, 0xB4)
                     ? ReadUInt32(thisPtr, 0xAC) : 0u) +
                 " caller=" + Hex32(caller));
    }

    g_OriginalProbe50Invalidate(thisPtr, rectPtr);

    if (trace) {
        WriteLog("PROBE #50 INVALIDATE EXIT this=" +
                 Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))));
    }
}

// Probe #49: observe the remaining optional calls in 0x463830.
using Probe49NoArgFn = void (__thiscall*)(void*);
using Probe49PropertyFn = bool (__thiscall*)(void*, std::uint32_t, void*);
static Probe49NoArgFn g_OriginalProbe49Fc = nullptr;
static Probe49NoArgFn g_OriginalProbe49Plus104 = nullptr;
static Probe49PropertyFn g_OriginalProbe49Property = nullptr;
static std::atomic<std::uint32_t> g_Probe49FcCount(0);
static std::atomic<std::uint32_t> g_Probe49Prop35Count(0);
static std::atomic<std::uint32_t> g_Probe49Plus104Count(0);

static void __fastcall HookedProbe49Fc(void* thisPtr, void*)
{
    const bool trace = Probe48Armed() && Probe48IsAbilities(thisPtr);
    const std::uint32_t caller = trace ? static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())) : 0;
    if (trace) {
        const auto n = ++g_Probe49FcCount;
        WriteLog("PROBE #49 +FC n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " caller=" + Hex32(caller));
    }
    g_OriginalProbe49Fc(thisPtr);
}

static void __fastcall HookedProbe49Plus104(void* thisPtr, void*)
{
    const bool trace = Probe48Armed() && Probe48IsAbilities(thisPtr);
    const std::uint32_t caller = trace ? static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())) : 0;
    if (trace) {
        const auto n = ++g_Probe49Plus104Count;
        WriteLog("PROBE #49 +104 n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " caller=" + Hex32(caller));
    }
    g_OriginalProbe49Plus104(thisPtr);
}

static bool __fastcall HookedProbe49Property(
    void* thisPtr, void*, std::uint32_t key, void* outValue)
{
    const bool trace =
        Probe48Armed() && key == 0x35u && Probe48IsAbilities(thisPtr);
    const std::uint32_t caller = trace ? static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(_ReturnAddress())) : 0;
    const bool result = g_OriginalProbe49Property(thisPtr, key, outValue);
    if (trace) {
        const auto n = ++g_Probe49Prop35Count;
        std::uint32_t value = 0xFFFFFFFFu;
        if (outValue && ProbeReadableRange(outValue, 1))
            value = *reinterpret_cast<unsigned char*>(outValue);
        WriteLog("PROBE #49 PROP35 n=" + std::to_string(n) +
                 " this=" + Hex32(static_cast<std::uint32_t>(
                     reinterpret_cast<std::uintptr_t>(thisPtr))) +
                 " result=" + std::string(result ? "1" : "0") +
                 " value=" + Hex32(value) +
                 " caller=" + Hex32(caller));
    }
    return result;
}

static void __fastcall HookedProbe48MakeBacking(
    void* thisPtr, void* /*edx*/, void** outObject)
{
    const bool trace = Probe48Armed() && Probe48IsAbilities(thisPtr);
    const std::uint32_t caller = trace
        ? static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()))
        : 0;
    const std::uint32_t oldB0 = trace ? ReadUInt32(thisPtr, 0xB0) : 0;
    const std::uint32_t beforeOut =
        (trace && outObject && ProbeReadableRange(outObject, sizeof(void*)))
        ? static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(*outObject))
        : 0;

    g_OriginalProbe48MakeBacking(thisPtr, outObject);

    if (trace)
    {
        const std::uint32_t n = ++g_Probe48MakeCount;
        const std::uint32_t produced =
            (outObject && ProbeReadableRange(outObject, sizeof(void*)))
            ? static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(*outObject))
            : 0;
        WriteLog(
            "PROBE #48 MAKE n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " oldB0=" + Hex32(oldB0) +
            " outBefore=" + Hex32(beforeOut) +
            " produced=" + Hex32(produced) +
            " afterB0=" + Hex32(ReadUInt32(thisPtr, 0xB0)) +
            " caller=" + Hex32(caller));
    }
}

static void __fastcall HookedProbe48AttachBacking(
    void* thisPtr, void* /*edx*/, void* object)
{
    const bool trace = Probe48Armed() && Probe48IsAbilities(thisPtr);
    const std::uint32_t caller = trace
        ? static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()))
        : 0;
    const std::uint32_t oldB0 = trace ? ReadUInt32(thisPtr, 0xB0) : 0;
    const std::uint32_t incoming = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(object));

    if (trace)
    {
        WriteLog(
            "PROBE #48 ATTACH ENTER this=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " oldB0=" + Hex32(oldB0) +
            " incoming=" + Hex32(incoming) +
            " caller=" + Hex32(caller));
    }

    g_OriginalProbe48AttachBacking(thisPtr, object);

    if (trace)
    {
        const std::uint32_t n = ++g_Probe48AttachCount;
        WriteLog(
            "PROBE #48 ATTACH EXIT n=" + std::to_string(n) +
            " this=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " oldB0=" + Hex32(oldB0) +
            " incoming=" + Hex32(incoming) +
            " newB0=" + Hex32(ReadUInt32(thisPtr, 0xB0)) +
            " changed=" + std::string(
                ReadUInt32(thisPtr, 0xB0) != oldB0 ? "1" : "0") +
            " caller=" + Hex32(caller));
    }
}

// Probe #47: correlate 0x004410C0 calls with an eventual lock of the
// exact target RenderSurface. Static analysis: bool __thiscall(void*, uint32_t).
using Probe47SurfaceAccessFn =
    bool (__thiscall*)(void* thisPtr, std::uint32_t arg);
static Probe47SurfaceAccessFn g_OriginalProbe47SurfaceAccess = nullptr;

static thread_local int g_Probe47Depth = 0;
static thread_local bool g_Probe47TouchedTarget[32] = {};
static thread_local std::uint32_t g_Probe47Caller[32] = {};
static std::atomic<std::uint32_t> g_Probe47MatchCount(0);

static bool __fastcall HookedProbe47SurfaceAccess(
    void* thisPtr, void* /*edx*/, std::uint32_t arg)
{
    const DWORD now = GetTickCount();
    const DWORD until = g_Probe23ArmedUntil.load();
    const bool armed =
        (until != 0 && static_cast<LONG>(until - now) > 0);

    const int slot = (g_Probe47Depth < 31) ? g_Probe47Depth : 31;

    if (armed)
    {
        g_Probe47TouchedTarget[slot] = false;
        g_Probe47Caller[slot] = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    }

    ++g_Probe47Depth;
    const bool result = g_OriginalProbe47SurfaceAccess(thisPtr, arg);
    --g_Probe47Depth;

    if (armed && g_Probe47TouchedTarget[slot])
    {
        const std::uint32_t n = ++g_Probe47MatchCount;
        WriteLog(
            "PROBE #47 MATCH n=" + std::to_string(n) +
            " depth=" + std::to_string(slot) +
            " wrapperThis=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " arg=" + Hex32(arg) +
            " result=" + std::string(result ? "1" : "0") +
            " caller4410C0=" + Hex32(g_Probe47Caller[slot]));
    }

    if (armed && g_Probe47TouchedTarget[slot] && slot > 0)
        g_Probe47TouchedTarget[slot - 1] = true;

    return result;
}


// Probe #53: capture the exact object that reaches the proven renderer lock
// (0x00696F10) for War Magic DID 0x06001365.  This is one-shot per process so
// normal rendering cannot flood the log.  We dump the object's own vtable,
// rather than assuming a renderer-object offset inside RenderSurface.
static void Probe53CaptureLockObject(void* thisPtr)
{
    if (!thisPtr || !ProbeReadableRange(thisPtr, 0x2C))
        return;

    if (ReadUInt32(thisPtr, 0x28) != PROBE46_TARGET_DID)
        return;

    bool expected = false;
    if (!g_Probe53Captured.compare_exchange_strong(expected, true))
        return;

    const std::uint32_t objectAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(thisPtr));
    const std::uint32_t vtableAddress = ReadUInt32(thisPtr, 0x00);

    WriteLog(
        "PROBE #53 LOCK OBJECT DID=" + Hex32(PROBE46_TARGET_DID) +
        " this=" + Hex32(objectAddress) +
        " vtable=" + Hex32(vtableAddress) +
        " caller=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));

    void* vtable = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(vtableAddress));

    if (!vtableAddress || !ProbeReadableRange(vtable, 0x84))
    {
        WriteLog("PROBE #53 LOCK OBJECT: vtable is null/unreadable.");
        return;
    }

    for (std::uint32_t offset = 0; offset <= 0x80; offset += 4)
    {
        const std::uint32_t fn = ReadUInt32(vtable, offset);
        WriteLog(
            "PROBE #53 VSLOT +" + Hex32(offset).substr(2) +
            " fn=" + Hex32(fn));
    }
}


using RendererUnlockFn =
    void (__thiscall*)(void* thisPtr);

static RendererUnlockFn
    g_RendererUnlock = nullptr;

static RendererUnlockFn
    g_OriginalRendererUnlock = nullptr;

static bool __fastcall HookedRendererLock(
    void* thisPtr,
    void* /*edx*/,
    void* lockState,
    std::uint32_t level,
    std::uint32_t* pitchOut,
    void** bitsOut)
{
    const bool probe68Target =
        Probe68Armed() &&
        thisPtr &&
        ProbeReadableRange(thisPtr, 0x124) &&
        ReadUInt32(thisPtr, 0x28) == PROBE46_TARGET_DID;

    const std::uint32_t probe68LockCaller = probe68Target
        ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))
        : 0;

    std::uint32_t probe68AcquireCaller = 0;
    if (probe68Target && g_Probe47Depth > 0)
    {
        const int slot = (g_Probe47Depth - 1 < 31) ? (g_Probe47Depth - 1) : 31;
        probe68AcquireCaller = g_Probe47Caller[slot];
        g_Probe47TouchedTarget[slot] = true;
    }

    const bool result =
        g_OriginalRendererLock(
            thisPtr,
            lockState,
            level,
            pitchOut,
            bitsOut);

    if (probe68Target)
    {
        std::uint32_t pitch = 0;
        void* bits = nullptr;
        if (pitchOut && ProbeReadableRange(pitchOut, sizeof(std::uint32_t)))
            pitch = *pitchOut;
        if (bitsOut && ProbeReadableRange(bitsOut, sizeof(void*)))
            bits = *bitsOut;

        const std::uint32_t hash = result
            ? Probe68HashMappedTarget(thisPtr, bits, pitch)
            : 0;

        g_Probe68TargetLockFrame.thisPtr = thisPtr;
        g_Probe68TargetLockFrame.bits = bits;
        g_Probe68TargetLockFrame.pitch = pitch;
        g_Probe68TargetLockFrame.level = level;
        g_Probe68TargetLockFrame.lockCaller = probe68LockCaller;
        g_Probe68TargetLockFrame.acquireCaller = probe68AcquireCaller;
        g_Probe68TargetLockFrame.hashAtLock = hash;
        g_Probe68TargetLockFrame.active = result && bits != nullptr;

        const std::uint32_t n = ++g_Probe68TargetLockCount;
        WriteLog(
            "PROBE #68 TARGET_LOCK phase=" +
            std::string(Probe68PhaseName(g_Probe68Phase.load())) +
            " n=" + std::to_string(n) +
            " tid=" + Hex32(GetCurrentThreadId()) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " level=" + std::to_string(level) +
            " result=" + std::string(result ? "1" : "0") +
            " pitch=" + std::to_string(pitch) +
            " bits=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bits))) +
            " hash=" + Hex32(hash) +
            " cpu114=" + Hex32(ReadUInt32(thisPtr, 0x114)) +
            " d3d120=" + Hex32(ReadUInt32(thisPtr, 0x120)) +
            " state11C=" + Hex32(ReadUInt32(thisPtr, 0x11C)) +
            " lockCaller=" + Hex32(probe68LockCaller) +
            " acquireCaller=" + Hex32(probe68AcquireCaller));
    }

    const DWORD probe46Now = GetTickCount();
    const DWORD probe46Until = g_Probe23ArmedUntil.load();
    const bool probe46Armed =
        (probe46Until != 0 && static_cast<LONG>(probe46Until - probe46Now) > 0);

    if (probe46Armed && thisPtr && ProbeReadableRange(thisPtr, 0x2C))
    {
        const std::uint32_t probe46Did = ReadUInt32(thisPtr, 0x28);
        if (probe46Did == PROBE46_TARGET_DID)
        {
            if (g_Probe47Depth > 0)
            {
                const int probe47Slot =
                    (g_Probe47Depth - 1 < 31) ? (g_Probe47Depth - 1) : 31;
                g_Probe47TouchedTarget[probe47Slot] = true;
            }

            const std::uint32_t n = ++g_Probe46LockCount;
            std::uint32_t pitch = 0;
            std::uint32_t bitsAddress = 0;
            if (pitchOut && ProbeReadableRange(pitchOut, sizeof(std::uint32_t)))
                pitch = *pitchOut;
            if (bitsOut && ProbeReadableRange(bitsOut, sizeof(void*)))
                bitsAddress = static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(*bitsOut));

            WriteLog(
                "PROBE #46 TARGET LOCK n=" + std::to_string(n) +
                " DID=" + Hex32(probe46Did) +
                " this=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(thisPtr))) +
                " level=" + std::to_string(level) +
                " result=" + std::string(result ? "1" : "0") +
                " pitch=" + std::to_string(pitch) +
                " bits=" + Hex32(bitsAddress) +
                " caller=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
        }
    }

    if (g_Probe9InsideTargetUpload)
    {
        const std::uint32_t objectAddress =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr));

        const std::uint32_t stateAddress =
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(lockState));

        std::uint32_t pitch = 0;
        std::uint32_t bitsAddress = 0;

        if (pitchOut != nullptr &&
            ProbeReadableRange(pitchOut, sizeof(std::uint32_t)))
        {
            pitch = *pitchOut;
        }

        if (bitsOut != nullptr &&
            ProbeReadableRange(bitsOut, sizeof(void*)))
        {
            bitsAddress =
                static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(*bitsOut));
        }

        WriteLog(
            std::string("RS DEST LOCK") +
            " result=" + (result ? "1" : "0") +
            " object=" + Hex32(objectAddress) +
            " lockState=" + Hex32(stateAddress) +
            " level=" + std::to_string(level) +
            " pitch=" + std::to_string(pitch) +
            " bits=" + Hex32(bitsAddress));
    }

    return result;
}

static void __fastcall HookedRendererUnlock(
    void* thisPtr,
    void* /*edx*/)
{
    const bool probe68Target =
        Probe68Armed() &&
        thisPtr &&
        ProbeReadableRange(thisPtr, 0x124) &&
        ReadUInt32(thisPtr, 0x28) == PROBE46_TARGET_DID;

    const std::uint32_t unlockCaller = probe68Target
        ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))
        : 0;

    std::uint32_t hashBeforeUnlock = 0;
    bool mappedChanged = false;
    if (probe68Target &&
        g_Probe68TargetLockFrame.active &&
        g_Probe68TargetLockFrame.thisPtr == thisPtr)
    {
        hashBeforeUnlock = Probe68HashMappedTarget(
            thisPtr,
            g_Probe68TargetLockFrame.bits,
            g_Probe68TargetLockFrame.pitch);
        mappedChanged =
            hashBeforeUnlock != 0 &&
            g_Probe68TargetLockFrame.hashAtLock != 0 &&
            hashBeforeUnlock != g_Probe68TargetLockFrame.hashAtLock;
    }

    g_OriginalRendererUnlock(thisPtr);

    if (probe68Target)
    {
        const std::uint32_t n = ++g_Probe68TargetUnlockCount;
        if (mappedChanged)
            ++g_Probe68TargetChangedCount;

        WriteLog(
            "PROBE #68 TARGET_UNLOCK phase=" +
            std::string(Probe68PhaseName(g_Probe68Phase.load())) +
            " n=" + std::to_string(n) +
            " tid=" + Hex32(GetCurrentThreadId()) +
            " this=" + Hex32(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " hashAtLock=" + Hex32(g_Probe68TargetLockFrame.hashAtLock) +
            " hashBeforeUnlock=" + Hex32(hashBeforeUnlock) +
            " mappedChanged=" + std::string(mappedChanged ? "1" : "0") +
            " state11C_after=" + Hex32(
                ProbeReadableRange(thisPtr, 0x120) ? ReadUInt32(thisPtr, 0x11C) : 0xFFFFFFFFu) +
            " unlockCaller=" + Hex32(unlockCaller) +
            " lockCaller=" + Hex32(g_Probe68TargetLockFrame.lockCaller) +
            " acquireCaller=" + Hex32(g_Probe68TargetLockFrame.acquireCaller));
    }

    if (g_Probe68TargetLockFrame.thisPtr == thisPtr)
        g_Probe68TargetLockFrame = Probe68TargetLockFrame{};
}


// ------------------------------------------------------------
// RenderSurface upload/commit path
// VA 0x00444200 / RVA 0x00044200
//
// Static analysis of this exact acclient.exe shows this function:
//   - reads RenderSurface +0x64 sourceBits
//   - obtains/locks a renderer destination
//   - copies sourceBits into that destination
//   - unlocks/commits it
//   - calls 0x004440D0, which frees and clears +0x64
//
// Probe #8 is observational only.
// ------------------------------------------------------------

using RenderSurfaceUploadFn =
    bool (__thiscall*)(void* thisPtr);

static RenderSurfaceUploadFn
    g_OriginalRenderSurfaceUpload = nullptr;

static bool __fastcall HookedRenderSurfaceUpload(
    void* thisPtr,
    void* /*edx*/)
{
    bool isTarget = false;

    if (ProbeReadableRange(thisPtr, 0x6C))
    {
        const std::uint32_t did = ReadUInt32(thisPtr, 0x28);

        if ((did & 0xFF000000u) == 0x06000000u)
            RegisterLiveTexture(did, thisPtr);

        if (did == LIVE_SWAP_TEST_DID)
        {
            isTarget = true;

            WriteLog(
                "RS UPLOAD ENTER DID=" + Hex32(did) +
                " this=" + Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(thisPtr))) +
                " vtable=" + Hex32(ReadUInt32(thisPtr, 0x00)) +
                " imageSize=" + std::to_string(ReadUInt32(thisPtr, 0x60)) +
                " sourceBits=" + Hex32(ReadUInt32(thisPtr, 0x64)));
        }
    }

    const DWORD probe46Now = GetTickCount();
    const DWORD probe46Until = g_Probe23ArmedUntil.load();
    const bool probe46Armed =
        (probe46Until != 0 && static_cast<LONG>(probe46Until - probe46Now) > 0);
    const bool probe46Target =
        ProbeReadableRange(thisPtr, 0x2C) &&
        ReadUInt32(thisPtr, 0x28) == PROBE46_TARGET_DID;

    if (probe46Armed && probe46Target)
    {
        const std::uint32_t n = ++g_Probe46UploadCount;
        WriteLog(
            "PROBE #46 TARGET UPLOAD ENTER n=" + std::to_string(n) +
            " DID=" + Hex32(PROBE46_TARGET_DID) +
            " this=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(thisPtr))) +
            " sourceBits=" + Hex32(ReadUInt32(thisPtr, 0x64)) +
            " caller=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()))));
    }

    if (isTarget)
        g_Probe9InsideTargetUpload = true;

    const bool result = g_OriginalRenderSurfaceUpload(thisPtr);

    if (probe46Armed && probe46Target)
    {
        WriteLog(
            "PROBE #46 TARGET UPLOAD EXIT DID=" + Hex32(PROBE46_TARGET_DID) +
            " result=" + std::string(result ? "1" : "0") +
            " sourceBits=" + Hex32(
                ProbeReadableRange(thisPtr, 0x68) ? ReadUInt32(thisPtr, 0x64) : 0xFFFFFFFFu));
    }

    if (isTarget)
        g_Probe9InsideTargetUpload = false;

    if (isTarget)
    {
        std::uint32_t sourceAfter = 0xFFFFFFFFu;
        std::uint32_t stateAfter = 0xFFFFFFFFu;

        if (ProbeReadableRange(thisPtr, 0x68))
        {
            sourceAfter = ReadUInt32(thisPtr, 0x64);
            stateAfter = ReadUInt32(thisPtr, 0x24);
        }

        WriteLog(
            std::string("RS UPLOAD EXIT result=") +
            (result ? "1" : "0") +
            " sourceBits=" + Hex32(sourceAfter) +
            " +24=" + Hex32(stateAfter));
    }

    return result;
}


// ------------------------------------------------------------

// PixelSource

// RVA 0x0ACF0

//

// Observed calling convention:

//   ECX = this

//   one 32-bit byte-count argument

//   EAX = returned source pointer

// ------------------------------------------------------------



using PixelSourceFn =

    void* (__thiscall*)(

        void* thisPtr,

        std::uint32_t byteCount

    );



static PixelSourceFn

    g_OriginalPixelSource = nullptr;

// ------------------------------------------------------------
// Verified CSurface::InitEnd probe.
//
// Exact acclient.exe:
//   VA  0x00537140
//   RVA 0x00137140
//   caller pushes one 32-bit argument and sets ECX=this
//   callee returns with RET 4.
//
// Observational only: no CSurface fields are modified.
// ------------------------------------------------------------

using CSurfaceInitEndFn =
    void (__thiscall*)(
        void* thisPtr,
        std::uint32_t mode
    );

static CSurfaceInitEndFn
    g_OriginalCSurfaceInitEnd = nullptr;

static constexpr std::uint32_t
    CSURFACE_PROBE_DID = 0x06004CC1u;

static std::mutex
    g_CSurfaceProbeMutex;

static std::unordered_set<std::uintptr_t>
    g_LoggedCSurfaceProbeObjects;


static std::mutex
    g_RenderSurfaceLifecycleMutex;

static void*
    g_TargetRenderSurface = nullptr;

static std::uint32_t
    g_TargetRenderSurfaceImageSize = 0;

static std::atomic<bool>
    g_RenderSurfaceLifecycleStop(false);


struct RenderSurfaceCorrelationItem
{
    void* object;
    std::uint32_t did;
    DWORD queuedAt;
};

static std::mutex
    g_RenderSurfaceCorrelationMutex;

static std::vector<RenderSurfaceCorrelationItem>
    g_RenderSurfaceCorrelationQueue;

static std::unordered_set<std::uintptr_t>
    g_RenderSurfaceCorrelationSeen;

static std::atomic<bool>
    g_RenderSurfaceCorrelationStop(false);






// ------------------------------------------------------------

// Per-thread RenderSurface context.

//

// SerializeFromCachePack establishes which Type-12 RenderSurface

// is currently being decoded. PixelSource runs inside that call.

// ------------------------------------------------------------



static thread_local std::uint32_t

    g_CurrentRenderSurfaceDID = 0;



static thread_local void*

    g_CurrentRenderSurface = nullptr;



static thread_local std::uint32_t g_CurrentPaletteDID = 0;



// ------------------------------------------------------------

// Helpers

// ------------------------------------------------------------



static void WriteLog(

    const std::string& text)

{

    if (!g_DeveloperTestsEnabled.load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> lock(g_LogMutex);



    const std::string logPath = ACCustomsDeveloperLogPath();

    std::ofstream log(

        logPath,

        std::ios::app

    );



    if (log.is_open())

        log << text << std::endl;

}





static std::string Hex32(

    std::uint32_t value)

{

    std::ostringstream out;



    out << "0x"

        << std::uppercase

        << std::hex

        << std::setfill('0')

        << std::setw(8)

        << value;



    return out.str();

}





static std::string DIDFileName(

    std::uint32_t did)

{

    std::ostringstream out;



    out << std::uppercase

        << std::hex

        << std::setfill('0')

        << std::setw(8)

        << did;



    return out.str();

}





static std::uint32_t ReadUInt32(

    const void* base,

    std::size_t offset)

{

    const auto* bytes =

        reinterpret_cast<const unsigned char*>(base);



    std::uint32_t value = 0;



    std::memcpy(

        &value,

        bytes + offset,

        sizeof(value)

    );



    return value;

}





static void LoadEncounteredDIDsLocked()
{
    g_EncounteredDIDs.clear();

    const std::string encounteredPath = ACCustomsEncounteredPath();
    std::ifstream input(encounteredPath);
    std::string line;

    while (std::getline(input, line))
    {
        if (line.empty() || line == "DID")
            continue;

        try
        {
            std::size_t consumed = 0;
            const unsigned long value = std::stoul(line, &consumed, 16);

            if (consumed == line.size())
                g_EncounteredDIDs.insert(static_cast<std::uint32_t>(value));
        }
        catch (...)
        {
            // Ignore malformed lines.
        }
    }

    g_EncounteredLoaded = true;
}


static void RecordEncounteredTexture(std::uint32_t did)
{
    if ((did & 0xFF000000u) != 0x06000000u)
        return;

    std::lock_guard<std::mutex> lock(g_EncounteredMutex);

    const std::string encounteredPath = ACCustomsEncounteredPath();
    const DWORD attributes = GetFileAttributesA(encounteredPath.c_str());

    // If the Manager deletes the CSV while AC is running, treat that
    // as a reset and begin accumulating encountered DIDs again.
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        g_EncounteredDIDs.clear();
        g_EncounteredLoaded = true;

        std::ofstream fresh(encounteredPath, std::ios::trunc);
        if (fresh.is_open())
            fresh << "DID" << std::endl;
    }
    else if (!g_EncounteredLoaded)
    {
        LoadEncounteredDIDsLocked();
    }

    if (!g_EncounteredDIDs.insert(did).second)
        return;

    std::ofstream output(encounteredPath, std::ios::app);

    if (!output.is_open())
    {
        g_EncounteredDIDs.erase(did);
        WriteLog("ERROR: Could not append encountered texture DID.");
        return;
    }

    output << DIDFileName(did) << std::endl;
}


static void WriteTextureMetadata(

    std::uint32_t did,

    std::uint32_t width,

    std::uint32_t height,

    std::uint32_t imageSize,

    std::uint32_t pixelFormat,

    std::uint32_t formatInfo,

    std::uint32_t paletteDID)

{

    std::lock_guard<std::mutex> lock(g_MetadataMutex);



    CreateDirectoryA(ACCustomsGetCaptureDirectory().c_str(), nullptr);



    const std::string didName = DIDFileName(did);



    std::ostringstream row;

    row << didName << ","

        << width << ","

        << height << ","

        << imageSize << ","

        << DIDFileName(pixelFormat) << ","

        << DIDFileName(formatInfo) << ","

        << DIDFileName(paletteDID);



    std::vector<std::string> rows;



    {

        const std::string metadataPath = ACCustomsMetadataPath();
        std::ifstream input(metadataPath);

        std::string line;



        while (std::getline(input, line))

        {

            if (line.empty() || line.rfind("DID,", 0) == 0)

                continue;



            const std::size_t comma = line.find(',');

            const std::string existingDID =

                (comma == std::string::npos)

                    ? line

                    : line.substr(0, comma);



            if (existingDID != didName)

                rows.push_back(line);

        }

    }



    rows.push_back(row.str());



    const std::string metadataPath = ACCustomsMetadataPath();
    std::ofstream output(metadataPath, std::ios::trunc);



    if (!output.is_open())

    {

        WriteLog("ERROR: Could not write texture metadata catalog.");

        return;

    }



    output

        << "DID,width,height,imageSize,pixelFormat,formatInfo,paletteDID"

        << std::endl;



    for (const std::string& existingRow : rows)

        output << existingRow << std::endl;

}





static bool ReadBinaryFileExact(
    const std::string& path,
    std::vector<unsigned char>& bytes,
    std::uint32_t expectedSize)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open())
        return false;

    const std::streamoff size = input.tellg();
    if (size != static_cast<std::streamoff>(expectedSize))
        return false;

    bytes.resize(expectedSize);
    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(expectedSize));

    return input.gcount() == static_cast<std::streamsize>(expectedSize);
}

static bool IsWritableRange(void* address, std::uint32_t byteCount)
{
    if (address == nullptr || byteCount == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
        return false;

    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 ||
        (mbi.Protect & PAGE_NOACCESS) != 0)
    {
        return false;
    }

    const DWORD writable =
        PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    if ((mbi.Protect & writable) == 0)
        return false;

    const std::uintptr_t start =
        reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t regionEnd =
        reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;

    return start + byteCount <= regionEnd;
}

static void RegisterLiveTexture(std::uint32_t did, void* renderSurface)
{
    if ((did & 0xFF000000u) != 0x06000000u ||
        !ProbeReadableRange(renderSurface, 0x70))
        return;

    LiveTextureEntry entry = {};
    entry.renderSurface = renderSurface;
    entry.width = ReadUInt32(renderSurface, 0x58);
    entry.height = ReadUInt32(renderSurface, 0x5C);
    entry.imageSize = ReadUInt32(renderSurface, 0x60);
    entry.pixelFormat = ReadUInt32(renderSurface, 0x68);
    entry.formatInfo = ReadUInt32(renderSurface, 0x6C);

    std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
    g_LiveTextureRegistry[did] = entry;
}

static bool GetLiveTexture(std::uint32_t did, LiveTextureEntry& entry)
{
    std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
    const auto it = g_LiveTextureRegistry.find(did);
    if (it == g_LiveTextureRegistry.end())
        return false;
    entry = it->second;
    return true;
}

// Registry entries are learned from RenderSurface uploads, but AC can later
// destroy/recycle those objects without giving AC Customs a corresponding
// unregister callback. Validate the object before treating a registry hit as a
// currently-resident texture. A stale hit is equivalent to "not resident":
// the next real upload will register the fresh surface again.
static bool LiveTextureEntryIsCurrent(
    std::uint32_t did,
    const LiveTextureEntry& entry,
    std::string* reason = nullptr)
{
    void* rs = entry.renderSurface;

    if (!rs)
    {
        if (reason != nullptr)
            *reason = "registered RenderSurface pointer is null";
        return false;
    }

    if (!ProbeReadableRange(rs, 0x70))
    {
        if (reason != nullptr)
            *reason = "registered RenderSurface is no longer readable";
        return false;
    }

    const std::uint32_t actualDid = ReadUInt32(rs, 0x28);
    if (actualDid != did)
    {
        if (reason != nullptr)
            *reason =
                "registered RenderSurface was recycled for DID " +
                Hex32(actualDid);
        return false;
    }

    return true;
}

static bool ForgetLiveTextureIfSame(
    std::uint32_t did,
    void* expectedRenderSurface)
{
    std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);

    const auto it = g_LiveTextureRegistry.find(did);
    if (it == g_LiveTextureRegistry.end())
        return false;

    // Do not erase a newer registration that raced with our stale check.
    if (expectedRenderSurface != nullptr &&
        it->second.renderSurface != expectedRenderSurface)
    {
        return false;
    }

    g_LiveTextureRegistry.erase(it);
    return true;
}

enum class LiveApplyFailureKind : std::uint32_t
{
    None = 0,
    NotRegistered,
    StaleSurface,
    ByteSizeMismatch,
    UnsupportedFormat,
    GeometryMismatch,
    RendererLockFailed,
    DestinationInvalid
};

static bool ApplyTextureLive(
    std::uint32_t did,
    const std::vector<unsigned char>& bytes,
    std::string* failureReason = nullptr,
    LiveApplyFailureKind* failureKind = nullptr)
{
    if (failureKind != nullptr)
        *failureKind = LiveApplyFailureKind::None;

    const auto fail =
        [&](LiveApplyFailureKind kind, const std::string& reason)
        {
            if (failureReason != nullptr)
                *failureReason = reason;
            if (failureKind != nullptr)
                *failureKind = kind;
            return false;
        };

    LiveTextureEntry entry = {};
    if (!GetLiveTexture(did, entry))
    {
        WriteLog("LIVE REGISTRY: DID not loaded " + Hex32(did));
        return fail(
            LiveApplyFailureKind::NotRegistered,
            "texture is not registered/live");
    }

    void* rs = entry.renderSurface;
    std::string currentReason;
    if (!LiveTextureEntryIsCurrent(did, entry, &currentReason) ||
        !ProbeReadableRange(rs, 0xBC))
    {
        WriteLog("LIVE REGISTRY: stale RenderSurface DID=" + Hex32(did));
        return fail(
            LiveApplyFailureKind::StaleSurface,
            currentReason.empty()
                ? std::string("registered RenderSurface is stale or belongs to another DID")
                : currentReason);
    }

    const std::uint32_t width = ReadUInt32(rs, 0x58);
    const std::uint32_t height = ReadUInt32(rs, 0x5C);
    const std::uint32_t imageSize = ReadUInt32(rs, 0x60);
    const std::uint32_t pixelFormat = ReadUInt32(rs, 0x68);

    if (bytes.size() != imageSize)
    {
        WriteLog("LIVE REGISTRY: byte-size mismatch DID=" + Hex32(did));
        return fail(
            LiveApplyFailureKind::ByteSizeMismatch,
            "replacement/original byte-size mismatch: got " +
            std::to_string(bytes.size()) +
            ", expected " + std::to_string(imageSize));
    }

    std::uint32_t sourceBpp = 0;
    if (pixelFormat == 0x15) sourceBpp = 4;
    else if (pixelFormat == 0x14) sourceBpp = 3;
    else
    {
        WriteLog("LIVE REGISTRY: unsupported format DID=" + Hex32(did) +
                 " format=" + Hex32(pixelFormat));
        return fail(
            LiveApplyFailureKind::UnsupportedFormat,
            "unsupported pixel format " + Hex32(pixelFormat));
    }

    const std::uint32_t sourceRowBytes = width * sourceBpp;
    if (sourceRowBytes == 0 ||
        static_cast<std::uint64_t>(sourceRowBytes) * height != imageSize)
    {
        WriteLog("LIVE REGISTRY: geometry mismatch DID=" + Hex32(did));
        return fail(
            LiveApplyFailureKind::GeometryMismatch,
            "surface geometry/image-size mismatch " +
            std::to_string(width) + "x" + std::to_string(height) +
            " imageSize=" + std::to_string(imageSize));
    }

    std::uint32_t pitch = 0;
    void* bits = nullptr;
    void* lockState = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(rs) + 0xB8);

    const bool locked =
        g_OriginalRendererLock(rs, lockState, 1, &pitch, &bits);

    // The live renderer resource is BGRA32 even when the serialized
    // RenderSurface source is PFID_R8G8B8 (0x14 / BGR24).
    const std::uint32_t requiredDestinationRowBytes = width * 4u;

    if (!locked || bits == nullptr || pitch < requiredDestinationRowBytes)
    {
        WriteLog("LIVE REGISTRY: lock failed DID=" + Hex32(did));
        return fail(
            LiveApplyFailureKind::RendererLockFailed,
            "renderer lock failed or returned insufficient pitch (pitch=" +
            std::to_string(pitch) +
            ", required=" + std::to_string(requiredDestinationRowBytes) + ")");
    }

    if (!IsWritableRange(bits, static_cast<std::size_t>(pitch) * height))
    {
        g_RendererUnlock(rs);
        WriteLog("LIVE REGISTRY: destination invalid DID=" + Hex32(did));
        return fail(
            LiveApplyFailureKind::DestinationInvalid,
            "renderer destination memory is not writable for the full surface");
    }

    auto* dstBits = static_cast<unsigned char*>(bits);

    if (pixelFormat == 0x15)
    {
        for (std::uint32_t y = 0; y < height; ++y)
            std::memcpy(
                dstBits + static_cast<std::size_t>(y) * pitch,
                bytes.data() + static_cast<std::size_t>(y) * sourceRowBytes,
                sourceRowBytes);

        WriteLog("PROBE #15 COPY DID=" + Hex32(did) +
                 " mode=BGRA32_DIRECT");
    }
    else
    {
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const unsigned char* srcRow =
                bytes.data() + static_cast<std::size_t>(y) * sourceRowBytes;
            unsigned char* dstRow =
                dstBits + static_cast<std::size_t>(y) * pitch;

            for (std::uint32_t x = 0; x < width; ++x)
            {
                const unsigned char* srcPixel =
                    srcRow + static_cast<std::size_t>(x) * 3u;
                unsigned char* dstPixel =
                    dstRow + static_cast<std::size_t>(x) * 4u;

                dstPixel[0] = srcPixel[0]; // B
                dstPixel[1] = srcPixel[1]; // G
                dstPixel[2] = srcPixel[2]; // R
                dstPixel[3] = 0xFFu;       // A
            }
        }

        WriteLog("PROBE #15 COPY DID=" + Hex32(did) +
                 " mode=BGR24_TO_BGRA32");
    }

    g_RendererUnlock(rs);

    WriteLog("LIVE REGISTRY APPLY DID=" + Hex32(did) +
             " this=" + Hex32(static_cast<std::uint32_t>(
                 reinterpret_cast<std::uintptr_t>(rs))) +
             " " + std::to_string(width) + "x" + std::to_string(height) +
             " pitch=" + std::to_string(pitch));
    return true;
}

// A RenderSurface can remain readable in AC's object graph after its actual
// D3D backing surface has been released. In that state the renderer lock
// returns false with pitch=0 forever, even though the registry entry still
// looks superficially valid. +0x120 is the D3D surface pointer established by
// the earlier renderer probes. Only use this test after a real lock failure; a
// missing backing means there is no resident themed GPU surface left to restore.
static bool LiveTextureRendererBackingIsRetired(
    std::uint32_t did,
    const LiveTextureEntry& entry,
    std::string* reason = nullptr)
{
    void* rs = entry.renderSurface;
    if (!rs ||
        !ProbeReadableRange(rs, 0x124) ||
        ReadUInt32(rs, 0x28) != did)
    {
        if (reason != nullptr)
            *reason = "RenderSurface object is no longer readable/current";
        return true;
    }

    const std::uint32_t backingAddress = ReadUInt32(rs, 0x120);
    if (backingAddress == 0)
    {
        if (reason != nullptr)
            *reason = "RenderSurface+0x120 backing surface is null";
        return true;
    }

    void* backing = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(backingAddress));

    if (!ProbeReadableRange(backing, sizeof(std::uint32_t)))
    {
        if (reason != nullptr)
            *reason =
                "RenderSurface+0x120 backing surface is no longer readable (" +
                Hex32(backingAddress) + ")";
        return true;
    }

    const std::uint32_t backingVtable = ReadUInt32(backing, 0x00);
    if (backingVtable == 0)
    {
        if (reason != nullptr)
            *reason =
                "RenderSurface+0x120 backing surface has no vtable (" +
                Hex32(backingAddress) + ")";
        return true;
    }

    return false;
}

static void DumpProbe14Surface(
    std::uint32_t did,
    const LiveTextureEntry& entry)
{
    void* rs = entry.renderSurface;

    WriteLog(
        "PROBE #14 BEFORE DID=" + Hex32(did) +
        " this=" + Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(rs))) +
        " width=" + std::to_string(ReadUInt32(rs, 0x58)) +
        " height=" + std::to_string(ReadUInt32(rs, 0x5C)) +
        " imageSize=" + std::to_string(ReadUInt32(rs, 0x60)) +
        " pixelFormat=" + Hex32(ReadUInt32(rs, 0x68)) +
        " formatInfo=" + Hex32(ReadUInt32(rs, 0x6C)) +
        " +20=" + Hex32(ReadUInt32(rs, 0x20)) +
        " +24=" + Hex32(ReadUInt32(rs, 0x24)) +
        " +50=" + Hex32(ReadUInt32(rs, 0x50)) +
        " +A0=" + Hex32(ReadUInt32(rs, 0xA0)) +
        " +A4=" + Hex32(ReadUInt32(rs, 0xA4)) +
        " +A8=" + Hex32(ReadUInt32(rs, 0xA8)) +
        " +B0=" + Hex32(ReadUInt32(rs, 0xB0)) +
        " +DC=" + Hex32(ReadUInt32(rs, 0xDC)) +
        " +E0=" + Hex32(ReadUInt32(rs, 0xE0)));
}

static bool Probe14ApplyOne(std::uint32_t did)
{
    LiveTextureEntry entry = {};
    if (!GetLiveTexture(did, entry))
    {
        WriteLog("PROBE #14 SKIP DID=" + Hex32(did) + " reason=not_registered");
        return false;
    }

    if (!ProbeReadableRange(entry.renderSurface, 0xE4))
    {
        WriteLog("PROBE #14 SKIP DID=" + Hex32(did) + " reason=surface_not_readable");
        return false;
    }

    DumpProbe14Surface(did, entry);

    const std::string path =
        ACCustomsGetReplacementDirectory() + "\\" +
        DIDFileName(did) + ".rgb";

    std::vector<unsigned char> bytes;
    if (!ReadBinaryFileExact(path, bytes, entry.imageSize))
    {
        WriteLog(
            "PROBE #14 SKIP DID=" + Hex32(did) +
            " reason=replacement_missing_or_wrong_size path=" + path);
        return false;
    }

    const bool ok = ApplyTextureLive(did, bytes);

    WriteLog(
        "PROBE #14 RESULT DID=" + Hex32(did) +
        " result=" + std::string(ok ? "APPLIED" : "FAILED"));

    return ok;
}


struct ApplySetStats {
    std::uint32_t filesFound=0, registered=0, applied=0;
    std::uint32_t notResident=0, retiredSurfaces=0, missingOriginal=0, incompatible=0;
    std::uint32_t readFailed=0, liveApplyFailed=0, failed=0;
    std::uint32_t lockRetries=0, retryRecovered=0;
    std::vector<std::string> details;
};

static bool ParseTextureDIDFromFileName(const std::string& name, std::uint32_t& did)
{
    did=0;
    if (name.size()!=12 || name.substr(8)!=".rgb") return false;
    char* end=nullptr;
    unsigned long v=std::strtoul(name.substr(0,8).c_str(), &end, 16);
    if (!end || *end!='\0' || (v & 0xFF000000ul)!=0x06000000ul) return false;
    did=static_cast<std::uint32_t>(v);
    return true;
}

static const char* ActiveThemeModeName(ActiveThemeMode mode)
{
    switch (mode)
    {
    case ActiveThemeMode::Vanilla: return "VANILLA";
    case ActiveThemeMode::TestReplacement: return "TEST_REPLACEMENT";
    default: return "UNKNOWN";
    }
}

static std::string ACCustomsBuildThemeOperationReport(
    const char* operation,
    const ApplySetStats& st,
    bool success,
    ActiveThemeMode modeBefore,
    ActiveThemeMode modeAfter)
{
    std::ostringstream out;
    out << (operation ? operation : "THEME")
        << " result=" << (success ? "SUCCESS" : "INCOMPLETE")
        << " modeBefore=" << ActiveThemeModeName(modeBefore)
        << " modeAfter=" << ActiveThemeModeName(modeAfter)
        << " files=" << st.filesFound
        << " resident=" << st.registered
        << " notResident=" << st.notResident
        << " retiredSurfaces=" << st.retiredSurfaces
        << " applied=" << st.applied
        << " missingOriginal=" << st.missingOriginal
        << " incompatible=" << st.incompatible
        << " readFailed=" << st.readFailed
        << " liveApplyFailed=" << st.liveApplyFailed
        << " lockRetries=" << st.lockRetries
        << " retryRecovered=" << st.retryRecovered
        << " failed=" << st.failed;

    for (const std::string& detail : st.details)
        out << "\n    " << detail;

    return out.str();
}

static ActiveThemeMode GetActiveThemeMode()
{
    return g_ActiveThemeMode.load(std::memory_order_acquire);
}

static void SetActiveThemeMode(ActiveThemeMode mode, const char* reason)
{
    const ActiveThemeMode previous =
        g_ActiveThemeMode.exchange(mode, std::memory_order_acq_rel);
    const std::uint32_t generation =
        g_Probe92ThemeGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

    WriteLog(
        "PROBE #92 ACTIVE_THEME previous=" +
        std::string(ActiveThemeModeName(previous)) +
        " current=" + ActiveThemeModeName(mode) +
        " generation=" + std::to_string(generation) +
        " reason=" + (reason ? std::string(reason) : std::string("unspecified")));
}

static bool CaptureResidentOriginal(
    std::uint32_t did,
    const LiveTextureEntry& entry,
    std::string* failureReason = nullptr);

static ApplySetStats ApplyReplacementSet(bool useReplacement)
{
    ApplySetStats st={};

    // Restore exactly the live textures that this theme actually changed.
    // A pack may contain DIDs that loaded while we were already Vanilla; those
    // must not be treated as missing-original failures because they were never
    // overridden in the first place.
    if (!useReplacement)
    {
        const std::vector<std::uint32_t> appliedDids =
            SnapshotActiveAppliedDids();
        st.filesFound = static_cast<std::uint32_t>(appliedDids.size());

        for (const std::uint32_t did : appliedDids)
        {
            LiveTextureEntry e={};
            if (!GetLiveTexture(did,e))
            {
                // The themed surface no longer exists. With desired mode now
                // Vanilla, any future load of this DID comes from the DAT, so
                // there is nothing resident left to restore.
                ++st.notResident;
                ForgetActiveAppliedDid(did);
                continue;
            }

            std::string registryReason;
            if (!LiveTextureEntryIsCurrent(did, e, &registryReason))
            {
                ForgetLiveTextureIfSame(did, e.renderSurface);
                ForgetActiveAppliedDid(did);
                ++st.retiredSurfaces;
                st.details.push_back(
                    "DID=" + Hex32(did) +
                    " restore skipped: stale live-registry entry evicted" +
                    (registryReason.empty()
                        ? std::string()
                        : std::string(" (" + registryReason + ")")));
                continue;
            }

            ++st.registered;

            if (e.pixelFormat!=0x14 && e.pixelFormat!=0x15)
            {
                ++st.incompatible;
                ++st.failed;
                st.details.push_back(
                    "DID=" + Hex32(did) +
                    " restore failed: unsupported pixel format " +
                    Hex32(e.pixelFormat));
                continue;
            }

            const std::string path =
                ACCustomsGetCaptureDirectory()+"\\"+DIDFileName(did)+".rgb";

            if (GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES)
            {
                ++st.missingOriginal;
                ++st.failed;
                st.details.push_back(
                    "DID=" + Hex32(did) +
                    " restore failed: captured vanilla original is missing: " + path);
                continue;
            }

            std::vector<unsigned char> bytes;
            if (!ReadBinaryFileExact(path,bytes,e.imageSize))
            {
                ++st.missingOriginal;
                ++st.readFailed;
                ++st.failed;
                st.details.push_back(
                    "DID=" + Hex32(did) +
                    " restore failed: captured vanilla original has wrong size or could not be read: " + path);
                continue;
            }

            std::string applyFailure;
            LiveApplyFailureKind failureKind =
                LiveApplyFailureKind::None;

            bool restored = ApplyTextureLive(
                did,
                bytes,
                &applyFailure,
                &failureKind);

            // Renderer lock failures can be momentary while AC is transitioning
            // a UI resource. Retry only this specific failure, and keep the
            // total wait tightly bounded so theme switching remains responsive.
            if (!restored &&
                failureKind == LiveApplyFailureKind::RendererLockFailed)
            {
                static constexpr int kRestoreLockRetryCount = 2;
                static constexpr DWORD kRestoreLockRetryDelayMs = 20;

                for (int retry = 0;
                     retry < kRestoreLockRetryCount && !restored;
                     ++retry)
                {
                    ++st.lockRetries;
                    Sleep(kRestoreLockRetryDelayMs);

                    std::string retryFailure;
                    LiveApplyFailureKind retryKind =
                        LiveApplyFailureKind::None;

                    restored = ApplyTextureLive(
                        did,
                        bytes,
                        &retryFailure,
                        &retryKind);

                    if (restored)
                    {
                        ++st.retryRecovered;
                        applyFailure.clear();
                        failureKind = LiveApplyFailureKind::None;
                        break;
                    }

                    applyFailure = retryFailure;
                    failureKind = retryKind;

                    if (failureKind !=
                        LiveApplyFailureKind::RendererLockFailed)
                    {
                        break;
                    }
                }
            }

            if (restored)
            {
                ++st.applied;
                ForgetActiveAppliedDid(did);
                continue;
            }

            // A readable RenderSurface object can outlive its D3D backing. If
            // the lock still fails after the bounded retries and +0x120 is gone,
            // the themed GPU resource itself no longer exists. Desired mode is
            // already Vanilla, so any future recreation/load will be vanilla.
            if (failureKind == LiveApplyFailureKind::RendererLockFailed)
            {
                LiveTextureEntry latest = {};
                std::string retiredReason;

                const bool haveLatest = GetLiveTexture(did, latest);
                if (!haveLatest ||
                    LiveTextureRendererBackingIsRetired(
                        did,
                        latest,
                        &retiredReason))
                {
                    if (haveLatest)
                        ForgetLiveTextureIfSame(did, latest.renderSurface);
                    ++st.retiredSurfaces;
                    ForgetActiveAppliedDid(did);
                    st.details.push_back(
                        "DID=" + Hex32(did) +
                        " restore skipped: renderer surface retired after lock failure" +
                        (retiredReason.empty()
                            ? std::string()
                            : std::string(" (" + retiredReason + ")")));
                    continue;
                }
            }

            ++st.liveApplyFailed;
            ++st.failed;
            st.details.push_back(
                "DID=" + Hex32(did) +
                " restore failed: " +
                (applyFailure.empty() ? std::string("live surface apply failed") : applyFailure));
        }

        WriteLog(std::string("UI SET #19: ORIGINAL")+
            " files="+std::to_string(st.filesFound)+
            " registered="+std::to_string(st.registered)+
            " retiredSurfaces="+std::to_string(st.retiredSurfaces)+
            " applied="+std::to_string(st.applied)+
            " missingOriginal="+std::to_string(st.missingOriginal)+
            " incompatible="+std::to_string(st.incompatible)+
            " lockRetries="+std::to_string(st.lockRetries)+
            " retryRecovered="+std::to_string(st.retryRecovered)+
            " failed="+std::to_string(st.failed));
        return st;
    }

    std::string search=ACCustomsGetReplacementDirectory()+"\\*.rgb";
    WIN32_FIND_DATAA fd={};
    HANDLE h=FindFirstFileA(search.c_str(), &fd);
    if (h==INVALID_HANDLE_VALUE) {
        WriteLog("UI SET #19: no replacement .rgb files found.");
        return st;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::uint32_t did=0;
        if (!ParseTextureDIDFromFileName(fd.cFileName,did)) continue;
        ++st.filesFound;

        LiveTextureEntry e={};
        if (!GetLiveTexture(did,e))
        {
            ++st.notResident;
            continue;
        }

        std::string registryReason;
        if (!LiveTextureEntryIsCurrent(did, e, &registryReason))
        {
            ForgetLiveTextureIfSame(did, e.renderSurface);
            ++st.retiredSurfaces;
            st.details.push_back(
                "DID=" + Hex32(did) +
                " apply skipped: stale live-registry entry evicted" +
                (registryReason.empty()
                    ? std::string()
                    : std::string(" (" + registryReason + ")")));
            continue;
        }

        ++st.registered;

        if (e.pixelFormat!=0x14 && e.pixelFormat!=0x15) {
            ++st.incompatible;
            continue;
        }

        // Never modify a resident vanilla surface unless we have first secured
        // restorable bytes for that exact DID. CaptureResidentOriginal is a
        // cheap file-size check when the original was already captured.
        std::string captureFailure;
        if (!CaptureResidentOriginal(did,e,&captureFailure))
        {
            ++st.missingOriginal;
            ++st.failed;
            st.details.push_back(
                "DID=" + Hex32(did) +
                " apply failed: could not secure vanilla original: " +
                (captureFailure.empty() ? std::string("unknown capture failure") : captureFailure));
            continue;
        }

        const std::string path=
            ACCustomsGetReplacementDirectory()+"\\"+fd.cFileName;

        std::vector<unsigned char> bytes;
        if (!ReadBinaryFileExact(path,bytes,e.imageSize)) {
            ++st.readFailed;
            ++st.failed;
            st.details.push_back(
                "DID=" + Hex32(did) +
                " apply failed: replacement file is missing, unreadable, or wrong size: " + path +
                " expectedBytes=" + std::to_string(e.imageSize));
            continue;
        }

        std::string applyFailure;
        LiveApplyFailureKind failureKind =
            LiveApplyFailureKind::None;

        bool applied = ApplyTextureLive(
            did,
            bytes,
            &applyFailure,
            &failureKind);

        // Apply can hit the same short renderer transition as Restore. Keep the
        // retry bounded, and only retry the renderer-lock failure class.
        if (!applied &&
            failureKind == LiveApplyFailureKind::RendererLockFailed)
        {
            static constexpr int kApplyLockRetryCount = 2;
            static constexpr DWORD kApplyLockRetryDelayMs = 20;

            for (int retry = 0;
                 retry < kApplyLockRetryCount && !applied;
                 ++retry)
            {
                ++st.lockRetries;
                Sleep(kApplyLockRetryDelayMs);

                std::string retryFailure;
                LiveApplyFailureKind retryKind =
                    LiveApplyFailureKind::None;

                applied = ApplyTextureLive(
                    did,
                    bytes,
                    &retryFailure,
                    &retryKind);

                if (applied)
                {
                    ++st.retryRecovered;
                    applyFailure.clear();
                    failureKind = LiveApplyFailureKind::None;
                    break;
                }

                applyFailure = retryFailure;
                failureKind = retryKind;

                if (failureKind !=
                    LiveApplyFailureKind::RendererLockFailed)
                {
                    break;
                }
            }
        }

        if (applied)
        {
            ++st.applied;
            MarkActiveAppliedDid(did);
            continue;
        }

        // If AC recycled the RenderSurface after our initial registry check,
        // evict the dead entry. Desired mode is already replacement mode, so a
        // future real load of this DID will be intercepted and themed normally.
        if (failureKind == LiveApplyFailureKind::StaleSurface)
        {
            LiveTextureEntry latest = {};
            std::string staleReason;
            const bool haveLatest = GetLiveTexture(did, latest);

            if (!haveLatest ||
                !LiveTextureEntryIsCurrent(did, latest, &staleReason))
            {
                if (haveLatest)
                    ForgetLiveTextureIfSame(did, latest.renderSurface);
                ++st.retiredSurfaces;
                st.details.push_back(
                    "DID=" + Hex32(did) +
                    " apply skipped: stale live-registry entry evicted" +
                    (staleReason.empty()
                        ? std::string()
                        : std::string(" (" + staleReason + ")")));
                continue;
            }
        }

        // Likewise, a readable RenderSurface whose D3D backing has been
        // released is no longer an apply target. Do not turn that harmless
        // retirement into a permanent Incomplete state.
        if (failureKind == LiveApplyFailureKind::RendererLockFailed)
        {
            LiveTextureEntry latest = {};
            std::string retiredReason;
            const bool haveLatest = GetLiveTexture(did, latest);

            if (!haveLatest ||
                LiveTextureRendererBackingIsRetired(
                    did,
                    latest,
                    &retiredReason))
            {
                if (haveLatest)
                    ForgetLiveTextureIfSame(did, latest.renderSurface);
                ++st.retiredSurfaces;
                st.details.push_back(
                    "DID=" + Hex32(did) +
                    " apply skipped: renderer surface retired after lock failure" +
                    (retiredReason.empty()
                        ? std::string()
                        : std::string(" (" + retiredReason + ")")));
                continue;
            }
        }

        ++st.liveApplyFailed;
        ++st.failed;
        st.details.push_back(
            "DID=" + Hex32(did) +
            " apply failed: " +
            (applyFailure.empty() ? std::string("live surface apply failed") : applyFailure));
    } while (FindNextFileA(h,&fd));

    FindClose(h);
    WriteLog(std::string("UI SET #19: REPLACEMENT")+
        " files="+std::to_string(st.filesFound)+
        " registered="+std::to_string(st.registered)+
        " retiredSurfaces="+std::to_string(st.retiredSurfaces)+
        " applied="+std::to_string(st.applied)+
        " missingOriginal="+std::to_string(st.missingOriginal)+
        " incompatible="+std::to_string(st.incompatible)+
        " lockRetries="+std::to_string(st.lockRetries)+
        " retryRecovered="+std::to_string(st.retryRecovered)+
        " failed="+std::to_string(st.failed));
    return st;
}


// ------------------------------------------------------------
// AC Customs runtime: capture Vanilla bytes from already-resident surfaces.
//
// The old pre-launch injector saw texture loads early enough that HookedPixelSource
// captured nearly every original automatically. A Decal plugin can initialize
// later, after some UI textures are already resident. Before the first custom
// Apply from VANILLA, snapshot any resident replacement-backed surface whose
// captured .rgb does not already exist. Lazy loads after Apply are still captured
// by HookedPixelSource before substitution, exactly as in Probe #88/#92.
// ------------------------------------------------------------
struct ResidentCaptureStats
{
    std::uint32_t filesFound = 0;
    std::uint32_t registered = 0;
    std::uint32_t alreadyCaptured = 0;
    std::uint32_t captured = 0;
    std::uint32_t failed = 0;
};

static bool CaptureResidentOriginal(
    std::uint32_t did,
    const LiveTextureEntry& entry,
    std::string* failureReason)
{
    const auto fail =
        [&](const std::string& reason)
        {
            if (failureReason != nullptr)
                *failureReason = reason;
            return false;
        };

    const std::string capturedPath =
        ACCustomsGetCaptureDirectory() + "\\" + DIDFileName(did) + ".rgb";

    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (GetFileAttributesExA(
            capturedPath.c_str(),
            GetFileExInfoStandard,
            &attrs))
    {
        const std::uint64_t existingSize =
            (static_cast<std::uint64_t>(attrs.nFileSizeHigh) << 32) |
            attrs.nFileSizeLow;
        if (existingSize == entry.imageSize)
            return true;
    }

    void* rs = entry.renderSurface;
    if (!ProbeReadableRange(rs, 0xBC) || ReadUInt32(rs, 0x28) != did)
        return fail("registered RenderSurface is stale or belongs to another DID");

    const std::uint32_t width = ReadUInt32(rs, 0x58);
    const std::uint32_t height = ReadUInt32(rs, 0x5C);
    const std::uint32_t imageSize = ReadUInt32(rs, 0x60);
    const std::uint32_t pixelFormat = ReadUInt32(rs, 0x68);

    std::uint32_t serializedBpp = 0;
    if (pixelFormat == 0x15) serializedBpp = 4;
    else if (pixelFormat == 0x14) serializedBpp = 3;
    else return fail("unsupported pixel format " + Hex32(pixelFormat));

    const std::uint32_t serializedRowBytes = width * serializedBpp;
    if (serializedRowBytes == 0 ||
        static_cast<std::uint64_t>(serializedRowBytes) * height != imageSize)
        return fail(
            "surface geometry/image-size mismatch " +
            std::to_string(width) + "x" + std::to_string(height) +
            " imageSize=" + std::to_string(imageSize));

    std::uint32_t pitch = 0;
    void* bits = nullptr;
    void* lockState = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(rs) + 0xB8);

    if (!g_OriginalRendererLock || !g_RendererUnlock ||
        !g_OriginalRendererLock(rs, lockState, 1, &pitch, &bits) ||
        !bits || pitch < width * 4u)
    {
        return fail(
            "could not lock live renderer surface for original capture (pitch=" +
            std::to_string(pitch) +
            ", required=" + std::to_string(width * 4u) + ")");
    }

    std::vector<unsigned char> original(imageSize);
    const auto* srcBits = static_cast<const unsigned char*>(bits);

    if (pixelFormat == 0x15)
    {
        for (std::uint32_t y = 0; y < height; ++y)
        {
            std::memcpy(
                original.data() + static_cast<std::size_t>(y) * serializedRowBytes,
                srcBits + static_cast<std::size_t>(y) * pitch,
                serializedRowBytes);
        }
    }
    else
    {
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const unsigned char* srcRow =
                srcBits + static_cast<std::size_t>(y) * pitch;
            unsigned char* dstRow =
                original.data() + static_cast<std::size_t>(y) * serializedRowBytes;

            for (std::uint32_t x = 0; x < width; ++x)
            {
                const unsigned char* srcPixel =
                    srcRow + static_cast<std::size_t>(x) * 4u;
                unsigned char* dstPixel =
                    dstRow + static_cast<std::size_t>(x) * 3u;
                dstPixel[0] = srcPixel[0];
                dstPixel[1] = srcPixel[1];
                dstPixel[2] = srcPixel[2];
            }
        }
    }

    g_RendererUnlock(rs);

    CreateDirectoryA(ACCustomsGetCaptureDirectory().c_str(), nullptr);
    std::ofstream file(capturedPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return fail("could not create captured-original file: " + capturedPath);

    file.write(
        reinterpret_cast<const char*>(original.data()),
        static_cast<std::streamsize>(original.size()));
    const bool ok = file.good();
    file.close();

    if (ok)
    {
        WriteLog(
            "ACCUSTOMS RESIDENT_ORIGINAL_CAPTURE DID=" + Hex32(did) +
            " bytes=" + std::to_string(original.size()) +
            " file=" + capturedPath);
        return true;
    }

    return fail("failed while writing captured-original file: " + capturedPath);
}

static std::string ACCustomsSnapshotAssetRootFromOutputPath(
    const std::string& outputPath)
{
    std::string stem = outputPath;
    if (stem.size() >= 5)
    {
        const std::string suffix = stem.substr(stem.size() - 5);
        if (suffix == ".json" || suffix == ".JSON")
            stem.resize(stem.size() - 5);
    }
    return stem + "_assets";
}

static bool ACCustomsSnapshotDumpOneTexture(
    std::uint32_t did,
    const LiveTextureEntry& entry,
    const std::string& path,
    ACCustomsSnapshotTextureAsset& asset)
{
    asset.did = did;
    asset.width = entry.width;
    asset.height = entry.height;
    asset.imageSize = entry.imageSize;
    asset.pixelFormat = entry.pixelFormat;
    asset.formatInfo = entry.formatInfo;
    asset.fileName = DIDFileName(did) + ".rgb";
    asset.status = "failed";

    void* rs = entry.renderSurface;
    if (!ProbeReadableRange(rs, 0xBC) || ReadUInt32(rs, 0x28) != did)
    {
        asset.status = "stale_surface";
        return false;
    }

    const std::uint32_t width = ReadUInt32(rs, 0x58);
    const std::uint32_t height = ReadUInt32(rs, 0x5C);
    const std::uint32_t imageSize = ReadUInt32(rs, 0x60);
    const std::uint32_t pixelFormat = ReadUInt32(rs, 0x68);
    const std::uint32_t formatInfo = ReadUInt32(rs, 0x6C);

    asset.width = width;
    asset.height = height;
    asset.imageSize = imageSize;
    asset.pixelFormat = pixelFormat;
    asset.formatInfo = formatInfo;

    std::uint32_t serializedBpp = 0;
    if (pixelFormat == 0x15u) serializedBpp = 4;
    else if (pixelFormat == 0x14u) serializedBpp = 3;
    else
    {
        asset.status = "unsupported_format";
        return false;
    }

    const std::uint32_t serializedRowBytes = width * serializedBpp;
    if (width == 0 || height == 0 || serializedRowBytes == 0 ||
        static_cast<std::uint64_t>(serializedRowBytes) * height != imageSize)
    {
        asset.status = "geometry_mismatch";
        return false;
    }

    if (!g_OriginalRendererLock || !g_RendererUnlock)
    {
        asset.status = "renderer_lock_unavailable";
        return false;
    }

    std::uint32_t pitch = 0;
    void* bits = nullptr;
    void* lockState = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(rs) + 0xB8);

    if (!g_OriginalRendererLock(rs, lockState, 1, &pitch, &bits) ||
        !bits || pitch < width * 4u)
    {
        asset.status = "lock_failed";
        return false;
    }

    std::vector<unsigned char> serialized(imageSize);
    const auto* srcBits = static_cast<const unsigned char*>(bits);

    if (pixelFormat == 0x15u)
    {
        for (std::uint32_t y = 0; y < height; ++y)
        {
            std::memcpy(
                serialized.data() + static_cast<std::size_t>(y) * serializedRowBytes,
                srcBits + static_cast<std::size_t>(y) * pitch,
                serializedRowBytes);
        }
    }
    else
    {
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const unsigned char* srcRow =
                srcBits + static_cast<std::size_t>(y) * pitch;
            unsigned char* dstRow =
                serialized.data() + static_cast<std::size_t>(y) * serializedRowBytes;

            for (std::uint32_t x = 0; x < width; ++x)
            {
                const unsigned char* srcPixel =
                    srcRow + static_cast<std::size_t>(x) * 4u;
                unsigned char* dstPixel =
                    dstRow + static_cast<std::size_t>(x) * 3u;
                dstPixel[0] = srcPixel[0];
                dstPixel[1] = srcPixel[1];
                dstPixel[2] = srcPixel[2];
            }
        }
    }

    g_RendererUnlock(rs);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        asset.status = "open_failed";
        return false;
    }

    out.write(
        reinterpret_cast<const char*>(serialized.data()),
        static_cast<std::streamsize>(serialized.size()));
    const bool ok = out.good();
    out.close();

    asset.status = ok ? "dumped" : "write_failed";
    return ok;
}

static bool ACCustomsSnapshotDumpGeneratedSurface(
    std::uint32_t surfaceAddress,
    const std::string& path,
    ACCustomsSnapshotGeneratedAsset& asset)
{
    asset = {};
    asset.surface = surfaceAddress;
    asset.pixelFormat = 0x15u; // serialized as BGRA32 regardless of source metadata
    asset.status = "failed";

    if (!surfaceAddress)
    {
        asset.status = "no_surface";
        return false;
    }

    void* rs = reinterpret_cast<void*>(static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(rs, 0xBC))
    {
        asset.status = "stale_surface";
        return false;
    }

    std::uint32_t width = ReadUInt32(rs, 0x58);
    std::uint32_t height = ReadUInt32(rs, 0x5C);

    // Normal DAT RenderSurface objects expose geometry at +0x58/+0x5C.
    // Generated item surfaces expose their drawable extent at +0xA0/+0xA4.
    if (width == 0 || height == 0 || width > 512 || height > 512)
    {
        width = ReadUInt32(rs, 0xA0);
        height = ReadUInt32(rs, 0xA4);
    }

    asset.width = width;
    asset.height = height;

    if (width == 0 || height == 0 || width > 512 || height > 512)
    {
        asset.status = "unsupported_geometry";
        return false;
    }

    if (!g_OriginalRendererLock || !g_RendererUnlock)
    {
        asset.status = "renderer_lock_unavailable";
        return false;
    }

    std::uint32_t pitch = 0;
    void* bits = nullptr;
    void* lockState = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(rs) + 0xB8);
    if (!g_OriginalRendererLock(rs, lockState, 1, &pitch, &bits) ||
        !bits || pitch < width * 4u)
    {
        asset.status = "lock_failed";
        return false;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    std::vector<unsigned char> serialized(
        rowBytes * static_cast<std::size_t>(height));
    const auto* src = static_cast<const unsigned char*>(bits);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(
            serialized.data() + static_cast<std::size_t>(y) * rowBytes,
            src + static_cast<std::size_t>(y) * pitch,
            rowBytes);
    }
    g_RendererUnlock(rs);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        asset.status = "open_failed";
        return false;
    }
    out.write(
        reinterpret_cast<const char*>(serialized.data()),
        static_cast<std::streamsize>(serialized.size()));
    const bool ok = out.good();
    out.close();

    asset.width = width;
    asset.height = height;
    asset.imageSize = static_cast<std::uint32_t>(serialized.size());
    asset.status = ok ? "dumped" : "write_failed";
    return ok;
}

static std::vector<ACCustomsSnapshotGeneratedAsset>
ACCustomsSnapshotDumpGeneratedSurfaces(
    const std::string& outputPath,
    const std::vector<ACCustomsSnapshotBlit>& blits)
{
    std::vector<ACCustomsSnapshotGeneratedAsset> assets;
    if (outputPath.empty())
        return assets;

    const std::string assetRoot =
        ACCustomsSnapshotAssetRootFromOutputPath(outputPath);
    const std::string generatedDirectory = assetRoot + "\\generated";
    CreateDirectoryA(assetRoot.c_str(), nullptr);
    CreateDirectoryA(generatedDirectory.c_str(), nullptr);

    // Live Mirror v1.2.4: capture ONLY generated surfaces that are
    // already correlated to a runtime item ID by the stable item-provenance
    // path. Do not touch unrelated DID-less compositor/text surfaces.
    std::unordered_set<std::uint32_t> surfaces;
    for (const auto& b : blits)
    {
        if (b.itemId != 0 &&
            b.sourceSurface != 0 &&
            (b.sourceDid & 0xFF000000u) != 0x06000000u)
        {
            surfaces.insert(b.sourceSurface);
        }
    }

    std::vector<std::uint32_t> ordered(surfaces.begin(), surfaces.end());
    std::sort(ordered.begin(), ordered.end());
    assets.reserve(ordered.size());

    for (const std::uint32_t surface : ordered)
    {
        ACCustomsSnapshotGeneratedAsset asset = {};
        asset.surface = surface;
        asset.fileName = "g_" + Hex32(surface).substr(2) + ".rgb";
        const std::string path = generatedDirectory + "\\" + asset.fileName;
        ACCustomsSnapshotDumpGeneratedSurface(surface, path, asset);
        asset.fileName = "g_" + Hex32(surface).substr(2) + ".rgb";
        assets.push_back(std::move(asset));
    }
    return assets;
}

static std::vector<ACCustomsSnapshotTextureAsset>
ACCustomsSnapshotDumpTextures(
    const std::string& outputPath,
    const std::unordered_set<std::uint32_t>& dids)
{
    std::vector<ACCustomsSnapshotTextureAsset> assets;
    if (outputPath.empty() || dids.empty())
        return assets;

    const std::string assetRoot =
        ACCustomsSnapshotAssetRootFromOutputPath(outputPath);
    const std::string textureDirectory = assetRoot + "\\textures";
    CreateDirectoryA(assetRoot.c_str(), nullptr);
    CreateDirectoryA(textureDirectory.c_str(), nullptr);

    std::vector<std::uint32_t> orderedDids(dids.begin(), dids.end());
    std::sort(orderedDids.begin(), orderedDids.end());
    assets.reserve(orderedDids.size());

    for (const std::uint32_t did : orderedDids)
    {
        ACCustomsSnapshotTextureAsset asset = {};
        asset.did = did;
        asset.fileName = DIDFileName(did) + ".rgb";

        LiveTextureEntry entry = {};
        if (!GetLiveTexture(did, entry))
        {
            asset.status = "not_registered";
            assets.push_back(asset);
            continue;
        }

        const std::string path = textureDirectory + "\\" + asset.fileName;

        // Live Mirror repeatedly captures geometry while the Manager is connected.
        // Reuse a previously dumped Vanilla asset when its exact byte size still
        // matches, avoiding renderer locks and disk rewrites on every refresh.
        if (g_ACCustomsSnapshotLiveMirror.load(std::memory_order_acquire))
        {
            WIN32_FILE_ATTRIBUTE_DATA fileData = {};
            if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileData) &&
                (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                ULARGE_INTEGER existingSize = {};
                existingSize.HighPart = fileData.nFileSizeHigh;
                existingSize.LowPart = fileData.nFileSizeLow;
                if (existingSize.QuadPart == static_cast<ULONGLONG>(entry.imageSize))
                {
                    asset.width = entry.width;
                    asset.height = entry.height;
                    asset.imageSize = entry.imageSize;
                    asset.pixelFormat = entry.pixelFormat;
                    asset.formatInfo = entry.formatInfo;
                    asset.status = "cached";
                    assets.push_back(asset);
                    continue;
                }
            }
        }

        ACCustomsSnapshotDumpOneTexture(did, entry, path, asset);
        assets.push_back(asset);
    }

    return assets;
}

static ResidentCaptureStats CaptureResidentOriginalsForReplacementSet()
{
    ResidentCaptureStats st = {};
    CreateDirectoryA(ACCustomsGetCaptureDirectory().c_str(), nullptr);

    const std::string search =
        ACCustomsGetReplacementDirectory() + "\\*.rgb";
    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return st;

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::uint32_t did = 0;
        if (!ParseTextureDIDFromFileName(fd.cFileName, did))
            continue;
        ++st.filesFound;

        LiveTextureEntry entry = {};
        if (!GetLiveTexture(did, entry))
            continue;
        ++st.registered;

        const std::string capturedPath =
            ACCustomsGetCaptureDirectory() + "\\" + fd.cFileName;
        WIN32_FILE_ATTRIBUTE_DATA attrs = {};
        if (GetFileAttributesExA(
                capturedPath.c_str(),
                GetFileExInfoStandard,
                &attrs))
        {
            const std::uint64_t existingSize =
                (static_cast<std::uint64_t>(attrs.nFileSizeHigh) << 32) |
                attrs.nFileSizeLow;
            if (existingSize == entry.imageSize)
            {
                ++st.alreadyCaptured;
                continue;
            }
        }

        if (CaptureResidentOriginal(did, entry))
            ++st.captured;
        else
            ++st.failed;
    }
    while (FindNextFileA(h, &fd));

    FindClose(h);

    WriteLog(
        "ACCUSTOMS RESIDENT_CAPTURE_SUMMARY files=" +
        std::to_string(st.filesFound) +
        " registered=" + std::to_string(st.registered) +
        " alreadyCaptured=" + std::to_string(st.alreadyCaptured) +
        " captured=" + std::to_string(st.captured) +
        " failed=" + std::to_string(st.failed));

    return st;
}

static bool ApplyLiveSwapBytes(bool useReplacement)
{
    LiveTextureEntry entry = {};
    if (!GetLiveTexture(LIVE_SWAP_TEST_DID, entry))
    {
        WriteLog("PROBE #20: 06001119 is not registered yet.");
        return false;
    }

    const std::string path =
        (useReplacement ? ACCustomsGetReplacementDirectory() : ACCustomsGetCaptureDirectory()) +
        "\\" + DIDFileName(LIVE_SWAP_TEST_DID) + ".rgb";

    std::vector<unsigned char> bytes;
    if (!ReadBinaryFileExact(path, bytes, entry.imageSize))
    {
        WriteLog("HOT SWAP #11: could not read exact-size file=" + path);
        return false;
    }

    return ApplyTextureLive(LIVE_SWAP_TEST_DID, bytes);
}



// ------------------------------------------------------------
// Probe #67: observe the higher-level two-surface copy routine at
// 0x00443290.
//
// Static analysis shows this routine owns BOTH important paths:
//   - direct mapped-buffer memcpy fast path (0x443338..0x443350), and
//   - fallback conversion/blit through 0x004428B0.
//
// Probe #66 only observed the fallback and also capped generic logs at 750,
// so a small icon such as War Magic DID 0x06001365 could be missed.  Probe
// #67 logs only exact target activity, while keeping uncapped aggregate
// counters for all replacement-backed live RenderSurfaces.
// ------------------------------------------------------------
using Probe67SurfaceCopyFn = bool (__thiscall*)(
    void* destHelper,
    void* sourceHelper);

static Probe67SurfaceCopyFn g_OriginalProbe67SurfaceCopy = nullptr;

static std::mutex g_Probe67TrackedMutex;
static std::unordered_map<std::uint32_t, std::uint32_t> g_Probe67SurfaceToDid;
static std::atomic<std::uint32_t> g_Probe67Phase(0);
static std::atomic<DWORD> g_Probe67ArmedUntil(0);
static std::atomic<std::uint32_t> g_Probe67CallCount(0);
static std::atomic<std::uint32_t> g_Probe67TrackedCount(0);
static std::atomic<std::uint32_t> g_Probe67TrackedSourceCount(0);
static std::atomic<std::uint32_t> g_Probe67TrackedDestCount(0);
static std::atomic<std::uint32_t> g_Probe67TargetSourceCount(0);
static std::atomic<std::uint32_t> g_Probe67TargetDestCount(0);
static std::atomic<std::uint32_t> g_Probe67TargetAnyCount(0);
static std::atomic<std::uint32_t> g_Probe67TargetLoggedCount(0);

static const char* Probe67PhaseName(std::uint32_t phase)
{
    switch (phase)
    {
    case 1: return "NATURAL";
    case 2: return "F8_POST";
    case 3: return "F9_POST";
    default: return "IDLE";
    }
}

static bool Probe67Armed()
{
    const DWORD until = g_Probe67ArmedUntil.load();
    return g_Probe67Phase.load() != 0 &&
           until != 0 &&
           static_cast<LONG>(until - GetTickCount()) > 0;
}

static std::size_t Probe67RefreshTrackedSurfaces()
{
    std::unordered_map<std::uint32_t, std::uint32_t> next;

    const std::string search =
        ACCustomsGetReplacementDirectory() + "\\*.rgb";

    WIN32_FIND_DATAA fd = {};
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            std::uint32_t did = 0;
            if (!ParseTextureDIDFromFileName(fd.cFileName, did))
                continue;

            LiveTextureEntry entry = {};
            if (!GetLiveTexture(did, entry) || !entry.renderSurface)
                continue;

            const std::uint32_t surface =
                static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(entry.renderSurface));
            if (surface != 0)
                next[surface] = did;
        }
        while (FindNextFileA(h, &fd));

        FindClose(h);
    }

    const std::size_t count = next.size();
    {
        std::lock_guard<std::mutex> lock(g_Probe67TrackedMutex);
        g_Probe67SurfaceToDid.swap(next);
    }
    return count;
}

static bool Probe67LookupDidForSurface(
    std::uint32_t surface,
    std::uint32_t& did)
{
    std::lock_guard<std::mutex> lock(g_Probe67TrackedMutex);
    const auto it = g_Probe67SurfaceToDid.find(surface);
    if (it == g_Probe67SurfaceToDid.end())
        return false;
    did = it->second;
    return true;
}

static void Probe67ResetCounters()
{
    g_Probe67CallCount.store(0);
    g_Probe67TrackedCount.store(0);
    g_Probe67TrackedSourceCount.store(0);
    g_Probe67TrackedDestCount.store(0);
    g_Probe67TargetSourceCount.store(0);
    g_Probe67TargetDestCount.store(0);
    g_Probe67TargetAnyCount.store(0);
    g_Probe67TargetLoggedCount.store(0);
}

static void Probe67ArmPhase(std::uint32_t phase, DWORD milliseconds)
{
    Probe67ResetCounters();
    g_Probe67Phase.store(phase);
    g_Probe67ArmedUntil.store(GetTickCount() + milliseconds);
}

static void Probe67LogAndStopPhase(const char* reason)
{
    const std::uint32_t phase = g_Probe67Phase.load();
    if (phase == 0)
        return;

    WriteLog(
        "PROBE #67 SUMMARY phase=" + std::string(Probe67PhaseName(phase)) +
        " reason=" + reason +
        " totalCopies=" + std::to_string(g_Probe67CallCount.load()) +
        " trackedCopies=" + std::to_string(g_Probe67TrackedCount.load()) +
        " trackedSource=" + std::to_string(g_Probe67TrackedSourceCount.load()) +
        " trackedDest=" + std::to_string(g_Probe67TrackedDestCount.load()) +
        " targetSource=" + std::to_string(g_Probe67TargetSourceCount.load()) +
        " targetDest=" + std::to_string(g_Probe67TargetDestCount.load()) +
        " targetAny=" + std::to_string(g_Probe67TargetAnyCount.load()) +
        " targetLogged=" + std::to_string(g_Probe67TargetLoggedCount.load()));

    g_Probe67Phase.store(0);
    g_Probe67ArmedUntil.store(0);
}

static void Probe67FinishExpiredPhase()
{
    const std::uint32_t phase = g_Probe67Phase.load();
    if (phase == 0)
        return;

    const DWORD until = g_Probe67ArmedUntil.load();
    if (until != 0 &&
        static_cast<LONG>(until - GetTickCount()) <= 0)
    {
        Probe67LogAndStopPhase("expired");
    }
}

static std::uint32_t Probe67HelperSurface(void* helper)
{
    if (!helper || !ProbeReadableRange(helper, 0x08))
        return 0;
    return ReadUInt32(helper, 0x04);
}

static std::uint32_t Probe67SurfaceDid(std::uint32_t surfaceAddress)
{
    if (!surfaceAddress)
        return 0;

    void* surface = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(surface, 0x2C))
        return 0;

    return ReadUInt32(surface, 0x28);
}

static std::uint32_t Probe67SurfaceField(
    std::uint32_t surfaceAddress,
    std::uint32_t offset)
{
    if (!surfaceAddress)
        return 0;

    void* surface = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(surfaceAddress));
    if (!ProbeReadableRange(surface, static_cast<std::size_t>(offset) + 4))
        return 0;

    return ReadUInt32(surface, offset);
}

static std::uint32_t Probe67HelperField(void* helper, std::uint32_t offset)
{
    if (!helper || !ProbeReadableRange(helper, static_cast<std::size_t>(offset) + 4))
        return 0;
    return ReadUInt32(helper, offset);
}

static bool Probe67FastPathCandidate(void* destHelper, void* sourceHelper)
{
    if (!destHelper || !sourceHelper ||
        !ProbeReadableRange(destHelper, 0x2C) ||
        !ProbeReadableRange(sourceHelper, 0x2C))
        return false;

    const std::uint32_t destSurface = Probe67HelperSurface(destHelper);
    const std::uint32_t sourceSurface = Probe67HelperSurface(sourceHelper);
    if (!destSurface || !sourceSurface)
        return false;

    void* dest = reinterpret_cast<void*>(static_cast<std::uintptr_t>(destSurface));
    void* source = reinterpret_cast<void*>(static_cast<std::uintptr_t>(sourceSurface));
    if (!ProbeReadableRange(dest, 0xE4) || !ProbeReadableRange(source, 0xE4))
        return false;

    if (ReadUInt32(source, 0xDC) != ReadUInt32(dest, 0xDC))
        return false;

    bool compatible = false;
    const std::uint32_t sourceFlagsE0 = ReadUInt32(source, 0xE0);
    if ((sourceFlagsE0 & 0x4u) != 0)
        compatible = ReadUInt32(dest, 0xA8) == ReadUInt32(source, 0xA8);
    else
        compatible = Probe67HelperField(destHelper, 0x28) ==
                     Probe67HelperField(sourceHelper, 0x28);

    if (!compatible)
        return false;

    const std::uint32_t dx0 = Probe67HelperField(destHelper, 0x0C);
    const std::uint32_t dy0 = Probe67HelperField(destHelper, 0x10);
    const std::uint32_t dx1 = Probe67HelperField(destHelper, 0x14);
    const std::uint32_t dy1 = Probe67HelperField(destHelper, 0x18);
    const std::uint32_t sx1 = Probe67HelperField(sourceHelper, 0x14);
    const std::uint32_t sy1 = Probe67HelperField(sourceHelper, 0x18);

    return dx0 == 0 &&
           dy0 == 0 &&
           dx1 == sx1 &&
           dy1 == sy1 &&
           dx1 == ReadUInt32(dest, 0xA0) &&
           dy1 == ReadUInt32(dest, 0xA4);
}

static bool __fastcall HookedProbe67SurfaceCopy(
    void* destHelper,
    void* /*edx*/,
    void* sourceHelper)
{
    const bool armed = Probe67Armed();

    std::uint32_t destSurface = 0;
    std::uint32_t sourceSurface = 0;
    std::uint32_t destDid = 0;
    std::uint32_t sourceDid = 0;
    bool destTracked = false;
    bool sourceTracked = false;
    bool target = false;
    bool fastCandidate = false;
    std::uint32_t caller = 0;

    if (armed)
    {
        ++g_Probe67CallCount;

        destSurface = Probe67HelperSurface(destHelper);
        sourceSurface = Probe67HelperSurface(sourceHelper);

        destTracked =
            destSurface != 0 &&
            Probe67LookupDidForSurface(destSurface, destDid);
        sourceTracked =
            sourceSurface != 0 &&
            Probe67LookupDidForSurface(sourceSurface, sourceDid);

        if (!destDid)
            destDid = Probe67SurfaceDid(destSurface);
        if (!sourceDid)
            sourceDid = Probe67SurfaceDid(sourceSurface);

        if (destTracked || sourceTracked ||
            destDid == PROBE46_TARGET_DID ||
            sourceDid == PROBE46_TARGET_DID)
        {
            ++g_Probe67TrackedCount;
            if (sourceTracked || sourceDid == PROBE46_TARGET_DID)
                ++g_Probe67TrackedSourceCount;
            if (destTracked || destDid == PROBE46_TARGET_DID)
                ++g_Probe67TrackedDestCount;
        }

        if (sourceDid == PROBE46_TARGET_DID)
            ++g_Probe67TargetSourceCount;
        if (destDid == PROBE46_TARGET_DID)
            ++g_Probe67TargetDestCount;

        target =
            sourceDid == PROBE46_TARGET_DID ||
            destDid == PROBE46_TARGET_DID;

        if (target)
        {
            ++g_Probe67TargetAnyCount;
            fastCandidate = Probe67FastPathCandidate(destHelper, sourceHelper);
            caller = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        }
    }

    const bool result = g_OriginalProbe67SurfaceCopy(destHelper, sourceHelper);

    if (armed && target)
    {
        const std::uint32_t n = ++g_Probe67TargetLoggedCount;
        WriteLog(
            "PROBE #67 TARGET_COPY phase=" +
            std::string(Probe67PhaseName(g_Probe67Phase.load())) +
            " n=" + std::to_string(n) +
            " tid=" + Hex32(GetCurrentThreadId()) +
            " destHelper=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(destHelper))) +
            " destSurface=" + Hex32(destSurface) +
            " destDID=" + Hex32(destDid) +
            " dest120=" + Hex32(Probe67SurfaceField(destSurface, 0x120)) +
            " destFmt=" + Hex32(Probe67SurfaceField(destSurface, 0xDC)) +
            " destBytes=" + Hex32(Probe67SurfaceField(destSurface, 0xA8)) +
            " sourceHelper=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(sourceHelper))) +
            " sourceSurface=" + Hex32(sourceSurface) +
            " sourceDID=" + Hex32(sourceDid) +
            " source120=" + Hex32(Probe67SurfaceField(sourceSurface, 0x120)) +
            " sourceFmt=" + Hex32(Probe67SurfaceField(sourceSurface, 0xDC)) +
            " sourceBytes=" + Hex32(Probe67SurfaceField(sourceSurface, 0xA8)) +
            " fastCandidate=" + std::string(fastCandidate ? "1" : "0") +
            " result=" + std::string(result ? "1" : "0") +
            " caller=" + Hex32(caller));
    }

    return result;
}


static DWORD WINAPI ACCustomsMaintenanceThread(LPVOID)
{
    WriteLog("ACCUSTOMS CONTROL: no F-key polling is installed. Apply/Restore are driven by Decal exports.");

    while (!g_LiveSwapStop.load())
    {
        Probe87CheckTimeouts();
        Probe87FinishSurfaceTraceIfExpired();
        Probe87FinishSetterTraceIfExpired();
        Sleep(50);
    }
    return 0;
}


// ------------------------------------------------------------

// PixelSource hook

//

// At the actual image-payload read:

//   1. Save AC's original bytes once.

//   2. If a same-sized replacement exists, copy it over the

//      returned source buffer before AC consumes it.

// ------------------------------------------------------------



static void* __fastcall HookedPixelSource(

    void* thisPtr,

    void* /*edx*/,

    std::uint32_t byteCount)

{

    void* result = g_OriginalPixelSource(thisPtr, byteCount);



    if (result == nullptr)

        return result;







    const std::uint32_t did =

        g_CurrentRenderSurfaceDID;



    void* renderSurface =

        g_CurrentRenderSurface;



    if (did == 0 ||

        renderSurface == nullptr ||

        (did & 0xFF000000u) != 0x06000000u)

    {

        return result;

    }



    const std::uint32_t expectedImageSize =

        ReadUInt32(

            renderSurface,

            0x60

        );



    const std::uint32_t pixelFormat =

        ReadUInt32(renderSurface, 0x68);



    if (pixelFormat == 0x00000065u &&

        byteCount == 4)

        {

            std::uint32_t value = 0;



            std::memcpy(

                &value,

                result,

                sizeof(value)

            );



            // INDEX16 RenderSurfaces serialize their palette DID

            // as this 4-byte metadata value.

            if ((value & 0xFF000000u) == 0x04000000u)

            {

                g_CurrentPaletteDID = value;

            }

        }



    // PixelSource is also used for metadata reads. Only operate

    // on the call whose byte count equals RenderSurface imageSize.

    if (expectedImageSize == 0 ||

        byteCount != expectedImageSize)

    {

        return result;

    }

    if (did == LIVE_SWAP_TEST_DID)
    {
        WriteLog(
            "PROBE #20 PIXELSOURCE payload DID=" + Hex32(did) +
            " bytes=" + std::to_string(byteCount) +
            " this=" + Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(renderSurface))));
    }



    const std::string didName =

        DIDFileName(did);





    // --------------------------------------------------------

    // Capture original texture

    // --------------------------------------------------------



    CreateDirectoryA(

        ACCustomsGetCaptureDirectory().c_str(),

        nullptr

    );



    const std::string capturedPath =

        ACCustomsGetCaptureDirectory() +

        "\\\\" +

        didName +

        ".rgb";



    const DWORD capturedAttributes =

        GetFileAttributesA(

            capturedPath.c_str()

        );



    if (capturedAttributes ==

        INVALID_FILE_ATTRIBUTES)

    {

        std::ofstream captured(

            capturedPath,

            std::ios::binary

        );



        if (captured.is_open())

        {

            captured.write(

                reinterpret_cast<const char*>(result),

                static_cast<std::streamsize>(

                    byteCount

                )

            );



            if (captured.good())

            {

                WriteLog(

                    "TEXTURE CAPTURED"

                    " DID=" + Hex32(did) +

                    " bytes=" +

                    std::to_string(byteCount) +

                    " file=" +

                    capturedPath

                );

            }

        }

    }





    // --------------------------------------------------------

    // Apply replacement texture

    // --------------------------------------------------------



    const std::string replacementPath =

        ACCustomsGetReplacementDirectory() +

        "\\\\" +

        didName +

        ".rgb";



    const ActiveThemeMode activeTheme = GetActiveThemeMode();

    if (activeTheme == ActiveThemeMode::Vanilla)
    {
        // Originals were captured above. Under vanilla, do not inject an
        // override into this newly deserialized texture. This fixes unseen
        // hover/pressed/active states appearing in the old custom theme after F8.
        if (GetFileAttributesA(replacementPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            WriteLog(
                "PROBE #88 LAZY_LOAD DID=" + Hex32(did) +
                " desired=VANILLA action=KEEP_ORIGINAL file=" +
                replacementPath);
        }

        return result;
    }



    std::ifstream replacement(

        replacementPath,

        std::ios::binary |

        std::ios::ate

    );



    if (!replacement.is_open())

        return result;



    const std::streamoff fileSize =

        replacement.tellg();



    if (fileSize !=

        static_cast<std::streamoff>(

            byteCount

        ))

    {

        WriteLog(

            "TEXTURE REPLACEMENT SIZE MISMATCH"

            " DID=" + Hex32(did) +

            " expected=" +

            std::to_string(byteCount) +

            " actual=" +

            std::to_string(

                static_cast<long long>(

                    fileSize

                )

            )

        );



        return result;

    }



    replacement.seekg(

        0,

        std::ios::beg

    );



    replacement.read(

        reinterpret_cast<char*>(result),

        static_cast<std::streamsize>(

            byteCount

        )

    );



    if (replacement.gcount() ==

        static_cast<std::streamsize>(

            byteCount

        ))

    {

        WriteLog(

            "TEXTURE REPLACED"

            " DID=" + Hex32(did) +

            " bytes=" +

            std::to_string(byteCount) +

            " file=" +

            replacementPath

        );

        WriteLog(
            "PROBE #88 LAZY_LOAD DID=" + Hex32(did) +
            " desired=TEST_REPLACEMENT action=APPLY_REPLACEMENT file=" +
            replacementPath);

    }

    else

    {

        WriteLog(

            "TEXTURE REPLACEMENT READ ERROR"

            " DID=" + Hex32(did) +

            " file=" +

            replacementPath

        );

    }



    return result;

}





static bool ProbeReadableRange(const void* address, std::size_t byteCount)
{
    if (address == nullptr || byteCount == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
        return false;

    if (mbi.State != MEM_COMMIT ||
        (mbi.Protect & PAGE_GUARD) != 0 ||
        (mbi.Protect & PAGE_NOACCESS) != 0)
        return false;

    const std::uintptr_t p =
        reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t regionEnd =
        reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;

    return p + byteCount <= regionEnd;
}

static void ProbeCSurfaceAfterInit(
    void* thisPtr,
    std::uint32_t mode)
{
    // Probe #4:
    // CSurface +0x6C -> ImgTex candidate.
    // ImgTex +0x58 -> array of 32-bit resource IDs.
    // ImgTex +0x60 -> entry count.
    //
    // Search only for our test RenderSurface DID 06004CC1.
    // Observational only: no game memory is modified.

    if (!ProbeReadableRange(thisPtr, 0x70))
        return;

    const std::uint32_t imgTexAddress =
        ReadUInt32(thisPtr, 0x6C);

    void* imgTex =
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(imgTexAddress));

    if (!ProbeReadableRange(imgTex, 0x64))
        return;

    const std::uint32_t imgTexDid =
        ReadUInt32(imgTex, 0x28);

    const std::uint32_t arrayAddress =
        ReadUInt32(imgTex, 0x58);

    const std::uint32_t count =
        ReadUInt32(imgTex, 0x60);

    // Defensive cap. A real texture-resource list should be small; if this
    // isn't the structure we think it is, do not walk an arbitrary range.
    if (count == 0 || count > 256 || arrayAddress == 0)
        return;

    const std::size_t arrayBytes =
        static_cast<std::size_t>(count) * sizeof(std::uint32_t);

    const void* arrayPtr =
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(arrayAddress));

    if (!ProbeReadableRange(arrayPtr, arrayBytes))
        return;

    bool foundTarget = false;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        const std::uint32_t value =
            ReadUInt32(arrayPtr, static_cast<std::size_t>(i) * 4);

        if (value == CSURFACE_PROBE_DID)
        {
            foundTarget = true;
            break;
        }
    }

    if (!foundTarget)
        return;

    // Log each matching ImgTex once.
    const std::uintptr_t key =
        reinterpret_cast<std::uintptr_t>(imgTex);

    {
        std::lock_guard<std::mutex> lock(g_CSurfaceProbeMutex);
        if (!g_LoggedCSurfaceProbeObjects.insert(key).second)
            return;
    }

    std::ostringstream line;
    line
        << "TARGET IMGTEX"
        << " csurface="
        << Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(thisPtr)))
        << " mode=" << std::dec << mode
        << " imgtex=" << Hex32(imgTexAddress)
        << " imgtexDID=" << Hex32(imgTexDid)
        << " array=" << Hex32(arrayAddress)
        << " count=" << std::dec << count
        << " surfaces=[";

    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (i != 0)
            line << ",";

        line << Hex32(
            ReadUInt32(
                arrayPtr,
                static_cast<std::size_t>(i) * 4));
    }

    line << "]";

    WriteLog(line.str());
}

static void __fastcall HookedCSurfaceInitEnd(
    void* thisPtr,
    void* /*edx*/,
    std::uint32_t mode)
{
    g_OriginalCSurfaceInitEnd(thisPtr, mode);
    ProbeCSurfaceAfterInit(thisPtr, mode);
}


static void LogTargetRenderSurfaceSnapshot(
    const char* phase,
    void* object)
{
    if (object == nullptr || phase == nullptr)
        return;

    // Snapshot only the first 0x100 bytes, and only if that exact range is
    // currently readable. No nested pointer dereferences and no writes.
    if (!ProbeReadableRange(object, 0x100))
    {
        WriteLog(
            std::string("RS LIFECYCLE ") + phase +
            " target object is no longer readable.");
        return;
    }

    std::ostringstream line;
    line
        << "RS LIFECYCLE " << phase
        << " this="
        << Hex32(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(object)));

    for (std::size_t offset = 0; offset < 0x100; offset += 4)
    {
        line
            << " +"
            << std::uppercase << std::hex
            << std::setw(2) << std::setfill('0') << offset
            << "="
            << Hex32(ReadUInt32(object, offset));
    }

    WriteLog(line.str());
}

static void LogRenderSurface20Object(
    void* renderSurface)
{
    if (!ProbeReadableRange(renderSurface, 0x24))
        return;

    const std::uint32_t childAddress =
        ReadUInt32(renderSurface, 0x20);

    if (childAddress == 0)
    {
        WriteLog("RS20 PROBE +20 is null.");
        return;
    }

    void* child =
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(childAddress));

    MEMORY_BASIC_INFORMATION mbi = {};
    const SIZE_T queried =
        VirtualQuery(child, &mbi, sizeof(mbi));

    {
        std::ostringstream info;
        info
            << "RS20 PROBE ptr=" << Hex32(childAddress);

        if (queried == sizeof(mbi))
        {
            info
                << " allocationBase="
                << Hex32(static_cast<std::uint32_t>(
                    reinterpret_cast<std::uintptr_t>(
                        mbi.AllocationBase)))
                << " regionSize=0x"
                << std::uppercase << std::hex
                << static_cast<std::uint32_t>(mbi.RegionSize)
                << " state=0x"
                << static_cast<std::uint32_t>(mbi.State)
                << " protect=0x"
                << static_cast<std::uint32_t>(mbi.Protect)
                << " type=0x"
                << static_cast<std::uint32_t>(mbi.Type);
        }

        WriteLog(info.str());
    }

    // Read-only dump of the pointed-to object. Do not follow anything inside.
    if (!ProbeReadableRange(child, 0x80))
    {
        WriteLog("RS20 PROBE pointed-to range is not readable for 0x80 bytes.");
        return;
    }

    std::ostringstream dump;
    dump << "RS20 OBJECT";

    for (std::size_t offset = 0; offset < 0x80; offset += 4)
    {
        dump
            << " +"
            << std::uppercase << std::hex
            << std::setw(2) << std::setfill('0') << offset
            << "="
            << Hex32(ReadUInt32(child, offset));
    }

    WriteLog(dump.str());
}


static DWORD WINAPI RenderSurfaceLifecycleThread(LPVOID)
{
    void* lastObject = nullptr;

    while (!g_RenderSurfaceLifecycleStop.load())
    {
        void* object = nullptr;

        {
            std::lock_guard<std::mutex> lock(
                g_RenderSurfaceLifecycleMutex);
            object = g_TargetRenderSurface;
        }

        if (object != nullptr && object != lastObject)
        {
            // Give AC time to finish constructing/uploading whatever follows
            // RenderSurface deserialization, then inspect the same address.
            Sleep(2000);
            LogTargetRenderSurfaceSnapshot("LATER_2S", object);
            LogRenderSurface20Object(object);

            Sleep(8000);
            LogTargetRenderSurfaceSnapshot("LATER_10S", object);

            lastObject = object;
        }
        else
        {
            Sleep(100);
        }
    }

    return 0;
}


static DWORD WINAPI RenderSurfaceCorrelationThread(LPVOID)
{
    const DWORD settleMs = 2000;
    const std::size_t maxLogs = 30;
    std::size_t logged = 0;

    while (!g_RenderSurfaceCorrelationStop.load() && logged < maxLogs)
    {
        RenderSurfaceCorrelationItem item = {};
        bool haveItem = false;

        {
            std::lock_guard<std::mutex> lock(
                g_RenderSurfaceCorrelationMutex);

            if (!g_RenderSurfaceCorrelationQueue.empty())
            {
                const DWORD now = GetTickCount();
                const RenderSurfaceCorrelationItem& front =
                    g_RenderSurfaceCorrelationQueue.front();

                if ((now - front.queuedAt) >= settleMs)
                {
                    item = front;
                    g_RenderSurfaceCorrelationQueue.erase(
                        g_RenderSurfaceCorrelationQueue.begin());
                    haveItem = true;
                }
            }
        }

        if (!haveItem)
        {
            Sleep(50);
            continue;
        }

        // Read only the fields needed for correlation after AC has had time
        // to transition the RenderSurface into its persistent state.
        if (!ProbeReadableRange(item.object, 0xB4))
            continue;

        const std::uint32_t currentDid =
            ReadUInt32(item.object, 0x28);

        // The object may have been freed/reused during the delay.
        if (currentDid != item.did)
            continue;

        const std::uint32_t field20 =
            ReadUInt32(item.object, 0x20);
        const std::uint32_t field24 =
            ReadUInt32(item.object, 0x24);
        const std::uint32_t width =
            ReadUInt32(item.object, 0x58);
        const std::uint32_t height =
            ReadUInt32(item.object, 0x5C);
        const std::uint32_t imageSize =
            ReadUInt32(item.object, 0x60);
        const std::uint32_t sourceBits =
            ReadUInt32(item.object, 0x64);
        const std::uint32_t fieldB0 =
            ReadUInt32(item.object, 0xB0);

        std::ostringstream line;
        line
            << "RS CORRELATION"
            << " DID=" << Hex32(item.did)
            << " this="
            << Hex32(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(item.object)))
            << " +20=" << Hex32(field20)
            << " +24=" << Hex32(field24)
            << " width=" << std::dec << width
            << " height=" << height
            << " imageSize=" << imageSize
            << " +64=" << Hex32(sourceBits)
            << " +B0=" << Hex32(fieldB0);

        WriteLog(line.str());
        ++logged;
    }

    WriteLog("RS CORRELATION probe #7 collection complete.");
    return 0;
}


// ------------------------------------------------------------

// SerializeFromCachePack hook

//

// arg1 is the Type-12 RenderSurface object for the resources

// we're interested in.

//

// Observed RenderSurface fields:

//   +0x28 DID

//   +0x58 width

//   +0x5C height

//   +0x60 imageSize

//   +0x64 sourceBits

// ------------------------------------------------------------



static bool __fastcall HookedSerializeFromCachePack(

    void* thisPtr,

    void* /*edx*/,

    void* arg1,

    void* arg2)

{

    std::uint32_t did = 0;



    if (arg1 != nullptr)

        did = ReadUInt32(arg1, 0x28);



    const bool isType12RenderSurface =

        (did & 0xFF000000u) == 0x06000000u;




    if (isType12RenderSurface)
        RecordEncounteredTexture(did);

        // Probe #7: retain only addresses/DIDs here. The worker waits two
        // seconds before reading persistent-state fields, keeping this hook
        // lightweight and avoiding any nested dereferences.
        {
            const std::uintptr_t key =
                reinterpret_cast<std::uintptr_t>(arg1);

            std::lock_guard<std::mutex> lock(
                g_RenderSurfaceCorrelationMutex);

            if (g_RenderSurfaceCorrelationSeen.size() < 30 &&
                g_RenderSurfaceCorrelationSeen.insert(key).second)
            {
                RenderSurfaceCorrelationItem item = {};
                item.object = arg1;
                item.did = did;
                item.queuedAt = GetTickCount();
                g_RenderSurfaceCorrelationQueue.push_back(item);
            }
        }

const std::uint32_t previousDID =

        g_CurrentRenderSurfaceDID;



    void* previousRenderSurface =

        g_CurrentRenderSurface;



    const std::uint32_t previousPaletteDID =

        g_CurrentPaletteDID;



    if (isType12RenderSurface)

    {

        g_CurrentRenderSurfaceDID = did;

        g_CurrentRenderSurface = arg1;

        g_CurrentPaletteDID = 0;

    }



    const bool result =

        g_OriginalSerializeFromCachePack(

            thisPtr,

            arg1,

            arg2

        );



    const std::uint32_t paletteDID =

        g_CurrentPaletteDID;



    g_CurrentRenderSurfaceDID =

        previousDID;



    g_CurrentRenderSurface =

        previousRenderSurface;



    g_CurrentPaletteDID =

        previousPaletteDID;



    if (isType12RenderSurface &&

        arg1 != nullptr)

    {

        const std::uint32_t width =

            ReadUInt32(arg1, 0x58);



        const std::uint32_t height =

            ReadUInt32(arg1, 0x5C);



        const std::uint32_t imageSize =

            ReadUInt32(arg1, 0x60);



        const std::uint32_t pixelFormat =

            ReadUInt32(arg1, 0x68);



        const std::uint32_t formatInfo =

            ReadUInt32(arg1, 0x6C);



        if (did == LIVE_SWAP_TEST_DID)
        {
            std::lock_guard<std::mutex> lock(g_LiveSwapMutex);
            g_LiveSwapRenderSurface = arg1;
            g_LiveSwapImageSize = imageSize;

            {
                std::lock_guard<std::mutex> lifecycleLock(
                    g_RenderSurfaceLifecycleMutex);
                g_TargetRenderSurface = arg1;
                g_TargetRenderSurfaceImageSize = imageSize;
            }

            LogTargetRenderSurfaceSnapshot(
                "IMMEDIATE_AFTER_DESERIALIZE",
                arg1);

            const std::uint32_t sourceBitsValue =
                ReadUInt32(arg1, 0x64);

            WriteLog(
                "LIVE SWAP TEST: tracked target"
                " DID=" + Hex32(did) +
                " object=" + Hex32(
                    static_cast<std::uint32_t>(
                        reinterpret_cast<std::uintptr_t>(arg1))) +
                " sourceBits=" + Hex32(sourceBitsValue) +
                " imageSize=" + std::to_string(imageSize));
        }



        WriteTextureMetadata(

            did,

            width,

            height,

            imageSize,

            pixelFormat,

            formatInfo,

            paletteDID

        );



        WriteLog(

            "TEXTURE DESERIALIZED"

            " DID=" + Hex32(did) +

            " width=" + std::to_string(width) +

            " height=" + std::to_string(height) +

            " imageSize=" + std::to_string(imageSize) +

            " pixelFormat=" + Hex32(pixelFormat) +

            " formatInfo=" + Hex32(formatInfo) +

            " paletteDID=" + Hex32(paletteDID)

        );

    }



    return result;

}








// ------------------------------------------------------------
// AC Customs production-light hooks.
//
// These are the only high-frequency interception paths used by the Decal
// plugin when DEVELOPER TESTS is OFF. Historical reverse-engineering probes
// remain in this source for reference but are not installed by the production
// initializer below.
// ------------------------------------------------------------

static bool DeveloperTestsEnabled()
{
    return g_DeveloperTestsEnabled.load(std::memory_order_acquire);
}

static bool __fastcall HookedCoreSerializeFromCachePack(
    void* thisPtr,
    void* /*edx*/,
    void* arg1,
    void* arg2)
{
    std::uint32_t did = 0;
    if (arg1 != nullptr && ProbeReadableRange(arg1, 0x70))
        did = ReadUInt32(arg1, 0x28);

    const bool isType12RenderSurface =
        (did & 0xFF000000u) == 0x06000000u;

    if (DeveloperTestsEnabled() && isType12RenderSurface)
        RecordEncounteredTexture(did);

    const std::uint32_t previousDID = g_CurrentRenderSurfaceDID;
    void* const previousRenderSurface = g_CurrentRenderSurface;
    const std::uint32_t previousPaletteDID = g_CurrentPaletteDID;

    if (isType12RenderSurface)
    {
        g_CurrentRenderSurfaceDID = did;
        g_CurrentRenderSurface = arg1;
        g_CurrentPaletteDID = 0;
    }

    const bool result = g_OriginalSerializeFromCachePack(thisPtr, arg1, arg2);

    g_CurrentRenderSurfaceDID = previousDID;
    g_CurrentRenderSurface = previousRenderSurface;
    g_CurrentPaletteDID = previousPaletteDID;
    return result;
}

static bool CaptureLazyOriginalBytes(
    std::uint32_t did,
    const void* bytes,
    std::uint32_t byteCount)
{
    if (!bytes || byteCount == 0)
        return false;

    CreateDirectoryA(ACCustomsGetCaptureDirectory().c_str(), nullptr);
    const std::string path =
        ACCustomsGetCaptureDirectory() + "\\" + DIDFileName(did) + ".rgb";

    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attrs))
    {
        const std::uint64_t size =
            (static_cast<std::uint64_t>(attrs.nFileSizeHigh) << 32) |
            attrs.nFileSizeLow;
        if (size == byteCount)
            return true;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        return false;

    output.write(
        reinterpret_cast<const char*>(bytes),
        static_cast<std::streamsize>(byteCount));
    return output.good();
}

static void* __fastcall HookedCorePixelSource(
    void* thisPtr,
    void* /*edx*/,
    std::uint32_t byteCount)
{
    void* result = g_OriginalPixelSource(thisPtr, byteCount);
    if (!result)
        return result;

    const std::uint32_t did = g_CurrentRenderSurfaceDID;
    void* const renderSurface = g_CurrentRenderSurface;
    if (did == 0 || !renderSurface ||
        (did & 0xFF000000u) != 0x06000000u ||
        !ProbeReadableRange(renderSurface, 0x70))
    {
        return result;
    }

    const std::uint32_t expectedImageSize = ReadUInt32(renderSurface, 0x60);
    const std::uint32_t pixelFormat = ReadUInt32(renderSurface, 0x68);

    // Preserve palette tracking used by the original serializer context.
    if (pixelFormat == 0x00000065u && byteCount == 4)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, result, sizeof(value));
        if ((value & 0xFF000000u) == 0x04000000u)
            g_CurrentPaletteDID = value;
    }

    if (expectedImageSize == 0 || byteCount != expectedImageSize)
        return result;

    // The normal runtime should not touch the filesystem for unrelated AC
    // textures. Only DIDs that exist in the selected/test replacement set are
    // relevant to swapping.
    if (!Probe87IsReplacementDid(did))
        return result;

    const ActiveThemeMode activeTheme = GetActiveThemeMode();
    if (activeTheme == ActiveThemeMode::Vanilla)
        return result;

    // This is a lazy load that happened while the custom theme is active.
    // Do not override it unless the DAT-provided vanilla payload was safely
    // captured first; every modified texture must be provably restorable.
    if (!CaptureLazyOriginalBytes(did, result, byteCount))
    {
        WriteLog(
            "CORE LAZY_OVERRIDE SKIP DID=" + Hex32(did) +
            " reason=original_capture_failed");
        return result;
    }

    const std::string replacementPath =
        ACCustomsGetReplacementDirectory() + "\\" + DIDFileName(did) + ".rgb";

    std::ifstream replacement(
        replacementPath,
        std::ios::binary | std::ios::ate);
    if (!replacement.is_open())
        return result;

    const std::streamoff fileSize = replacement.tellg();
    if (fileSize != static_cast<std::streamoff>(byteCount))
        return result;

    replacement.seekg(0, std::ios::beg);
    replacement.read(
        reinterpret_cast<char*>(result),
        static_cast<std::streamsize>(byteCount));

    if (replacement.gcount() != static_cast<std::streamsize>(byteCount))
        return result;

    MarkActiveAppliedDid(did);
    WriteLog(
        "CORE LAZY_OVERRIDE DID=" + Hex32(did) +
        " bytes=" + std::to_string(byteCount));
    return result;
}

static bool __fastcall HookedCoreRenderSurfaceUpload(
    void* thisPtr,
    void* /*edx*/)
{
    if (thisPtr && ProbeReadableRange(thisPtr, 0x70))
    {
        const std::uint32_t did = ReadUInt32(thisPtr, 0x28);
        if ((did & 0xFF000000u) == 0x06000000u)
        {
            // Keep a lightweight in-memory registry of live AC texture surfaces
            // even while the process is in Vanilla mode. Runtime-selected .acui
            // packs are not known at startup, so filtering this registry by the
            // current replacement set would leave already-resident textures
            // undiscoverable when the user later presses Apply. This does NOT
            // perform filesystem I/O or Encountered logging in normal mode.
            RegisterLiveTexture(did, thisPtr);

            if (DeveloperTestsEnabled())
                RecordEncounteredTexture(did);
        }
    }

    return g_OriginalRenderSurfaceUpload(thisPtr);
}

static DWORD WINAPI DisableCoreDrawHookWorker(LPVOID)
{
    // Never rewrite the currently executing detour prologue from inside that
    // detour. Give the UI traversal time to return, then remove the one-shot
    // discovery detour entirely.
    Sleep(50);
    if (g_CoreDrawHookTarget)
        MH_DisableHook(g_CoreDrawHookTarget);
    return 0;
}

static void __fastcall HookedCoreProbe87Draw(
    void* thisPtr,
    void* /*edx*/,
    void* arg1,
    void* arg2,
    void* arg3,
    void* arg4)
{
    const bool snapshotActive =
        g_ACCustomsSnapshotActive.load(std::memory_order_acquire);

    // Normal startup discovery is one-shot and then this detour is disabled.
    // Snapshot capture temporarily re-enables it, so never schedule the normal
    // one-shot disable worker while a developer snapshot is active.
    if (!snapshotActive &&
        g_Probe87CaptureArmed.load(std::memory_order_acquire))
    {
        Probe87DiscoverRootFromDraw(thisPtr);

        if (!g_Probe87CaptureArmed.load(std::memory_order_acquire) &&
            g_CoreDrawHookTarget &&
            !g_CoreDrawDisableScheduled.exchange(true, std::memory_order_acq_rel))
        {
            HANDLE worker = CreateThread(
                nullptr, 0, DisableCoreDrawHookWorker, nullptr, 0, nullptr);
            if (worker)
                CloseHandle(worker);
        }
    }

    bool snapshotPushed = false;
    if (snapshotActive)
    {
        // The desktop/root objects are destroyed when the player logs out, even
        // though acclient.exe and the native plugin remain loaded. Validate the
        // cached desktop against the root that owns this *current* draw. If AC
        // rebuilt the UI, discard only address-based session state and relearn
        // the new desktop before taking the snapshot.
        Probe87RootEntry currentRoot = {};
        const bool haveCurrentRoot =
            Probe87FindBackedRoot(thisPtr, currentRoot) &&
            currentRoot.parent != 0;

        const std::uint32_t cachedDesktop =
            g_Probe87DesktopRoot.load(std::memory_order_acquire);

        // A backed root can be nested several parents below the desktop. The
        // previous logout fix compared currentRoot.parent directly to the
        // desktop and therefore treated ordinary nested draws as a new UI
        // session, repeatedly wiping correlation state during a snapshot.
        const bool belongsToCachedDesktop =
            cachedDesktop != 0 &&
            haveCurrentRoot &&
            Probe87NodeBelongsToDesktop(
                currentRoot.node,
                cachedDesktop);

        if (cachedDesktop != 0 &&
            haveCurrentRoot &&
            !belongsToCachedDesktop)
        {
            Probe87ResetUiSessionDiscovery(
                "SNAPSHOT_DESKTOP_CHANGED",
                cachedDesktop,
                currentRoot.parent);
        }

        if (g_Probe87DesktopRoot.load(std::memory_order_acquire) == 0)
        {
            g_Probe87CaptureArmed.store(true, std::memory_order_release);
            Probe87DiscoverRootFromDraw(thisPtr);
        }

        // Do not capture from an old-but-still-readable heap object. A current
        // draw must positively belong to the desktop we will traverse.
        const std::uint32_t activeDesktop =
            g_Probe87DesktopRoot.load(std::memory_order_acquire);
        if (activeDesktop != 0 &&
            haveCurrentRoot &&
            Probe87NodeBelongsToDesktop(
                currentRoot.node,
                activeDesktop))
        {
            ACCustomsSnapshotCaptureTreeOnUiThread();
        }

        if (g_ACCustomsSnapshotDrawDepth < 64)
        {
            Probe87DrawFrame& frame =
                g_ACCustomsSnapshotDrawStack[g_ACCustomsSnapshotDrawDepth++];
            frame.thisPtr = thisPtr;
            frame.arg1 = arg1;
            frame.arg2 = arg2;
            frame.arg3 = arg3;
            frame.arg4 = arg4;
            frame.caller = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            snapshotPushed = true;
        }
    }

    g_OriginalProbe87Draw(thisPtr, arg1, arg2, arg3, arg4);

    if (snapshotPushed && g_ACCustomsSnapshotDrawDepth > 0)
        --g_ACCustomsSnapshotDrawDepth;
}

static bool InstallCoreHook(
    void* target,
    void* detour,
    void** original)
{
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
        return false;

    status = MH_EnableHook(target);
    return status == MH_OK || status == MH_ERROR_ENABLED;
}

static DWORD WINAPI InitializeProductionThread(LPVOID)
{
    HMODULE exeModule = GetModuleHandleA(nullptr);
    if (!exeModule)
        return 1;

    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(exeModule);

    void* const serializeFromCachePack =
        reinterpret_cast<void*>(base + 0x17AC0u);
    void* const pixelSource =
        reinterpret_cast<void*>(base + 0x0ACF0u);
    void* const renderSurfaceUpload =
        reinterpret_cast<void*>(base + 0x44200u);
    void* const probe87Draw =
        reinterpret_cast<void*>(base + 0x2A08B0u);
    void* const snapshotBlit =
        reinterpret_cast<void*>(base + 0x42C70u);
    void* const probe87ItemRefresh =
        reinterpret_cast<void*>(base + 0xE2D10u);
    void* const stateLinkTransition =
        reinterpret_cast<void*>(base + 0x72160u);
    void* const stateLinkImageSet =
        reinterpret_cast<void*>(base + 0x2A0610u);

    g_CoreDrawHookTarget = probe87Draw;
    g_ACCustomsSnapshotBlitHookTarget = snapshotBlit;
    g_ACCustomsStateLinkTransitionHookTarget = stateLinkTransition;
    g_ACCustomsStateLinkImageSetHookTarget = stateLinkImageSet;
    g_CoreDrawDisableScheduled.store(false, std::memory_order_release);

    // Apply/Restore call these AC functions directly. No observation detours
    // are needed around the renderer lock/unlock path in production mode.
    g_OriginalRendererLock =
        reinterpret_cast<RendererLockFn>(base + 0x296F10u);
    g_RendererUnlock =
        reinterpret_cast<RendererUnlockFn>(base + 0x296FB0u);

    g_Probe87InvalidateSelf =
        reinterpret_cast<Probe87InvalidateSelfFn>(base + 0x2A0430u);
    g_Probe87LookupItem =
        reinterpret_cast<Probe87LookupItemFn>(base + 0x1583F0u);
    g_Probe87CacheGet =
        reinterpret_cast<Probe87CacheGetFn>(base + 0x18EC60u);
    g_Probe87CacheRebuild =
        reinterpret_cast<Probe87CacheRebuildFn>(base + 0x18DFB0u);

    // Build the replacement-DID set before any texture hook can run.
    Probe87ResetState();
    g_Probe53Captured.store(false);
    Probe87LoadReplacementDidSet();

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        return 2;

    // Install the snapshot blit detour but leave it DISABLED during normal
    // gameplay. ACCustoms_CaptureUiSnapshot enables it only for the short
    // developer capture window.
    status = MH_CreateHook(
        snapshotBlit,
        reinterpret_cast<void*>(&HookedACCustomsSnapshotBlit),
        reinterpret_cast<void**>(&g_OriginalACCustomsSnapshotBlit));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        // Snapshot capture is a developer aid. Never make the production
        // Apply/Restore engine fail to initialize just because this optional
        // observation hook is unavailable.
        g_ACCustomsSnapshotBlitHookTarget = nullptr;
        g_OriginalACCustomsSnapshotBlit = nullptr;
    }

    // Optional control-state linkage hooks. Like the snapshot blit hook,
    // these are installed DISABLED and are active only during an explicit
    // Developer Tests capture window.
    status = MH_CreateHook(
        stateLinkTransition,
        reinterpret_cast<void*>(&HookedACCustomsStateLinkTransition),
        reinterpret_cast<void**>(&g_OriginalACCustomsStateLinkTransition));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        g_ACCustomsStateLinkTransitionHookTarget = nullptr;
        g_OriginalACCustomsStateLinkTransition = nullptr;
    }
    if (g_ACCustomsStateLinkTransitionHookTarget &&
        g_OriginalACCustomsStateLinkTransition)
    {
        const MH_STATUS stateStatus =
            MH_EnableHook(g_ACCustomsStateLinkTransitionHookTarget);
        if (stateStatus != MH_OK && stateStatus != MH_ERROR_ENABLED)
        {
            g_ACCustomsStateLinkTransitionHookTarget = nullptr;
            g_OriginalACCustomsStateLinkTransition = nullptr;
        }
        else
        {
            WriteLog("ACCUSTOMS LIVE_MIRROR transition-driven state learning enabled.");
        }
    }

    status = MH_CreateHook(
        stateLinkImageSet,
        reinterpret_cast<void*>(&HookedACCustomsStateLinkImageSet),
        reinterpret_cast<void**>(&g_OriginalACCustomsStateLinkImageSet));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
    {
        g_ACCustomsStateLinkImageSetHookTarget = nullptr;
        g_OriginalACCustomsStateLinkImageSet = nullptr;
    }

    if (!InstallCoreHook(
            serializeFromCachePack,
            reinterpret_cast<void*>(&HookedCoreSerializeFromCachePack),
            reinterpret_cast<void**>(&g_OriginalSerializeFromCachePack)))
        return 3;

    if (!InstallCoreHook(
            pixelSource,
            reinterpret_cast<void*>(&HookedCorePixelSource),
            reinterpret_cast<void**>(&g_OriginalPixelSource)))
        return 4;

    if (!InstallCoreHook(
            renderSurfaceUpload,
            reinterpret_cast<void*>(&HookedCoreRenderSurfaceUpload),
            reinterpret_cast<void**>(&g_OriginalRenderSurfaceUpload)))
        return 5;

    if (!InstallCoreHook(
            probe87Draw,
            reinterpret_cast<void*>(&HookedCoreProbe87Draw),
            reinterpret_cast<void**>(&g_OriginalProbe87Draw)))
        return 6;

    if (!InstallCoreHook(
            probe87ItemRefresh,
            reinterpret_cast<void*>(&HookedProbe87ItemRefresh),
            reinterpret_cast<void**>(&g_OriginalProbe87ItemRefresh)))
        return 7;

    g_Probe87CaptureArmed.store(true, std::memory_order_release);

    return 0;
}

// ------------------------------------------------------------

// Initialization

// ------------------------------------------------------------



static DWORD WINAPI InitializeThread(

    LPVOID)

{

    WriteLog("");

    WriteLog(

        "========================================"

    );

    WriteLog(

        "ACModernUI texture interceptor starting"

    );

    WriteLog(

        "========================================"

    );



    HMODULE exeModule =

        GetModuleHandleA(nullptr);



    if (exeModule == nullptr)

    {

        WriteLog(

            "ERROR: GetModuleHandle failed."

        );



        return 1;

    }



    const std::uintptr_t base =

        reinterpret_cast<std::uintptr_t>(

            exeModule

        );



    constexpr std::uintptr_t

        SerializeFromCachePackRVA =

            0x17AC0;



    constexpr std::uintptr_t

        PixelSourceRVA =

            0x0ACF0;


    constexpr std::uintptr_t

        RenderSurfaceUploadRVA =

            0x44200;


    constexpr std::uintptr_t

        RendererLockRVA =

            0x296F10;


    constexpr std::uintptr_t
        Probe47SurfaceAccessRVA =
            0x410C0;

    constexpr std::uintptr_t Probe67SurfaceCopyRVA = 0x43290;


    constexpr std::uintptr_t Probe48MakeBackingRVA = 0x61920;
    constexpr std::uintptr_t Probe48AttachBackingRVA = 0x61BA0;
    constexpr std::uintptr_t Probe49FcRVA = 0x61D60;
    constexpr std::uintptr_t Probe49PropertyRVA = 0x60CC0;
    constexpr std::uintptr_t Probe49Plus104RVA = 0x602C0;
    constexpr std::uintptr_t Probe50MutateRVA = 0x2A0D50;
    constexpr std::uintptr_t Probe50InvalidateRVA = 0x29FF00;
    constexpr std::uintptr_t Probe87BlitRVA = 0x42C70;
    constexpr std::uintptr_t Probe87CompositeRVA = 0x428B0;
    constexpr std::uintptr_t Probe87CopyRVA = 0x43290;
    constexpr std::uintptr_t Probe87ObjectBindRVA = 0x1A4760;
    constexpr std::uintptr_t Probe87RawBindRVA = 0x1A47D0;
    constexpr std::uintptr_t Probe87DrawRVA = 0x2A08B0;
    constexpr std::uintptr_t Probe87InvalidateSelfRVA = 0x2A0430;
    constexpr std::uintptr_t Probe87ImageSetRVA = 0x2A0610;
    constexpr std::uintptr_t Probe87ImageClearRVA = 0x2A0660;

    constexpr std::uintptr_t Probe87ItemRefreshRVA = 0xE2D10;
    constexpr std::uintptr_t Probe87LookupItemRVA = 0x1583F0;
    constexpr std::uintptr_t Probe87CacheGetRVA = 0x18EC60;
    constexpr std::uintptr_t Probe87CacheRebuildRVA = 0x18DFB0;


    constexpr std::uintptr_t

        RendererUnlockRVA =

            0x296FB0;



    constexpr std::uintptr_t

        CSurfaceInitEndRVA =

            0x137140;


    constexpr std::uintptr_t UiLockSetterRVA = 0x1D4340;
    constexpr std::uintptr_t UiPostLockRVA = 0x5B4C0;
    constexpr std::uintptr_t UiDirtyCandidateRVA = 0x19B8A0;
    constexpr std::uintptr_t UiDispatchRVA = 0x7A0B0;
    constexpr std::uintptr_t UiListenerCommonRVA = 0xD31D0;
    constexpr std::uintptr_t UiListenerSpecialRVA = 0xD5CB0;
    constexpr std::uintptr_t UiEventTypeRVA = 0x29A00;
    constexpr std::uintptr_t UiEventMapRVA = 0x29120;
    constexpr std::uintptr_t UiEventRouterRVA = 0x64C90;
    constexpr std::uintptr_t UiRefresh1490RVA = 0x71490;
    constexpr std::uintptr_t UiRefresh20B0RVA = 0x720B0;
    constexpr std::uintptr_t UiVirtual9CRVA = 0x72160;
    constexpr std::uintptr_t UiStateUpdateRVA = 0x60820;
    constexpr std::uintptr_t UiStateForwardRVA = 0x64FA0;
    constexpr std::uintptr_t Probe40State462390RVA = 0x62390;
    constexpr std::uintptr_t Probe40Prop464090RVA = 0x64090;
    constexpr std::uintptr_t Probe40Leaf463830RVA = 0x63830;
    constexpr std::uintptr_t Probe39Event472300RVA = 0x72300;
    constexpr std::uintptr_t Probe39Path4723B0RVA = 0x723B0;
    constexpr std::uintptr_t Probe39Path4724A0RVA = 0x724A0;
    constexpr std::uintptr_t UiOpen465F90RVA = 0x65F90;
    constexpr std::uintptr_t UiSide465FB0RVA = 0x65FB0;
    constexpr std::uintptr_t UiSide460410RVA = 0x60410;
    constexpr std::uintptr_t UiEventDispatchRVA = 0x5AC50;
    constexpr std::uintptr_t UiEventConsumerRVA = 0x725D0;
    constexpr std::uintptr_t UiEventActionRVA = 0x72210;



    void* serializeFromCachePack =

        reinterpret_cast<void*>(

            base +

            SerializeFromCachePackRVA

        );



    void* pixelSource =

        reinterpret_cast<void*>(

            base +

            PixelSourceRVA

        );


    void* renderSurfaceUpload =

        reinterpret_cast<void*>(

            base +

            RenderSurfaceUploadRVA

        );


    void* rendererLock =

        reinterpret_cast<void*>(

            base +

            RendererLockRVA

        );

    void* rendererUnlock =
        reinterpret_cast<void*>(
            base + RendererUnlockRVA
        );


    void* probe47SurfaceAccess =
        reinterpret_cast<void*>(
            base + Probe47SurfaceAccessRVA
        );

    void* probe67SurfaceCopy =
        reinterpret_cast<void*>(
            base + Probe67SurfaceCopyRVA
        );


    void* probe48MakeBacking =
        reinterpret_cast<void*>(base + Probe48MakeBackingRVA);
    void* probe48AttachBacking =
        reinterpret_cast<void*>(base + Probe48AttachBackingRVA);

    void* probe49Fc = reinterpret_cast<void*>(base + Probe49FcRVA);
    void* probe49Property = reinterpret_cast<void*>(base + Probe49PropertyRVA);
    void* probe49Plus104 = reinterpret_cast<void*>(base + Probe49Plus104RVA);
    void* probe50Mutate = reinterpret_cast<void*>(base + Probe50MutateRVA);
    void* probe50Invalidate = reinterpret_cast<void*>(base + Probe50InvalidateRVA);
    void* probe87Blit = reinterpret_cast<void*>(base + Probe87BlitRVA);
    void* probe87Composite = reinterpret_cast<void*>(base + Probe87CompositeRVA);
    void* probe87Copy = reinterpret_cast<void*>(base + Probe87CopyRVA);
    void* probe87ObjectBind = reinterpret_cast<void*>(base + Probe87ObjectBindRVA);
    void* probe87RawBind = reinterpret_cast<void*>(base + Probe87RawBindRVA);
    void* probe87Draw = reinterpret_cast<void*>(base + Probe87DrawRVA);
    void* probe87ImageSet = reinterpret_cast<void*>(base + Probe87ImageSetRVA);
    void* probe87ImageClear = reinterpret_cast<void*>(base + Probe87ImageClearRVA);
    void* probe87ItemRefresh = reinterpret_cast<void*>(base + Probe87ItemRefreshRVA);
    g_Probe87InvalidateSelf = reinterpret_cast<Probe87InvalidateSelfFn>(base + Probe87InvalidateSelfRVA);
    g_Probe87LookupItem = reinterpret_cast<Probe87LookupItemFn>(base + Probe87LookupItemRVA);
    g_Probe87CacheGet = reinterpret_cast<Probe87CacheGetFn>(base + Probe87CacheGetRVA);
    g_Probe87CacheRebuild = reinterpret_cast<Probe87CacheRebuildFn>(base + Probe87CacheRebuildRVA);


    g_RendererUnlock =
        reinterpret_cast<RendererUnlockFn>(
            base + RendererUnlockRVA
        );



    void* cSurfaceInitEnd =

        reinterpret_cast<void*>(

            base +

            CSurfaceInitEndRVA

        );


    void* uiLockSetter = reinterpret_cast<void*>(base + UiLockSetterRVA);
    void* uiPostLock = reinterpret_cast<void*>(base + UiPostLockRVA);
    void* uiDirtyCandidate = reinterpret_cast<void*>(base + UiDirtyCandidateRVA);
    void* uiDispatch = reinterpret_cast<void*>(base + UiDispatchRVA);
    void* uiListenerCommon = reinterpret_cast<void*>(base + UiListenerCommonRVA);
    void* uiListenerSpecial = reinterpret_cast<void*>(base + UiListenerSpecialRVA);
    void* uiEventType = reinterpret_cast<void*>(base + UiEventTypeRVA);
    void* uiEventMap = reinterpret_cast<void*>(base + UiEventMapRVA);
    void* uiEventRouter = reinterpret_cast<void*>(base + UiEventRouterRVA);
    void* uiRefresh1490 = reinterpret_cast<void*>(base + UiRefresh1490RVA);
    void* uiRefresh20B0 = reinterpret_cast<void*>(base + UiRefresh20B0RVA);
    void* uiVirtual9C = reinterpret_cast<void*>(base + UiVirtual9CRVA);
    void* uiStateUpdate = reinterpret_cast<void*>(base + UiStateUpdateRVA);
    void* uiStateForward = reinterpret_cast<void*>(base + UiStateForwardRVA);
    void* probe40State462390 = reinterpret_cast<void*>(base + Probe40State462390RVA);
    void* probe40Prop464090 = reinterpret_cast<void*>(base + Probe40Prop464090RVA);
    void* probe40Leaf463830 = reinterpret_cast<void*>(base + Probe40Leaf463830RVA);
    void* probe39Event472300 = reinterpret_cast<void*>(base + Probe39Event472300RVA);
    void* probe39Path4723B0 = reinterpret_cast<void*>(base + Probe39Path4723B0RVA);
    void* probe39Path4724A0 = reinterpret_cast<void*>(base + Probe39Path4724A0RVA);
    void* uiOpen465F90 = reinterpret_cast<void*>(base + UiOpen465F90RVA);
    void* uiSide465FB0 = reinterpret_cast<void*>(base + UiSide465FB0RVA);
    void* uiSide460410 = reinterpret_cast<void*>(base + UiSide460410RVA);
    void* uiEventDispatch = reinterpret_cast<void*>(base + UiEventDispatchRVA);
    void* uiEventConsumer = reinterpret_cast<void*>(base + UiEventConsumerRVA);
    void* uiEventAction = reinterpret_cast<void*>(base + UiEventActionRVA);
    g_UiRegistryGetter = reinterpret_cast<UiRegistryGetterFn>(base + 0x0007A810u);





    // --------------------------------------------------------

    // Initialize MinHook

    // --------------------------------------------------------



    MH_STATUS status =

        MH_Initialize();



    if (status != MH_OK &&

        status != MH_ERROR_ALREADY_INITIALIZED)

    {

        WriteLog(

            std::string(

                "ERROR: MH_Initialize: "

            ) +

            MH_StatusToString(status)

        );



        return 1;

    }





    // --------------------------------------------------------

    // SerializeFromCachePack

    // --------------------------------------------------------



    status = MH_CreateHook(

        serializeFromCachePack,

        reinterpret_cast<LPVOID>(

            &HookedSerializeFromCachePack

        ),

        reinterpret_cast<LPVOID*>(

            &g_OriginalSerializeFromCachePack

        )

    );



    if (status != MH_OK)

    {

        WriteLog(

            std::string(

                "ERROR: SerializeFromCachePack "

                "MH_CreateHook: "

            ) +

            MH_StatusToString(status)

        );



        return 1;

    }



    status =

        MH_EnableHook(

            serializeFromCachePack

        );



    if (status != MH_OK)

    {

        WriteLog(

            std::string(

                "ERROR: SerializeFromCachePack "

                "MH_EnableHook: "

            ) +

            MH_StatusToString(status)

        );



        return 1;

    }



    WriteLog(

        "SUCCESS: SerializeFromCachePack hook enabled."

    );





    // Probe #50 hooks intentionally not installed in Probe #51:
    // they were high-volume and did not identify an Abilities invalidation.

    // --------------------------------------------------------
    // Probe #87: discover first-backed roots for UI nodes reached by the common 0x006A08B0 traversal during capture,
    // then invalidate every captured root from its owning UI thread.
    // --------------------------------------------------------
    status = MH_CreateHook(
        probe87Draw,
        reinterpret_cast<LPVOID>(&HookedProbe87Draw),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe87Draw));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 draw MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe87Draw);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 draw MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }
    WriteLog("PROBE #87 SUCCESS: broad UI traversal/root-discovery hook enabled at RVA 0x2A08B0 (abs 0x006A08B0, vtable slot +0x3C).");

    status = MH_CreateHook(
        probe87ItemRefresh,
        reinterpret_cast<LPVOID>(&HookedProbe87ItemRefresh),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe87ItemRefresh));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 item-refresh MH_CreateHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe87ItemRefresh);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 item-refresh MH_EnableHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    WriteLog(
        "PROBE #87 SUCCESS: item-widget icon refresh hook enabled at "
        "RVA 0xE2D10 (abs 0x004E2D10); cacheGet=0x0058EC60 "
        "cacheRebuild=0x0058DFB0 lookupItem=0x005583F0.");

    // Live Mirror v1.2.4b: direct cache-surface correlation + item-only pixels ON, state metadata OFF.
    // Observe 0x0058DFB0 only to correlate the real file-backed DIDs used while
    // AC composes an item cache entry. Generated-surface pixel locking is enabled only for item-correlated DID-less blits.
    status = MH_CreateHook(
        reinterpret_cast<void*>(base + Probe87CacheRebuildRVA),
        reinterpret_cast<LPVOID>(&HookedACCustomsObservedCacheRebuild),
        reinterpret_cast<LPVOID*>(&g_OriginalACCustomsObservedCacheRebuild));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: item-cache provenance MH_CreateHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(reinterpret_cast<void*>(base + Probe87CacheRebuildRVA));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: item-cache provenance MH_EnableHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    WriteLog(
        "ACCUSTOMS LIVE_MIRROR direct cache-surface item correlation enabled; item-only generated pixel capture enabled; state metadata disabled.");

    status = MH_CreateHook(
        probe87ImageSet,
        reinterpret_cast<LPVOID>(&HookedProbe87ImageSet),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe87ImageSet));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 image-set MH_CreateHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe87ImageSet);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 image-set MH_EnableHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    WriteLog(
        "PROBE #87 SUCCESS: image-resource setter hook enabled at "
        "RVA 0x2A0610 (abs 0x006A0610, node+0x98).");

    status = MH_CreateHook(
        probe87ImageClear,
        reinterpret_cast<LPVOID>(&HookedProbe87ImageClear),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe87ImageClear));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 image-clear MH_CreateHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe87ImageClear);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 image-clear MH_EnableHook: ") +
            MH_StatusToString(status));
        return 1;
    }
    WriteLog(
        "PROBE #87 SUCCESS: image-resource clear hook enabled at "
        "RVA 0x2A0660 (abs 0x006A0660, node+0x98).");

    status = MH_CreateHook(
        probe87Composite,
        reinterpret_cast<LPVOID>(&HookedProbe87Composite),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe87Composite));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 inventory composite MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe87Composite);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 inventory composite MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }
    WriteLog("PROBE #87 SUCCESS: Inventory direct-composite trace hook enabled at RVA 0x428B0.");



    status = MH_CreateHook(
        probe87Blit,
        reinterpret_cast<LPVOID>(&HookedProbe87Blit),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe87Blit));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 wrapper MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe87Blit);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #87 wrapper MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }
    WriteLog("PROBE #87 SUCCESS: replacement redraw wrapper hook enabled at RVA 0x42C70; InvalidateSelf=0x006A0430; multi-thread scheduler=WH_GETMESSAGE.");

    // --------------------------------------------------------
    // Probe #49: remaining 0x463830 open-path calls
    // --------------------------------------------------------
    status = MH_CreateHook(probe49Fc,
        reinterpret_cast<LPVOID>(&HookedProbe49Fc),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe49Fc));
    if (status != MH_OK) { WriteLog(std::string("ERROR: Probe #49 +FC create: ") + MH_StatusToString(status)); return 1; }

    status = MH_CreateHook(probe49Property,
        reinterpret_cast<LPVOID>(&HookedProbe49Property),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe49Property));
    if (status != MH_OK) { WriteLog(std::string("ERROR: Probe #49 PROP35 create: ") + MH_StatusToString(status)); return 1; }

    status = MH_CreateHook(probe49Plus104,
        reinterpret_cast<LPVOID>(&HookedProbe49Plus104),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe49Plus104));
    if (status != MH_OK) { WriteLog(std::string("ERROR: Probe #49 +104 create: ") + MH_StatusToString(status)); return 1; }

    status = MH_EnableHook(probe49Fc);
    if (status != MH_OK) { WriteLog(std::string("ERROR: Probe #49 +FC enable: ") + MH_StatusToString(status)); return 1; }
    status = MH_EnableHook(probe49Property);
    if (status != MH_OK) { WriteLog(std::string("ERROR: Probe #49 PROP35 enable: ") + MH_StatusToString(status)); return 1; }
    status = MH_EnableHook(probe49Plus104);
    if (status != MH_OK) { WriteLog(std::string("ERROR: Probe #49 +104 enable: ") + MH_StatusToString(status)); return 1; }
    WriteLog("SUCCESS: probe #49 hooks enabled at RVA 0x61D60, 0x60CC0, 0x602C0.");

    // --------------------------------------------------------
    // Probe #48: Abilities backing-object lifecycle
    // --------------------------------------------------------
    status = MH_CreateHook(
        probe48MakeBacking,
        reinterpret_cast<LPVOID>(&HookedProbe48MakeBacking),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe48MakeBacking));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #48 MAKE MH_CreateHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        probe48AttachBacking,
        reinterpret_cast<LPVOID>(&HookedProbe48AttachBacking),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe48AttachBacking));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #48 ATTACH MH_CreateHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    status = MH_EnableHook(probe48MakeBacking);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #48 MAKE MH_EnableHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    status = MH_EnableHook(probe48AttachBacking);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #48 ATTACH MH_EnableHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    WriteLog("SUCCESS: probe #48 hooks enabled at RVA 0x61920 and 0x61BA0.");

    // --------------------------------------------------------
    // Probe #47: 0x4410C0 surface-access wrapper
    // --------------------------------------------------------
    status = MH_CreateHook(
        probe47SurfaceAccess,
        reinterpret_cast<LPVOID>(&HookedProbe47SurfaceAccess),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe47SurfaceAccess)
    );
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #47 MH_CreateHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    status = MH_EnableHook(probe47SurfaceAccess);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #47 MH_EnableHook: ") +
                 MH_StatusToString(status));
        return 1;
    }
    WriteLog("SUCCESS: probe #47 hook enabled at RVA 0x410C0.");

    // Probe #70 supersedes Probe #67.  The 0x00443290 hook is intentionally not installed.

    // --------------------------------------------------------
    // Probe #9: renderer destination lock
    // --------------------------------------------------------

    status = MH_CreateHook(
        rendererLock,
        reinterpret_cast<LPVOID>(&HookedRendererLock),
        reinterpret_cast<LPVOID*>(&g_OriginalRendererLock)
    );

    if (status != MH_OK)
    {
        WriteLog(
            std::string("ERROR: Renderer lock MH_CreateHook: ") +
            MH_StatusToString(status)
        );
        return 1;
    }

    status = MH_EnableHook(rendererLock);

    if (status != MH_OK)
    {
        WriteLog(
            std::string("ERROR: Renderer lock MH_EnableHook: ") +
            MH_StatusToString(status)
        );
        return 1;
    }

    WriteLog(
        "PROBE #73 BASELINE: renderer lock trampoline enabled at RVA 0x296F10."
    );

    status = MH_CreateHook(
        rendererUnlock,
        reinterpret_cast<LPVOID>(&HookedRendererUnlock),
        reinterpret_cast<LPVOID*>(&g_OriginalRendererUnlock)
    );
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #70 baseline renderer unlock MH_CreateHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    status = MH_EnableHook(rendererUnlock);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #70 baseline renderer unlock MH_EnableHook: ") +
                 MH_StatusToString(status));
        return 1;
    }

    // Critical: live F8/F9 swaps must bypass the observation detour exactly as
    // ApplyTextureLive already does for g_OriginalRendererLock.
    g_RendererUnlock = g_OriginalRendererUnlock;
    WriteLog("PROBE #73 BASELINE: renderer unlock trampoline enabled at RVA 0x296FB0; live-swap unlock uses original trampoline.");


    // --------------------------------------------------------
    // Probe #8: RenderSurface upload/commit path
    // --------------------------------------------------------

    status = MH_CreateHook(
        renderSurfaceUpload,
        reinterpret_cast<LPVOID>(&HookedRenderSurfaceUpload),
        reinterpret_cast<LPVOID*>(&g_OriginalRenderSurfaceUpload)
    );

    if (status != MH_OK)
    {
        WriteLog(
            std::string("ERROR: RenderSurface upload MH_CreateHook: ") +
            MH_StatusToString(status)
        );
        return 1;
    }

    status = MH_EnableHook(renderSurfaceUpload);

    if (status != MH_OK)
    {
        WriteLog(
            std::string("ERROR: RenderSurface upload MH_EnableHook: ") +
            MH_StatusToString(status)
        );
        return 1;
    }

    WriteLog(
        "SUCCESS: probe #8 RenderSurface upload hook enabled at RVA 0x44200."
    );


    // --------------------------------------------------------

    // PixelSource

    // --------------------------------------------------------



    status = MH_CreateHook(

        pixelSource,

        reinterpret_cast<LPVOID>(

            &HookedPixelSource

        ),

        reinterpret_cast<LPVOID*>(

            &g_OriginalPixelSource

        )

    );



    if (status != MH_OK)

    {

        WriteLog(

            std::string(

                "ERROR: PixelSource "

                "MH_CreateHook: "

            ) +

            MH_StatusToString(status)

        );



        return 1;

    }



    status =

        MH_EnableHook(

            pixelSource

        );



    if (status != MH_OK)

    {

        WriteLog(

            std::string(

                "ERROR: PixelSource "

                "MH_EnableHook: "

            ) +

            MH_StatusToString(status)

        );



        return 1;

    }



    WriteLog(

        "SUCCESS: PixelSource hook enabled."

    );



    status = MH_CreateHook(
        cSurfaceInitEnd,
        reinterpret_cast<LPVOID>(&HookedCSurfaceInitEnd),
        reinterpret_cast<LPVOID*>(&g_OriginalCSurfaceInitEnd)
    );

    if (status != MH_OK)
    {
        WriteLog(
            std::string("ERROR: CSurface::InitEnd MH_CreateHook: ") +
            MH_StatusToString(status)
        );
        return 1;
    }

    status = MH_EnableHook(cSurfaceInitEnd);

    if (status != MH_OK)
    {
        WriteLog(
            std::string("ERROR: CSurface::InitEnd MH_EnableHook: ") +
            MH_StatusToString(status)
        );
        return 1;
    }

    status = MH_CreateHook(
        uiLockSetter,
        reinterpret_cast<LPVOID>(&HookedUiLockSetter),
        reinterpret_cast<LPVOID*>(&g_OriginalUiLockSetter));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #21 UI lock setter MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiLockSetter);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #21 UI lock setter MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiDirtyCandidate,
        reinterpret_cast<LPVOID>(&HookedUiDirtyCandidate),
        reinterpret_cast<LPVOID*>(&g_OriginalUiDirtyCandidate));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #24 dirty candidate MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiDirtyCandidate);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #24 dirty candidate MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiDispatch,
        reinterpret_cast<LPVOID>(&HookedUiDispatch),
        reinterpret_cast<LPVOID*>(&g_OriginalUiDispatch));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #25 dispatcher MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiDispatch);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #25 dispatcher MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiListenerCommon,
        reinterpret_cast<LPVOID>(&HookedUiListenerCommon),
        reinterpret_cast<LPVOID*>(&g_OriginalUiListenerCommon));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #27 common callback MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiListenerCommon);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #27 common callback MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiListenerSpecial,
        reinterpret_cast<LPVOID>(&HookedUiListenerSpecial),
        reinterpret_cast<LPVOID*>(&g_OriginalUiListenerSpecial));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #27 special callback MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiListenerSpecial);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #27 special callback MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiEventType,
        reinterpret_cast<LPVOID>(&HookedUiEventType),
        reinterpret_cast<LPVOID*>(&g_OriginalUiEventType));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #28 event-type MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiEventType);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #28 event-type MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiEventMap,
        reinterpret_cast<LPVOID>(&HookedUiEventMap),
        reinterpret_cast<LPVOID*>(&g_OriginalUiEventMap));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #29 RVA 0x29120 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiEventMap);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #29 RVA 0x29120 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiEventRouter,
        reinterpret_cast<LPVOID>(&HookedUiEventRouter),
        reinterpret_cast<LPVOID*>(&g_OriginalUiEventRouter));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #30 RVA 0x64C90 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiEventRouter);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #30 RVA 0x64C90 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiRefresh1490,
        reinterpret_cast<LPVOID>(&HookedUiRefresh1490),
        reinterpret_cast<LPVOID*>(&g_OriginalUiRefresh1490));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #31 RVA 0x71490 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiRefresh1490);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #31 RVA 0x71490 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiRefresh20B0,
        reinterpret_cast<LPVOID>(&HookedUiRefresh20B0),
        reinterpret_cast<LPVOID*>(&g_OriginalUiRefresh20B0));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #31 RVA 0x720B0 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiRefresh20B0);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #31 RVA 0x720B0 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiVirtual9C,
        reinterpret_cast<LPVOID>(&HookedUiVirtual9C),
        reinterpret_cast<LPVOID*>(&g_OriginalUiVirtual9C));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #32 RVA 0x72160 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiVirtual9C);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #32 RVA 0x72160 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiStateUpdate,
        reinterpret_cast<LPVOID>(&HookedUiStateUpdate),
        reinterpret_cast<LPVOID*>(&g_OriginalUiStateUpdate));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #33 RVA 0x60820 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiStateUpdate);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #33 RVA 0x60820 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiStateForward,
        reinterpret_cast<LPVOID>(&HookedUiStateForward),
        reinterpret_cast<LPVOID*>(&g_OriginalUiStateForward));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #33 RVA 0x64FA0 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiStateForward);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #33 RVA 0x64FA0 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    // Probe #41: deliberately do NOT hook 0x462390. Probe #40 proved our
    // assumed prototype/calling convention for that address was wrong.
    WriteLog("PROBE #41: 0x462390 disabled; tracing only 0x464090/0x463830 propagation.");

    status = MH_CreateHook(
        probe40Prop464090,
        reinterpret_cast<LPVOID>(&HookedProbe40Prop464090),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe40Prop464090));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #40 RVA 0x64090 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe40Prop464090);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #40 RVA 0x64090 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        probe40Leaf463830,
        reinterpret_cast<LPVOID>(&HookedProbe40Leaf463830),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe40Leaf463830));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #40 RVA 0x63830 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe40Leaf463830);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #40 RVA 0x63830 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    WriteLog("SUCCESS: probe #42 virtual-target resolver enabled; propagation hooks remain at RVAs 0x64090 and 0x63830; 0x62390 remains disabled.");
    WriteLog("PROBE #42: F11 arms tracing for 3 seconds. Toggle ONLY Abilities closed->open->closed->open. No active redraw calls are made.");
    WriteLog("PROBE #58: leave Abilities OPEN, press F11, then CLOSE -> OPEN Abilities once within 10 seconds. Captures bounded SetTexture/draw submission callers.");

    status = MH_CreateHook(
        probe39Event472300,
        reinterpret_cast<LPVOID>(&HookedProbe39Event472300),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe39Event472300));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #39 RVA 0x72300 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe39Event472300);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #39 RVA 0x72300 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        probe39Path4723B0,
        reinterpret_cast<LPVOID>(&HookedProbe39Path4723B0),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe39Path4723B0));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #39 RVA 0x723B0 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe39Path4723B0);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #39 RVA 0x723B0 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        probe39Path4724A0,
        reinterpret_cast<LPVOID>(&HookedProbe39Path4724A0),
        reinterpret_cast<LPVOID*>(&g_OriginalProbe39Path4724A0));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #39 RVA 0x724A0 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(probe39Path4724A0);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #39 RVA 0x724A0 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    WriteLog("SUCCESS: probe #39 Abilities-class hooks enabled at RVAs 0x72300, 0x723B0, 0x724A0.");
    WriteLog("PROBE #39: F11 arms tracing for 3 seconds. Toggle ONLY Abilities closed->open->closed->open using the normal UI control.");

    status = MH_CreateHook(
        uiOpen465F90,
        reinterpret_cast<LPVOID>(&HookedUiOpen465F90),
        reinterpret_cast<LPVOID*>(&g_OriginalUiOpen465F90));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #38 RVA 0x65F90 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiOpen465F90);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #38 RVA 0x65F90 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    WriteLog("SUCCESS: probe #38 observational OPEN-path hook enabled at RVA 0x65F90.");
    WriteLog("PROBE #38: F11 arms tracing for 3 seconds. Toggle Abilities open/closed repeatedly; OPEN465F90 marks opening, SIDE465FB0 marks closing.");

    status = MH_CreateHook(
        uiSide465FB0,
        reinterpret_cast<LPVOID>(&HookedUiSide465FB0),
        reinterpret_cast<LPVOID*>(&g_OriginalUiSide465FB0));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #34 RVA 0x65FB0 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiSide465FB0);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #34 RVA 0x65FB0 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiSide460410,
        reinterpret_cast<LPVOID>(&HookedUiSide460410),
        reinterpret_cast<LPVOID*>(&g_OriginalUiSide460410));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #34 RVA 0x60410 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiSide460410);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #34 RVA 0x60410 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiEventDispatch,
        reinterpret_cast<LPVOID>(&HookedUiEventDispatch),
        reinterpret_cast<LPVOID*>(&g_OriginalUiEventDispatch));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #35 RVA 0x5AC50 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiEventDispatch);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #35 RVA 0x5AC50 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiEventConsumer,
        reinterpret_cast<LPVOID>(&HookedUiEventConsumer),
        reinterpret_cast<LPVOID*>(&g_OriginalUiEventConsumer));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #36 RVA 0x725D0 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiEventConsumer);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #36 RVA 0x725D0 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    status = MH_CreateHook(
        uiEventAction,
        reinterpret_cast<LPVOID>(&HookedUiEventAction),
        reinterpret_cast<LPVOID*>(&g_OriginalUiEventAction));
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #36 RVA 0x72210 MH_CreateHook: ") + MH_StatusToString(status));
        return 1;
    }
    status = MH_EnableHook(uiEventAction);
    if (status != MH_OK)
    {
        WriteLog(std::string("ERROR: Probe #36 RVA 0x72210 MH_EnableHook: ") + MH_StatusToString(status));
        return 1;
    }

    WriteLog("SUCCESS: probe #36 observational consumer hooks enabled at RVA 0x725D0 and RVA 0x72210.");
    WriteLog("PROBE #36: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #35 observational event-dispatch hook enabled at RVA 0x5AC50.");
    WriteLog("PROBE #35: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #34 observational side-path hooks enabled at RVA 0x65FB0 and RVA 0x60410.");
    WriteLog("PROBE #34: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #33 observational downstream hooks enabled at RVA 0x60820 and RVA 0x64FA0.");
    WriteLog("PROBE #33: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #32 observational virtual +0x9C hook enabled at RVA 0x72160.");
    WriteLog("PROBE #32: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #31 observational hooks enabled at RVA 0x71490 and RVA 0x720B0.");
    WriteLog("PROBE #31: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #30 observational router hook enabled at RVA 0x64C90.");
    WriteLog("PROBE #30: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #29 observational hook enabled at RVA 0x29120.");
    WriteLog("PROBE #29: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #28 UI event-type tracing enabled at RVA 0x29A00.");
    WriteLog("PROBE #28: F11 arms tracing for 3 seconds; open/close Stats or Abilities repeatedly during the window.");

    WriteLog("SUCCESS: probe #27 listener callback tracing enabled.");
    WriteLog("PROBE #27: hooks common callback RVA 0xD31D0 and special callback RVA 0xD5CB0.");

    WriteLog("PROBE #26: snapshots dispatcher listeners and their vtable +0x22C callback targets.");
    WriteLog("PROBE #22: UI lock setter RVA 0x1D4340. No high-frequency post-lock hook.");
    WriteLog("PROBE #22: F8=bulk originals; F9=bulk replacements; manually toggle UI lock after each.");



    WriteLog(
        "PROBE #22: renderer unlock resolved at RVA 0x296FB0."
    );

    HANDLE maintenanceThread =
        CreateThread(
            nullptr,
            0,
            ACCustomsMaintenanceThread,
            nullptr,
            0,
            nullptr);

    if (maintenanceThread != nullptr)
        CloseHandle(maintenanceThread);
    else
        WriteLog("ERROR: Could not start AC Customs maintenance thread.");

    HANDLE lifecycleThread =
        CreateThread(
            nullptr,
            0,
            RenderSurfaceLifecycleThread,
            nullptr,
            0,
            nullptr);

    if (lifecycleThread != nullptr)
        CloseHandle(lifecycleThread);
    else
        WriteLog(
            "ERROR: Could not start RenderSurface lifecycle probe thread.");

    HANDLE correlationThread =
        CreateThread(
            nullptr,
            0,
            RenderSurfaceCorrelationThread,
            nullptr,
            0,
            nullptr);

    if (correlationThread != nullptr)
        CloseHandle(correlationThread);
    else
        WriteLog(
            "ERROR: Could not start RenderSurface correlation probe thread.");



    // Decal replaces the old F11 arming step. From this point forward the
    // existing draw/item hooks learn backed roots, item owners, and item IDs
    // while the client remains completely vanilla.
    Probe87ResetState();
    g_Probe53Captured.store(false);
    Probe87LoadReplacementDidSet();
    g_Probe87CaptureArmed.store(true);

    WriteLog(
        "ACCUSTOMS INITIALIZATION COMPLETE: safeMode=VANILLA captureArmed=1 hotkeys=NONE"
    );

    return 0;

}






// ------------------------------------------------------------
// Developer hover inspector.
//
// Pull-only diagnostic: no extra hook, timer, or worker thread. The managed
// Decal view asks at most once per second while DEVELOPER TESTS is enabled and
// the AC Customs panel is open.
//
// IMPORTANT: AC's native child draw walker at 0x006A0B20 visits child links in
// the same order used below and calls each child's draw virtual in that order.
// Therefore a later visit is a later draw and is visually above an earlier
// visit. The hover stack is returned in reverse visit order: TOP -> BOTTOM.
// ------------------------------------------------------------

enum class ACCustomsHoverLayerKind : std::uint32_t
{
    TextureDid = 1,
    ItemGenerated = 2,
    GeneratedSurface = 3
};

struct ACCustomsHoverTextureCandidate
{
    ACCustomsHoverLayerKind kind;
    std::uint32_t did;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t depth;
    std::uint32_t visitOrder;
    std::uint32_t nodeAddress;
    std::uint32_t itemId;
};

static bool ACCustomsNodeClientRect(void* node, RECT& out)
{
    out = {};
    if (!node || !ProbeReadableRange(node, 0xB0))
        return false;

    std::int32_t l = 0, t = 0, r = 0, b = 0;
    if (!Probe87ReadNodeRect(node, l, t, r, b))
        return false;

    const std::int32_t width = r - l + 1;
    const std::int32_t height = b - t + 1;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return false;

    std::int64_t x = 0;
    std::int64_t y = 0;
    void* current = node;
    std::unordered_set<std::uint32_t> seen;

    for (std::uint32_t depth = 0; depth < 32 && current; ++depth)
    {
        if (!ProbeReadableRange(current, 0xB0))
            return false;

        const std::uint32_t address = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(current));
        if (!address || !seen.insert(address).second)
            return false;

        std::int32_t cl = 0, ct = 0, cr = 0, cb = 0;
        if (!Probe87ReadNodeRect(current, cl, ct, cr, cb))
            return false;

        x += cl;
        y += ct;

        const std::uint32_t parent = ReadUInt32(current, 0xAC);
        if (!parent)
            break;
        current = reinterpret_cast<void*>(static_cast<std::uintptr_t>(parent));
    }

    if (x < -65536 || y < -65536 || x > 65536 || y > 65536)
        return false;

    out.left = static_cast<LONG>(x);
    out.top = static_cast<LONG>(y);
    out.right = static_cast<LONG>(x + width);
    out.bottom = static_cast<LONG>(y + height);
    return true;
}

static bool ACCustomsReadImageNodeResource(
    void* node,
    std::uint32_t& did,
    std::uint32_t& width,
    std::uint32_t& height)
{
    did = 0;
    width = 0;
    height = 0;

    if (!Probe87IsImageNode(node) || !ProbeReadableRange(node, 0x9C))
        return false;

    const std::uint32_t resourceAddress = ReadUInt32(node, 0x98);
    if (!resourceAddress)
        return false;

    void* resource = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(resourceAddress));
    if (!ProbeReadableRange(resource, 0x0C))
        return false;

    const std::uint32_t underlyingAddress = ReadUInt32(resource, 0x08);
    if (!underlyingAddress)
        return false;

    void* underlying = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(underlyingAddress));
    if (!ProbeReadableRange(underlying, 0xA8))
        return false;

    const std::uint32_t possibleDid = ReadUInt32(underlying, 0x28);
    if ((possibleDid & 0xFF000000u) == 0x06000000u)
        did = possibleDid;

    width = ReadUInt32(underlying, 0xA0);
    height = ReadUInt32(underlying, 0xA4);

    // Wrapper/generated resources do not always expose geometry here. For a
    // real DID, use the proven live registry as a fallback.
    if (did != 0 &&
        (width == 0 || height == 0 || width > 8192 || height > 8192))
    {
        std::lock_guard<std::mutex> lock(g_LiveTextureRegistryMutex);
        auto it = g_LiveTextureRegistry.find(did);
        if (it != g_LiveTextureRegistry.end())
        {
            width = it->second.width;
            height = it->second.height;
        }
    }

    return true;
}

static std::uint32_t ACCustomsFindItemIdForImageNode(void* node)
{
    if (!node)
        return 0;

    const std::uint32_t nodeAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));

    std::lock_guard<std::mutex> lock(g_Probe87Mutex);
    for (const auto& kv : g_Probe87ItemOwners)
    {
        const Probe87ItemOwnerEntry& entry = kv.second;
        if (entry.imageNode == nodeAddress && entry.itemId != 0)
            return entry.itemId;
    }
    return 0;
}

static bool ACCustomsDescribeImageLayer(
    void* node,
    const RECT& rect,
    std::uint32_t depth,
    std::uint32_t visitOrder,
    ACCustomsHoverTextureCandidate& out)
{
    out = {};
    if (!Probe87IsImageNode(node))
        return false;

    std::uint32_t did = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!ACCustomsReadImageNodeResource(node, did, width, height))
        return false;

    const std::uint32_t itemId = ACCustomsFindItemIdForImageNode(node);

    // For generated surfaces, use the on-screen image-node geometry if the
    // underlying generated resource does not expose useful source dimensions.
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
    {
        width = rect.right > rect.left
            ? static_cast<std::uint32_t>(rect.right - rect.left) : 0;
        height = rect.bottom > rect.top
            ? static_cast<std::uint32_t>(rect.bottom - rect.top) : 0;
    }

    out.kind = did != 0
        ? ACCustomsHoverLayerKind::TextureDid
        : (itemId != 0
            ? ACCustomsHoverLayerKind::ItemGenerated
            : ACCustomsHoverLayerKind::GeneratedSurface);
    out.did = did;
    out.width = width;
    out.height = height;
    out.depth = depth;
    out.visitOrder = visitOrder;
    out.nodeAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));
    out.itemId = itemId;
    return true;
}

static void ACCustomsScanHoverTree(
    void* node,
    const POINT& clientPoint,
    std::uint32_t depth,
    std::unordered_set<std::uint32_t>& visited,
    std::uint32_t& visitedNodes,
    std::uint32_t& visitOrder,
    std::vector<ACCustomsHoverTextureCandidate>& matches)
{
    if (!node || depth > 40 || visitedNodes >= 4096)
        return;
    if (!ProbeReadableRange(node, 0x128))
        return;

    const std::uint32_t address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(node));
    if (!address || !visited.insert(address).second)
        return;

    ++visitedNodes;
    const std::uint32_t order = ++visitOrder;

    RECT rect = {};
    if (ACCustomsNodeClientRect(node, rect) &&
        clientPoint.x >= rect.left && clientPoint.x < rect.right &&
        clientPoint.y >= rect.top && clientPoint.y < rect.bottom)
    {
        ACCustomsHoverTextureCandidate candidate = {};
        if (ACCustomsDescribeImageLayer(node, rect, depth, order, candidate))
            matches.push_back(candidate);
    }

    const std::uint32_t childCount = ReadUInt32(node, 0x120);
    if (childCount == 0 || childCount > 2048)
        return;

    const std::uint32_t headRaw = ReadUInt32(node, 0x124);
    std::uint32_t link = headRaw >= 8 ? (headRaw - 8) : 0;
    std::uint32_t iter = 0;

    // This is intentionally the same link direction used by AC's native draw
    // walker at 0x006A0B20. Later recursive visits are later draw operations.
    while (link && iter < childCount + 8 && iter < 4096)
    {
        void* linkPtr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link));
        if (!ProbeReadableRange(linkPtr, 0x14))
            break;

        const std::uint32_t childAddress = ReadUInt32(linkPtr, 0x10);
        const std::uint32_t nextRaw = ReadUInt32(linkPtr, 0x08);

        if (childAddress)
        {
            void* child = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(childAddress));
            ACCustomsScanHoverTree(
                child,
                clientPoint,
                depth + 1,
                visited,
                visitedNodes,
                visitOrder,
                matches);
        }

        link = nextRaw >= 8 ? (nextRaw - 8) : 0;
        ++iter;
    }
}

static bool ACCustomsCollectHoveredLayers(
    std::vector<ACCustomsHoverTextureCandidate>& matches)
{
    matches.clear();

    if (!g_ACCustomsInitialized.load(std::memory_order_acquire) ||
        !g_DeveloperTestsEnabled.load(std::memory_order_acquire))
    {
        return false;
    }

    const std::uint32_t desktopAddress =
        g_Probe87DesktopRoot.load(std::memory_order_acquire);
    if (!desktopAddress)
        return false;

    POINT screen = {};
    if (!GetCursorPos(&screen))
        return false;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId())
        return false;

    POINT client = screen;
    if (!ScreenToClient(hwnd, &client))
        return false;

    void* desktop = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(desktopAddress));
    if (!ProbeReadableRange(desktop, 0x128))
        return false;

    std::unordered_set<std::uint32_t> visited;
    std::uint32_t visitedNodes = 0;
    std::uint32_t visitOrder = 0;

    ACCustomsScanHoverTree(
        desktop,
        client,
        0,
        visited,
        visitedNodes,
        visitOrder,
        matches);

    return !matches.empty();
}

static std::string ACCustomsHoverStackWireText(
    const std::vector<ACCustomsHoverTextureCandidate>& matches)
{
    std::ostringstream out;
    std::unordered_set<std::uint64_t> seen;
    std::uint32_t emitted = 0;

    // Reverse draw order: last drawn == visually topmost.
    for (auto it = matches.rbegin(); it != matches.rend() && emitted < 12; ++it)
    {
        const ACCustomsHoverTextureCandidate& c = *it;

        // Collapse repeated instances of the same meaningful layer so a tiled
        // background does not flood the small developer readout.
        const std::uint64_t key = c.did != 0
            ? (0x100000000ull | static_cast<std::uint64_t>(c.did))
            : (c.itemId != 0
                ? (0x200000000ull | static_cast<std::uint64_t>(c.itemId))
                : (0x300000000ull | static_cast<std::uint64_t>(c.nodeAddress)));
        if (!seen.insert(key).second)
            continue;

        const char kind = c.did != 0 ? 'T' : (c.itemId != 0 ? 'I' : 'G');
        out << kind << '|'
            << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
            << c.did << '|'
            << std::dec << c.width << '|'
            << c.height << '|'
            << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
            << c.itemId << '\n';
        ++emitted;
    }

    return out.str();
}

// ------------------------------------------------------------
// Decal-facing native API
// ------------------------------------------------------------

extern "C" __declspec(dllexport) int __cdecl ACCustoms_Initialize()
{
    std::lock_guard<std::mutex> lock(g_ACCustomsControlMutex);

    if (g_ACCustomsInitialized.load(std::memory_order_acquire))
        return 1;
    if (g_ACCustomsInitializationFailed.load(std::memory_order_acquire))
        return 0;

    g_ActiveThemeMode.store(ActiveThemeMode::Vanilla, std::memory_order_release);
    g_Probe92ThemeGeneration.store(0, std::memory_order_release);
    g_LiveSwapStop.store(false, std::memory_order_release);
    ClearActiveAppliedDids();

    const DWORD result = InitializeProductionThread(nullptr);
    if (result != 0)
    {
        g_ACCustomsInitializationFailed.store(true, std::memory_order_release);
        WriteLog("ACCUSTOMS INITIALIZE result=FAILED code=" + std::to_string(result));
        return 0;
    }

    g_ACCustomsInitialized.store(true, std::memory_order_release);
    ACCustomsStartLiveMirrorBridge();
    WriteLog("ACCUSTOMS INITIALIZE result=READY activeTheme=VANILLA liveMirrorBridge=READY");
    return 1;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_ApplyTestReplacement()
{
    if (!g_ACCustomsInitialized.load(std::memory_order_acquire))
    {
        ACCustomsRecordThemeReport(
            "APPLY result=REJECTED reason=native engine is not initialized");
        return 0;
    }

    bool expected = false;
    if (!g_ACCustomsApplyInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        WriteLog("ACCUSTOMS APPLY rejected=BUSY");
        ACCustomsRecordThemeReport(
            "APPLY result=REJECTED reason=another theme operation is already in progress");
        return 0;
    }

    int result = 0;
    {
        std::lock_guard<std::mutex> lock(g_ACCustomsControlMutex);

        if (g_ACCustomsSnapshotWriterRunning.load(
                std::memory_order_acquire))
        {
            WriteLog(
                "ACCUSTOMS APPLY rejected=LIVE_UI_SNAPSHOT_ACTIVE");
            ACCustomsRecordThemeReport(
                "APPLY result=REJECTED reason=Live UI snapshot capture is still active");
            g_ACCustomsApplyInProgress.store(
                false,
                std::memory_order_release);
            return 0;
        }

        const ActiveThemeMode modeBefore = GetActiveThemeMode();

        // If we are still vanilla, capture originals for any replacement-backed
        // textures that were already resident before Decal/native initialization.
        if (modeBefore == ActiveThemeMode::Vanilla)
            CaptureResidentOriginalsForReplacementSet();

        // Desired state changes before any redraw-triggered lazy loads, matching
        // the proven Probe #88 ordering.
        SetActiveThemeMode(ActiveThemeMode::TestReplacement, "DECAL_APPLY");
        const ApplySetStats st = ApplyReplacementSet(true);

        WriteLog(
            "ACCUSTOMS APPLY complete applied=" + std::to_string(st.applied) +
            " registered=" + std::to_string(st.registered) +
            " failed=" + std::to_string(st.failed));

        Probe87QueueRootInvalidation(9);
        result = (st.failed == 0) ? 1 : 0;

        ACCustomsRecordThemeReport(
            ACCustomsBuildThemeOperationReport(
                "APPLY",
                st,
                result != 0,
                modeBefore,
                GetActiveThemeMode()));
    }

    g_ACCustomsApplyInProgress.store(false, std::memory_order_release);
    return result;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_RestoreVanilla()
{
    if (!g_ACCustomsInitialized.load(std::memory_order_acquire))
    {
        ACCustomsRecordThemeReport(
            "RESTORE result=REJECTED reason=native engine is not initialized");
        return 0;
    }

    bool expected = false;
    if (!g_ACCustomsApplyInProgress.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        WriteLog("ACCUSTOMS RESTORE rejected=BUSY");
        ACCustomsRecordThemeReport(
            "RESTORE result=REJECTED reason=another theme operation is already in progress");
        return 0;
    }

    int result = 0;
    {
        std::lock_guard<std::mutex> lock(g_ACCustomsControlMutex);

        if (g_ACCustomsSnapshotWriterRunning.load(
                std::memory_order_acquire))
        {
            WriteLog(
                "ACCUSTOMS RESTORE rejected=LIVE_UI_SNAPSHOT_ACTIVE");
            ACCustomsRecordThemeReport(
                "RESTORE result=REJECTED reason=Live UI snapshot capture is still active");
            g_ACCustomsApplyInProgress.store(
                false,
                std::memory_order_release);
            return 0;
        }

        const ActiveThemeMode modeBefore = GetActiveThemeMode();
        SetActiveThemeMode(ActiveThemeMode::Vanilla, "DECAL_RESTORE");
        const ApplySetStats st = ApplyReplacementSet(false);

        WriteLog(
            "ACCUSTOMS RESTORE complete applied=" + std::to_string(st.applied) +
            " registered=" + std::to_string(st.registered) +
            " missingOriginal=" + std::to_string(st.missingOriginal) +
            " failed=" + std::to_string(st.failed));

        Probe87QueueRootInvalidation(8);
        result = (st.failed == 0) ? 1 : 0;

        ACCustomsRecordThemeReport(
            ACCustomsBuildThemeOperationReport(
                "RESTORE",
                st,
                result != 0,
                modeBefore,
                GetActiveThemeMode()));
    }

    g_ACCustomsApplyInProgress.store(false, std::memory_order_release);
    return result;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_GetLastThemeReport(
    char* buffer,
    std::uint32_t bufferSize)
{
    if (!buffer || bufferSize == 0)
        return 0;

    buffer[0] = '\0';

    std::lock_guard<std::mutex> lock(g_ThemeDiagnosticMutex);
    if (g_LastThemeOperationReport.empty())
        return 0;

    const std::size_t copyLength =
        std::min<std::size_t>(
            g_LastThemeOperationReport.size(),
            static_cast<std::size_t>(bufferSize - 1));

    std::memcpy(buffer, g_LastThemeOperationReport.data(), copyLength);
    buffer[copyLength] = '\0';
    return static_cast<int>(copyLength);
}


extern "C" __declspec(dllexport) int __cdecl ACCustoms_SetReplacementDirectory(const char* directory)
{
    if (!directory || !*directory)
        return 0;

    if (GetActiveThemeMode() != ActiveThemeMode::Vanilla ||
        g_ACCustomsApplyInProgress.load(std::memory_order_acquire))
    {
        WriteLog("ACCUSTOMS SET_REPLACEMENT_DIRECTORY rejected=THEME_ACTIVE_OR_BUSY");
        return 0;
    }

    const DWORD attrs = GetFileAttributesA(directory);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return 0;

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsPathMutex);
        g_ACCustomsReplacementDirectory = directory;
    }

    // The lazy-load fast path uses this DID set to avoid filesystem work for
    // unrelated AC textures, so it must follow the selected theme directory.
    Probe87LoadReplacementDidSet();
    WriteLog("ACCUSTOMS SET_REPLACEMENT_DIRECTORY path=" + std::string(directory));
    return 1;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_SetCaptureDirectory(const char* directory)
{
    if (!directory || !*directory)
        return 0;

    if (g_ACCustomsApplyInProgress.load(std::memory_order_acquire))
        return 0;

    // The managed side creates parent directories. CreateDirectoryA here is a
    // final guard for an already-existing parent.
    CreateDirectoryA(directory, nullptr);
    const DWORD attrs = GetFileAttributesA(directory);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return 0;

    {
        std::lock_guard<std::mutex> lock(g_ACCustomsPathMutex);
        g_ACCustomsCaptureDirectory = directory;
    }

    WriteLog("ACCUSTOMS SET_CAPTURE_DIRECTORY path=" + std::string(directory));
    return 1;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_SetDeveloperTests(int enabled)
{
    const bool turnOn = enabled != 0;
    const bool wasOn = g_DeveloperTestsEnabled.load(std::memory_order_acquire);

    if (turnOn == wasOn)
        return 1;

    if (turnOn)
    {
        g_DeveloperTestsEnabled.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> logLock(g_LogMutex);
            const std::string logPath = ACCustomsDeveloperLogPath();
            std::ofstream fresh(logPath, std::ios::out | std::ios::trunc);
        }
        WriteLog("========================================");
        WriteLog("AC Customs DEVELOPER TESTS enabled");
        WriteLog("diagnostics=logging + encountered texture collection");
        WriteLog("historical probe hooks remain uninstalled");
        WriteLog("========================================");
    }
    else
    {
        WriteLog("AC Customs DEVELOPER TESTS disabled");
        g_DeveloperTestsEnabled.store(false, std::memory_order_release);
    }

    return 1;
}


extern "C" __declspec(dllexport) int __cdecl ACCustoms_CaptureUiSnapshot(const char* outputPath)
{
    if (!g_ACCustomsInitialized.load(std::memory_order_acquire))
        return 0;
    if (!g_DeveloperTestsEnabled.load(std::memory_order_acquire))
        return 0;
    return ACCustomsStartUiSnapshot(outputPath) ? 1 : 0;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_CaptureControlStateLinks(const char* outputPath)
{
    if (!g_ACCustomsInitialized.load(std::memory_order_acquire))
        return 0;
    if (!g_DeveloperTestsEnabled.load(std::memory_order_acquire))
        return 0;
    return ACCustomsStartStateLinkCapture(outputPath) ? 1 : 0;
}


extern "C" __declspec(dllexport) int __cdecl ACCustoms_GetHoveredTexture(
    std::uint32_t* didOut,
    std::uint32_t* widthOut,
    std::uint32_t* heightOut)
{
    if (didOut) *didOut = 0;
    if (widthOut) *widthOut = 0;
    if (heightOut) *heightOut = 0;

    std::vector<ACCustomsHoverTextureCandidate> matches;
    if (!ACCustomsCollectHoveredLayers(matches))
        return 0;

    // Backward-compatible single-texture API: return the topmost REAL 0x06 DID.
    // Generated item layers are exposed by ACCustoms_GetHoveredTextureStack.
    for (auto it = matches.rbegin(); it != matches.rend(); ++it)
    {
        if (it->did == 0)
            continue;

        if (didOut) *didOut = it->did;
        if (widthOut) *widthOut = it->width;
        if (heightOut) *heightOut = it->height;
        return 1;
    }

    return 0;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_GetHoveredTextureStack(
    char* buffer,
    std::uint32_t bufferSize)
{
    if (!buffer || bufferSize == 0)
        return 0;

    buffer[0] = '\0';

    std::vector<ACCustomsHoverTextureCandidate> matches;
    if (!ACCustomsCollectHoveredLayers(matches))
        return 0;

    const std::string wire = ACCustomsHoverStackWireText(matches);
    if (wire.empty())
        return 0;

    const std::size_t copyLength =
        wire.size() < static_cast<std::size_t>(bufferSize - 1)
        ? wire.size()
        : static_cast<std::size_t>(bufferSize - 1);

    std::memcpy(buffer, wire.data(), copyLength);
    buffer[copyLength] = '\0';
    return static_cast<int>(copyLength);
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_GetActiveMode()
{
    if (!g_ACCustomsInitialized.load(std::memory_order_acquire))
        return -1;
    return GetActiveThemeMode() == ActiveThemeMode::TestReplacement ? 1 : 0;
}

extern "C" __declspec(dllexport) int __cdecl ACCustoms_Shutdown()
{
    // For this proof-of-concept we leave hooks installed until acclient.exe exits.
    // If the managed plugin is disabled mid-session, restore vanilla first so no
    // custom visual state remains. Native DLL unloading/unhooking is a separate
    // production-hardening milestone.
    if (g_ACCustomsInitialized.load(std::memory_order_acquire) &&
        GetActiveThemeMode() == ActiveThemeMode::TestReplacement)
    {
        ACCustoms_RestoreVanilla();
    }
    return 1;
}


// ------------------------------------------------------------
// DLL entry point
// ------------------------------------------------------------

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        // Deliberately passive: Decal calls ACCustoms_Initialize explicitly.
        // Merely loading this DLL never installs hooks or changes UI state.
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_LiveSwapStop.store(true, std::memory_order_release);
        g_ACCustomsLiveMirrorBridgeStop.store(true, std::memory_order_release);
    }

    return TRUE;
}
