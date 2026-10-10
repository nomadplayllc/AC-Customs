// AC Customs UI Manager
//
// Standalone Win32 editor/browser for AC Customs. The Manager reads AC texture
// metadata and pixels directly from client_portal.dat, stores user replacements
// under %LOCALAPPDATA%\ACCustoms, and optionally mirrors the live UI through the
// plugin's named-pipe snapshot bridge. See docs/ARCHITECTURE.md and LIVE_MIRROR.md.
//
// IMPORTANT: Build x86 to match the supported AC/Decal environment.

#define UNICODE
#define _UNICODE

// Experimental branch only. Change to 0 before shipping a public build.
#define AC_CUSTOMS_TEMPLATE_DEVTOOLS 1

#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <wincodec.h>
#include <objidl.h>
#include <CommDlg.h>

#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <utility>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")

static const wchar_t* WINDOW_CLASS =
    L"ACModernUIManagerWindow";

static const wchar_t* WINDOW_TITLE =
    L"AC Customs";

static const UINT WM_APP_PREVIEW_READY = WM_APP + 1;
static const UINT_PTR PREVIEW_TIMER_ID = 1;

static const int THUMBNAIL_SIZE = 64;

struct AppPaths
{
    std::wstring installRoot;
    std::wstring converterPath;
    std::wstring defaultsRoot;
    std::wstring defaultCustomTabsPath;
    std::wstring defaultTextureNotesPath;

    std::wstring localAppDataRoot;
    std::wstring managerRoot;
    std::wstring datPreferencePath;

    std::wstring userRoot;
    std::wstring replacementDirectory;
    std::wstring customTabsPath;
    std::wstring textureNotesPath;

    std::wstring runtimeRoot;
    std::wstring liveMirrorRoot;
    std::wstring snapshotsRoot;
    std::wstring importStageDirectory;
    std::wstring importBackupDirectory;
    std::wstring encounteredPath;
    std::wstring logsRoot;

};

static AppPaths g_AppPaths;
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
// Declared early so DAT reloading can be blocked while the scan uses it.
static std::atomic<bool> g_TemplateDevRunning{false};
#endif

static const int ID_REPLACE_PNG = 1001;
static const int ID_COPY_DID = 1002;
static const int ID_COPY_TEXTURE = 1003;
static const int ID_DISPLAY_UNSUPPORTED = 1004;
static const int ID_GO_TO_TEXTURE = 1005;
static const int ID_REMOVE_REPLACEMENT = 1006;
static const int ID_DISPLAY_REPLACED = 1007;
static const int ID_CLEAR_ENCOUNTERED = 1008;
static const int ID_ADD_CUSTOM_TAB = 1009;
static const int ID_RENAME_CUSTOM_TAB = 1010;
static const int ID_DELETE_CUSTOM_TAB = 1011;
static const int ID_REMOVE_FROM_CUSTOM_TAB = 1012;
static const int ID_EDIT_TEXTURE_NOTE = 1013;
static const int ID_FILTER_WIDTH = 1014;
static const int ID_FILTER_HEIGHT = 1015;
static const int ID_APPLY_SIZE_FILTER = 1016;
static const int ID_CLEAR_SIZE_FILTER = 1017;
static const int ID_IMPORT_UI_PACK = 1018;
static const int ID_EXPORT_UI_PACK = 1019;
static const int ID_OPEN_LIVE_UI = 1020;

static const int ID_LIVE_LOAD_SNAPSHOT = 1101;
static const int ID_LIVE_RELOAD = 1102;
static const int ID_LIVE_TOGGLE_REPLACEMENTS = 1103;
static const int ID_LIVE_SHOW_BOUNDS = 1104;
static const int ID_LIVE_REPLACE = 1105;
static const int ID_LIVE_REMOVE = 1106;
static const int ID_LIVE_LOCATE = 1107;
static const int ID_LIVE_MIRROR = 1108;
static const int ID_LIVE_BG_LOCATE = 1109;
static const int ID_LIVE_FG_LOCATE = 1110;

static const UINT WM_APP_LIVE_MIRROR_READY = WM_APP + 20;
static const UINT WM_APP_LIVE_MIRROR_STATUS = WM_APP + 21;

static const UINT ID_CUSTOM_TAB_MENU_BASE = 20000;

struct TextureRecord
{
    std::string did;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t pixelFormat = 0;
    std::uint32_t formatInfo = 0;
    std::uint32_t paletteDID = 0;
};

struct DatTextureEntry
{
    std::uint16_t flags = 0;
    std::uint16_t version = 0;
    std::uint32_t id = 0;
    std::int32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t rawDate = 0;
    std::int32_t iteration = 0;
};

struct DatScanStats
{
    std::uint32_t nodes = 0;
    std::uint32_t fileEntries = 0;
    std::uint32_t type06Entries = 0;
    std::uint32_t accepted = 0;
    std::uint32_t compressedSkipped = 0;
    std::uint32_t unsupportedSkipped = 0;
    std::uint32_t malformedSkipped = 0;
    std::uint64_t elapsedMs = 0;
};

struct CustomTab
{
    std::string name;
    std::unordered_set<std::string> dids;
};

static HWND g_TabControl = nullptr;
static HWND g_ListView = nullptr;
static HWND g_StatusText = nullptr;
static HWND g_DetailsTitle = nullptr;
static HWND g_DetailsText = nullptr;
static HWND g_LargePreview = nullptr;
static HWND g_ReplaceButton = nullptr;
static HWND g_CopyButton = nullptr;
static HWND g_CopyTextureButton = nullptr;
static HWND g_DisplayUnsupportedCheck = nullptr;
static HWND g_DisplayReplacedCheck = nullptr;
static HWND g_ClearEncounteredButton = nullptr;
static HWND g_AddCustomTabButton = nullptr;
static HWND g_FilterWidthEdit = nullptr;
static HWND g_FilterHeightEdit = nullptr;
static HWND g_ApplySizeFilterButton = nullptr;
static HWND g_ClearSizeFilterButton = nullptr;
static HWND g_ImportPackButton = nullptr;
static HWND g_ExportPackButton = nullptr;
static HWND g_LiveUiButton = nullptr;
static HWND g_ReplacementTitle = nullptr;
static HWND g_ReplacementPreview = nullptr;
static HWND g_InfoText = nullptr;
static HWND g_ProgressBar = nullptr;
static HBITMAP g_LargePreviewBitmap = nullptr;
static HBITMAP g_ReplacementPreviewBitmap = nullptr;
static int g_SelectedRow = -1;
static int g_SelectedListRow = -1;
static HIMAGELIST g_ThumbnailList = nullptr;
static IWICImagingFactory* g_WicFactory = nullptr;
static HWND g_MainWindow = nullptr;

static std::mutex g_PreviewMutex;
static std::condition_variable g_PreviewCv;
static std::deque<std::size_t> g_PreviewQueue;
static std::unordered_set<std::size_t> g_PreviewQueued;
static bool g_StopPreviewWorker = false;
static std::thread g_PreviewWorker;

static std::vector<TextureRecord> g_Textures;

// Direct-DAT texture source. The DAT is opened read-only with permissive
// sharing so Asheron's Call can use the same file concurrently.
static HANDLE g_DatFile = INVALID_HANDLE_VALUE;
static std::mutex g_DatReadMutex;
static std::wstring g_DatPath;
static std::string g_DatPathUtf8;
static std::uint32_t g_DatBlockSize = 0;
static std::uint32_t g_DatRootBlock = 0;
static std::uint64_t g_DatActualFileSize = 0;
static std::unordered_map<std::uint32_t, DatTextureEntry>
    g_DatTextureEntries;
static DatScanStats g_DatScanStats;
static std::string g_DatScanSummary;
static std::string g_DatLoadNotice;
static bool g_DatLoadNoticeError = false;

// Lightweight timing counters for direct DAT preview diagnostics.
static std::atomic<std::uint64_t> g_DatPreviewLoadCount{0};
static std::atomic<std::uint64_t> g_DatPreviewTotalMicros{0};
static std::atomic<std::uint64_t> g_DatPreviewMaxMicros{0};
static std::vector<std::string> g_TabPrefixes;
static std::string g_ActivePrefix;
static bool g_ReplacementsOnly = false;
static bool g_EncounteredOnly = false;
static bool g_DisplayUnsupported = false;
static bool g_DisplayReplaced = true;
static std::unordered_set<std::string> g_EncounteredDIDs;
static std::vector<CustomTab> g_CustomTabs;
static std::unordered_map<std::string, std::string> g_TextureNotes;
static int g_ActiveCustomTab = -1;
static bool g_FilterWidthEnabled = false;
static bool g_FilterHeightEnabled = false;
static std::uint32_t g_FilterWidth = 0;
static std::uint32_t g_FilterHeight = 0;

static std::string g_CurrentPackName;
static std::string g_CurrentPackAuthor;
static std::string g_CurrentPackDescription;
static std::wstring g_CurrentPackSource;

static std::wstring ToWide(
    const std::string& text)
{
    if (text.empty())
        return std::wstring();

    const int length =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            text.c_str(),
            static_cast<int>(text.size()),
            nullptr,
            0);

    if (length <= 0)
        return std::wstring();

    std::wstring result(
        static_cast<std::size_t>(length),
        L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        &result[0],
        length);

    return result;
}

static std::string FromWide(const std::wstring& text)
{
    if (text.empty())
        return std::string();

    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);

    if (length <= 0)
        return std::string();

    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        &result[0], length, nullptr, nullptr);
    return result;
}


static std::wstring JoinPath(
    const std::wstring& left,
    const std::wstring& right)
{
    if (left.empty())
        return right;

    if (right.empty())
        return left;

    if (left.back() == L'\\' ||
        left.back() == L'/')
    {
        return left + right;
    }

    return left + L"\\" + right;
}

static bool EnsureDirectoryExists(
    const std::wstring& path)
{
    if (path.empty())
        return false;

    const DWORD attributes =
        GetFileAttributesW(
            path.c_str());

    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        return
            (attributes &
             FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    if (CreateDirectoryW(
            path.c_str(),
            nullptr))
    {
        return true;
    }

    return
        GetLastError() ==
        ERROR_ALREADY_EXISTS;
}

static bool FileExists(
    const std::wstring& path)
{
    const DWORD attributes =
        GetFileAttributesW(
            path.c_str());

    return
        attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes &
         FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool GetExecutableDirectory(
    std::wstring& directory)
{
    directory.clear();

    std::vector<wchar_t> buffer(32768);

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(
                buffer.size()));

    if (length == 0 ||
        length >= buffer.size())
    {
        return false;
    }

    std::wstring path(
        buffer.data(),
        length);

    const std::size_t slash =
        path.find_last_of(
            L"\\/");

    if (slash == std::wstring::npos)
        return false;

    directory =
        path.substr(
            0,
            slash);

    return !directory.empty();
}

static bool InitializeAppPaths(
    std::wstring& error)
{
    error.clear();

    AppPaths paths;

    if (!GetExecutableDirectory(
            paths.installRoot))
    {
        error =
            L"Could not determine the AC Customs UI Manager "
            L"installation directory.";
        return false;
    }

    paths.converterPath =
        JoinPath(
            paths.installRoot,
            L"ACModernUIConverter.exe");

    paths.defaultsRoot =
        JoinPath(
            paths.installRoot,
            L"Defaults");

    paths.defaultCustomTabsPath =
        JoinPath(
            paths.defaultsRoot,
            L"custom_tabs.txt");

    paths.defaultTextureNotesPath =
        JoinPath(
            paths.defaultsRoot,
            L"texture_notes.txt");

    wchar_t localAppData[32768] = {};

    const DWORD localLength =
        GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            localAppData,
            static_cast<DWORD>(
                _countof(localAppData)));

    if (localLength == 0 ||
        localLength >= _countof(localAppData))
    {
        error =
            L"Could not resolve %LOCALAPPDATA%.";
        return false;
    }

    paths.localAppDataRoot =
        JoinPath(
            localAppData,
            L"ACCustoms");

    paths.managerRoot =
        JoinPath(
            paths.localAppDataRoot,
            L"Manager");

    paths.datPreferencePath =
        JoinPath(
            paths.managerRoot,
            L"client_dat_path.txt");

    paths.userRoot =
        JoinPath(
            paths.localAppDataRoot,
            L"User");

    paths.replacementDirectory =
        JoinPath(
            paths.userRoot,
            L"textures");

    paths.customTabsPath =
        JoinPath(
            paths.userRoot,
            L"custom_tabs.txt");

    paths.textureNotesPath =
        JoinPath(
            paths.userRoot,
            L"texture_notes.txt");

    paths.runtimeRoot =
        JoinPath(
            paths.localAppDataRoot,
            L"Runtime");

    paths.liveMirrorRoot =
        JoinPath(
            paths.runtimeRoot,
            L"LiveMirror");

    paths.snapshotsRoot =
        JoinPath(
            paths.runtimeRoot,
            L"Snapshots");

    paths.importStageDirectory =
        JoinPath(
            paths.runtimeRoot,
            L"ImportStage");

    paths.importBackupDirectory =
        JoinPath(
            paths.runtimeRoot,
            L"ImportBackup");

    paths.encounteredPath =
        JoinPath(
            paths.runtimeRoot,
            L"encountered_textures.csv");

    paths.logsRoot =
        JoinPath(
            paths.localAppDataRoot,
            L"Logs");

    const std::wstring directories[] =
    {
        paths.localAppDataRoot,
        paths.managerRoot,
        paths.userRoot,
        paths.replacementDirectory,
        paths.runtimeRoot,
        paths.liveMirrorRoot,
        paths.snapshotsRoot,
        paths.logsRoot
    };

    for (const std::wstring& directory :
         directories)
    {
        if (!EnsureDirectoryExists(
                directory))
        {
            error =
                L"Could not create AC Customs user-data directory:\r\n\r\n" +
                directory;
            return false;
        }
    }

    // Starter custom groups are copied once from the shipped Defaults folder.
    // Existing user customizations are never overwritten by an update.
    if (!FileExists(
            paths.customTabsPath) &&
        FileExists(
            paths.defaultCustomTabsPath))
    {
        if (!CopyFileW(
                paths.defaultCustomTabsPath.c_str(),
                paths.customTabsPath.c_str(),
                TRUE) &&
            GetLastError() !=
                ERROR_FILE_EXISTS)
        {
            error =
                L"Could not initialize the default custom texture groups.";
            return false;
        }
    }

    // Starter texture notes use the same first-run-only behavior. A user's
    // edited notes always win over the shipped defaults on later updates.
    if (!FileExists(
            paths.textureNotesPath) &&
        FileExists(
            paths.defaultTextureNotesPath))
    {
        if (!CopyFileW(
                paths.defaultTextureNotesPath.c_str(),
                paths.textureNotesPath.c_str(),
                TRUE) &&
            GetLastError() !=
                ERROR_FILE_EXISTS)
        {
            error =
                L"Could not initialize the default texture notes.";
            return false;
        }
    }

    g_AppPaths =
        std::move(paths);

    return true;
}


static std::wstring GetDatPreferencePath(
    bool)
{
    return
        g_AppPaths.datPreferencePath;
}

static bool SaveRememberedDatPath(
    const std::wstring& path)
{
    const std::wstring settingsPath =
        GetDatPreferencePath(true);

    if (settingsPath.empty())
        return false;

    const std::string utf8 =
        FromWide(path);

    HANDLE file =
        CreateFileW(
            settingsPath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    const DWORD byteCount =
        static_cast<DWORD>(
            utf8.size());

    DWORD written = 0;

    const BOOL ok =
        WriteFile(
            file,
            utf8.data(),
            byteCount,
            &written,
            nullptr);

    CloseHandle(file);

    return
        ok != FALSE &&
        written == byteCount;
}

static bool LoadRememberedDatPath(
    std::wstring& path)
{
    path.clear();

    const std::wstring settingsPath =
        GetDatPreferencePath(false);

    if (settingsPath.empty())
        return false;

    HANDLE file =
        CreateFileW(
            settingsPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size = {};

    if (!GetFileSizeEx(
            file,
            &size) ||
        size.QuadPart <= 0 ||
        size.QuadPart > 131072)
    {
        CloseHandle(file);
        return false;
    }

    std::string utf8(
        static_cast<std::size_t>(
            size.QuadPart),
        '\0');

    const DWORD byteCount =
        static_cast<DWORD>(
            utf8.size());

    DWORD read = 0;

    const BOOL ok =
        ReadFile(
            file,
            utf8.data(),
            byteCount,
            &read,
            nullptr);

    CloseHandle(file);

    if (ok == FALSE ||
        read != byteCount)
    {
        return false;
    }

    while (!utf8.empty() &&
           (utf8.back() == '\r' ||
            utf8.back() == '\n' ||
            utf8.back() == '\0'))
    {
        utf8.pop_back();
    }

    if (utf8.empty())
        return false;

    path =
        ToWide(utf8);

    return !path.empty();
}

static bool ParseDecimal(
    const std::string& text,
    std::uint32_t& value)
{
    try
    {
        std::size_t consumed = 0;

        const unsigned long parsed =
            std::stoul(
                text,
                &consumed,
                10);

        if (consumed != text.size())
            return false;

        value =
            static_cast<std::uint32_t>(
                parsed);

        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool ParseHex(
    const std::string& text,
    std::uint32_t& value)
{
    try
    {
        std::size_t consumed = 0;

        const unsigned long parsed =
            std::stoul(
                text,
                &consumed,
                16);

        if (consumed != text.size())
            return false;

        value =
            static_cast<std::uint32_t>(
                parsed);

        return true;
    }
    catch (...)
    {
        return false;
    }
}

static std::uint16_t DatReadLe16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8);
}

static std::uint32_t DatReadLe32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

static std::int32_t DatReadLeI32(const unsigned char* p)
{
    return static_cast<std::int32_t>(DatReadLe32(p));
}

static void CloseDatFile()
{
    std::lock_guard<std::mutex> lock(g_DatReadMutex);

    if (g_DatFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_DatFile);
        g_DatFile = INVALID_HANDLE_VALUE;
    }

    g_DatPath.clear();
    g_DatPathUtf8.clear();
    g_DatBlockSize = 0;
    g_DatRootBlock = 0;
    g_DatActualFileSize = 0;
    g_DatTextureEntries.clear();
}

static bool DatReadAt(
    std::uint64_t offset,
    void* destination,
    std::size_t byteCount)
{
    if (g_DatFile == INVALID_HANDLE_VALUE ||
        destination == nullptr ||
        byteCount == 0 ||
        offset > g_DatActualFileSize ||
        byteCount > g_DatActualFileSize - offset)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_DatReadMutex);

    LARGE_INTEGER position = {};
    position.QuadPart = static_cast<LONGLONG>(offset);

    if (!SetFilePointerEx(
            g_DatFile,
            position,
            nullptr,
            FILE_BEGIN))
    {
        return false;
    }

    unsigned char* output =
        static_cast<unsigned char*>(destination);

    std::size_t remaining = byteCount;

    while (remaining != 0)
    {
        const DWORD request = static_cast<DWORD>(
            std::min<std::size_t>(
                remaining,
                static_cast<std::size_t>(0x40000000u)));

        DWORD read = 0;
        if (!ReadFile(
                g_DatFile,
                output,
                request,
                &read,
                nullptr) ||
            read == 0)
        {
            return false;
        }

        output += read;
        remaining -= read;
    }

    return true;
}

static bool DatReadBlockChain(
    std::int32_t startingBlock,
    void* destination,
    std::size_t byteCount)
{
    if (startingBlock <= 0 ||
        destination == nullptr ||
        byteCount == 0 ||
        g_DatBlockSize <= 4)
    {
        return false;
    }

    unsigned char* output =
        static_cast<unsigned char*>(destination);

    const std::size_t payloadPerBlock =
        static_cast<std::size_t>(g_DatBlockSize - 4u);

    std::size_t totalRead = 0;
    std::int32_t currentBlock = startingBlock;
    std::uint32_t guard = 0;

    while (currentBlock > 0 && totalRead < byteCount)
    {
        if (++guard > 1048576u)
            return false;

        const std::uint64_t blockOffset =
            static_cast<std::uint32_t>(currentBlock);

        if (blockOffset + 4u > g_DatActualFileSize)
            return false;

        std::uint32_t nextBlock = 0;
        if (!DatReadAt(
                blockOffset,
                &nextBlock,
                sizeof(nextBlock)))
        {
            return false;
        }

        const std::size_t bytesThisBlock =
            min(
                payloadPerBlock,
                byteCount - totalRead);

        if (!DatReadAt(
                blockOffset + 4u,
                output + totalRead,
                bytesThisBlock))
        {
            return false;
        }

        totalRead += bytesThisBlock;

        if (totalRead >= byteCount)
            break;

        currentBlock =
            static_cast<std::int32_t>(nextBlock);
    }

    return totalRead == byteCount;
}

struct DatBTreeNodeView
{
    std::vector<std::int32_t> branches;
    std::vector<DatTextureEntry> files;
};

static bool DatReadBTreeNode(
    std::int32_t blockOffset,
    DatBTreeNodeView& node)
{
    node = DatBTreeNodeView{};

    constexpr std::size_t kNodeSize = 1720u;
    constexpr std::size_t kMaxBranches = 62u;
    constexpr std::size_t kMaxFiles = 61u;
    constexpr std::size_t kFileEntrySize = 24u;

    std::vector<unsigned char> bytes(kNodeSize);
    if (!DatReadBlockChain(
            blockOffset,
            bytes.data(),
            bytes.size()))
    {
        return false;
    }

    const std::int32_t fileCount =
        DatReadLeI32(bytes.data() + kMaxBranches * 4u);

    if (fileCount < 0 ||
        fileCount > static_cast<std::int32_t>(kMaxFiles))
    {
        return false;
    }

    std::size_t discoveredBranches = 0;
    std::int32_t previous = 0;
    bool reachedEnd = false;

    for (std::size_t i = 0; i < kMaxBranches; ++i)
    {
        const std::int32_t branch =
            DatReadLeI32(bytes.data() + i * 4u);

        if (branch == 0 ||
            branch == previous ||
            static_cast<std::uint32_t>(branch) == 0xCDCDCDCDu)
        {
            reachedEnd = true;
        }

        if (!reachedEnd)
        {
            node.branches.push_back(branch);
            ++discoveredBranches;
            previous = branch;
        }
    }

    const std::size_t filesOffset =
        kMaxBranches * 4u + 4u;

    node.files.reserve(
        static_cast<std::size_t>(fileCount));

    for (std::int32_t i = 0; i < fileCount; ++i)
    {
        const unsigned char* entryBytes =
            bytes.data() +
            filesOffset +
            static_cast<std::size_t>(i) *
                kFileEntrySize;

        DatTextureEntry entry;
        entry.flags = DatReadLe16(entryBytes + 0u);
        entry.version = DatReadLe16(entryBytes + 2u);
        entry.id = DatReadLe32(entryBytes + 4u);
        entry.offset = DatReadLeI32(entryBytes + 8u);
        entry.size = DatReadLe32(entryBytes + 12u);
        entry.rawDate = DatReadLe32(entryBytes + 16u);
        entry.iteration = DatReadLeI32(entryBytes + 20u);

        node.files.push_back(entry);
    }

    // Retail DAT nodes are either leaves (no branches) or have fileCount + 1
    // valid branches.  Ignore any unused values after that logical boundary.
    if (discoveredBranches != 0)
    {
        const std::size_t wanted =
            static_cast<std::size_t>(fileCount) + 1u;

        if (node.branches.size() < wanted)
            return false;

        node.branches.resize(wanted);
    }

    return true;
}

static bool DatParseRenderSurfaceHeader(
    const DatTextureEntry& entry,
    TextureRecord& texture)
{
    // Compressed records use zlib.  This first experiment deliberately skips
    // them so the Manager has no new runtime dependency; scan statistics make
    // the omission visible immediately if the user's DAT contains any.
    if ((entry.flags & 0x0001u) != 0)
        return false;

    if (entry.offset <= 0 || entry.size < 24u)
        return false;

    unsigned char header[24] = {};
    if (!DatReadBlockChain(
            entry.offset,
            header,
            sizeof(header)))
    {
        return false;
    }

    const std::uint32_t serializedDid =
        DatReadLe32(header + 0u);
    const std::uint32_t dataCategory =
        DatReadLe32(header + 4u);
    const std::int32_t width =
        DatReadLeI32(header + 8u);
    const std::int32_t height =
        DatReadLeI32(header + 12u);
    const std::uint32_t pixelFormat =
        DatReadLe32(header + 16u);
    const std::int32_t sourceLength =
        DatReadLeI32(header + 20u);

    if (serializedDid != entry.id ||
        width <= 0 ||
        height <= 0 ||
        sourceLength <= 0)
    {
        return false;
    }

    std::uint32_t bytesPerPixel = 0;
    if (pixelFormat == 0x14u)
        bytesPerPixel = 3u;
    else if (pixelFormat == 0x15u)
        bytesPerPixel = 4u;
    else
        return false;

    const std::uint64_t expected =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) *
        bytesPerPixel;

    if (expected !=
            static_cast<std::uint64_t>(sourceLength) ||
        24ull + expected > entry.size)
    {
        return false;
    }

    char didText[16] = {};
    sprintf_s(
        didText,
        "%08X",
        entry.id);

    texture = TextureRecord{};
    texture.did = didText;
    texture.width = static_cast<std::uint32_t>(width);
    texture.height = static_cast<std::uint32_t>(height);
    texture.imageSize = static_cast<std::uint32_t>(sourceLength);
    texture.pixelFormat = pixelFormat;

    // The serialized RenderSurface exposes DataCategory here, not the runtime
    // RenderSurface's internal +0x6C field. Preserve it as useful source
    // metadata for this experimental DAT-backed catalog.
    texture.formatInfo = dataCategory;
    texture.paletteDID = 0;
    return true;
}

static bool DatScanBTreeRecursive(
    std::int32_t blockOffset,
    std::unordered_set<std::int32_t>& visited,
    std::uint32_t depth)
{
    if (blockOffset <= 0 ||
        depth > 64u ||
        !visited.insert(blockOffset).second)
    {
        return blockOffset > 0;
    }

    DatBTreeNodeView node;
    if (!DatReadBTreeNode(blockOffset, node))
        return false;

    ++g_DatScanStats.nodes;

    const bool leaf = node.branches.empty();

    for (std::size_t i = 0;
         i < node.files.size();
         ++i)
    {
        if (!leaf &&
            i < node.branches.size() &&
            !DatScanBTreeRecursive(
                node.branches[i],
                visited,
                depth + 1u))
        {
            return false;
        }

        const DatTextureEntry& entry =
            node.files[i];

        ++g_DatScanStats.fileEntries;

        if ((entry.id & 0xFF000000u) !=
            0x06000000u)
        {
            continue;
        }

        ++g_DatScanStats.type06Entries;

        if ((entry.flags & 0x0001u) != 0)
        {
            ++g_DatScanStats.compressedSkipped;
            continue;
        }

        TextureRecord texture;
        if (!DatParseRenderSurfaceHeader(
                entry,
                texture))
        {
            // Distinguish known unsupported pixel formats from malformed data.
            unsigned char header[24] = {};
            if (entry.size >= sizeof(header) &&
                entry.offset > 0 &&
                DatReadBlockChain(
                    entry.offset,
                    header,
                    sizeof(header)))
            {
                const std::uint32_t format =
                    DatReadLe32(header + 16u);

                if (format != 0x14u &&
                    format != 0x15u)
                {
                    ++g_DatScanStats.unsupportedSkipped;
                }
                else
                {
                    ++g_DatScanStats.malformedSkipped;
                }
            }
            else
            {
                ++g_DatScanStats.malformedSkipped;
            }

            continue;
        }

        g_DatTextureEntries[entry.id] = entry;
        g_Textures.push_back(std::move(texture));
        ++g_DatScanStats.accepted;
    }

    if (!leaf &&
        node.branches.size() ==
            node.files.size() + 1u)
    {
        if (!DatScanBTreeRecursive(
                node.branches.back(),
                visited,
                depth + 1u))
        {
            return false;
        }
    }

    return true;
}

static bool ChooseDatFile(
    HWND owner,
    std::wstring& path)
{
    wchar_t fileName[32768] = {};

    if (!g_DatPath.empty() &&
        g_DatPath.size() < _countof(fileName))
    {
        wcscpy_s(
            fileName,
            g_DatPath.c_str());
    }
    else
    {
        const wchar_t* commonPaths[] =
        {
            L"C:\\Turbine\\Asheron's Call\\client.dat",
            L"C:\\Turbine\\Asheron's Call\\client_portal.dat"
        };

        for (const wchar_t* candidate : commonPaths)
        {
            const DWORD attributes =
                GetFileAttributesW(candidate);

            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                wcscpy_s(
                    fileName,
                    candidate);
                break;
            }
        }
    }

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"Asheron's Call DAT files (*.dat)\0*.dat\0"
        L"All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile =
        static_cast<DWORD>(
            _countof(fileName));
    dialog.lpstrTitle =
        L"Select the Asheron's Call client DAT";
    dialog.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY |
        OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"dat";

    if (!GetOpenFileNameW(&dialog))
        return false;

    path = fileName;
    return true;
}

static bool OpenAndScanDatPath(
    const std::wstring& selected,
    std::wstring& error)
{
    error.clear();
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
    if (g_TemplateDevRunning.load())
    {
        error = L"Cannot reload client DAT during a developer template scan.";
        return false;
    }
#endif

    CloseDatFile();

    g_Textures.clear();
    g_DatScanStats = DatScanStats{};
    g_DatScanSummary.clear();

    g_DatPreviewLoadCount.store(
        0,
        std::memory_order_relaxed);

    g_DatPreviewTotalMicros.store(
        0,
        std::memory_order_relaxed);

    g_DatPreviewMaxMicros.store(
        0,
        std::memory_order_relaxed);

    HANDLE file = CreateFileW(
        selected.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ |
            FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_RANDOM_ACCESS,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        error =
            L"Could not open the selected DAT for read-only access.\r\n\r\n"
            L"Windows error: " +
            std::to_wstring(GetLastError());
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(file, &fileSize) ||
        fileSize.QuadPart < 400)
    {
        CloseHandle(file);
        error =
            L"The selected file is too small to be an AC DAT.";
        return false;
    }

    g_DatFile = file;
    g_DatActualFileSize =
        static_cast<std::uint64_t>(
            fileSize.QuadPart);

    unsigned char header[400] = {};
    if (!DatReadAt(
            0u,
            header,
            sizeof(header)))
    {
        CloseDatFile();
        error =
            L"Could not read the AC DAT header.";
        return false;
    }

    const std::uint32_t magic =
        DatReadLe32(header + 320u);
    const std::uint32_t blockSize =
        DatReadLe32(header + 324u);
    const std::uint32_t databaseType =
        DatReadLe32(header + 332u);
    const std::int32_t rootBlock =
        DatReadLeI32(header + 352u);

    if (magic != 0x00005442u)
    {
        CloseDatFile();
        error =
            L"The selected file does not have the AC retail DAT signature.";
        return false;
    }

    if (databaseType != 1u)
    {
        CloseDatFile();
        error =
            L"The selected DAT is not a Portal/client DAT (database type 1).";
        return false;
    }

    if (blockSize <= 4u ||
        blockSize > 1024u * 1024u ||
        rootBlock <= 0)
    {
        CloseDatFile();
        error =
            L"The selected DAT has an invalid block size or root block.";
        return false;
    }

    g_DatPath = selected;
    g_DatPathUtf8 =
        FromWide(selected);

    g_DatBlockSize =
        blockSize;

    g_DatRootBlock =
        static_cast<std::uint32_t>(
            rootBlock);

    g_DatTextureEntries.clear();

    const auto started =
        std::chrono::steady_clock::now();

    std::unordered_set<std::int32_t> visited;

    const bool scanned =
        DatScanBTreeRecursive(
            rootBlock,
            visited,
            0u);

    const auto finished =
        std::chrono::steady_clock::now();

    g_DatScanStats.elapsedMs =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    finished - started).count());

    if (!scanned)
    {
        CloseDatFile();
        g_Textures.clear();

        error =
            L"The Manager could not traverse the selected DAT's file index.";
        return false;
    }

    if (g_Textures.empty())
    {
        const std::uint32_t compressed =
            g_DatScanStats.compressedSkipped;

        CloseDatFile();
        g_Textures.clear();

        error =
            L"No uncompressed BGR/BGRA 0x06 textures were found in this DAT.";

        if (compressed != 0)
        {
            error +=
                L"\r\n\r\nCompressed 0x06 entries were skipped by this experimental build: " +
                std::to_wstring(compressed);
        }

        return false;
    }

    std::sort(
        g_Textures.begin(),
        g_Textures.end(),
        [](const TextureRecord& a,
           const TextureRecord& b)
        {
            return a.did < b.did;
        });

    std::ostringstream summary;
    summary
        << "DAT scan: "
        << g_DatScanStats.accepted
        << " BGR/BGRA textures in "
        << g_DatScanStats.elapsedMs
        << " ms"
        << " | compressed skipped="
        << g_DatScanStats.compressedSkipped
        << " | other formats="
        << g_DatScanStats.unsupportedSkipped
        << " | malformed="
        << g_DatScanStats.malformedSkipped;

    g_DatScanSummary =
        summary.str();

    return true;
}

static bool SelectAndLoadDat(
    HWND owner,
    std::wstring& error)
{
    error.clear();

    std::wstring selected;

    if (!ChooseDatFile(
            owner,
            selected))
    {
        // Cancelling the Windows picker is not an error.
        return false;
    }

    const std::wstring previousPath =
        g_DatPath;

    if (!OpenAndScanDatPath(
            selected,
            error))
    {
        // If the user was changing from an already working DAT, restore it so
        // a bad selection cannot destroy the current Manager session.
        if (!previousPath.empty())
        {
            std::wstring restoreError;
            OpenAndScanDatPath(
                previousPath,
                restoreError);
        }

        return false;
    }

    SaveRememberedDatPath(
        selected);

    g_DatLoadNotice.clear();
    g_DatLoadNoticeError = false;

    return true;
}

static bool LoadTextureCatalog()
{
    std::wstring remembered;

    if (!LoadRememberedDatPath(
            remembered))
    {
        g_DatLoadNotice =
            "Select your Asheron's Call client DAT to build the texture catalog.";
        g_DatLoadNoticeError = false;
        return false;
    }

    std::wstring error;

    if (!OpenAndScanDatPath(
            remembered,
            error))
    {
        g_DatLoadNotice =
            "The previously selected DAT could not be loaded. "
            "Choose the DAT again to continue.";

        if (!error.empty())
        {
            g_DatLoadNotice +=
                "  " +
                FromWide(error);
        }

        g_DatLoadNoticeError = true;
        return false;
    }

    g_DatLoadNotice.clear();
    g_DatLoadNoticeError = false;

    return true;
}


static bool LoadDatTexturePixels(
    const TextureRecord& texture,
    std::vector<unsigned char>& bgra)
{
    bgra.clear();

    std::uint32_t did = 0;
    if (!ParseHex(texture.did, did))
        return false;

    const auto found =
        g_DatTextureEntries.find(did);

    if (found == g_DatTextureEntries.end())
        return false;

    const DatTextureEntry entry =
        found->second;

    if ((entry.flags & 0x0001u) != 0 ||
        entry.size < 24u)
    {
        return false;
    }

    const auto started =
        std::chrono::steady_clock::now();

    std::vector<unsigned char> serialized(
        entry.size);

    if (!DatReadBlockChain(
            entry.offset,
            serialized.data(),
            serialized.size()))
    {
        return false;
    }

    const std::uint32_t serializedDid =
        DatReadLe32(serialized.data() + 0u);
    const std::int32_t width =
        DatReadLeI32(serialized.data() + 8u);
    const std::int32_t height =
        DatReadLeI32(serialized.data() + 12u);
    const std::uint32_t format =
        DatReadLe32(serialized.data() + 16u);
    const std::int32_t sourceLength =
        DatReadLeI32(serialized.data() + 20u);

    if (serializedDid != did ||
        width != static_cast<std::int32_t>(texture.width) ||
        height != static_cast<std::int32_t>(texture.height) ||
        format != texture.pixelFormat ||
        sourceLength !=
            static_cast<std::int32_t>(texture.imageSize) ||
        sourceLength <= 0 ||
        24ull + static_cast<std::uint64_t>(sourceLength) >
            serialized.size())
    {
        return false;
    }

    const unsigned char* source =
        serialized.data() + 24u;

    const std::size_t pixelCount =
        static_cast<std::size_t>(texture.width) *
        static_cast<std::size_t>(texture.height);

    bgra.resize(pixelCount * 4u);

    if (texture.pixelFormat == 0x15u)
    {
        memcpy(
            bgra.data(),
            source,
            bgra.size());
    }
    else if (texture.pixelFormat == 0x14u)
    {
        for (std::size_t i = 0;
             i < pixelCount;
             ++i)
        {
            bgra[i * 4u + 0u] =
                source[i * 3u + 0u];
            bgra[i * 4u + 1u] =
                source[i * 3u + 1u];
            bgra[i * 4u + 2u] =
                source[i * 3u + 2u];
            bgra[i * 4u + 3u] = 255u;
        }
    }
    else
    {
        bgra.clear();
        return false;
    }

    const auto finished =
        std::chrono::steady_clock::now();

    const std::uint64_t micros =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                    finished - started).count());

    g_DatPreviewLoadCount.fetch_add(
        1u,
        std::memory_order_relaxed);

    g_DatPreviewTotalMicros.fetch_add(
        micros,
        std::memory_order_relaxed);

    std::uint64_t maximum =
        g_DatPreviewMaxMicros.load(
            std::memory_order_relaxed);

    while (micros > maximum &&
           !g_DatPreviewMaxMicros.compare_exchange_weak(
                maximum,
                micros,
                std::memory_order_relaxed))
    {
    }

    return true;
}

static std::wstring FormatName(
    std::uint32_t pixelFormat)
{
    switch (pixelFormat)
    {
        case 0x14:
            return L"BGR";

        case 0x15:
            return L"BGRA";

        case 0x65:
            return L"INDEX16";

        default:
        {
            wchar_t buffer[32] = {};

            swprintf_s(
                buffer,
                L"0x%08X",
                pixelFormat);

            return buffer;
        }
    }
}

static bool IsPreviewable(
    const TextureRecord& texture)
{
    return
        texture.pixelFormat == 0x14 ||
        texture.pixelFormat == 0x15;
}


static bool LoadDatPreviewCanvasPixels(
    const TextureRecord& texture,
    int canvasSize,
    std::vector<unsigned char>& canvas)
{
    canvas.clear();

    if (!IsPreviewable(texture) ||
        canvasSize <= 0)
    {
        return false;
    }

    std::vector<unsigned char> source;
    if (!LoadDatTexturePixels(texture, source))
        return false;

    if (texture.width == 0 ||
        texture.height == 0 ||
        source.size() !=
            static_cast<std::size_t>(texture.width) *
            static_cast<std::size_t>(texture.height) *
            4u)
    {
        return false;
    }

    int scaledWidth = canvasSize;
    int scaledHeight = canvasSize;

    if (texture.width >= texture.height)
    {
        scaledHeight = max(
            1,
            static_cast<int>(
                (static_cast<unsigned long long>(texture.height) *
                 static_cast<unsigned long long>(canvasSize)) /
                texture.width));
    }
    else
    {
        scaledWidth = max(
            1,
            static_cast<int>(
                (static_cast<unsigned long long>(texture.width) *
                 static_cast<unsigned long long>(canvasSize)) /
                texture.height));
    }

    canvas.assign(
        static_cast<std::size_t>(canvasSize) *
            static_cast<std::size_t>(canvasSize) *
            4u,
        0u);

    const int offsetX =
        (canvasSize - scaledWidth) / 2;
    const int offsetY =
        (canvasSize - scaledHeight) / 2;

    // Bilinear scale entirely in memory. This preserves the original alpha
    // channel and avoids the 32-bit BI_RGB alpha loss we previously saw when
    // GDI performed the scaling step.
    for (int y = 0; y < scaledHeight; ++y)
    {
        const double sourceYUnclamped =
            ((static_cast<double>(y) + 0.5) *
             static_cast<double>(texture.height) /
             static_cast<double>(scaledHeight)) -
            0.5;

        const double sourceY =
            max(
                0.0,
                min(
                    static_cast<double>(texture.height - 1u),
                    sourceYUnclamped));

        const int y0 =
            static_cast<int>(std::floor(sourceY));
        const int y1 = min(
            static_cast<int>(texture.height) - 1,
            y0 + 1);
        const double fy =
            sourceY - static_cast<double>(y0);

        for (int x = 0; x < scaledWidth; ++x)
        {
            const double sourceXUnclamped =
                ((static_cast<double>(x) + 0.5) *
                 static_cast<double>(texture.width) /
                 static_cast<double>(scaledWidth)) -
                0.5;

            const double sourceX =
                max(
                    0.0,
                    min(
                        static_cast<double>(texture.width - 1u),
                        sourceXUnclamped));

            const int x0 =
                static_cast<int>(std::floor(sourceX));
            const int x1 = min(
                static_cast<int>(texture.width) - 1,
                x0 + 1);
            const double fx =
                sourceX - static_cast<double>(x0);

            const unsigned char* p00 =
                &source[(static_cast<std::size_t>(y0) * texture.width +
                         static_cast<std::size_t>(x0)) * 4u];
            const unsigned char* p10 =
                &source[(static_cast<std::size_t>(y0) * texture.width +
                         static_cast<std::size_t>(x1)) * 4u];
            const unsigned char* p01 =
                &source[(static_cast<std::size_t>(y1) * texture.width +
                         static_cast<std::size_t>(x0)) * 4u];
            const unsigned char* p11 =
                &source[(static_cast<std::size_t>(y1) * texture.width +
                         static_cast<std::size_t>(x1)) * 4u];

            unsigned char* out =
                &canvas[
                    (static_cast<std::size_t>(offsetY + y) *
                         static_cast<std::size_t>(canvasSize) +
                     static_cast<std::size_t>(offsetX + x)) *
                    4u];

            for (int channel = 0;
                 channel < 4;
                 ++channel)
            {
                const double top =
                    static_cast<double>(p00[channel]) *
                        (1.0 - fx) +
                    static_cast<double>(p10[channel]) * fx;
                const double bottom =
                    static_cast<double>(p01[channel]) *
                        (1.0 - fx) +
                    static_cast<double>(p11[channel]) * fx;

                const double value =
                    top * (1.0 - fy) +
                    bottom * fy;

                out[channel] =
                    static_cast<unsigned char>(
                        max(0.0, min(255.0, value + 0.5)));
            }
        }
    }

    return true;
}

static HBITMAP LoadPreviewBitmap(
    const TextureRecord& texture,
    int canvasSize)
{
    std::vector<unsigned char> canvas;
    if (!LoadDatPreviewCanvasPixels(
            texture,
            canvasSize,
            canvas))
    {
        return nullptr;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = canvasSize;
    bitmapInfo.bmiHeader.biHeight = -canvasSize;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDC = GetDC(nullptr);

    HBITMAP bitmap = CreateDIBSection(
        screenDC,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0);

    ReleaseDC(nullptr, screenDC);

    if (bitmap == nullptr || bits == nullptr)
    {
        if (bitmap != nullptr)
            DeleteObject(bitmap);
        return nullptr;
    }

    memcpy(
        bits,
        canvas.data(),
        canvas.size());

    return bitmap;
}

static std::wstring ReplacementRawPath(
    const TextureRecord& texture)
{
    return
        g_AppPaths.replacementDirectory +
        L"\\" +
        ToWide(texture.did) +
        L".rgb";
}

static bool HasReplacement(
    const TextureRecord& texture)
{
    const std::wstring path =
        ReplacementRawPath(texture);

    const DWORD attributes =
        GetFileAttributesW(path.c_str());

    return
        attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}


static HBITMAP LoadReplacementRawBitmap(
    const TextureRecord& texture,
    int canvasSize)
{
    if (!IsPreviewable(texture) ||
        texture.width == 0 ||
        texture.height == 0)
    {
        return nullptr;
    }

    const std::uint32_t bytesPerPixel =
        texture.pixelFormat == 0x15 ? 4u : 3u;

    const unsigned long long expectedSize =
        static_cast<unsigned long long>(texture.width) *
        static_cast<unsigned long long>(texture.height) *
        bytesPerPixel;

    if (expectedSize != texture.imageSize)
        return nullptr;

    const std::wstring path =
        ReplacementRawPath(texture);

    std::ifstream input(
        path,
        std::ios::binary);

    if (!input.is_open())
        return nullptr;

    std::vector<BYTE> raw(
        texture.imageSize);

    input.read(
        reinterpret_cast<char*>(raw.data()),
        static_cast<std::streamsize>(raw.size()));

    if (input.gcount() !=
        static_cast<std::streamsize>(raw.size()))
    {
        return nullptr;
    }

    input.peek();

    if (!input.eof())
        return nullptr;

    // AC PFID_R8G8B8 replacement files are tightly packed: width * 3 bytes
    // per row. Windows 24-bit BI_RGB DIBs, however, require each scanline to
    // be DWORD-aligned. Build a padded display-only copy before StretchDIBits.
    // The .rgb replacement itself remains tightly packed and is not modified.
    std::vector<BYTE> dibSource;
    const BYTE* sourcePixels = raw.data();

    if (texture.pixelFormat == 0x14)
    {
        const std::size_t sourceStride =
            static_cast<std::size_t>(texture.width) * 3u;
        const std::size_t dibStride =
            (sourceStride + 3u) & ~static_cast<std::size_t>(3u);

        if (dibStride != sourceStride)
        {
            dibSource.assign(
                dibStride * static_cast<std::size_t>(texture.height),
                0);

            for (std::uint32_t y = 0; y < texture.height; ++y)
            {
                memcpy(
                    dibSource.data() +
                        static_cast<std::size_t>(y) * dibStride,
                    raw.data() +
                        static_cast<std::size_t>(y) * sourceStride,
                    sourceStride);
            }

            sourcePixels = dibSource.data();
        }
    }

    BITMAPINFO sourceInfo = {};
    sourceInfo.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);
    sourceInfo.bmiHeader.biWidth =
        static_cast<LONG>(texture.width);
    sourceInfo.bmiHeader.biHeight =
        -static_cast<LONG>(texture.height);
    sourceInfo.bmiHeader.biPlanes = 1;
    sourceInfo.bmiHeader.biBitCount =
        static_cast<WORD>(bytesPerPixel * 8);
    sourceInfo.bmiHeader.biCompression =
        BI_RGB;

    BITMAPINFO destinationInfo = {};
    destinationInfo.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);
    destinationInfo.bmiHeader.biWidth =
        canvasSize;
    destinationInfo.bmiHeader.biHeight =
        -static_cast<LONG>(canvasSize);
    destinationInfo.bmiHeader.biPlanes = 1;
    destinationInfo.bmiHeader.biBitCount = 32;
    destinationInfo.bmiHeader.biCompression =
        BI_RGB;

    void* destinationBits = nullptr;

    HDC screenDC =
        GetDC(nullptr);

    HBITMAP destinationBitmap =
        CreateDIBSection(
            screenDC,
            &destinationInfo,
            DIB_RGB_COLORS,
            &destinationBits,
            nullptr,
            0);

    if (destinationBitmap == nullptr ||
        destinationBits == nullptr)
    {
        if (destinationBitmap != nullptr)
            DeleteObject(destinationBitmap);

        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }

    ZeroMemory(
        destinationBits,
        static_cast<std::size_t>(canvasSize) *
            static_cast<std::size_t>(canvasSize) *
            4);

    HDC destinationDC =
        CreateCompatibleDC(screenDC);

    HGDIOBJ oldDestination =
        SelectObject(
            destinationDC,
            destinationBitmap);

    UINT scaledWidth =
        static_cast<UINT>(canvasSize);

    UINT scaledHeight =
        static_cast<UINT>(canvasSize);

    if (texture.width >= texture.height)
    {
        scaledHeight =
            static_cast<UINT>(
                (static_cast<unsigned long long>(
                    texture.height) *
                 static_cast<unsigned long long>(
                    canvasSize)) /
                texture.width);

        if (scaledHeight == 0)
            scaledHeight = 1;
    }
    else
    {
        scaledWidth =
            static_cast<UINT>(
                (static_cast<unsigned long long>(
                    texture.width) *
                 static_cast<unsigned long long>(
                    canvasSize)) /
                texture.height);

        if (scaledWidth == 0)
            scaledWidth = 1;
    }

    const int offsetX =
        (canvasSize -
         static_cast<int>(scaledWidth)) /
        2;

    const int offsetY =
        (canvasSize -
         static_cast<int>(scaledHeight)) /
        2;

    SetStretchBltMode(
        destinationDC,
        HALFTONE);

    const int copied =
        StretchDIBits(
            destinationDC,
            offsetX,
            offsetY,
            static_cast<int>(scaledWidth),
            static_cast<int>(scaledHeight),
            0,
            0,
            static_cast<int>(texture.width),
            static_cast<int>(texture.height),
            sourcePixels,
            &sourceInfo,
            DIB_RGB_COLORS,
            SRCCOPY);

    SelectObject(
        destinationDC,
        oldDestination);

    DeleteDC(
        destinationDC);

    ReleaseDC(
        nullptr,
        screenDC);

    if (copied == GDI_ERROR)
    {
        DeleteObject(
            destinationBitmap);

        return nullptr;
    }

    return destinationBitmap;
}

static void ClearReplacementPreview()
{
    if (g_ReplacementPreview != nullptr)
    {
        ShowWindow(
            g_ReplacementPreview,
            SW_HIDE);

        SendMessageW(
            g_ReplacementPreview,
            STM_SETIMAGE,
            IMAGE_BITMAP,
            0);
    }

    if (g_ReplacementPreviewBitmap != nullptr)
    {
        DeleteObject(
            g_ReplacementPreviewBitmap);

        g_ReplacementPreviewBitmap = nullptr;
    }
}

static void UpdateReplacementPreview(
    const TextureRecord& texture)
{
    // Always clear the previous selection first. This prevents a stale
    // replacement image remaining visible when the newly selected DID
    // has no replacement.
    ClearReplacementPreview();

    g_ReplacementPreviewBitmap =
        LoadReplacementRawBitmap(
            texture,
            192);

    if (g_ReplacementPreviewBitmap != nullptr)
    {
        SendMessageW(
            g_ReplacementPreview,
            STM_SETIMAGE,
            IMAGE_BITMAP,
            reinterpret_cast<LPARAM>(
                g_ReplacementPreviewBitmap));

        ShowWindow(
            g_ReplacementPreview,
            SW_SHOW);

        InvalidateRect(
            g_ReplacementPreview,
            nullptr,
            TRUE);

        SetWindowTextW(
            g_ReplacementTitle,
            L"Current Replacement");
    }
    else
    {
        SetWindowTextW(
            g_ReplacementTitle,
            L"Current Replacement: None");

        ShowWindow(
            g_ReplacementPreview,
            SW_HIDE);
    }
}

static int AddThumbnail(
    const TextureRecord& texture)
{
    if (g_ThumbnailList == nullptr)
        return -1;

    HBITMAP bitmap =
        LoadPreviewBitmap(texture, THUMBNAIL_SIZE);

    if (bitmap == nullptr)
        return -1;

    const int imageIndex =
        ImageList_Add(
            g_ThumbnailList,
            bitmap,
            nullptr);

    DeleteObject(bitmap);

    return imageIndex;
}


static void SetSubItem(
    int row,
    int column,
    const std::wstring& text);
static void ClearLargePreview();
static void ClearReplacementPreview();
static void PopulateList();
static void BuildTextureTabs();
static void QueueSmallPreviewsForActiveTab();
static void QueueVisiblePreviews();

static bool PreviewFileExists(const TextureRecord& texture)
{
    // Historical name retained to minimize churn in the legacy/modern UI
    // plumbing.  This experimental build never looks at dat_previews; it only
    // reports whether the selected DAT contains an indexed texture record.
    std::uint32_t did = 0;
    return ParseHex(texture.did, did) &&
        g_DatTextureEntries.find(did) !=
            g_DatTextureEntries.end();
}

static void PreviewWorkerMain()
{
    for (;;)
    {
        std::size_t textureIndex = 0;

        {
            std::unique_lock<std::mutex> lock(g_PreviewMutex);
            g_PreviewCv.wait(lock, [] {
                return g_StopPreviewWorker || !g_PreviewQueue.empty();
            });

            if (g_StopPreviewWorker && g_PreviewQueue.empty())
                return;

            textureIndex = g_PreviewQueue.front();
            g_PreviewQueue.pop_front();
        }

        bool success = false;
        if (textureIndex < g_Textures.size())
            success = PreviewFileExists(g_Textures[textureIndex]);

        {
            std::lock_guard<std::mutex> lock(g_PreviewMutex);
            g_PreviewQueued.erase(textureIndex);
        }

        if (g_MainWindow != nullptr)
        {
            PostMessageW(
                g_MainWindow,
                WM_APP_PREVIEW_READY,
                static_cast<WPARAM>(textureIndex),
                success ? 1 : 0);
        }
    }
}

static void QueuePreview(std::size_t textureIndex, bool priority)
{
    if (textureIndex >= g_Textures.size() ||
        !IsPreviewable(g_Textures[textureIndex]))
        return;

    {
        std::lock_guard<std::mutex> lock(g_PreviewMutex);

        if (g_PreviewQueued.find(textureIndex) != g_PreviewQueued.end())
        {
            if (priority)
            {
                for (auto it = g_PreviewQueue.begin();
                     it != g_PreviewQueue.end(); ++it)
                {
                    if (*it == textureIndex)
                    {
                        g_PreviewQueue.erase(it);
                        g_PreviewQueue.push_front(textureIndex);
                        break;
                    }
                }
            }
            return;
        }

        g_PreviewQueued.insert(textureIndex);
        if (priority)
            g_PreviewQueue.push_front(textureIndex);
        else
            g_PreviewQueue.push_back(textureIndex);
    }

    g_PreviewCv.notify_one();
}

static void QueueVisiblePreviews()
{
    if (g_ListView == nullptr)
        return;

    const int itemCount = ListView_GetItemCount(g_ListView);
    if (itemCount <= 0)
        return;

    const int top = ListView_GetTopIndex(g_ListView);
    const int perPage = ListView_GetCountPerPage(g_ListView);
    const int last = min(itemCount - 1, top + perPage + 1);

    for (int row = top; row <= last; ++row)
    {
        LVITEMW item = {};
        item.mask = LVIF_PARAM | LVIF_IMAGE;
        item.iItem = row;

        if (!ListView_GetItem(g_ListView, &item))
            continue;

        const std::size_t textureIndex =
            static_cast<std::size_t>(item.lParam);

        if (textureIndex >= g_Textures.size() ||
            !IsPreviewable(g_Textures[textureIndex]) ||
            item.iImage >= 0)
            continue;

        SetSubItem(row, 4, L"Loading...");
        QueuePreview(textureIndex, false);
    }
}

static int FindListRowForTexture(std::size_t textureIndex)
{
    const int count = ListView_GetItemCount(g_ListView);
    for (int row = 0; row < count; ++row)
    {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (ListView_GetItem(g_ListView, &item) &&
            static_cast<std::size_t>(item.lParam) == textureIndex)
            return row;
    }
    return -1;
}

static void ApplyReadyPreview(std::size_t textureIndex, bool success)
{
    if (textureIndex >= g_Textures.size())
        return;

    const int row = FindListRowForTexture(textureIndex);
    if (row >= 0)
    {
        if (success)
        {
            const int imageIndex = AddThumbnail(g_Textures[textureIndex]);
            if (imageIndex >= 0)
            {
                LVITEMW item = {};
                item.mask = LVIF_IMAGE;
                item.iItem = row;
                item.iImage = imageIndex;
                ListView_SetItem(g_ListView, &item);
                SetSubItem(row, 4, L"PNG");
            }
            else
            {
                SetSubItem(row, 4, L"Failed");
            }
        }
        else
        {
            SetSubItem(row, 4, L"Failed");
        }
    }

    if (g_SelectedRow == static_cast<int>(textureIndex))
    {
        ClearLargePreview();
        if (success)
        {
            g_LargePreviewBitmap =
                LoadPreviewBitmap(g_Textures[textureIndex], 192);
            if (g_LargePreviewBitmap != nullptr)
            {
                SendMessageW(
                    g_LargePreview,
                    STM_SETIMAGE,
                    IMAGE_BITMAP,
                    reinterpret_cast<LPARAM>(g_LargePreviewBitmap));
            }
        }
        SetWindowTextW(
            g_DetailsTitle,
            success
                ? L"Selected Texture (Original)"
                : L"Selected Texture (Preview failed)");
    }
}

static void AddColumn(
    int index,
    int width,
    const wchar_t* title)
{
    LVCOLUMNW column = {};
    column.mask =
        LVCF_TEXT |
        LVCF_WIDTH |
        LVCF_SUBITEM;

    column.iSubItem = index;
    column.cx = width;
    column.pszText =
        const_cast<wchar_t*>(title);

    ListView_InsertColumn(
        g_ListView,
        index,
        &column);
}

static void SetSubItem(
    int row,
    int column,
    const std::wstring& text)
{
    ListView_SetItemText(
        g_ListView,
        row,
        column,
        const_cast<wchar_t*>(
            text.c_str()));
}

static void AutoSizeListColumns()
{
    const int count = Header_GetItemCount(ListView_GetHeader(g_ListView));
    for (int column = 0; column < count; ++column)
    {
        ListView_SetColumnWidth(g_ListView, column, LVSCW_AUTOSIZE);
        const int contentWidth = ListView_GetColumnWidth(g_ListView, column);

        ListView_SetColumnWidth(g_ListView, column, LVSCW_AUTOSIZE_USEHEADER);
        const int headerWidth = ListView_GetColumnWidth(g_ListView, column);

        ListView_SetColumnWidth(
            g_ListView, column,
            (contentWidth > headerWidth ? contentWidth : headerWidth) + 8);
    }
}

static std::string SanitizeNoteForStorage(std::string note)
{
    for (char& ch : note)
        if (ch == '\t' || ch == '\r' || ch == '\n')
            ch = ' ';
    return note;
}

static void SaveTextureNotes()
{
    std::ofstream output(std::filesystem::path(g_AppPaths.textureNotesPath), std::ios::trunc);
    if (!output.is_open())
        return;
    for (const auto& entry : g_TextureNotes)
        if (!entry.second.empty())
            output << entry.first << '\t' << SanitizeNoteForStorage(entry.second) << '\n';
}

static void LoadTextureNotes()
{
    g_TextureNotes.clear();
    std::ifstream input(std::filesystem::path(g_AppPaths.textureNotesPath));
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t separator = line.find('\t');
        if (separator == std::string::npos)
            continue;
        const std::string did = line.substr(0, separator);
        const std::string note = line.substr(separator + 1);
        if (!did.empty() && !note.empty())
            g_TextureNotes[did] = note;
    }
}

static std::wstring NoteForTexture(const TextureRecord& texture)
{
    const auto found = g_TextureNotes.find(texture.did);
    return found == g_TextureNotes.end() ? L"" : ToWide(found->second);
}

static void SaveCustomTabs()
{
    std::ofstream output(std::filesystem::path(g_AppPaths.customTabsPath), std::ios::trunc);
    if (!output.is_open())
        return;

    // One record per membership: tab name, TAB, DID. Empty tabs are stored
    // with an empty DID so they survive a restart.
    for (const CustomTab& tab : g_CustomTabs)
    {
        if (tab.dids.empty())
        {
            output << tab.name << "\t\n";
            continue;
        }

        for (const std::string& did : tab.dids)
            output << tab.name << '\t' << did << '\n';
    }
}

static void LoadCustomTabs()
{
    g_CustomTabs.clear();

    std::ifstream input(std::filesystem::path(g_AppPaths.customTabsPath));
    std::string line;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const std::size_t separator = line.find('\t');
        if (separator == std::string::npos)
            continue;

        const std::string name = line.substr(0, separator);
        const std::string did = line.substr(separator + 1);
        if (name.empty())
            continue;

        std::size_t tabIndex = g_CustomTabs.size();
        for (std::size_t i = 0; i < g_CustomTabs.size(); ++i)
        {
            if (g_CustomTabs[i].name == name)
            {
                tabIndex = i;
                break;
            }
        }

        if (tabIndex == g_CustomTabs.size())
        {
            CustomTab tab;
            tab.name = name;
            g_CustomTabs.push_back(tab);
        }

        if (!did.empty())
            g_CustomTabs[tabIndex].dids.insert(did);
    }
}

static std::wstring CustomGroupsForTexture(const TextureRecord& texture)
{
    std::wstring groups;

    for (const CustomTab& tab : g_CustomTabs)
    {
        if (tab.dids.find(texture.did) == tab.dids.end())
            continue;

        if (!groups.empty())
            groups += L", ";

        groups += ToWide(tab.name);
    }

    return groups;
}

static bool IsInActiveCustomTab(const TextureRecord& texture)
{
    if (g_ActiveCustomTab < 0 ||
        static_cast<std::size_t>(g_ActiveCustomTab) >= g_CustomTabs.size())
    {
        return true;
    }

    return g_CustomTabs[static_cast<std::size_t>(g_ActiveCustomTab)].dids.find(texture.did) !=
        g_CustomTabs[static_cast<std::size_t>(g_ActiveCustomTab)].dids.end();
}

static const wchar_t* CUSTOM_TAB_PROMPT_CLASS = L"ACModernUICustomTabPrompt";
static const int ID_CUSTOM_TAB_PROMPT_EDIT = 1101;
static HWND g_PromptEdit = nullptr;
static bool g_PromptAccepted = false;
static std::wstring g_CustomTabPromptResult;

static LRESULT CALLBACK CustomTabPromptProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            const wchar_t* initial = reinterpret_cast<const wchar_t*>(create->lpCreateParams);

            CreateWindowExW(0, L"STATIC", L"Text:", WS_CHILD | WS_VISIBLE,
                12, 14, 80, 22, window, nullptr, GetModuleHandleW(nullptr), nullptr);

            g_PromptEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial ? initial : L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                12, 38, 300, 25, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CUSTOM_TAB_PROMPT_EDIT)),
                GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                146, 76, 80, 28, window, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                232, 76, 80, 28, window, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);

            SendMessageW(g_PromptEdit, EM_SETSEL, 0, -1);
            SetFocus(g_PromptEdit);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK &&
                (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
            {
                wchar_t buffer[256] = {};
                GetWindowTextW(g_PromptEdit, buffer, 256);
                g_CustomTabPromptResult = buffer;
                g_PromptAccepted = true;
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL &&
                (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0))
            {
                DestroyWindow(window);
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

static bool PromptForCustomTabName(HWND owner, const wchar_t* title,
    const std::wstring& initial, std::string& result)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CustomTabPromptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = CUSTOM_TAB_PROMPT_CLASS;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        registered = true;
    }

    g_PromptAccepted = false;
    g_PromptEdit = nullptr;
    g_CustomTabPromptResult.clear();

    RECT ownerRect = {};
    GetWindowRect(owner, &ownerRect);
    const int width = 340;
    const int height = 150;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND prompt = CreateWindowExW(
        WS_EX_DLGMODALFRAME, CUSTOM_TAB_PROMPT_CLASS, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr),
        const_cast<wchar_t*>(initial.c_str()));

    if (prompt == nullptr)
    {
        return false;
    }

    // Create and activate the prompt before disabling the Manager. Disabling
    // the owner first can cause Windows to activate another application,
    // leaving this small owned popup hidden behind other windows.
    ShowWindow(prompt, SW_SHOWNORMAL);
    UpdateWindow(prompt);
    SetWindowPos(prompt, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(prompt);
    SetActiveWindow(prompt);
    if (g_PromptEdit != nullptr)
        SetFocus(g_PromptEdit);

    EnableWindow(owner, FALSE);

    MSG message = {};
    while (IsWindow(prompt) && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(prompt, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    std::wstring name;

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetActiveWindow(owner);

    name = g_CustomTabPromptResult;
    g_CustomTabPromptResult.clear();

    if (!g_PromptAccepted || name.empty())
        return false;

    result = FromWide(name);
    return !result.empty();
}

static void EditTextureNote(HWND owner, std::size_t textureIndex)
{
    if (textureIndex >= g_Textures.size())
        return;

    const std::string did = g_Textures[textureIndex].did;
    std::string current;
    const auto found = g_TextureNotes.find(did);
    if (found != g_TextureNotes.end())
        current = found->second;

    std::string note;
    if (!PromptForCustomTabName(owner, L"Texture Note", ToWide(current), note))
        return;

    note = SanitizeNoteForStorage(note);
    if (note.empty())
        g_TextureNotes.erase(did);
    else
        g_TextureNotes[did] = note;

    SaveTextureNotes();
    PopulateList();
}

static bool CustomTabNameExists(const std::string& name, int exceptIndex = -1)
{
    for (std::size_t i = 0; i < g_CustomTabs.size(); ++i)
    {
        if (static_cast<int>(i) != exceptIndex && g_CustomTabs[i].name == name)
            return true;
    }
    return false;
}

static void RebuildTabsKeepingCustomSelection(int customIndex)
{
    BuildTextureTabs();
    if (customIndex >= 0 && static_cast<std::size_t>(customIndex) < g_CustomTabs.size())
    {
        const int tabIndex = static_cast<int>(g_TabPrefixes.size() + 3 + customIndex);
        g_ActivePrefix.clear();
        g_ReplacementsOnly = false;
        g_EncounteredOnly = false;
        g_ActiveCustomTab = customIndex;
        TabCtrl_SetCurSel(g_TabControl, tabIndex);
    }
}

static void AddCustomTab(HWND owner)
{
    std::string name;
    if (!PromptForCustomTabName(owner, L"Add Custom Tab", L"", name))
        return;

    if (name.find('\t') != std::string::npos || name.find('\r') != std::string::npos || name.find('\n') != std::string::npos)
    {
        MessageBoxW(owner, L"Tab names cannot contain tabs or line breaks.", WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (CustomTabNameExists(name))
    {
        MessageBoxW(owner, L"A custom tab with that name already exists.", WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    CustomTab tab;
    tab.name = name;
    g_CustomTabs.push_back(tab);
    SaveCustomTabs();
    RebuildTabsKeepingCustomSelection(static_cast<int>(g_CustomTabs.size() - 1));
    PopulateList();
}

static std::vector<std::size_t> GetSelectedTextureIndices()
{
    std::vector<std::size_t> selected;

    int row = -1;
    while ((row = ListView_GetNextItem(g_ListView, row, LVNI_SELECTED)) != -1)
    {
        LVITEMW item = {};
        item.mask = LVIF_PARAM;
        item.iItem = row;

        if (ListView_GetItem(g_ListView, &item))
        {
            const std::size_t textureIndex =
                static_cast<std::size_t>(item.lParam);

            if (textureIndex < g_Textures.size())
                selected.push_back(textureIndex);
        }
    }

    return selected;
}

static void AddSelectedTexturesToCustomTab(
    std::size_t fallbackTextureIndex,
    std::size_t customIndex)
{
    if (customIndex >= g_CustomTabs.size())
        return;

    std::vector<std::size_t> selected = GetSelectedTextureIndices();

    // Preserve the old single-item behavior if the context-clicked item is
    // somehow not part of the current ListView selection.
    if (selected.empty() && fallbackTextureIndex < g_Textures.size())
        selected.push_back(fallbackTextureIndex);

    std::size_t added = 0;
    for (const std::size_t textureIndex : selected)
    {
        if (textureIndex >= g_Textures.size())
            continue;

        const auto result =
            g_CustomTabs[customIndex].dids.insert(g_Textures[textureIndex].did);

        if (result.second)
            ++added;
    }

    SaveCustomTabs();

    const std::wstring status =
        std::to_wstring(added) +
        (added == 1
            ? L" texture added to custom tab."
            : L" textures added to custom tab.");

    SetWindowTextW(g_InfoText, status.c_str());
}

static void RemoveSelectedTexturesFromActiveCustomTab(
    std::size_t fallbackTextureIndex)
{
    if (g_ActiveCustomTab < 0 ||
        static_cast<std::size_t>(g_ActiveCustomTab) >= g_CustomTabs.size())
        return;

    std::vector<std::size_t> selected = GetSelectedTextureIndices();
    if (selected.empty() && fallbackTextureIndex < g_Textures.size())
        selected.push_back(fallbackTextureIndex);

    CustomTab& tab =
        g_CustomTabs[static_cast<std::size_t>(g_ActiveCustomTab)];

    for (const std::size_t textureIndex : selected)
    {
        if (textureIndex < g_Textures.size())
            tab.dids.erase(g_Textures[textureIndex].did);
    }

    SaveCustomTabs();
    ClearLargePreview();
    ClearReplacementPreview();
    PopulateList();
    QueueSmallPreviewsForActiveTab();
    QueueVisiblePreviews();
}

static void RenameCustomTab(HWND owner, int customIndex)
{
    if (customIndex < 0 || static_cast<std::size_t>(customIndex) >= g_CustomTabs.size())
        return;

    std::string name;
    if (!PromptForCustomTabName(owner, L"Rename Custom Tab",
            ToWide(g_CustomTabs[static_cast<std::size_t>(customIndex)].name), name))
        return;

    if (name.find('\t') != std::string::npos || name.find('\r') != std::string::npos || name.find('\n') != std::string::npos)
        return;

    if (CustomTabNameExists(name, customIndex))
    {
        MessageBoxW(owner, L"A custom tab with that name already exists.", WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    g_CustomTabs[static_cast<std::size_t>(customIndex)].name = name;
    SaveCustomTabs();
    RebuildTabsKeepingCustomSelection(customIndex);
    PopulateList();
}

static void DeleteCustomTab(HWND owner, int customIndex)
{
    if (customIndex < 0 || static_cast<std::size_t>(customIndex) >= g_CustomTabs.size())
        return;

    const std::wstring message = L"Delete custom tab \"" +
        ToWide(g_CustomTabs[static_cast<std::size_t>(customIndex)].name) +
        L"\"?\r\n\r\nThis only deletes the group. It does not delete any textures or replacements.";

    if (MessageBoxW(owner, message.c_str(), L"Delete Custom Tab",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    g_CustomTabs.erase(g_CustomTabs.begin() + customIndex);
    SaveCustomTabs();
    BuildTextureTabs();
    PopulateList();
    QueueSmallPreviewsForActiveTab();
    QueueVisiblePreviews();
}

static void LoadEncounteredDIDs()
{
    g_EncounteredDIDs.clear();

    std::ifstream input(std::filesystem::path(g_AppPaths.encounteredPath));
    std::string line;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty() || line == "DID")
            continue;

        g_EncounteredDIDs.insert(line);
    }
}


static bool IsEncounteredTexture(
    const TextureRecord& texture)
{
    return
        g_EncounteredDIDs.find(texture.did) !=
        g_EncounteredDIDs.end();
}


static void ClearEncounteredData(HWND owner)
{
    if (MessageBoxW(
            owner,
            L"Clear all encountered texture history?\r\n\r\n"
            L"Newly encountered textures will begin accumulating again "
            L"while AC is running.",
            L"Clear Encountered",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    const DWORD attributes =
        GetFileAttributesW(g_AppPaths.encounteredPath.c_str());

    if (attributes != INVALID_FILE_ATTRIBUTES &&
        !DeleteFileW(g_AppPaths.encounteredPath.c_str()))
    {
        wchar_t message[512] = {};

        swprintf_s(
            message,
            L"Could not clear encountered texture history.\r\n\r\n"
            L"Windows error: %lu",
            GetLastError());

        MessageBoxW(
            owner,
            message,
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);

        return;
    }

    g_EncounteredDIDs.clear();

    if (g_EncounteredOnly)
    {
        ClearLargePreview();
        ClearReplacementPreview();
        SetWindowTextW(
            g_DetailsText,
            L"Select a texture to inspect it.");
        SetWindowTextW(
            g_ReplacementTitle,
            L"Current Replacement: None");
        PopulateList();
    }

    SetWindowTextW(
        g_InfoText,
        L"Encountered texture history cleared.");
}


static void BuildTextureTabs()
{
    g_TabPrefixes.clear();

    for (const TextureRecord& texture : g_Textures)
    {
        if (texture.did.size() < 5)
            continue;

        const std::string prefix =
            texture.did.substr(0, 5);

        bool found = false;

        for (const std::string& existing : g_TabPrefixes)
        {
            if (existing == prefix)
            {
                found = true;
                break;
            }
        }

        if (!found)
            g_TabPrefixes.push_back(prefix);
    }

    TabCtrl_DeleteAllItems(g_TabControl);

    for (std::size_t i = 0; i < g_TabPrefixes.size(); ++i)
    {
        const std::wstring label =
            ToWide(g_TabPrefixes[i]);

        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText =
            const_cast<wchar_t*>(label.c_str());

        TabCtrl_InsertItem(
            g_TabControl,
            static_cast<int>(i),
            &item);
    }

    TCITEMW allItem = {};
    allItem.mask = TCIF_TEXT;
    allItem.pszText = const_cast<wchar_t*>(L"All");

    TabCtrl_InsertItem(
        g_TabControl,
        static_cast<int>(g_TabPrefixes.size()),
        &allItem);

    TCITEMW replacementsItem = {};
    replacementsItem.mask = TCIF_TEXT;
    replacementsItem.pszText =
        const_cast<wchar_t*>(L"Replacements");

    TabCtrl_InsertItem(
        g_TabControl,
        static_cast<int>(g_TabPrefixes.size() + 1),
        &replacementsItem);

    TCITEMW encounteredItem = {};
    encounteredItem.mask = TCIF_TEXT;
    encounteredItem.pszText =
        const_cast<wchar_t*>(L"Encountered");

    TabCtrl_InsertItem(
        g_TabControl,
        static_cast<int>(g_TabPrefixes.size() + 2),
        &encounteredItem);

    for (std::size_t i = 0; i < g_CustomTabs.size(); ++i)
    {
        const std::wstring label = ToWide(g_CustomTabs[i].name);
        TCITEMW customItem = {};
        customItem.mask = TCIF_TEXT;
        customItem.pszText = const_cast<wchar_t*>(label.c_str());
        TabCtrl_InsertItem(
            g_TabControl,
            static_cast<int>(g_TabPrefixes.size() + 3 + i),
            &customItem);
    }

    g_ReplacementsOnly = false;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;

    if (!g_TabPrefixes.empty())
    {
        g_ActivePrefix = g_TabPrefixes.front();
        TabCtrl_SetCurSel(g_TabControl, 0);
    }
    else
    {
        g_ActivePrefix.clear();
        TabCtrl_SetCurSel(g_TabControl, 0);
    }
}

static bool TextureMatchesActiveTab(
    const TextureRecord& texture)
{
    return
        g_ActivePrefix.empty() ||
        (texture.did.size() >= g_ActivePrefix.size() &&
         texture.did.compare(
             0,
             g_ActivePrefix.size(),
             g_ActivePrefix) == 0);
}

static bool ReadSizeFilterValue(HWND edit, bool& enabled, std::uint32_t& value)
{
    wchar_t buffer[32] = {};
    GetWindowTextW(edit, buffer, 32);
    std::wstring text = buffer;
    if (text.empty())
    {
        enabled = false;
        value = 0;
        return true;
    }

    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || parsed == 0)
        return false;

    enabled = true;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

static bool TextureMatchesSizeFilter(const TextureRecord& texture)
{
    if (g_FilterWidthEnabled && texture.width != g_FilterWidth)
        return false;
    if (g_FilterHeightEnabled && texture.height != g_FilterHeight)
        return false;
    return true;
}

static void ApplySizeFilter(HWND owner)
{
    bool widthEnabled = false;
    bool heightEnabled = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    if (!ReadSizeFilterValue(g_FilterWidthEdit, widthEnabled, width) ||
        !ReadSizeFilterValue(g_FilterHeightEdit, heightEnabled, height))
    {
        MessageBoxW(owner,
            L"Width and Height must be positive whole numbers, or left blank for Any.",
            WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    g_FilterWidthEnabled = widthEnabled;
    g_FilterHeightEnabled = heightEnabled;
    g_FilterWidth = width;
    g_FilterHeight = height;

    PopulateList();
    QueueSmallPreviewsForActiveTab();
    QueueVisiblePreviews();
}

static void ClearSizeFilter()
{
    SetWindowTextW(g_FilterWidthEdit, L"");
    SetWindowTextW(g_FilterHeightEdit, L"");
    g_FilterWidthEnabled = false;
    g_FilterHeightEnabled = false;
    g_FilterWidth = 0;
    g_FilterHeight = 0;

    PopulateList();
    QueueSmallPreviewsForActiveTab();
    QueueVisiblePreviews();
}

static void PopulateList()
{
    if (g_EncounteredOnly)
        LoadEncounteredDIDs();

    ListView_DeleteAllItems(g_ListView);

    if (g_ThumbnailList != nullptr)
        ImageList_RemoveAll(g_ThumbnailList);

    g_SelectedRow = -1;
    g_SelectedListRow = -1;

    std::uint32_t displayed = 0;
    std::uint32_t previewable = 0;

    for (std::size_t i = 0;
         i < g_Textures.size();
         ++i)
    {
        const TextureRecord& texture =
            g_Textures[i];

        if (!TextureMatchesActiveTab(texture))
            continue;

        if (!TextureMatchesSizeFilter(texture))
            continue;

        if (g_ReplacementsOnly && !HasReplacement(texture))
            continue;

        if (g_EncounteredOnly && !IsEncounteredTexture(texture))
            continue;

        if (g_ActiveCustomTab >= 0 && !IsInActiveCustomTab(texture))
            continue;

        // The Replacements tab always shows replacements by definition.
        if (!g_ReplacementsOnly &&
            !g_DisplayReplaced &&
            HasReplacement(texture))
        {
            continue;
        }

        if (!g_DisplayUnsupported && !IsPreviewable(texture))
            continue;

        if (IsPreviewable(texture))
            ++previewable;

        const std::wstring did =
            ToWide(texture.did);

        LVITEMW item = {};
        item.mask =
            LVIF_TEXT |
            LVIF_IMAGE |
            LVIF_PARAM;
        item.iItem =
            static_cast<int>(displayed);
        item.iSubItem = 0;

        // The legacy Win32 list is hidden by the modern shell. Do not perform
        // eager DAT reads here; the visible ImGui browser loads only submitted
        // rows into its bounded GPU thumbnail cache.
        item.iImage = -1;

        item.lParam =
            static_cast<LPARAM>(i);
        item.pszText =
            const_cast<wchar_t*>(
                did.c_str());

        const int row =
            ListView_InsertItem(
                g_ListView,
                &item);

        if (row < 0)
            continue;

        const std::wstring size =
            std::to_wstring(texture.width) +
            L"x" +
            std::to_wstring(texture.height);

        SetSubItem(row, 1, size);
        SetSubItem(
            row,
            2,
            FormatName(texture.pixelFormat));
        SetSubItem(
            row,
            3,
            std::to_wstring(texture.imageSize));
        SetSubItem(
            row,
            4,
            IsPreviewable(texture)
                ? L"DAT"
                : L"Unsupported");
        SetSubItem(
            row,
            5,
            HasReplacement(texture)
                ? L"REPLACED"
                : L"");

        SetSubItem(
            row,
            6,
            CustomGroupsForTexture(texture));

        SetSubItem(
            row,
            7,
            NoteForTexture(texture));

        ++displayed;
    }

    const std::wstring groupName =
        g_ReplacementsOnly
            ? L"Replacements"
            : (g_EncounteredOnly
                ? L"Encountered"
                : (g_ActiveCustomTab >= 0 &&
                   static_cast<std::size_t>(g_ActiveCustomTab) < g_CustomTabs.size()
                    ? ToWide(g_CustomTabs[static_cast<std::size_t>(g_ActiveCustomTab)].name)
                    : (g_ActivePrefix.empty()
                        ? L"All"
                        : ToWide(g_ActivePrefix))));

    const std::wstring status =
        L"Group: " + groupName +
        L"    Textures: " +
        std::to_wstring(displayed) +
        L" of " +
        std::to_wstring(g_Textures.size()) +
        L"    Previewable: " +
        std::to_wstring(previewable);

    SetWindowTextW(
        g_StatusText,
        status.c_str());

    AutoSizeListColumns();
}

static void QueueSmallPreviewsForActiveTab()
{
    // Direct-DAT mode has no disk preview cache. The modern browser loads only
    // visible/submitted textures on demand.
    for (std::size_t i = 0; i < g_Textures.size(); ++i)
    {
        const TextureRecord& texture = g_Textures[i];

        if (!TextureMatchesActiveTab(texture))
            continue;

        if (g_ReplacementsOnly && !HasReplacement(texture))
            continue;

        if (g_EncounteredOnly && !IsEncounteredTexture(texture))
            continue;

        if (g_ActiveCustomTab >= 0 && !IsInActiveCustomTab(texture))
            continue;

        if (!g_ReplacementsOnly &&
            !g_DisplayReplaced &&
            HasReplacement(texture))
        {
            continue;
        }

        if (!g_DisplayUnsupported && !IsPreviewable(texture))
            continue;

        if (!IsPreviewable(texture) || texture.imageSize > 4096)
            continue;

        if (PreviewFileExists(texture))
            continue;

        QueuePreview(i, false);
    }
}

static void GoToTexture(std::size_t textureIndex)
{
    if (textureIndex >= g_Textures.size())
        return;

    const TextureRecord& texture = g_Textures[textureIndex];
    if (texture.did.size() < 5)
        return;

    const std::string targetPrefix = texture.did.substr(0, 5);
    int targetTab = -1;

    for (std::size_t i = 0; i < g_TabPrefixes.size(); ++i)
    {
        if (g_TabPrefixes[i] == targetPrefix)
        {
            targetTab = static_cast<int>(i);
            break;
        }
    }

    if (targetTab < 0)
        return;

    g_ActivePrefix = targetPrefix;
    g_ReplacementsOnly = false;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;
    TabCtrl_SetCurSel(g_TabControl, targetTab);

    ClearLargePreview();
    ClearReplacementPreview();
    PopulateList();
    QueueSmallPreviewsForActiveTab();
    QueueVisiblePreviews();

    const int row = FindListRowForTexture(textureIndex);
    if (row < 0)
        return;

    ListView_SetItemState(
        g_ListView,
        row,
        LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_ListView, row, FALSE);
    SetFocus(g_ListView);
}

static void ClearLargePreview()
{
    if (g_LargePreviewBitmap != nullptr)
    {
        SendMessageW(
            g_LargePreview,
            STM_SETIMAGE,
            IMAGE_BITMAP,
            0);

        DeleteObject(
            g_LargePreviewBitmap);

        g_LargePreviewBitmap = nullptr;
    }
}

static void ShowTextureDetails(
    int row)
{
    ClearLargePreview();

    if (row < 0 ||
        static_cast<std::size_t>(row) >= g_Textures.size())
    {
        g_SelectedRow = -1;

        SetWindowTextW(
            g_DetailsText,
            L"Select a texture to inspect it.");

        return;
    }

    g_SelectedRow = row;

    const TextureRecord& texture =
        g_Textures[static_cast<std::size_t>(row)];

    wchar_t details[1024] = {};

    const std::wstring formatName =
        FormatName(texture.pixelFormat);

    wchar_t palette[32] = {};

    if (texture.paletteDID != 0)
    {
        swprintf_s(
            palette,
            L"%08X",
            texture.paletteDID);
    }
    else
    {
        wcscpy_s(
            palette,
            L"None");
    }

    swprintf_s(
        details,
        L"DID:          %S\r\n"
        L"Dimensions:   %u x %u\r\n"
        L"Format:       %s (0x%08X)\r\n"
        L"Payload:      %u bytes\r\n"
        L"Format Info:  0x%08X\r\n"
        L"Palette DID:  %s",
        texture.did.c_str(),
        texture.width,
        texture.height,
        formatName.c_str(),
        texture.pixelFormat,
        texture.imageSize,
        texture.formatInfo,
        palette);

    SetWindowTextW(
        g_DetailsText,
        details);

    if (IsPreviewable(texture))
    {
        g_LargePreviewBitmap =
            LoadPreviewBitmap(texture, 192);

        if (g_LargePreviewBitmap != nullptr)
        {
            SendMessageW(
                g_LargePreview,
                STM_SETIMAGE,
                IMAGE_BITMAP,
                reinterpret_cast<LPARAM>(g_LargePreviewBitmap));
            SetWindowTextW(
                g_DetailsTitle,
                L"Selected Texture (Original)");
        }
        else
        {
            SetWindowTextW(
                g_DetailsTitle,
                L"Selected Texture (Loading preview...)");
            QueuePreview(static_cast<std::size_t>(row), true);
        }
    }
    else
    {
        SetWindowTextW(
            g_DetailsTitle,
            L"Selected Texture (Unsupported preview format)");
    }


    UpdateReplacementPreview(texture);
}





// -----------------------------------------------------------------------------
// ACUI pack support
//
// .acui is a standard ZIP container using the STORE method (no compression).
// Keeping v1 store-only makes the format dependency-free in this native manager
// while remaining readable by standard ZIP tools and .NET ZipArchive.
// -----------------------------------------------------------------------------

struct AcuiTextureEntry
{
    std::string did;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t pixelFormat = 0;
    std::string file;
};

struct AcuiManifest
{
    std::uint32_t formatVersion = 0;
    std::string name;
    std::string author;
    std::string description;
    std::vector<AcuiTextureEntry> textures;
};

struct StoredZipEntry
{
    std::string name;
    std::vector<BYTE> data;
    std::uint32_t crc32 = 0;
    std::uint32_t localHeaderOffset = 0;
};

static std::uint16_t ReadLe16(const BYTE* data)
{
    return static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8);
}

static std::uint32_t ReadLe32(const BYTE* data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) |
        (static_cast<std::uint32_t>(data[3]) << 24);
}

static void WriteLe16(std::ofstream& output, std::uint16_t value)
{
    const BYTE bytes[2] = {
        static_cast<BYTE>(value & 0xFF),
        static_cast<BYTE>((value >> 8) & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

static void WriteLe32(std::ofstream& output, std::uint32_t value)
{
    const BYTE bytes[4] = {
        static_cast<BYTE>(value & 0xFF),
        static_cast<BYTE>((value >> 8) & 0xFF),
        static_cast<BYTE>((value >> 16) & 0xFF),
        static_cast<BYTE>((value >> 24) & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

static std::uint32_t AcuiCrc32(const BYTE* data, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u &
                static_cast<std::uint32_t>(-(static_cast<int>(crc & 1u))));
    }
    return ~crc;
}

static bool ReadBinaryFile(const std::wstring& path, std::vector<BYTE>& data)
{
    data.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open())
        return false;

    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > 0xFFFFFFFFull)
        return false;

    data.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!data.empty())
        input.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));

    return input.good() || input.eof();
}

static bool WriteBinaryFile(const std::wstring& path, const std::vector<BYTE>& data)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        return false;
    if (!data.empty())
        output.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return output.good();
}

static std::string Hex8(std::uint32_t value)
{
    char buffer[16] = {};
    sprintf_s(buffer, "%08X", value);
    return buffer;
}

static std::string JsonEscape(const std::string& value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char ch : value)
    {
        switch (ch)
        {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20)
                {
                    char escaped[8] = {};
                    sprintf_s(escaped, "\\u%04X", static_cast<unsigned int>(ch));
                    result += escaped;
                }
                else
                {
                    result.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return result;
}

static std::string BuildAcuiManifestJson(
    const std::string& name,
    const std::string& author,
    const std::string& description,
    const std::vector<std::size_t>& textureIndices)
{
    std::ostringstream out;
    out << "{\n"
        << "  \"formatVersion\": 1,\n"
        << "  \"name\": \"" << JsonEscape(name) << "\",\n"
        << "  \"author\": \"" << JsonEscape(author) << "\",\n"
        << "  \"description\": \"" << JsonEscape(description) << "\",\n"
        << "  \"textures\": [\n";

    for (std::size_t i = 0; i < textureIndices.size(); ++i)
    {
        const TextureRecord& texture = g_Textures[textureIndices[i]];
        out << "    {\n"
            << "      \"did\": \"" << texture.did << "\",\n"
            << "      \"width\": " << texture.width << ",\n"
            << "      \"height\": " << texture.height << ",\n"
            << "      \"imageSize\": " << texture.imageSize << ",\n"
            << "      \"pixelFormat\": \"0x" << Hex8(texture.pixelFormat) << "\",\n"
            << "      \"formatInfo\": \"0x" << Hex8(texture.formatInfo) << "\",\n"
            << "      \"paletteDID\": \"0x" << Hex8(texture.paletteDID) << "\",\n"
            << "      \"file\": \"textures/" << texture.did << ".rgb\"\n"
            << "    }";
        if (i + 1 != textureIndices.size())
            out << ',';
        out << '\n';
    }

    out << "  ]\n}\n";
    return out.str();
}

static bool WriteStoredZip(
    const std::wstring& path,
    std::vector<StoredZipEntry>& entries,
    std::wstring& error)
{
    if (entries.empty() || entries.size() > 0xFFFFu)
    {
        error = L"The pack contains an unsupported number of ZIP entries.";
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error = L"Could not create the .acui file.";
        return false;
    }

    for (StoredZipEntry& entry : entries)
    {
        if (entry.name.empty() || entry.name.size() > 0xFFFFu ||
            entry.data.size() > 0xFFFFFFFFull)
        {
            error = L"A pack entry is too large or has an invalid name.";
            return false;
        }

        const std::streamoff offset = output.tellp();
        if (offset < 0 || static_cast<unsigned long long>(offset) > 0xFFFFFFFFull)
        {
            error = L"The .acui file exceeded the ZIP32 size limit.";
            return false;
        }

        entry.localHeaderOffset = static_cast<std::uint32_t>(offset);
        entry.crc32 = AcuiCrc32(entry.data.data(), entry.data.size());

        WriteLe32(output, 0x04034B50u);
        WriteLe16(output, 20);
        WriteLe16(output, 0);
        WriteLe16(output, 0); // STORE
        WriteLe16(output, 0);
        WriteLe16(output, 0);
        WriteLe32(output, entry.crc32);
        WriteLe32(output, static_cast<std::uint32_t>(entry.data.size()));
        WriteLe32(output, static_cast<std::uint32_t>(entry.data.size()));
        WriteLe16(output, static_cast<std::uint16_t>(entry.name.size()));
        WriteLe16(output, 0);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        if (!entry.data.empty())
            output.write(reinterpret_cast<const char*>(entry.data.data()),
                static_cast<std::streamsize>(entry.data.size()));

        if (!output.good())
        {
            error = L"Writing the .acui file failed.";
            return false;
        }
    }

    const std::streamoff centralOffsetValue = output.tellp();
    if (centralOffsetValue < 0 ||
        static_cast<unsigned long long>(centralOffsetValue) > 0xFFFFFFFFull)
    {
        error = L"The .acui central directory exceeded the ZIP32 size limit.";
        return false;
    }
    const std::uint32_t centralOffset =
        static_cast<std::uint32_t>(centralOffsetValue);

    for (const StoredZipEntry& entry : entries)
    {
        WriteLe32(output, 0x02014B50u);
        WriteLe16(output, 20);
        WriteLe16(output, 20);
        WriteLe16(output, 0);
        WriteLe16(output, 0); // STORE
        WriteLe16(output, 0);
        WriteLe16(output, 0);
        WriteLe32(output, entry.crc32);
        WriteLe32(output, static_cast<std::uint32_t>(entry.data.size()));
        WriteLe32(output, static_cast<std::uint32_t>(entry.data.size()));
        WriteLe16(output, static_cast<std::uint16_t>(entry.name.size()));
        WriteLe16(output, 0);
        WriteLe16(output, 0);
        WriteLe16(output, 0);
        WriteLe16(output, 0);
        WriteLe32(output, 0);
        WriteLe32(output, entry.localHeaderOffset);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }

    const std::streamoff centralEndValue = output.tellp();
    if (centralEndValue < centralOffsetValue ||
        static_cast<unsigned long long>(centralEndValue) > 0xFFFFFFFFull)
    {
        error = L"The .acui central directory is invalid.";
        return false;
    }

    const std::uint32_t centralSize = static_cast<std::uint32_t>(
        centralEndValue - centralOffsetValue);

    WriteLe32(output, 0x06054B50u);
    WriteLe16(output, 0);
    WriteLe16(output, 0);
    WriteLe16(output, static_cast<std::uint16_t>(entries.size()));
    WriteLe16(output, static_cast<std::uint16_t>(entries.size()));
    WriteLe32(output, centralSize);
    WriteLe32(output, centralOffset);
    WriteLe16(output, 0);

    if (!output.good())
    {
        error = L"Finalizing the .acui file failed.";
        return false;
    }

    return true;
}

static bool LoadStoredZip(
    const std::wstring& path,
    std::unordered_map<std::string, std::vector<BYTE>>& files,
    std::wstring& error)
{
    files.clear();
    std::vector<BYTE> bytes;
    if (!ReadBinaryFile(path, bytes))
    {
        error = L"Could not read the selected .acui file.";
        return false;
    }

    if (bytes.size() < 22)
    {
        error = L"The selected file is not a valid .acui ZIP container.";
        return false;
    }

    const std::size_t searchStart =
        bytes.size() > (22u + 0xFFFFu) ? bytes.size() - (22u + 0xFFFFu) : 0u;
    std::size_t eocd = static_cast<std::size_t>(-1);
    for (std::size_t pos = bytes.size() - 22u;; --pos)
    {
        if (ReadLe32(bytes.data() + pos) == 0x06054B50u)
        {
            eocd = pos;
            break;
        }
        if (pos == searchStart)
            break;
    }

    if (eocd == static_cast<std::size_t>(-1) || eocd + 22u > bytes.size())
    {
        error = L"The .acui ZIP end record could not be found.";
        return false;
    }

    const std::uint16_t disk = ReadLe16(bytes.data() + eocd + 4);
    const std::uint16_t centralDisk = ReadLe16(bytes.data() + eocd + 6);
    const std::uint16_t diskEntries = ReadLe16(bytes.data() + eocd + 8);
    const std::uint16_t totalEntries = ReadLe16(bytes.data() + eocd + 10);
    const std::uint32_t centralSize = ReadLe32(bytes.data() + eocd + 12);
    const std::uint32_t centralOffset = ReadLe32(bytes.data() + eocd + 16);

    if (disk != 0 || centralDisk != 0 || diskEntries != totalEntries ||
        static_cast<unsigned long long>(centralOffset) + centralSize > bytes.size())
    {
        error = L"Multi-disk or ZIP64 .acui files are not supported by format version 1.";
        return false;
    }

    std::size_t pos = centralOffset;
    for (std::uint16_t entryIndex = 0; entryIndex < totalEntries; ++entryIndex)
    {
        if (pos + 46u > bytes.size() || ReadLe32(bytes.data() + pos) != 0x02014B50u)
        {
            error = L"The .acui central directory is damaged.";
            return false;
        }

        const std::uint16_t flags = ReadLe16(bytes.data() + pos + 8);
        const std::uint16_t method = ReadLe16(bytes.data() + pos + 10);
        const std::uint32_t crc = ReadLe32(bytes.data() + pos + 16);
        const std::uint32_t compressedSize = ReadLe32(bytes.data() + pos + 20);
        const std::uint32_t uncompressedSize = ReadLe32(bytes.data() + pos + 24);
        const std::uint16_t nameLength = ReadLe16(bytes.data() + pos + 28);
        const std::uint16_t extraLength = ReadLe16(bytes.data() + pos + 30);
        const std::uint16_t commentLength = ReadLe16(bytes.data() + pos + 32);
        const std::uint32_t localOffset = ReadLe32(bytes.data() + pos + 42);

        const std::size_t next = pos + 46u + nameLength + extraLength + commentLength;
        if (next > bytes.size())
        {
            error = L"The .acui central directory contains an invalid entry.";
            return false;
        }

        if ((flags & 0x0001u) != 0 || method != 0 || compressedSize != uncompressedSize)
        {
            error = L"This .acui uses ZIP compression/encryption not supported by format version 1. Re-export it with ACModernUI Manager.";
            return false;
        }

        const std::string name(
            reinterpret_cast<const char*>(bytes.data() + pos + 46u), nameLength);

        if (files.find(name) != files.end())
        {
            error = L"The .acui contains duplicate ZIP entry names.";
            return false;
        }

        if (static_cast<unsigned long long>(localOffset) + 30u > bytes.size() ||
            ReadLe32(bytes.data() + localOffset) != 0x04034B50u)
        {
            error = L"The .acui contains an invalid local file header.";
            return false;
        }

        const std::uint16_t localNameLength = ReadLe16(bytes.data() + localOffset + 26);
        const std::uint16_t localExtraLength = ReadLe16(bytes.data() + localOffset + 28);
        const std::size_t dataOffset = static_cast<std::size_t>(localOffset) +
            30u + localNameLength + localExtraLength;
        if (static_cast<unsigned long long>(dataOffset) + uncompressedSize > bytes.size())
        {
            error = L"The .acui contains truncated file data.";
            return false;
        }

        std::vector<BYTE> data(uncompressedSize);
        if (uncompressedSize != 0)
            memcpy(data.data(), bytes.data() + dataOffset, uncompressedSize);

        if (AcuiCrc32(data.data(), data.size()) != crc)
        {
            error = L"The .acui failed its CRC integrity check.";
            return false;
        }

        files.emplace(name, std::move(data));
        pos = next;
    }

    return true;
}

static void JsonSkipWhitespace(const std::string& text, std::size_t& pos)
{
    while (pos < text.size() &&
        (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
        ++pos;
}

static bool ParseJsonString(const std::string& text, std::size_t& pos, std::string& value)
{
    JsonSkipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != '"')
        return false;
    ++pos;
    value.clear();

    while (pos < text.size())
    {
        const char ch = text[pos++];
        if (ch == '"')
            return true;
        if (ch != '\\')
        {
            value.push_back(ch);
            continue;
        }

        if (pos >= text.size())
            return false;
        const char escaped = text[pos++];
        switch (escaped)
        {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: return false;
        }
    }
    return false;
}

static bool FindJsonFieldValue(
    const std::string& object,
    const char* key,
    std::size_t& valuePos)
{
    const std::string needle = std::string("\"") + key + "\"";
    std::size_t pos = object.find(needle);
    if (pos == std::string::npos)
        return false;
    pos += needle.size();
    JsonSkipWhitespace(object, pos);
    if (pos >= object.size() || object[pos] != ':')
        return false;
    ++pos;
    JsonSkipWhitespace(object, pos);
    valuePos = pos;
    return true;
}

static bool JsonStringField(
    const std::string& object,
    const char* key,
    std::string& value,
    bool required = true)
{
    std::size_t pos = 0;
    if (!FindJsonFieldValue(object, key, pos))
    {
        value.clear();
        return !required;
    }
    return ParseJsonString(object, pos, value);
}

static bool JsonUIntField(
    const std::string& object,
    const char* key,
    std::uint32_t& value)
{
    std::size_t pos = 0;
    if (!FindJsonFieldValue(object, key, pos))
        return false;

    std::size_t end = pos;
    while (end < object.size() && object[end] >= '0' && object[end] <= '9')
        ++end;
    if (end == pos)
        return false;

    try
    {
        const unsigned long parsed = std::stoul(object.substr(pos, end - pos), nullptr, 10);
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool JsonHexField(
    const std::string& object,
    const char* key,
    std::uint32_t& value)
{
    std::string text;
    if (!JsonStringField(object, key, text, true))
        return false;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text.erase(0, 2);
    return ParseHex(text, value);
}

static bool ExtractJsonObject(
    const std::string& text,
    std::size_t& pos,
    std::string& object)
{
    JsonSkipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != '{')
        return false;

    const std::size_t start = pos;
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (; pos < text.size(); ++pos)
    {
        const char ch = text[pos];
        if (inString)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (ch == '\\')
            {
                escaped = true;
                continue;
            }
            if (ch == '"')
                inString = false;
            continue;
        }

        if (ch == '"')
        {
            inString = true;
            continue;
        }
        if (ch == '{')
            ++depth;
        else if (ch == '}')
        {
            --depth;
            if (depth == 0)
            {
                ++pos;
                object = text.substr(start, pos - start);
                return true;
            }
        }
    }
    return false;
}

static bool ParseAcuiManifest(
    const std::vector<BYTE>& manifestBytes,
    AcuiManifest& manifest,
    std::wstring& error)
{
    const std::string json(
        reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size());

    manifest = AcuiManifest();
    if (!JsonUIntField(json, "formatVersion", manifest.formatVersion) ||
        manifest.formatVersion != 1)
    {
        error = L"Unsupported or missing ACUI formatVersion. This Manager supports formatVersion 1.";
        return false;
    }

    if (!JsonStringField(json, "name", manifest.name, true) || manifest.name.empty())
    {
        error = L"The ACUI manifest does not contain a valid pack name.";
        return false;
    }
    if (!JsonStringField(json, "author", manifest.author, false) ||
        !JsonStringField(json, "description", manifest.description, false))
    {
        error = L"The ACUI manifest contains invalid text metadata.";
        return false;
    }

    std::size_t texturesPos = 0;
    if (!FindJsonFieldValue(json, "textures", texturesPos) ||
        texturesPos >= json.size() || json[texturesPos] != '[')
    {
        error = L"The ACUI manifest is missing its textures array.";
        return false;
    }
    ++texturesPos;

    std::unordered_set<std::string> seen;
    while (true)
    {
        JsonSkipWhitespace(json, texturesPos);
        if (texturesPos >= json.size())
        {
            error = L"The ACUI textures array is truncated.";
            return false;
        }
        if (json[texturesPos] == ']')
            break;
        if (json[texturesPos] == ',')
        {
            ++texturesPos;
            continue;
        }

        std::string object;
        if (!ExtractJsonObject(json, texturesPos, object))
        {
            error = L"The ACUI textures array contains malformed JSON.";
            return false;
        }

        AcuiTextureEntry entry;
        if (!JsonStringField(object, "did", entry.did, true) ||
            !JsonUIntField(object, "width", entry.width) ||
            !JsonUIntField(object, "height", entry.height) ||
            !JsonUIntField(object, "imageSize", entry.imageSize) ||
            !JsonHexField(object, "pixelFormat", entry.pixelFormat) ||
            !JsonStringField(object, "file", entry.file, true))
        {
            error = L"An ACUI texture entry is missing required metadata.";
            return false;
        }

        std::transform(entry.did.begin(), entry.did.end(), entry.did.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        std::uint32_t didValue = 0;
        if (entry.did.size() != 8 || !ParseHex(entry.did, didValue) ||
            entry.file != "textures/" + entry.did + ".rgb")
        {
            error = L"An ACUI texture entry has an invalid DID or file path.";
            return false;
        }

        if (!seen.insert(entry.did).second)
        {
            error = L"The ACUI manifest contains the same texture DID more than once.";
            return false;
        }
        manifest.textures.push_back(entry);
    }

    if (manifest.textures.empty())
    {
        error = L"The ACUI pack contains no replacement textures.";
        return false;
    }

    return true;
}

static const TextureRecord* FindTextureRecordByDid(const std::string& did)
{
    for (const TextureRecord& texture : g_Textures)
    {
        if (texture.did == did)
            return &texture;
    }
    return nullptr;
}

static bool ValidateAcuiPack(
    const AcuiManifest& manifest,
    const std::unordered_map<std::string, std::vector<BYTE>>& files,
    std::wstring& error)
{
    for (const AcuiTextureEntry& entry : manifest.textures)
    {
        const TextureRecord* texture = FindTextureRecordByDid(entry.did);
        if (texture == nullptr)
        {
            error = L"This pack references texture DID " + ToWide(entry.did) +
                L", which is not present in this client's texture catalog.";
            return false;
        }

        if (!IsPreviewable(*texture))
        {
            error = L"Texture DID " + ToWide(entry.did) +
                L" uses a replacement format that AC Customs v1 does not support.";
            return false;
        }

        if (entry.width != texture->width ||
            entry.height != texture->height ||
            entry.imageSize != texture->imageSize ||
            entry.pixelFormat != texture->pixelFormat)
        {
            error = L"Texture metadata mismatch for DID " + ToWide(entry.did) +
                L". The pack is not compatible with this texture catalog.";
            return false;
        }

        const auto fileIt = files.find(entry.file);
        if (fileIt == files.end())
        {
            error = L"The ACUI archive is missing " + ToWide(entry.file) + L".";
            return false;
        }

        if (fileIt->second.size() != texture->imageSize)
        {
            error = L"Replacement byte count mismatch for DID " + ToWide(entry.did) + L".";
            return false;
        }

        const std::uint32_t bytesPerPixel = texture->pixelFormat == 0x15 ? 4u : 3u;
        const unsigned long long expected =
            static_cast<unsigned long long>(texture->width) *
            static_cast<unsigned long long>(texture->height) * bytesPerPixel;
        if (expected != texture->imageSize)
        {
            error = L"The local texture catalog has inconsistent size metadata for DID " +
                ToWide(entry.did) + L".";
            return false;
        }
    }
    return true;
}

static bool DirectoryExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool DeleteDirectoryTree(const std::wstring& path)
{
    if (!DirectoryExists(path))
        return true;

    WIN32_FIND_DATAW findData = {};
    const std::wstring pattern = path + L"\\*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(findData.cFileName, L".") == 0 ||
                wcscmp(findData.cFileName, L"..") == 0)
                continue;

            const std::wstring child = path + L"\\" + findData.cFileName;
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if (!DeleteDirectoryTree(child))
                {
                    FindClose(find);
                    return false;
                }
            }
            else
            {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(child.c_str()))
                {
                    FindClose(find);
                    return false;
                }
            }
        } while (FindNextFileW(find, &findData));
        FindClose(find);
    }

    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}

static bool StageImportedWorkspace(
    const AcuiManifest& manifest,
    const std::unordered_map<std::string, std::vector<BYTE>>& files,
    std::wstring& error)
{
    DeleteDirectoryTree(g_AppPaths.importStageDirectory);
    if (!CreateDirectoryW(g_AppPaths.importStageDirectory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        error = L"Could not create the temporary import workspace.";
        return false;
    }

    for (const AcuiTextureEntry& entry : manifest.textures)
    {
        const auto fileIt = files.find(entry.file);
        if (fileIt == files.end())
        {
            error = L"A validated pack file disappeared during staging.";
            DeleteDirectoryTree(g_AppPaths.importStageDirectory);
            return false;
        }

        const std::wstring destination =
            std::wstring(g_AppPaths.importStageDirectory) + L"\\" + ToWide(entry.did) + L".rgb";
        if (!WriteBinaryFile(destination, fileIt->second))
        {
            error = L"Could not write the staged replacement for DID " +
                ToWide(entry.did) + L".";
            DeleteDirectoryTree(g_AppPaths.importStageDirectory);
            return false;
        }
    }

    return true;
}

static bool CommitImportedWorkspace(std::wstring& error)
{
    if (!DeleteDirectoryTree(g_AppPaths.importBackupDirectory))
    {
        error = L"Could not clear an old ACUI import backup directory.";
        return false;
    }

    const bool hadWorkspace = DirectoryExists(g_AppPaths.replacementDirectory);
    if (hadWorkspace)
    {
        if (!MoveFileExW(g_AppPaths.replacementDirectory.c_str(), g_AppPaths.importBackupDirectory.c_str(),
                MOVEFILE_WRITE_THROUGH))
        {
            error = L"Could not move the current replacement workspace aside.";
            return false;
        }
    }

    if (!MoveFileExW(g_AppPaths.importStageDirectory.c_str(), g_AppPaths.replacementDirectory.c_str(),
            MOVEFILE_WRITE_THROUGH))
    {
        const DWORD importError = GetLastError();
        if (hadWorkspace)
            MoveFileExW(g_AppPaths.importBackupDirectory.c_str(), g_AppPaths.replacementDirectory.c_str(),
                MOVEFILE_WRITE_THROUGH);

        wchar_t message[256] = {};
        swprintf_s(message,
            L"Could not activate the imported workspace (Windows error %lu).",
            importError);
        error = message;
        return false;
    }

    if (hadWorkspace)
        DeleteDirectoryTree(g_AppPaths.importBackupDirectory);
    return true;
}

static std::vector<std::size_t> GetReplacementTextureIndices()
{
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < g_Textures.size(); ++i)
    {
        if (HasReplacement(g_Textures[i]))
            indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), [](std::size_t a, std::size_t b) {
        return g_Textures[a].did < g_Textures[b].did;
    });
    return indices;
}

static bool ChooseAcuiOpen(HWND owner, std::wstring& path)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"AC Customs UI Packs (*.acui)\0*.acui\0All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Import AC Customs UI Pack";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrDefExt = L"acui";
    const bool chosen =
        GetOpenFileNameW(&dialog) != FALSE;

    if (owner != nullptr && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
        SetActiveWindow(owner);
    }

    if (!chosen)
        return false;

    path = fileName;
    return true;
}

static bool ChooseAcuiSave(HWND owner, const std::string& packName, std::wstring& path)
{
    std::wstring suggested = ToWide(packName.empty() ? "ACCustomsPack" : packName);
    for (wchar_t& ch : suggested)
    {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }
    if (suggested.empty())
        suggested = L"ACCustomsPack";
    suggested += L".acui";

    wchar_t fileName[MAX_PATH] = {};
    wcsncpy_s(fileName, suggested.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"AC Customs UI Packs (*.acui)\0*.acui\0All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Export AC Customs UI Pack";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    dialog.lpstrDefExt = L"acui";
    const bool chosen =
        GetSaveFileNameW(&dialog) != FALSE;

    if (owner != nullptr && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
        SetActiveWindow(owner);
    }

    if (!chosen)
        return false;

    path = fileName;
    return true;
}

static void SelectReplacementsTab()
{
    g_ActivePrefix.clear();
    g_ReplacementsOnly = true;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;
    TabCtrl_SetCurSel(g_TabControl,
        static_cast<int>(g_TabPrefixes.size() + 1));
    ClearLargePreview();
    ClearReplacementPreview();
    PopulateList();
    QueueSmallPreviewsForActiveTab();
    QueueVisiblePreviews();
}

static void ExportUiPack(HWND owner)
{
    std::vector<std::size_t> textureIndices = GetReplacementTextureIndices();
    if (textureIndices.empty())
    {
        MessageBoxW(owner,
            L"There are no replacement textures to export.",
            L"Export UI Pack", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::string packName;
    const std::wstring initial = g_CurrentPackName.empty()
        ? L"Untitled UI Pack" : ToWide(g_CurrentPackName);
    if (!PromptForCustomTabName(owner, L"Export UI Pack - Pack Name", initial, packName))
        return;

    packName = SanitizeNoteForStorage(packName);
    if (packName.empty())
        return;

    std::wstring destination;
    if (!ChooseAcuiSave(owner, packName, destination))
        return;

    std::vector<StoredZipEntry> zipEntries;
    StoredZipEntry manifestEntry;
    manifestEntry.name = "manifest.json";
    const std::string manifestJson = BuildAcuiManifestJson(
        packName, g_CurrentPackAuthor, g_CurrentPackDescription, textureIndices);
    manifestEntry.data.assign(manifestJson.begin(), manifestJson.end());
    zipEntries.push_back(std::move(manifestEntry));

    for (std::size_t textureIndex : textureIndices)
    {
        const TextureRecord& texture = g_Textures[textureIndex];
        if (!IsPreviewable(texture))
        {
            MessageBoxW(owner,
                (L"Cannot export unsupported replacement format for DID " +
                    ToWide(texture.did) + L".").c_str(),
                L"Export UI Pack", MB_OK | MB_ICONERROR);
            return;
        }

        StoredZipEntry entry;
        entry.name = "textures/" + texture.did + ".rgb";
        if (!ReadBinaryFile(ReplacementRawPath(texture), entry.data) ||
            entry.data.size() != texture.imageSize)
        {
            MessageBoxW(owner,
                (L"Replacement data is missing or has the wrong byte count for DID " +
                    ToWide(texture.did) + L".").c_str(),
                L"Export UI Pack", MB_OK | MB_ICONERROR);
            return;
        }
        zipEntries.push_back(std::move(entry));
    }

    std::wstring error;
    if (!WriteStoredZip(destination, zipEntries, error))
    {
        MessageBoxW(owner, error.c_str(), L"Export UI Pack", MB_OK | MB_ICONERROR);
        return;
    }

    g_CurrentPackName = packName;
    g_CurrentPackSource = destination;
    SetWindowTextW(g_InfoText,
        (L"Exported " + std::to_wstring(textureIndices.size()) +
            L" replacements to " + destination).c_str());

    MessageBoxW(owner,
        (L"UI pack exported successfully.\r\n\r\nPack: " + ToWide(packName) +
            L"\r\nTextures: " + std::to_wstring(textureIndices.size()) +
            L"\r\n\r\n" + destination).c_str(),
        L"Export UI Pack", MB_OK | MB_ICONINFORMATION);
}

static void ImportUiPack(HWND owner)
{
    std::wstring source;
    if (!ChooseAcuiOpen(owner, source))
        return;

    std::unordered_map<std::string, std::vector<BYTE>> files;
    std::wstring error;
    if (!LoadStoredZip(source, files, error))
    {
        MessageBoxW(owner, error.c_str(), L"Import UI Pack", MB_OK | MB_ICONERROR);
        return;
    }

    const auto manifestIt = files.find("manifest.json");
    if (manifestIt == files.end())
    {
        MessageBoxW(owner, L"The selected .acui does not contain manifest.json.",
            L"Import UI Pack", MB_OK | MB_ICONERROR);
        return;
    }

    AcuiManifest manifest;
    if (!ParseAcuiManifest(manifestIt->second, manifest, error) ||
        !ValidateAcuiPack(manifest, files, error))
    {
        MessageBoxW(owner, error.c_str(), L"Import UI Pack", MB_OK | MB_ICONERROR);
        return;
    }

    const std::vector<std::size_t> existing = GetReplacementTextureIndices();
    if (!existing.empty())
    {
        const std::wstring warning =
            L"Importing this UI pack will replace the current replacement workspace.\r\n\r\n"
            L"Current replacements: " + std::to_wstring(existing.size()) +
            L"\r\nImported replacements: " + std::to_wstring(manifest.textures.size()) +
            L"\r\n\r\nExport your current work first if you want to keep it.\r\n\r\nContinue?";
        if (MessageBoxW(owner, warning.c_str(), L"Import UI Pack",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    if (!StageImportedWorkspace(manifest, files, error) ||
        !CommitImportedWorkspace(error))
    {
        DeleteDirectoryTree(g_AppPaths.importStageDirectory);
        MessageBoxW(owner, error.c_str(), L"Import UI Pack", MB_OK | MB_ICONERROR);
        return;
    }

    g_CurrentPackName = manifest.name;
    g_CurrentPackAuthor = manifest.author;
    g_CurrentPackDescription = manifest.description;
    g_CurrentPackSource = source;

    SelectReplacementsTab();
    SetWindowTextW(g_InfoText,
        (L"Imported " + ToWide(manifest.name) + L" (" +
            std::to_wstring(manifest.textures.size()) + L" replacements).").c_str());

    MessageBoxW(owner,
        (L"UI pack imported successfully.\r\n\r\nPack: " + ToWide(manifest.name) +
            L"\r\nTextures: " + std::to_wstring(manifest.textures.size()) +
            L"\r\n\r\nThe Replacements workspace now contains exactly this pack.").c_str(),
        L"Import UI Pack", MB_OK | MB_ICONINFORMATION);
}

static void SetBusyState(
    bool busy,
    const wchar_t* message)
{
    SetWindowTextW(
        g_InfoText,
        message);

    EnableWindow(
        g_ReplaceButton,
        busy ? FALSE : TRUE);

    SendMessageW(
        g_ProgressBar,
        PBM_SETMARQUEE,
        busy ? TRUE : FALSE,
        30);

    ShowWindow(
        g_ProgressBar,
        busy ? SW_SHOW : SW_HIDE);
}

static void PumpMessagesWhileWaiting(
    HANDLE processHandle)
{
    for (;;)
    {
        const DWORD result =
            MsgWaitForMultipleObjects(
                1,
                &processHandle,
                FALSE,
                INFINITE,
                QS_ALLINPUT);

        if (result == WAIT_OBJECT_0)
            break;

        if (result == WAIT_OBJECT_0 + 1)
        {
            MSG message = {};

            while (PeekMessageW(
                       &message,
                       nullptr,
                       0,
                       0,
                       PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
}

static void CopySelectedDidToClipboard(
    HWND owner)
{
    if (g_SelectedRow < 0 ||
        static_cast<std::size_t>(g_SelectedRow) >= g_Textures.size())
    {
        return;
    }

    const std::wstring did =
        ToWide(
            g_Textures[
                static_cast<std::size_t>(
                    g_SelectedRow)].did);

    if (!OpenClipboard(owner))
        return;

    EmptyClipboard();

    const SIZE_T bytes =
        (did.size() + 1) *
        sizeof(wchar_t);

    HGLOBAL memory =
        GlobalAlloc(
            GMEM_MOVEABLE,
            bytes);

    if (memory != nullptr)
    {
        void* destination =
            GlobalLock(memory);

        if (destination != nullptr)
        {
            memcpy(
                destination,
                did.c_str(),
                bytes);

            GlobalUnlock(memory);

            if (SetClipboardData(
                    CF_UNICODETEXT,
                    memory) != nullptr)
            {
                memory = nullptr;
                SetWindowTextW(
                    g_InfoText,
                    L"DID copied to clipboard.");
            }
        }

        if (memory != nullptr)
            GlobalFree(memory);
    }

    CloseClipboard();
}


// Photoshop and other editors may interpret CF_DIBV5's alpha channel
// differently. Publish a standard straight-alpha PNG in addition to DIBV5,
// and offer a Save PNG action as a reliable file-based fallback.
//
// Input is the exact BGRA buffer used by Manager's texture preview; no
// alpha premultiplication or background flattening is applied.
static bool EncodeBgraAsPng(const std::vector<BYTE>& bgra,
                            UINT width, UINT height,
                            std::vector<BYTE>& png)
{
    png.clear();
    const std::uint64_t count =
        static_cast<std::uint64_t>(width) * height * 4u;
    if (g_WicFactory == nullptr || width == 0 || height == 0 ||
        count == 0 || count > 0xFFFFFFFFull ||
        bgra.size() != static_cast<std::size_t>(count))
        return false;

    // WIC PNG encoder's 32-bit RGBA path uses straight (unassociated) alpha.
    // Converting channels rather than applying a color transform ensures
    // transparent RGB bytes and soft edges survive exactly as authored.
    std::vector<BYTE> rgba(bgra.size());
    for (std::size_t i = 0; i < bgra.size(); i += 4u)
    {
        rgba[i]     = bgra[i + 2u];
        rgba[i + 1u] = bgra[i + 1u];
        rgba[i + 2u] = bgra[i];
        rgba[i + 3u] = bgra[i + 3u];
    }

    IStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (SUCCEEDED(hr))
        hr = g_WicFactory->CreateEncoder(
            GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(hr)) hr = frame->Initialize(properties);
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    // WIC may negotiate BGRA rather than RGBA. Both support straight alpha,
    // but the channel order MUST match the negotiated encoder format.
    const BYTE* encodePixels = rgba.data();
    if (SUCCEEDED(hr) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        encodePixels = bgra.data();
    else if (SUCCEEDED(hr) && !IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA))
        hr = E_FAIL;
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(height, width * 4u,
            static_cast<UINT>(rgba.size()), const_cast<BYTE*>(encodePixels));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    if (SUCCEEDED(hr))
    {
        STATSTG stat = {};
        HGLOBAL encoded = nullptr;
        hr = stream->Stat(&stat, STATFLAG_NONAME);
        if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream, &encoded);
        if (SUCCEEDED(hr) && encoded != nullptr && stat.cbSize.QuadPart > 0 &&
            stat.cbSize.QuadPart <= static_cast<ULONGLONG>(GlobalSize(encoded)))
        {
            const void* bytes = GlobalLock(encoded);
            if (bytes != nullptr)
            {
                const std::size_t size =
                    static_cast<std::size_t>(stat.cbSize.QuadPart);
                png.assign(static_cast<const BYTE*>(bytes),
                    static_cast<const BYTE*>(bytes) + size);
                GlobalUnlock(encoded);
            }
            else hr = E_FAIL;
        }
        else hr = E_FAIL;
    }

    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (FAILED(hr)) png.clear();
    return SUCCEEDED(hr) && !png.empty();
}

// GMEM_MOVEABLE is mandatory for SetClipboardData. Ownership transfers only
// when SetClipboardData succeeds, so callers must free any remaining handle.
static HGLOBAL MakeClipboardBlock(const BYTE* bytes, std::size_t size)
{
    if (bytes == nullptr || size == 0) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory == nullptr) return nullptr;
    void* output = GlobalLock(memory);
    if (output == nullptr)
    {
        GlobalFree(memory);
        return nullptr;
    }
    memcpy(output, bytes, size);
    GlobalUnlock(memory);
    return memory;
}

static bool CopyBgraToClipboard(HWND owner,
                                const std::vector<BYTE>& bgra,
                                UINT width, UINT height,
                                const wchar_t* label)
{
    const std::uint64_t rawSize =
        static_cast<std::uint64_t>(width) * height * 4u;
    if (rawSize == 0 || rawSize > 0xFFFFFFFFull ||
        bgra.size() != static_cast<std::size_t>(rawSize))
        return false;

    // DIBV5 remains available to applications that don't support PNG.
    // The header describes top-down, non-premultiplied BGRA with true alpha.
    const std::size_t dibSize = sizeof(BITMAPV5HEADER) + bgra.size();
    std::vector<BYTE> dib(dibSize, 0u);
    BITMAPV5HEADER* header =
        reinterpret_cast<BITMAPV5HEADER*>(dib.data());
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = static_cast<LONG>(width);
    header->bV5Height = -static_cast<LONG>(height);
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(bgra.size());
    header->bV5RedMask = 0x00FF0000u;
    header->bV5GreenMask = 0x0000FF00u;
    header->bV5BlueMask = 0x000000FFu;
    header->bV5AlphaMask = 0xFF000000u;
    header->bV5CSType = LCS_sRGB;
    memcpy(dib.data() + sizeof(BITMAPV5HEADER),
        bgra.data(), bgra.size());

    HGLOBAL dibMemory = MakeClipboardBlock(dib.data(), dib.size());
    if (!dibMemory) return false;

    std::vector<BYTE> png;
    const bool pngReady = EncodeBgraAsPng(bgra, width, height, png);
    const UINT pngFormat = pngReady ? RegisterClipboardFormatW(L"PNG") : 0;
    HGLOBAL pngMemory = pngFormat ? MakeClipboardBlock(png.data(), png.size()) : nullptr;

    if (!OpenClipboard(owner))
    {
        GlobalFree(dibMemory);
        if (pngMemory) GlobalFree(pngMemory);
        return false;
    }
    EmptyClipboard();
    bool copiedPng = false;
    bool copiedDib = false;
    // Advertise PNG first; the receiving application selects which format it
    // understands. PNG is lossless and keeps transparent pixels intact.
    if (pngMemory != nullptr && SetClipboardData(pngFormat, pngMemory) != nullptr)
    {
        pngMemory = nullptr;
        copiedPng = true;
    }
    if (SetClipboardData(CF_DIBV5, dibMemory) != nullptr)
    {
        dibMemory = nullptr;
        copiedDib = true;
    }
    CloseClipboard();
    if (pngMemory) GlobalFree(pngMemory);
    if (dibMemory) GlobalFree(dibMemory);

    if (copiedPng || copiedDib)
    {
        SetWindowTextW(g_InfoText, copiedPng
            ? label
            : L"Copied DIBV5 only (PNG clipboard encoding unavailable).");
        return true;
    }
    SetWindowTextW(g_InfoText, L"Could not write texture to clipboard.");
    return false;
}

// Used by both Copy Replacement and Save Replacement PNG. 24-bit originals
// are converted to fully opaque BGRA; 32-bit originals keep their alpha.
static bool LoadReplacementPixelsBgra(const TextureRecord& texture,
                                      std::vector<BYTE>& bgra)
{
    bgra.clear();
    if (!IsPreviewable(texture) || !HasReplacement(texture) ||
        texture.width == 0 || texture.height == 0)
        return false;
    const std::uint32_t bpp = texture.pixelFormat == 0x15u ? 4u : 3u;
    const std::uint64_t count =
        static_cast<std::uint64_t>(texture.width) * texture.height;
    if (count * bpp != texture.imageSize || count > 0xFFFFFFFFull / 4u)
        return false;
    std::vector<BYTE> raw;
    if (!ReadBinaryFile(ReplacementRawPath(texture), raw) ||
        raw.size() != texture.imageSize)
        return false;
    bgra.resize(static_cast<std::size_t>(count) * 4u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
    {
        const BYTE* source = raw.data() + i * bpp;
        BYTE* dest = bgra.data() + i * 4u;
        dest[0] = source[0];
        dest[1] = source[1];
        dest[2] = source[2];
        dest[3] = bpp == 4u ? source[3] : 255u;
    }
    return true;
}

static void CopySelectedTextureToClipboard(HWND owner)
{
    if (g_SelectedRow < 0 ||
        static_cast<std::size_t>(g_SelectedRow) >= g_Textures.size())
        return;
    const TextureRecord& texture =
        g_Textures[static_cast<std::size_t>(g_SelectedRow)];
    std::vector<BYTE> bgra;
    if (!IsPreviewable(texture) || !LoadDatTexturePixels(texture, bgra))
    {
        SetWindowTextW(g_InfoText,
            L"Could not read original texture pixels from the DAT.");
        return;
    }
    CopyBgraToClipboard(owner, bgra, texture.width, texture.height,
        L"Original copied as PNG + DIBV5 (alpha preserved).");
}

static void CopySelectedReplacementTextureToClipboard(HWND owner)
{
    if (g_SelectedRow < 0 ||
        static_cast<std::size_t>(g_SelectedRow) >= g_Textures.size())
        return;
    const TextureRecord& texture =
        g_Textures[static_cast<std::size_t>(g_SelectedRow)];
    std::vector<BYTE> bgra;
    if (!LoadReplacementPixelsBgra(texture, bgra))
    {
        SetWindowTextW(g_InfoText,
            L"Could not read the replacement texture pixels.");
        return;
    }
    CopyBgraToClipboard(owner, bgra, texture.width, texture.height,
        L"Replacement copied as PNG + DIBV5 (alpha preserved).");
}

// Save a real transparent PNG when a target editor chooses an opaque clipboard
// format. This also gives artists a repeatable test independent of clipboard.
static void SaveSelectedTextureAsPng(HWND owner, bool replacement)
{
    if (g_SelectedRow < 0 ||
        static_cast<std::size_t>(g_SelectedRow) >= g_Textures.size())
    {
        MessageBoxW(owner, L"Select a texture before exporting its PNG.",
            L"AC Customs - Save Texture", MB_OK | MB_ICONWARNING);
        return;
    }

    const TextureRecord& texture =
        g_Textures[static_cast<std::size_t>(g_SelectedRow)];
    std::wstring name(texture.did.begin(), texture.did.end());
    name += replacement ? L"_replacement.png" : L"_original.png";
    wchar_t filename[32768] = {};
    wcscpy_s(filename, _countof(filename), name.c_str());

    // Show Save As BEFORE decoding and encoding. Previously, an encoding
    // failure returned silently without ever displaying the dialog.
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG Images (*.png)\0*.png\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Save AC Customs Texture as Transparent PNG";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog))
    {
        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0)
        {
            const std::wstring message = L"Could not open Save As dialog (error " +
                std::to_wstring(dialogError) + L").";
            MessageBoxW(owner, message.c_str(), L"AC Customs - Save Texture",
                MB_OK | MB_ICONERROR);
        }
        return; // User canceled if dialogError == 0.
    }

    std::vector<BYTE> bgra;
    const bool loaded = replacement
        ? LoadReplacementPixelsBgra(texture, bgra)
        : (IsPreviewable(texture) && LoadDatTexturePixels(texture, bgra));
    if (!loaded)
    {
        MessageBoxW(owner, L"Could not read the selected texture pixels.",
            L"AC Customs - Save Texture", MB_OK | MB_ICONERROR);
        return;
    }
    std::vector<BYTE> png;
    if (!EncodeBgraAsPng(bgra, texture.width, texture.height, png))
    {
        MessageBoxW(owner,
            L"Could not encode this texture as a PNG with alpha.\n"
            L"No file was written. Please report the texture DID.",
            L"AC Customs - Save Texture", MB_OK | MB_ICONERROR);
        return;
    }
    if (!WriteBinaryFile(filename, png))
    {
        MessageBoxW(owner, L"Could not write the PNG to the selected path.",
            L"AC Customs - Save Texture", MB_OK | MB_ICONERROR);
        return;
    }
    SetWindowTextW(g_InfoText,
        L"Saved transparent PNG with original alpha channel.");
}



static bool ChooseReplacementPng(
    HWND owner,
    std::wstring& path)
{
    wchar_t fileName[MAX_PATH] = {};

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"PNG Images (*.png)\0*.png\0"
        L"All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle =
        L"Choose replacement PNG";
    dialog.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY |
        OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"png";

    const bool chosen =
        GetOpenFileNameW(&dialog) != FALSE;

    // Keep AC Customs as the active app after the shell dialog closes.
    if (owner != nullptr && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
        SetActiveWindow(owner);
    }

    if (!chosen)
        return false;

    path = fileName;
    return true;
}

static bool RunConverter(
    HWND owner,
    const TextureRecord& texture,
    const std::wstring& pngPath)
{
    if (!FileExists(
            g_AppPaths.converterPath))
    {
        const std::wstring message =
            L"ACModernUIConverter.exe is missing.\r\n\r\n"
            L"Expected location:\r\n" +
            g_AppPaths.converterPath;

        MessageBoxW(
            owner,
            message.c_str(),
            WINDOW_TITLE,
            MB_OK |
                MB_ICONERROR);

        return false;
    }

    if (!EnsureDirectoryExists(
            g_AppPaths.replacementDirectory))
    {
        MessageBoxW(
            owner,
            L"Could not create the AC Customs replacement workspace.",
            WINDOW_TITLE,
            MB_OK |
                MB_ICONERROR);

        return false;
    }

    const std::wstring outputPath =
        JoinPath(
            g_AppPaths.replacementDirectory,
            ToWide(texture.did) + L".rgb");

    // Pass metadata from the Manager's direct client_portal.dat scan. This
    // deliberately removes the old converter dependency on dat_textures.csv
    // and on the retired development-workspace layout.
    std::wstring commandLine =
        L"\"" +
        g_AppPaths.converterPath +
        L"\" replace " +
        ToWide(texture.did) +
        L" " + std::to_wstring(texture.width) +
        L" " + std::to_wstring(texture.height) +
        L" " + std::to_wstring(texture.imageSize) +
        L" " + std::to_wstring(texture.pixelFormat) +
        L" \"" + pngPath +
        L"\" \"" + outputPath +
        L"\"";

    std::vector<wchar_t> mutableCommand(
        commandLine.begin(),
        commandLine.end());

    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process = {};

    const BOOL started =
        CreateProcessW(
            g_AppPaths.converterPath.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            g_AppPaths.userRoot.c_str(),
            &startup,
            &process);

    if (!started)
    {
        wchar_t message[512] = {};

        swprintf_s(
            message,
            L"Could not start ACModernUIConverter.exe.\r\n\r\n"
            L"Windows error: %lu",
            GetLastError());

        MessageBoxW(
            owner,
            message,
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);

        return false;
    }

    SetBusyState(
        true,
        L"Converting replacement PNG...");

    PumpMessagesWhileWaiting(
        process.hProcess);

    SetBusyState(
        false,
        L"Replacement conversion finished.");

    DWORD exitCode = 1;

    GetExitCodeProcess(
        process.hProcess,
        &exitCode);

    CloseHandle(
        process.hThread);

    CloseHandle(
        process.hProcess);

    // The converter is a console helper. It now runs without creating a
    // console window, and we explicitly restore the Manager as a safeguard.
    if (owner != nullptr && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
        SetActiveWindow(owner);
    }

    if (exitCode != 0)
    {
        MessageBoxW(
            owner,
            L"The converter rejected the replacement PNG.\r\n\r\n"
            L"Check that its dimensions exactly match the selected texture.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);

        return false;
    }

    const DWORD attributes =
        GetFileAttributesW(
            outputPath.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        MessageBoxW(
            owner,
            L"The converter reported success, but the replacement file "
            L"could not be found.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);

        return false;
    }

    // Do not call the legacy UpdateReplacementPreview() here. That routine
    // explicitly ShowWindow()s the old Win32 preview control, which is hidden
    // behind the modern ImGui shell and can otherwise reappear over the
    // Inspector. The modern Inspector reloads its own GPU preview after this
    // function succeeds.

    if (g_SelectedListRow >= 0)
    {
        SetSubItem(
            g_SelectedListRow,
            5,
            L"REPLACED");
    }

    SetWindowTextW(
        g_InfoText,
        L"Replacement ready.");

    // Successful replacement is intentionally non-modal. The Inspector updates
    // in place; only actual errors still produce a message box.
    return true;
}

static bool ReplaceSelectedTexture(
    HWND owner)
{
    if (g_SelectedRow < 0 ||
        static_cast<std::size_t>(g_SelectedRow) >=
            g_Textures.size())
    {
        MessageBoxW(
            owner,
            L"Select a texture first.",
            WINDOW_TITLE,
            MB_OK | MB_ICONINFORMATION);

        return false;
    }

    const TextureRecord& texture =
        g_Textures[
            static_cast<std::size_t>(
                g_SelectedRow)];

    if (!IsPreviewable(texture))
    {
        MessageBoxW(
            owner,
            L"Automatic replacement currently supports only "
            L"BGR (0x14) and BGRA (0x15) textures.",
            WINDOW_TITLE,
            MB_OK | MB_ICONINFORMATION);

        return false;
    }

    std::wstring pngPath;

    if (!ChooseReplacementPng(
            owner,
            pngPath))
    {
        return false;
    }

    return RunConverter(
        owner,
        texture,
        pngPath);
}

static void RemoveReplacementTexture(
    HWND owner,
    std::size_t textureIndex)
{
    if (textureIndex >= g_Textures.size())
        return;

    const TextureRecord& texture = g_Textures[textureIndex];

    if (!HasReplacement(texture))
    {
        SetWindowTextW(g_InfoText, L"This texture has no replacement file.");
        return;
    }

    const std::wstring path = ReplacementRawPath(texture);
    const std::wstring message =
        L"Remove the replacement for DID " + ToWide(texture.did) +
        L"?\r\n\r\nThe original DAT texture will be used again.";

    if (MessageBoxW(
            owner,
            message.c_str(),
            L"Remove Replacement",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    if (!DeleteFileW(path.c_str()))
    {
        wchar_t errorMessage[512] = {};
        swprintf_s(
            errorMessage,
            L"Could not delete the replacement file.\r\n\r\nWindows error: %lu",
            GetLastError());

        MessageBoxW(
            owner,
            errorMessage,
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);
        return;
    }

    ClearReplacementPreview();
    SetWindowTextW(g_ReplacementTitle, L"Current Replacement: None");
    SetWindowTextW(g_InfoText, L"Replacement removed. Original texture will be used.");

    if (g_ReplacementsOnly)
    {
        ClearLargePreview();
        SetWindowTextW(g_DetailsText, L"Select a texture to inspect it.");
        PopulateList();
        QueueSmallPreviewsForActiveTab();
        QueueVisiblePreviews();
    }
    else
    {
        const int row = FindListRowForTexture(textureIndex);
        if (row >= 0)
            SetSubItem(row, 5, L"");

        if (g_SelectedRow == static_cast<int>(textureIndex))
            UpdateReplacementPreview(texture);
    }
}


// -----------------------------------------------------------------------------
// Live UI editor
//
// Replays a captured AC UI snapshot inside the Manager. The snapshot is only
// scene/layout metadata; replacement files still live in the Manager's normal
// The user replacement workspace. Clicking a replayed file texture selects
// the corresponding TextureRecord, so both the tab browser and Live UI operate
// on one replacement set.
// -----------------------------------------------------------------------------

struct LiveRectI
{
    int l = 0;
    int t = 0;
    int r = 0;
    int b = 0;
};

struct LiveTextureMeta
{
    std::uint32_t did = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t pixelFormat = 0;
    std::string fileName;
};

struct LiveTexturePixels
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<unsigned char> bgra;
    bool loaded = false;
};

struct LiveGeneratedMeta
{
    std::uint32_t surface = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t pixelFormat = 0;
    std::string fileName;
};

struct LiveControlState
{
    std::uint32_t control = 0;
    std::uint32_t root = 0;
    LiveRectI rect;
    LiveRectI rootRect;
    std::uint32_t currentState = 1;
    std::uint32_t presentMask = 0;
    std::uint32_t did1 = 0;
    std::uint32_t did2 = 0;
    std::uint32_t did3 = 0;
    std::uint32_t did6 = 0;
    std::uint32_t did7 = 0;
    std::uint32_t did8 = 0;
    std::uint32_t did13 = 0;
};

struct LiveItemProvenance
{
    std::uint32_t itemId = 0;
    std::uint32_t primaryDid = 0;
    std::vector<std::uint32_t> componentDids;
};

struct LiveBlit
{
    std::uint32_t sequence = 0;
    std::uint32_t root = 0;
    LiveRectI rootRect;
    std::uint32_t sourceSurface = 0;
    std::uint32_t sourceDid = 0;
    std::uint32_t control = 0;
    std::uint32_t itemId = 0;
    std::uint32_t itemPrimaryDid = 0;
    LiveRectI sourceRect;
    LiveRectI destRect;
    std::uint32_t blendMode = 0;
    std::uint32_t alphaBits = 0x3F800000u;
};

struct LiveRootScene
{
    std::uint32_t root = 0;
    LiveRectI rect;
    std::vector<std::size_t> blitIndices;
    std::uint32_t nodeIndex = 0xFFFFFFFFu;
    std::uint32_t depth = 0xFFFFFFFFu;
    std::uint32_t siblingIndex = 0xFFFFFFFFu;
};

struct LiveNodeOrder
{
    std::uint32_t index = 0xFFFFFFFFu;
    std::uint32_t depth = 0xFFFFFFFFu;
    std::uint32_t siblingIndex = 0xFFFFFFFFu;
};

struct LiveRootSurface
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
};

struct LiveHitResult
{
    bool hit = false;
    std::uint32_t did = 0;
    std::uint32_t sourceSurface = 0;
    std::uint32_t control = 0;
    std::uint32_t itemId = 0;
    bool generatedItem = false;
    LiveRectI sceneRect;
};

static const wchar_t* LIVE_WINDOW_CLASS =
    L"ACModernUIManagerLiveEditorWindow";

static HWND g_LiveWindow = nullptr;
static HWND g_LiveLoadButton = nullptr;
static HWND g_LiveReloadButton = nullptr;
static HWND g_LiveToggleReplacementsButton = nullptr;
static HWND g_LiveBoundsCheck = nullptr;
static HWND g_LiveDetailsText = nullptr;
static HWND g_LiveStateText = nullptr;
static HWND g_LiveOriginalTitle = nullptr;
static HWND g_LiveOriginalPreview = nullptr;
static HWND g_LiveReplacementTitle = nullptr;
static HWND g_LiveReplacementPreview = nullptr;
static HWND g_LiveForegroundTitle = nullptr;
static HWND g_LiveForegroundPreview = nullptr;

// Generated foreground candidate preview tiles.
static HWND g_LiveGeneratedMatchPreviews[3] = {};
static HWND g_LiveReplaceButton = nullptr;
static HWND g_LiveRemoveButton = nullptr;
static HWND g_LiveLocateButton = nullptr;
static HWND g_LiveMirrorButton = nullptr;
static HWND g_LiveStatusText = nullptr;

static HBITMAP g_LiveOriginalBitmap = nullptr;
static HBITMAP g_LiveReplacementBitmap = nullptr;
static HBITMAP g_LiveForegroundBitmap = nullptr;
static HBITMAP g_LiveGeneratedMatchBitmaps[3] = {};

static std::wstring g_LiveSnapshotPath;
static std::wstring g_LiveAssetTextureDir;
static std::wstring g_LiveAssetGeneratedDir;
static std::unordered_map<std::uint32_t, LiveTextureMeta> g_LiveMeta;
static std::unordered_map<std::uint32_t, LiveTexturePixels> g_LiveTextureCache;
static std::unordered_map<std::uint32_t, LiveGeneratedMeta> g_LiveGeneratedMeta;
static std::unordered_map<std::uint32_t, LiveTexturePixels> g_LiveGeneratedCache;
static std::unordered_map<std::uint32_t, LiveControlState> g_LiveControlStates;
static std::unordered_map<std::uint32_t, LiveItemProvenance> g_LiveItemProvenance;
static std::vector<LiveBlit> g_LiveBlits;
static std::vector<LiveRootScene> g_LiveRoots;
static std::vector<std::size_t> g_LiveRootDrawOrder;
static LiveRectI g_LiveDesktopRect;
static bool g_LiveHaveDesktopRect = false;
static bool g_LiveUseReplacements = true;
static bool g_LiveShowBounds = false;
static std::vector<unsigned char> g_LiveCanvas;
static int g_LiveCanvasWidth = 0;
static int g_LiveCanvasHeight = 0;
static int g_LiveRendered = 0;
static int g_LiveMissing = 0;
static int g_LiveGenerated = 0;
static int g_LiveGeneratedRendered = 0;
static int g_LiveRootsRendered = 0;
static int g_LiveDrawX = 0;
static int g_LiveDrawY = 0;
static int g_LiveDrawW = 0;
static int g_LiveDrawH = 0;
static LiveRectI g_LiveHoveredSceneRect;
static bool g_LiveHaveHoveredSceneRect = false;
static LiveRectI g_LiveSelectedSceneRect;
static bool g_LiveHaveSelectedSceneRect = false;
static std::uint32_t g_LiveHoveredDid = 0;
static std::uint32_t g_LiveHoveredControl = 0;
static std::uint32_t g_LivePressedControl = 0;
static bool g_LiveMouseDown = false;
static bool g_LiveTrackingMouseLeave = false;
static std::uint32_t g_LiveSelectedControl = 0;
static std::uint32_t g_LiveSelectedItemId = 0;
static bool g_LiveSelectedGeneratedItem = false;
static std::uint32_t g_LiveSelectedGeneratedSurface = 0;
static std::uint64_t g_LiveSelectedGeneratedHash = 0;
static std::size_t g_LiveSelectedTextureIndex = static_cast<std::size_t>(-1);
static std::wstring g_LiveLastStateText;
static std::wstring g_LiveLastStatusText;

enum class LiveMirrorStatus : int
{
    Frozen = 0,
    WaitingForAC = 1,
    Connected = 2,
    Capturing = 3,
    VanillaRequired = 4,
    Busy = 5,
    PathError = 6,
    SceneUnavailable = 7,
    ProtocolError = 8
};

static const wchar_t* LIVE_MIRROR_PIPE =
    L"\\\\.\\pipe\\ACCustoms.LiveUI.v1";
static std::atomic<bool> g_LiveMirrorEnabled(false);
static std::atomic<bool> g_LiveMirrorStop(false);
static std::atomic<bool> g_LiveMirrorRunning(false);
static std::atomic<int> g_LiveMirrorStatus(
    static_cast<int>(LiveMirrorStatus::Frozen));
static std::atomic<std::uint32_t> g_LiveMirrorGeneration(0);
static std::atomic<std::uint32_t> g_LiveMirrorAppliedGeneration(0);
static std::atomic<std::uint32_t> g_LiveMirrorFailedGeneration(0);
static std::wstring g_LiveMirrorSnapshotPath;
static std::mutex g_LiveMirrorErrorMutex;
static std::wstring g_LiveMirrorLastError;

static std::string LiveHex8(std::uint32_t value)
{
    char buffer[16] = {};
    sprintf_s(buffer, "%08X", value);
    return buffer;
}

static bool LiveExtractString(
    const std::string& line,
    const char* key,
    std::string& value)
{
    const std::string token =
        std::string("\"") + key + "\":\"";
    const std::size_t p = line.find(token);
    if (p == std::string::npos)
        return false;

    const std::size_t start = p + token.size();
    const std::size_t end = line.find('"', start);
    if (end == std::string::npos)
        return false;

    value = line.substr(start, end - start);
    return true;
}

static bool LiveExtractUInt(
    const std::string& line,
    const char* key,
    std::uint32_t& value)
{
    const std::string token =
        std::string("\"") + key + "\":";
    const std::size_t p = line.find(token);
    if (p == std::string::npos)
        return false;

    const char* start = line.c_str() + p + token.size();
    value = static_cast<std::uint32_t>(std::strtoul(start, nullptr, 10));
    return true;
}

static bool LiveExtractHexField(
    const std::string& line,
    const char* key,
    std::uint32_t& value)
{
    std::string text;
    if (!LiveExtractString(line, key, text))
        return false;

    if (text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X'))
    {
        text = text.substr(2);
    }

    return ParseHex(text, value);
}

static bool LiveExtractHexArray(
    const std::string& line,
    const char* key,
    std::vector<std::uint32_t>& values)
{
    values.clear();
    const std::string token =
        std::string("\"") + key + "\":[";
    const std::size_t p = line.find(token);
    if (p == std::string::npos)
        return false;
    const std::size_t end = line.find(']', p + token.size());
    if (end == std::string::npos)
        return false;

    std::string body = line.substr(
        p + token.size(), end - (p + token.size()));
    std::size_t pos = 0;
    while (pos < body.size())
    {
        const std::size_t q1 = body.find('\"', pos);
        if (q1 == std::string::npos) break;
        const std::size_t q2 = body.find('\"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string text = body.substr(q1 + 1, q2 - q1 - 1);
        if (text.size() > 2 && text[0] == '0' &&
            (text[1] == 'x' || text[1] == 'X'))
            text = text.substr(2);
        std::uint32_t value = 0;
        if (ParseHex(text, value))
            values.push_back(value);
        pos = q2 + 1;
    }
    return true;
}

static bool LiveExtractRect(
    const std::string& line,
    const char* key,
    LiveRectI& rect)
{
    const std::string token =
        std::string("\"") + key + "\":[";
    const std::size_t p = line.find(token);
    if (p == std::string::npos)
        return false;

    const std::size_t end = line.find(']', p + token.size());
    if (end == std::string::npos)
        return false;

    std::string values =
        line.substr(p + token.size(), end - (p + token.size()));
    for (char& c : values)
        if (c == ',')
            c = ' ';

    std::istringstream input(values);
    return static_cast<bool>(
        input >> rect.l >> rect.t >> rect.r >> rect.b);
}

static bool LiveFileExists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool LiveReadFileBytes(
    const std::wstring& path,
    std::vector<unsigned char>& bytes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open())
        return false;

    const std::streamoff size = input.tellg();
    if (size <= 0)
        return false;

    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(size));

    return input.good() ||
        input.gcount() == static_cast<std::streamsize>(size);
}

static std::wstring LiveStripJsonExtension(const std::wstring& path)
{
    if (path.size() >= 5)
    {
        const std::wstring suffix = path.substr(path.size() - 5);
        if (_wcsicmp(suffix.c_str(), L".json") == 0)
            return path.substr(0, path.size() - 5);
    }
    return path;
}

static bool LiveFindLatestSnapshot(std::wstring& path)
{
    const std::wstring& directory =
        g_AppPaths.snapshotsRoot;

    if (directory.empty())
        return false;

    const std::wstring pattern =
        directory + L"\\ui_snapshot_*.json";

    WIN32_FIND_DATAW data = {};
    HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    FILETIME newest = {};
    std::wstring newestPath;

    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;

        if (!found || CompareFileTime(&data.ftLastWriteTime, &newest) > 0)
        {
            found = true;
            newest = data.ftLastWriteTime;
            newestPath = directory + L"\\" + data.cFileName;
        }
    }
    while (FindNextFileW(handle, &data));

    FindClose(handle);

    if (!found)
        return false;

    path = newestPath;
    return true;
}

static bool LiveChooseSnapshot(
    HWND owner,
    std::wstring& path)
{
    wchar_t fileName[MAX_PATH] = {};

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"AC Customs UI Snapshots (*.json)\0*.json\0"
        L"All Files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Open AC Customs UI snapshot";
    dialog.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY;
    dialog.lpstrDefExt = L"json";

    if (!GetOpenFileNameW(&dialog))
        return false;

    path = fileName;
    return true;
}

static bool LiveLoadSnapshot(
    const std::wstring& path,
    std::wstring& error)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        error = L"Could not open the snapshot JSON.";
        return false;
    }

    std::unordered_map<std::uint32_t, LiveTextureMeta> meta;
    std::unordered_map<std::uint32_t, LiveGeneratedMeta> generatedMeta;
    std::unordered_map<std::uint32_t, LiveControlState> controlStates;
    std::unordered_map<std::uint32_t, LiveItemProvenance> itemProvenance;
    std::unordered_map<std::uint32_t, LiveNodeOrder> nodeOrder;
    std::vector<LiveBlit> blits;
    LiveRectI desktopRect = {};
    bool haveDesktopRect = false;

    enum class Section
    {
        None,
        Nodes,
        Assets,
        GeneratedAssets,
        ControlStates,
        ItemProvenance,
        Blits
    };

    Section section = Section::None;
    std::string line;

    while (std::getline(input, line))
    {
        if (line.find("\"nodes\": [") != std::string::npos)
        {
            section = Section::Nodes;
            continue;
        }
        if (line.find("\"textureAssets\": [") != std::string::npos)
        {
            section = Section::Assets;
            continue;
        }
        if (line.find("\"generatedAssets\": [") != std::string::npos)
        {
            section = Section::GeneratedAssets;
            continue;
        }
        if (line.find("\"controlStates\": [") != std::string::npos)
        {
            section = Section::ControlStates;
            continue;
        }
        if (line.find("\"itemProvenance\": [") != std::string::npos)
        {
            section = Section::ItemProvenance;
            continue;
        }
        if (line.find("\"blits\": [") != std::string::npos)
        {
            section = Section::Blits;
            continue;
        }
        if ((section == Section::Nodes ||
             section == Section::Assets ||
             section == Section::GeneratedAssets ||
             section == Section::ControlStates ||
             section == Section::ItemProvenance ||
             section == Section::Blits) &&
            (line.find("  ],") != std::string::npos ||
             line.find("  ]") != std::string::npos))
        {
            section = Section::None;
            continue;
        }

        if (section == Section::Nodes &&
            line.find("\"index\"") != std::string::npos)
        {
            std::uint32_t address = 0;
            LiveNodeOrder order;
            LiveExtractUInt(line, "index", order.index);
            LiveExtractUInt(line, "depth", order.depth);
            LiveExtractUInt(line, "siblingIndex", order.siblingIndex);
            if (LiveExtractHexField(line, "address", address))
                nodeOrder[address] = order;

            if (!haveDesktopRect && order.depth == 0)
            {
                LiveRectI rect;
                if (LiveExtractRect(line, "rect", rect))
                {
                    desktopRect = rect;
                    haveDesktopRect = true;
                }
            }
        }
        else if (section == Section::Assets &&
                 line.find("\"dumpStatus\"") != std::string::npos)
        {
            LiveTextureMeta texture;
            std::string didText;
            std::string pixelFormatText;

            if (!LiveExtractString(line, "did", didText))
                continue;

            if (didText.size() > 2 && didText[0] == '0' &&
                (didText[1] == 'x' || didText[1] == 'X'))
            {
                didText = didText.substr(2);
            }

            if (!ParseHex(didText, texture.did))
                continue;

            LiveExtractUInt(line, "width", texture.width);
            LiveExtractUInt(line, "height", texture.height);
            LiveExtractUInt(line, "imageSize", texture.imageSize);

            if (LiveExtractString(line, "pixelFormat", pixelFormatText))
            {
                if (pixelFormatText.size() > 2 &&
                    pixelFormatText[0] == '0' &&
                    (pixelFormatText[1] == 'x' || pixelFormatText[1] == 'X'))
                {
                    pixelFormatText = pixelFormatText.substr(2);
                }
                ParseHex(pixelFormatText, texture.pixelFormat);
            }

            LiveExtractString(line, "file", texture.fileName);
            meta[texture.did] = texture;
        }
        else if (section == Section::GeneratedAssets &&
                 line.find("\"dumpStatus\"") != std::string::npos)
        {
            LiveGeneratedMeta generated;
            LiveExtractHexField(line, "surface", generated.surface);
            LiveExtractUInt(line, "width", generated.width);
            LiveExtractUInt(line, "height", generated.height);
            LiveExtractUInt(line, "imageSize", generated.imageSize);
            LiveExtractHexField(line, "pixelFormat", generated.pixelFormat);
            LiveExtractString(line, "file", generated.fileName);
            if (generated.surface != 0)
                generatedMeta[generated.surface] = generated;
        }
        else if (section == Section::ControlStates &&
                 line.find("\"control\"") != std::string::npos)
        {
            LiveControlState control;
            LiveExtractHexField(line, "control", control.control);
            LiveExtractHexField(line, "root", control.root);
            LiveExtractRect(line, "rect", control.rect);
            LiveExtractRect(line, "rootRect", control.rootRect);
            LiveExtractUInt(line, "currentState", control.currentState);
            LiveExtractUInt(line, "presentMask", control.presentMask);
            LiveExtractHexField(line, "did1", control.did1);
            LiveExtractHexField(line, "did2", control.did2);
            LiveExtractHexField(line, "did3", control.did3);
            LiveExtractHexField(line, "did6", control.did6);
            LiveExtractHexField(line, "did7", control.did7);
            LiveExtractHexField(line, "did8", control.did8);
            LiveExtractHexField(line, "did13", control.did13);
            if (control.control != 0)
                controlStates[control.control] = control;
        }
        else if (section == Section::ItemProvenance &&
                 line.find("\"itemId\"") != std::string::npos)
        {
            LiveItemProvenance item;
            LiveExtractHexField(line, "itemId", item.itemId);
            LiveExtractHexField(line, "primaryDid", item.primaryDid);
            LiveExtractHexArray(line, "componentDids", item.componentDids);
            if (item.itemId != 0)
                itemProvenance[item.itemId] = item;
        }
        else if (section == Section::Blits &&
                 line.find("\"firstSequence\"") != std::string::npos)
        {
            LiveBlit blit;
            LiveExtractUInt(line, "firstSequence", blit.sequence);
            LiveExtractHexField(line, "root", blit.root);
            LiveExtractRect(line, "rootRect", blit.rootRect);
            LiveExtractHexField(line, "sourceSurface", blit.sourceSurface);
            LiveExtractHexField(line, "sourceDid", blit.sourceDid);
            LiveExtractHexField(line, "control", blit.control);
            LiveExtractHexField(line, "itemId", blit.itemId);
            LiveExtractHexField(line, "itemPrimaryDid", blit.itemPrimaryDid);
            LiveExtractRect(line, "sourceRect", blit.sourceRect);
            LiveExtractRect(line, "destRect", blit.destRect);
            LiveExtractHexField(line, "blendMode", blit.blendMode);
            LiveExtractHexField(line, "alphaBits", blit.alphaBits);

            if (blit.root != 0)
                blits.push_back(blit);
        }
    }

    if (meta.empty())
    {
        error =
            L"This snapshot has no textureAssets section.\r\n\r\n"
            L"Use a formatVersion 2 (or newer) UI snapshot with dumped assets.";
        return false;
    }

    if (blits.empty())
    {
        error = L"This snapshot contains no backed-root blits.";
        return false;
    }

    std::sort(
        blits.begin(),
        blits.end(),
        [](const LiveBlit& a, const LiveBlit& b)
        {
            return a.sequence < b.sequence;
        });

    std::map<std::uint32_t, LiveRootScene> grouped;
    for (std::size_t i = 0; i < blits.size(); ++i)
    {
        LiveRootScene& root = grouped[blits[i].root];
        root.root = blits[i].root;
        root.rect = blits[i].rootRect;
        root.blitIndices.push_back(i);
    }

    std::vector<LiveRootScene> roots;
    roots.reserve(grouped.size());
    for (auto& pair : grouped)
    {
        const auto orderIt = nodeOrder.find(pair.second.root);
        if (orderIt != nodeOrder.end())
        {
            pair.second.nodeIndex = orderIt->second.index;
            pair.second.depth = orderIt->second.depth;
            pair.second.siblingIndex = orderIt->second.siblingIndex;
        }
        roots.push_back(std::move(pair.second));
    }

    if (!haveDesktopRect)
    {
        desktopRect = roots.front().rect;
        for (const LiveRootScene& root : roots)
        {
            desktopRect.l = min(desktopRect.l, root.rect.l);
            desktopRect.t = min(desktopRect.t, root.rect.t);
            desktopRect.r = max(desktopRect.r, root.rect.r);
            desktopRect.b = max(desktopRect.b, root.rect.b);
        }
        haveDesktopRect = true;
    }

    std::vector<std::size_t> drawOrder;
    drawOrder.reserve(roots.size());
    for (std::size_t i = 0; i < roots.size(); ++i)
        drawOrder.push_back(i);

    std::stable_sort(
        drawOrder.begin(),
        drawOrder.end(),
        [&roots](std::size_t a, std::size_t b)
        {
            const LiveRootScene& left = roots[a];
            const LiveRootScene& right = roots[b];

            if (left.nodeIndex != right.nodeIndex)
                return left.nodeIndex < right.nodeIndex;
            if (left.depth != right.depth)
                return left.depth < right.depth;
            return left.siblingIndex < right.siblingIndex;
        });

    g_LiveSnapshotPath = path;
    g_LiveAssetTextureDir =
        LiveStripJsonExtension(path) + L"_assets\\textures";
    g_LiveAssetGeneratedDir =
        LiveStripJsonExtension(path) + L"_assets\\generated";
    g_LiveMeta.swap(meta);
    g_LiveGeneratedMeta.swap(generatedMeta);
    g_LiveControlStates.swap(controlStates);
    g_LiveItemProvenance.swap(itemProvenance);
    g_LiveBlits.swap(blits);
    g_LiveRoots.swap(roots);
    g_LiveRootDrawOrder.swap(drawOrder);
    g_LiveDesktopRect = desktopRect;
    g_LiveHaveDesktopRect = haveDesktopRect;
    g_LiveTextureCache.clear();
    g_LiveGeneratedCache.clear();
    g_LiveHoveredDid = 0;
    g_LiveHoveredControl = 0;
    g_LivePressedControl = 0;
    g_LiveMouseDown = false;
    g_LiveSelectedControl = 0;
    g_LiveSelectedItemId = 0;
    g_LiveSelectedGeneratedItem = false;
    g_LiveSelectedGeneratedSurface = 0;
    g_LiveSelectedGeneratedHash = 0;
    g_LiveHaveHoveredSceneRect = false;
    g_LiveHaveSelectedSceneRect = false;
    g_LiveSelectedTextureIndex = static_cast<std::size_t>(-1);
    return true;
}

static LiveTexturePixels LiveLoadTexture(std::uint32_t did)
{
    LiveTexturePixels result;

    const auto found = g_LiveMeta.find(did);
    if (found == g_LiveMeta.end())
        return result;

    const LiveTextureMeta& meta = found->second;
    if (meta.width == 0 || meta.height == 0 ||
        meta.imageSize == 0 || meta.fileName.empty())
    {
        return result;
    }

    std::wstring path =
        g_LiveAssetTextureDir + L"\\" + ToWide(meta.fileName);

    if (g_LiveUseReplacements)
    {
        const std::wstring replacement =
            g_AppPaths.replacementDirectory +
            L"\\" + ToWide(meta.fileName);

        if (LiveFileExists(replacement))
            path = replacement;
    }

    std::vector<unsigned char> raw;
    if (!LiveReadFileBytes(path, raw) ||
        raw.size() != meta.imageSize)
    {
        return result;
    }

    result.width = meta.width;
    result.height = meta.height;
    result.bgra.resize(
        static_cast<std::size_t>(meta.width) *
        static_cast<std::size_t>(meta.height) * 4u);

    if (meta.pixelFormat == 0x15u)
    {
        if (raw.size() != result.bgra.size())
            return LiveTexturePixels{};

        result.bgra = std::move(raw);
    }
    else if (meta.pixelFormat == 0x14u)
    {
        const std::size_t expected =
            static_cast<std::size_t>(meta.width) *
            static_cast<std::size_t>(meta.height) * 3u;

        if (raw.size() != expected)
            return LiveTexturePixels{};

        for (std::size_t i = 0, j = 0;
             i < raw.size();
             i += 3, j += 4)
        {
            result.bgra[j + 0] = raw[i + 0];
            result.bgra[j + 1] = raw[i + 1];
            result.bgra[j + 2] = raw[i + 2];
            result.bgra[j + 3] = 255;
        }
    }
    else
    {
        return LiveTexturePixels{};
    }

    result.loaded = true;
    return result;
}

static const LiveTexturePixels* LiveGetTexture(std::uint32_t did)
{
    auto found = g_LiveTextureCache.find(did);
    if (found == g_LiveTextureCache.end())
    {
        found = g_LiveTextureCache.emplace(
            did,
            LiveLoadTexture(did)).first;
    }

    return found->second.loaded ? &found->second : nullptr;
}

static LiveTexturePixels LiveLoadGenerated(std::uint32_t surface)
{
    LiveTexturePixels result;
    const auto found = g_LiveGeneratedMeta.find(surface);
    if (found == g_LiveGeneratedMeta.end())
        return result;
    const LiveGeneratedMeta& meta = found->second;
    if (meta.width == 0 || meta.height == 0 || meta.fileName.empty())
        return result;

    const std::wstring path =
        g_LiveAssetGeneratedDir + L"\\" + ToWide(meta.fileName);
    std::vector<unsigned char> raw;
    if (!LiveReadFileBytes(path, raw))
        return result;
    const std::size_t expected =
        static_cast<std::size_t>(meta.width) *
        static_cast<std::size_t>(meta.height) * 4u;
    if (raw.size() != expected)
        return result;

    result.width = meta.width;
    result.height = meta.height;
    result.bgra = std::move(raw);
    result.loaded = true;
    return result;
}

static const LiveTexturePixels* LiveGetGenerated(std::uint32_t surface)
{
    auto found = g_LiveGeneratedCache.find(surface);
    if (found == g_LiveGeneratedCache.end())
    {
        found = g_LiveGeneratedCache.emplace(
            surface, LiveLoadGenerated(surface)).first;
    }
    return found->second.loaded ? &found->second : nullptr;
}

static std::uint64_t LiveHashGeneratedPixels(const LiveTexturePixels& texture)
{
    if (!texture.loaded || texture.width == 0 || texture.height == 0 ||
        texture.bgra.size() != static_cast<std::size_t>(texture.width) *
            static_cast<std::size_t>(texture.height) * 4u)
    {
        return 0;
    }

    std::uint64_t hash = 14695981039346656037ull;
    const std::uint64_t prime = 1099511628211ull;

    auto addByte = [&](unsigned char value)
    {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= prime;
    };

    auto addUInt32 = [&](std::uint32_t value)
    {
        addByte(static_cast<unsigned char>(value & 0xFFu));
        addByte(static_cast<unsigned char>((value >> 8) & 0xFFu));
        addByte(static_cast<unsigned char>((value >> 16) & 0xFFu));
        addByte(static_cast<unsigned char>((value >> 24) & 0xFFu));
    };

    addUInt32(texture.width);
    addUInt32(texture.height);

    for (const unsigned char value : texture.bgra)
        addByte(value);

    return hash;
}

static std::wstring LiveGeneratedHashText(std::uint64_t hash)
{
    wchar_t buffer[32] = {};

    swprintf_s(
        buffer,
        L"%016llX",
        static_cast<unsigned long long>(hash));

    return buffer;
}

static HBITMAP LiveCreateGeneratedPreviewBitmap(
    const LiveTexturePixels& texture,
    int canvasSize)
{
    if (!texture.loaded ||
        texture.width == 0 ||
        texture.height == 0 ||
        canvasSize <= 0 ||
        texture.bgra.size() !=
            static_cast<std::size_t>(texture.width) *
            static_cast<std::size_t>(texture.height) * 4u)
    {
        return nullptr;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth =
        canvasSize;
    bitmapInfo.bmiHeader.biHeight =
        -canvasSize;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression =
        BI_RGB;

    void* bits = nullptr;

    HDC screenDC = GetDC(nullptr);

    HBITMAP bitmap =
        CreateDIBSection(
            screenDC,
            &bitmapInfo,
            DIB_RGB_COLORS,
            &bits,
            nullptr,
            0);

    ReleaseDC(nullptr, screenDC);

    if (bitmap == nullptr || bits == nullptr)
        return nullptr;

    ZeroMemory(
        bits,
        static_cast<std::size_t>(canvasSize) *
            static_cast<std::size_t>(canvasSize) *
            4u);

    const double scaleX =
        static_cast<double>(canvasSize) /
        static_cast<double>(texture.width);

    const double scaleY =
        static_cast<double>(canvasSize) /
        static_cast<double>(texture.height);

    const double scale =
        min(scaleX, scaleY);

    int drawWidth =
        static_cast<int>(
            static_cast<double>(texture.width) *
            scale);

    int drawHeight =
        static_cast<int>(
            static_cast<double>(texture.height) *
            scale);

    if (drawWidth < 1)
        drawWidth = 1;
    if (drawHeight < 1)
        drawHeight = 1;

    const int offsetX =
        (canvasSize - drawWidth) / 2;

    const int offsetY =
        (canvasSize - drawHeight) / 2;

    unsigned char* destination =
        static_cast<unsigned char*>(bits);

    for (int y = 0; y < drawHeight; ++y)
    {
        const std::uint32_t sourceY =
            static_cast<std::uint32_t>(
                (static_cast<unsigned long long>(y) *
                 texture.height) /
                static_cast<unsigned int>(drawHeight));

        for (int x = 0; x < drawWidth; ++x)
        {
            const std::uint32_t sourceX =
                static_cast<std::uint32_t>(
                    (static_cast<unsigned long long>(x) *
                     texture.width) /
                    static_cast<unsigned int>(drawWidth));

            const unsigned char* sourcePixel =
                &texture.bgra[
                    (static_cast<std::size_t>(sourceY) *
                         texture.width +
                     sourceX) *
                    4u];

            unsigned char* destinationPixel =
                destination +
                (static_cast<std::size_t>(offsetY + y) *
                     static_cast<std::size_t>(canvasSize) +
                 static_cast<std::size_t>(offsetX + x)) *
                    4u;

            destinationPixel[0] = sourcePixel[0];
            destinationPixel[1] = sourcePixel[1];
            destinationPixel[2] = sourcePixel[2];
            destinationPixel[3] = 255;
        }
    }

    return bitmap;
}

static bool LiveStatePresent(const LiveControlState& control, std::uint32_t state)
{
    return state < 32 && (control.presentMask & (1u << state)) != 0;
}

static std::uint32_t LiveStateDid(
    const LiveControlState& control,
    std::uint32_t state)
{
    switch (state)
    {
        case 1: return control.did1;
        case 2: return control.did2;
        case 3: return control.did3;
        case 6: return control.did6;
        case 7: return control.did7;
        case 8: return control.did8;
        case 13: return control.did13;
        default: return 0;
    }
}

static std::uint32_t LiveEffectiveDidForBlit(const LiveBlit& blit)
{
    if ((blit.sourceDid & 0xFF000000u) != 0x06000000u ||
        blit.control == 0)
        return blit.sourceDid;

    const auto found = g_LiveControlStates.find(blit.control);
    if (found == g_LiveControlStates.end())
        return blit.sourceDid;

    const LiveControlState& control = found->second;

    // Only substitute the image that participates in this control's state
    // table. A control may own other file-backed children that must continue
    // drawing their own DIDs unchanged.
    const std::uint32_t stateDids[] =
        { control.did1, control.did2, control.did3, control.did6,
          control.did7, control.did8, control.did13 };
    bool stateTexture = false;
    for (const std::uint32_t did : stateDids)
    {
        if (did != 0 && blit.sourceDid == did)
        {
            stateTexture = true;
            break;
        }
    }
    if (!stateTexture)
        return blit.sourceDid;

    const bool selectedFamily =
        control.currentState == 6 ||
        control.currentState == 7 ||
        control.currentState == 8;
    const std::uint32_t baseState = selectedFamily ? 6u : 1u;
    std::uint32_t desiredState = baseState;

    if (g_LiveHoveredControl == blit.control)
    {
        if (g_LiveMouseDown && g_LivePressedControl == blit.control)
            desiredState = selectedFamily ? 8u : 3u;
        else
            desiredState = selectedFamily ? 7u : 2u;
    }

    // A missing state or a present state without a direct texture assignment
    // inherits the family's base image, matching what we observed for Stats.
    if (LiveStatePresent(control, desiredState))
    {
        const std::uint32_t did = LiveStateDid(control, desiredState);
        if ((did & 0xFF000000u) == 0x06000000u)
            return did;
    }

    const std::uint32_t baseDid = LiveStateDid(control, baseState);
    if ((baseDid & 0xFF000000u) == 0x06000000u)
        return baseDid;
    return blit.sourceDid;
}

static std::uint32_t LiveEditableDidForBlit(const LiveBlit& blit)
{
    const std::uint32_t effective = LiveEffectiveDidForBlit(blit);
    if ((effective & 0xFF000000u) == 0x06000000u)
        return effective;

    std::uint32_t primary = blit.itemPrimaryDid;
    if (!primary && blit.itemId)
    {
        const auto found = g_LiveItemProvenance.find(blit.itemId);
        if (found != g_LiveItemProvenance.end())
            primary = found->second.primaryDid;
    }
    return (primary & 0xFF000000u) == 0x06000000u ? primary : 0;
}

static void LiveBlendPixel(
    unsigned char* destination,
    const unsigned char* source,
    float globalAlpha,
    bool alphaBlend)
{
    if (!alphaBlend)
    {
        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = source[3];
        return;
    }

    float sourceAlpha =
        (static_cast<float>(source[3]) / 255.0f) * globalAlpha;

    if (sourceAlpha < 0.0f)
        sourceAlpha = 0.0f;
    if (sourceAlpha > 1.0f)
        sourceAlpha = 1.0f;

    const float inverse = 1.0f - sourceAlpha;

    destination[0] = static_cast<unsigned char>(
        source[0] * sourceAlpha + destination[0] * inverse + 0.5f);
    destination[1] = static_cast<unsigned char>(
        source[1] * sourceAlpha + destination[1] * inverse + 0.5f);
    destination[2] = static_cast<unsigned char>(
        source[2] * sourceAlpha + destination[2] * inverse + 0.5f);
    destination[3] = static_cast<unsigned char>(
        (sourceAlpha +
         (static_cast<float>(destination[3]) / 255.0f) * inverse) *
        255.0f + 0.5f);
}

static void LiveDrawBlitToBuffer(
    const LiveBlit& blit,
    const LiveTexturePixels& texture,
    std::vector<unsigned char>& buffer,
    int bufferWidth,
    int bufferHeight)
{
    const int sourceWidth =
        blit.sourceRect.r - blit.sourceRect.l;
    const int sourceHeight =
        blit.sourceRect.b - blit.sourceRect.t;
    const int destinationWidth =
        blit.destRect.r - blit.destRect.l;
    const int destinationHeight =
        blit.destRect.b - blit.destRect.t;

    if (sourceWidth <= 0 || sourceHeight <= 0 ||
        destinationWidth <= 0 || destinationHeight <= 0)
    {
        return;
    }

    float globalAlpha = 1.0f;
    memcpy(&globalAlpha, &blit.alphaBits, sizeof(globalAlpha));
    if (!(globalAlpha >= 0.0f && globalAlpha <= 8.0f))
        globalAlpha = 1.0f;

    const bool alphaBlend = blit.blendMode != 0;

    for (int y = 0; y < destinationHeight; ++y)
    {
        const int destinationY = blit.destRect.t + y;
        if (destinationY < 0 || destinationY >= bufferHeight)
            continue;

        const int sourceY =
            blit.sourceRect.t +
            static_cast<int>(
                (static_cast<long long>(y) * sourceHeight) /
                destinationHeight);

        if (sourceY < 0 ||
            sourceY >= static_cast<int>(texture.height))
        {
            continue;
        }

        for (int x = 0; x < destinationWidth; ++x)
        {
            const int destinationX = blit.destRect.l + x;
            if (destinationX < 0 || destinationX >= bufferWidth)
                continue;

            const int sourceX =
                blit.sourceRect.l +
                static_cast<int>(
                    (static_cast<long long>(x) * sourceWidth) /
                    destinationWidth);

            if (sourceX < 0 ||
                sourceX >= static_cast<int>(texture.width))
            {
                continue;
            }

            const unsigned char* source =
                &texture.bgra[
                    (static_cast<std::size_t>(sourceY) *
                         texture.width +
                     static_cast<std::size_t>(sourceX)) *
                    4u];

            unsigned char* destination =
                &buffer[
                    (static_cast<std::size_t>(destinationY) *
                         static_cast<std::size_t>(bufferWidth) +
                     static_cast<std::size_t>(destinationX)) *
                    4u];

            LiveBlendPixel(
                destination,
                source,
                globalAlpha,
                alphaBlend);
        }
    }
}

static bool LiveRenderRoot(
    const LiveRootScene& scene,
    LiveRootSurface& surface)
{
    surface.width = scene.rect.r - scene.rect.l + 1;
    surface.height = scene.rect.b - scene.rect.t + 1;

    if (surface.width <= 0 || surface.height <= 0 ||
        surface.width > 4096 || surface.height > 4096)
    {
        return false;
    }

    surface.bgra.assign(
        static_cast<std::size_t>(surface.width) *
            static_cast<std::size_t>(surface.height) * 4u,
        0);

    for (const std::size_t index : scene.blitIndices)
    {
        const LiveBlit& blit = g_LiveBlits[index];

        const LiveTexturePixels* texture = nullptr;
        const std::uint32_t effectiveDid = LiveEffectiveDidForBlit(blit);

        if ((effectiveDid & 0xFF000000u) == 0x06000000u)
        {
            texture = LiveGetTexture(effectiveDid);
        }
        else if (blit.sourceSurface != 0)
        {
            texture = LiveGetGenerated(blit.sourceSurface);
            if (texture != nullptr)
                ++g_LiveGeneratedRendered;
        }

        // If a generated runtime surface could not be dumped, fall back to
        // the primary item artwork when provenance is available.
        if (texture == nullptr && blit.itemId != 0)
        {
            const std::uint32_t itemDid = LiveEditableDidForBlit(blit);
            if (itemDid != 0)
                texture = LiveGetTexture(itemDid);
        }

        if (texture == nullptr)
        {
            if ((blit.sourceDid & 0xFF000000u) == 0x06000000u)
                ++g_LiveMissing;
            else
                ++g_LiveGenerated;
            continue;
        }

        LiveDrawBlitToBuffer(
            blit,
            *texture,
            surface.bgra,
            surface.width,
            surface.height);

        ++g_LiveRendered;
    }

    return true;
}

static void LiveCompositeRoot(
    const LiveRootSurface& source,
    int destinationX,
    int destinationY)
{
    for (int y = 0; y < source.height; ++y)
    {
        const int dy = destinationY + y;
        if (dy < 0 || dy >= g_LiveCanvasHeight)
            continue;

        for (int x = 0; x < source.width; ++x)
        {
            const int dx = destinationX + x;
            if (dx < 0 || dx >= g_LiveCanvasWidth)
                continue;

            const unsigned char* sourcePixel =
                &source.bgra[
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(source.width) +
                     static_cast<std::size_t>(x)) *
                    4u];

            if (sourcePixel[3] == 0)
                continue;

            unsigned char* destinationPixel =
                &g_LiveCanvas[
                    (static_cast<std::size_t>(dy) *
                         static_cast<std::size_t>(g_LiveCanvasWidth) +
                     static_cast<std::size_t>(dx)) *
                    4u];

            LiveBlendPixel(
                destinationPixel,
                sourcePixel,
                1.0f,
                true);
        }
    }
}

static void LiveRebuildCanvas()
{
    g_LiveCanvas.clear();
    g_LiveCanvasWidth = 0;
    g_LiveCanvasHeight = 0;
    g_LiveRendered = 0;
    g_LiveMissing = 0;
    g_LiveGenerated = 0;
    g_LiveGeneratedRendered = 0;
    g_LiveRootsRendered = 0;

    if (!g_LiveHaveDesktopRect || g_LiveRoots.empty())
        return;

    g_LiveCanvasWidth =
        g_LiveDesktopRect.r - g_LiveDesktopRect.l + 1;
    g_LiveCanvasHeight =
        g_LiveDesktopRect.b - g_LiveDesktopRect.t + 1;

    if (g_LiveCanvasWidth <= 0 || g_LiveCanvasHeight <= 0 ||
        g_LiveCanvasWidth > 8192 || g_LiveCanvasHeight > 8192)
    {
        g_LiveCanvasWidth = 0;
        g_LiveCanvasHeight = 0;
        return;
    }

    g_LiveCanvas.resize(
        static_cast<std::size_t>(g_LiveCanvasWidth) *
        static_cast<std::size_t>(g_LiveCanvasHeight) * 4u);

    for (std::size_t i = 0; i < g_LiveCanvas.size(); i += 4)
    {
        g_LiveCanvas[i + 0] = 48;
        g_LiveCanvas[i + 1] = 48;
        g_LiveCanvas[i + 2] = 48;
        g_LiveCanvas[i + 3] = 255;
    }

    for (const std::size_t rootIndex : g_LiveRootDrawOrder)
    {
        if (rootIndex >= g_LiveRoots.size())
            continue;

        const LiveRootScene& root = g_LiveRoots[rootIndex];
        LiveRootSurface surface;

        if (!LiveRenderRoot(root, surface))
            continue;

        LiveCompositeRoot(
            surface,
            root.rect.l - g_LiveDesktopRect.l,
            root.rect.t - g_LiveDesktopRect.t);

        ++g_LiveRootsRendered;
    }
}

static void LiveClearGeneratedMatchPreviewBitmaps()
{
    for (int i = 0; i < 3; ++i)
    {
        if (g_LiveGeneratedMatchPreviews[i] != nullptr)
        {
            SendMessageW(
                g_LiveGeneratedMatchPreviews[i],
                STM_SETIMAGE,
                IMAGE_BITMAP,
                0);
        }

        if (g_LiveGeneratedMatchBitmaps[i] != nullptr)
        {
            DeleteObject(
                g_LiveGeneratedMatchBitmaps[i]);

            g_LiveGeneratedMatchBitmaps[i] =
                nullptr;
        }
    }
}
static void LiveClearInspectorBitmaps()
{
    LiveClearGeneratedMatchPreviewBitmaps();
    if (g_LiveOriginalPreview != nullptr)
        SendMessageW(g_LiveOriginalPreview, STM_SETIMAGE, IMAGE_BITMAP, 0);
    if (g_LiveReplacementPreview != nullptr)
        SendMessageW(g_LiveReplacementPreview, STM_SETIMAGE, IMAGE_BITMAP, 0);

    if (g_LiveOriginalBitmap != nullptr)
    {
        DeleteObject(g_LiveOriginalBitmap);
        g_LiveOriginalBitmap = nullptr;
    }

    if (g_LiveReplacementBitmap != nullptr)
    {
        DeleteObject(g_LiveReplacementBitmap);
        g_LiveReplacementBitmap = nullptr;
    }

    if (g_LiveForegroundPreview != nullptr)
    {
        SendMessageW(
            g_LiveForegroundPreview,
            STM_SETIMAGE,
            IMAGE_BITMAP,
            0);
    }

    if (g_LiveForegroundBitmap != nullptr)
    {
        DeleteObject(
            g_LiveForegroundBitmap);

        g_LiveForegroundBitmap =
            nullptr;
    }
}

static int LiveFindTextureIndex(std::uint32_t did)
{
    const std::string target = LiveHex8(did);

    for (std::size_t i = 0; i < g_Textures.size(); ++i)
    {
        if (_stricmp(g_Textures[i].did.c_str(), target.c_str()) == 0)
            return static_cast<int>(i);
    }

    return -1;
}

struct LiveDatMatch
{
    std::size_t textureIndex =
        static_cast<std::size_t>(-1);

    double score = 0.0;
};

struct LiveLayerMatch
{
    std::size_t foregroundIndex =
        static_cast<std::size_t>(-1);

    double score = 0.0;
    double residualScore = 0.0;
    double spillPenalty = 0.0;
    double fullScore = 0.0;
    double improvement = 0.0;
    int offsetX = 0;
    int offsetY = 0;
};

struct LiveCandidatePixels
{
    std::size_t textureIndex =
        static_cast<std::size_t>(-1);

    LiveTexturePixels pixels;
    double flatScore = 0.0;
};

static LiveDatMatch
    g_LiveGeneratedBackground;

static bool
    g_LiveHaveGeneratedBackground = false;

static LiveTexturePixels
    g_LiveGeneratedResidual;

static std::vector<LiveLayerMatch>
    g_LiveGeneratedMatches;

static bool LiveLoadDatPreviewPixels(
    const TextureRecord& texture,
    LiveTexturePixels& pixels)
{
    pixels = LiveTexturePixels{};

    if (!IsPreviewable(texture))
        return false;

    if (!LoadDatTexturePixels(
            texture,
            pixels.bgra))
    {
        return false;
    }

    pixels.width = texture.width;
    pixels.height = texture.height;
    pixels.loaded = true;
    return true;
}

static double LivePixelSimilarity(
    const LiveTexturePixels& generated,
    const LiveTexturePixels& candidate)
{
    if (!generated.loaded ||
        !candidate.loaded ||
        generated.width != candidate.width ||
        generated.height != candidate.height ||
        generated.bgra.size() !=
            candidate.bgra.size() ||
        generated.bgra.empty())
    {
        return 0.0;
    }

    double totalError = 0.0;
    double maximumError = 0.0;

    const std::size_t pixelCount =
        generated.bgra.size() / 4u;

    for (std::size_t i = 0;
         i < pixelCount;
         ++i)
    {
        const unsigned char* a =
            &generated.bgra[i * 4u];

        const unsigned char* b =
            &candidate.bgra[i * 4u];

        const int alphaA =
            static_cast<int>(a[3]);

        const int alphaB =
            static_cast<int>(b[3]);

        const int visibleAlpha =
            alphaA > alphaB
                ? alphaA
                : alphaB;

        // Ignore invisible RGB garbage underneath fully
        // transparent pixels.
        if (visibleAlpha == 0)
            continue;

        const double visibility =
            static_cast<double>(
                visibleAlpha) /
            255.0;

        const int blueDiff =
            std::abs(
                static_cast<int>(a[0]) -
                static_cast<int>(b[0]));

        const int greenDiff =
            std::abs(
                static_cast<int>(a[1]) -
                static_cast<int>(b[1]));

        const int redDiff =
            std::abs(
                static_cast<int>(a[2]) -
                static_cast<int>(b[2]));

        const int alphaDiff =
            std::abs(
                alphaA -
                alphaB);

        totalError +=
            visibility *
            static_cast<double>(
                blueDiff +
                greenDiff +
                redDiff);

        maximumError +=
            visibility *
            (255.0 * 3.0);

        totalError +=
            static_cast<double>(
                alphaDiff);

        maximumError +=
            255.0;
    }

    if (maximumError <= 0.0)
        return 100.0;

    double score =
        100.0 *
        (1.0 -
         (totalError / maximumError));

    if (score < 0.0)
        score = 0.0;

    if (score > 100.0)
        score = 100.0;

    return score;
}

static LiveTexturePixels LiveCompositePixels(
    const LiveTexturePixels& background,
    const LiveTexturePixels& foreground)
{
    LiveTexturePixels result;

    if (!background.loaded ||
        !foreground.loaded ||
        background.width != foreground.width ||
        background.height != foreground.height ||
        background.bgra.size() !=
            foreground.bgra.size())
    {
        return result;
    }

    result.width =
        background.width;

    result.height =
        background.height;

    result.bgra.resize(
        background.bgra.size());

    const std::size_t pixelCount =
        result.bgra.size() / 4u;

    for (std::size_t i = 0;
         i < pixelCount;
         ++i)
    {
        const unsigned char* bg =
            &background.bgra[i * 4u];

        const unsigned char* fg =
            &foreground.bgra[i * 4u];

        unsigned char* out =
            &result.bgra[i * 4u];

        const double foregroundAlpha =
            static_cast<double>(fg[3]) /
            255.0;

        const double backgroundAlpha =
            static_cast<double>(bg[3]) /
            255.0;

        const double outputAlpha =
            foregroundAlpha +
            backgroundAlpha *
                (1.0 - foregroundAlpha);

        if (outputAlpha <= 0.0)
        {
            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
            out[3] = 0;
            continue;
        }

        for (int channel = 0;
             channel < 3;
             ++channel)
        {
            const double value =
                (
                    static_cast<double>(
                        fg[channel]) *
                        foregroundAlpha +
                    static_cast<double>(
                        bg[channel]) *
                        backgroundAlpha *
                        (1.0 -
                         foregroundAlpha)
                ) /
                outputAlpha;

            int rounded =
                static_cast<int>(
                    value + 0.5);

            if (rounded < 0)
                rounded = 0;

            if (rounded > 255)
                rounded = 255;

            out[channel] =
                static_cast<unsigned char>(
                    rounded);
        }

        int alpha =
            static_cast<int>(
                outputAlpha *
                    255.0 +
                0.5);

        if (alpha < 0)
            alpha = 0;

        if (alpha > 255)
            alpha = 255;

        out[3] =
            static_cast<unsigned char>(
                alpha);
    }

    result.loaded = true;
    return result;
}

static double LivePixelDifference255(
    const unsigned char* a,
    const unsigned char* b)
{
    const int blueDiff =
        std::abs(
            static_cast<int>(a[0]) -
            static_cast<int>(b[0]));

    const int greenDiff =
        std::abs(
            static_cast<int>(a[1]) -
            static_cast<int>(b[1]));

    const int redDiff =
        std::abs(
            static_cast<int>(a[2]) -
            static_cast<int>(b[2]));

    const int alphaDiff =
        std::abs(
            static_cast<int>(a[3]) -
            static_cast<int>(b[3]));

    const int visibleAlpha =
        max(
            static_cast<int>(a[3]),
            static_cast<int>(b[3]));

    const double visibility =
        static_cast<double>(
            visibleAlpha) /
        255.0;

    const double rgbDifference =
        (
            static_cast<double>(
                blueDiff +
                greenDiff +
                redDiff) /
            3.0
        ) *
        visibility;

    return
        rgbDifference * 0.85 +
        static_cast<double>(
            alphaDiff) *
        0.15;
}

static double LiveResidualWeightForPixel(
    const unsigned char* generated,
    const unsigned char* background)
{
    const double difference =
        LivePixelDifference255(
            generated,
            background);

    // A few channel levels of difference are normally just
    // render/conversion noise. Strong differences are treated
    // as genuine foreground.
    const double lowThreshold = 4.0;
    const double highThreshold = 24.0;

    if (difference <= lowThreshold)
        return 0.0;

    if (difference >= highThreshold)
        return 1.0;

    return
        (difference - lowThreshold) /
        (highThreshold - lowThreshold);
}

static LiveTexturePixels LiveBuildResidualPixels(
    const LiveTexturePixels& generated,
    const LiveTexturePixels& background)
{
    LiveTexturePixels result;

    if (!generated.loaded ||
        !background.loaded ||
        generated.width != background.width ||
        generated.height != background.height ||
        generated.bgra.size() !=
            background.bgra.size())
    {
        return result;
    }

    result.width =
        generated.width;

    result.height =
        generated.height;

    result.bgra.assign(
        generated.bgra.size(),
        0);

    const std::size_t pixelCount =
        generated.bgra.size() / 4u;

    for (std::size_t i = 0;
         i < pixelCount;
         ++i)
    {
        const unsigned char* source =
            &generated.bgra[i * 4u];

        const unsigned char* bg =
            &background.bgra[i * 4u];

        const double weight =
            LiveResidualWeightForPixel(
                source,
                bg);

        if (weight <= 0.05)
            continue;

        unsigned char* destination =
            &result.bgra[i * 4u];

        // Dim weak residuals and leave strong residuals at
        // full brightness. This makes the inspector preview
        // show exactly what remains unexplained.
        for (int channel = 0;
             channel < 3;
             ++channel)
        {
            int value =
                static_cast<int>(
                    static_cast<double>(
                        source[channel]) *
                    weight +
                    0.5);

            if (value < 0)
                value = 0;

            if (value > 255)
                value = 255;

            destination[channel] =
                static_cast<unsigned char>(
                    value);
        }

        destination[3] = 255;
    }

    result.loaded = true;
    return result;
}

static void LiveScoreForegroundResidual(
    const LiveTexturePixels& generated,
    const LiveTexturePixels& background,
    const LiveTexturePixels& composite,
    double& residualScore,
    double& spillPenalty)
{
    residualScore = 0.0;
    spillPenalty = 100.0;

    if (!generated.loaded ||
        !background.loaded ||
        !composite.loaded ||
        generated.bgra.size() !=
            background.bgra.size() ||
        generated.bgra.size() !=
            composite.bgra.size())
    {
        return;
    }

    double residualError = 0.0;
    double residualWeight = 0.0;

    double spillError = 0.0;
    double explainedWeight = 0.0;

    const std::size_t pixelCount =
        generated.bgra.size() / 4u;

    for (std::size_t i = 0;
         i < pixelCount;
         ++i)
    {
        const unsigned char* generatedPixel =
            &generated.bgra[i * 4u];

        const unsigned char* backgroundPixel =
            &background.bgra[i * 4u];

        const unsigned char* compositePixel =
            &composite.bgra[i * 4u];

        const double residual =
            LiveResidualWeightForPixel(
                generatedPixel,
                backgroundPixel);

        const double explained =
            1.0 - residual;

        const double compositeError =
            LivePixelDifference255(
                generatedPixel,
                compositePixel) /
            255.0;

        const double backgroundDamage =
            LivePixelDifference255(
                backgroundPixel,
                compositePixel) /
            255.0;

        residualError +=
            residual *
            compositeError;

        residualWeight +=
            residual;

        spillError +=
            explained *
            backgroundDamage;

        explainedWeight +=
            explained;
    }

    if (residualWeight > 0.0001)
    {
        residualScore =
            100.0 *
            (1.0 -
             residualError /
                 residualWeight);
    }

    if (residualScore < 0.0)
        residualScore = 0.0;

    if (residualScore > 100.0)
        residualScore = 100.0;

    if (explainedWeight > 0.0001)
    {
        spillPenalty =
            100.0 *
            spillError /
            explainedWeight;
    }
    else
    {
        spillPenalty = 0.0;
    }

    if (spillPenalty < 0.0)
        spillPenalty = 0.0;

    if (spillPenalty > 100.0)
        spillPenalty = 100.0;
}
static void LiveCompositePixelOver(
    const unsigned char* background,
    const unsigned char* foreground,
    unsigned char* output)
{
    const double fa =
        static_cast<double>(foreground[3]) /
        255.0;

    const double ba =
        static_cast<double>(background[3]) /
        255.0;

    const double oa =
        fa +
        ba * (1.0 - fa);

    if (oa <= 0.0)
    {
        output[0] = 0;
        output[1] = 0;
        output[2] = 0;
        output[3] = 0;
        return;
    }

    for (int channel = 0;
         channel < 3;
         ++channel)
    {
        const double value =
            (
                static_cast<double>(
                    foreground[channel]) *
                    fa +
                static_cast<double>(
                    background[channel]) *
                    ba *
                    (1.0 - fa)
            ) /
            oa;

        int rounded =
            static_cast<int>(
                value + 0.5);

        rounded =
            max(0, min(255, rounded));

        output[channel] =
            static_cast<unsigned char>(
                rounded);
    }

    output[3] =
        static_cast<unsigned char>(
            max(
                0,
                min(
                    255,
                    static_cast<int>(
                        oa * 255.0 +
                        0.5))));
}

static bool LiveResidualBounds(
    const LiveTexturePixels& generated,
    const LiveTexturePixels& background,
    int& left,
    int& top,
    int& right,
    int& bottom)
{
    left =
        static_cast<int>(
            generated.width);

    top =
        static_cast<int>(
            generated.height);

    right = -1;
    bottom = -1;

    for (std::uint32_t y = 0;
         y < generated.height;
         ++y)
    {
        for (std::uint32_t x = 0;
             x < generated.width;
             ++x)
        {
            const std::size_t p =
                (
                    static_cast<std::size_t>(y) *
                        generated.width +
                    x
                ) *
                4u;

            const double residual =
                LiveResidualWeightForPixel(
                    &generated.bgra[p],
                    &background.bgra[p]);

            if (residual < 0.20)
                continue;

            left =
                min(
                    left,
                    static_cast<int>(x));

            top =
                min(
                    top,
                    static_cast<int>(y));

            right =
                max(
                    right,
                    static_cast<int>(x));

            bottom =
                max(
                    bottom,
                    static_cast<int>(y));
        }
    }

    return
        right >= left &&
        bottom >= top;
}

static void LiveScoreForegroundPlacement(
    const LiveTexturePixels& generated,
    const LiveTexturePixels& background,
    const LiveTexturePixels& foreground,
    int offsetX,
    int offsetY,
    double& residualScore,
    double& spillPenalty,
    double& fullScore)
{
    // Strict center-out residual foreground matcher v2.

    residualScore = 0.0;
    spillPenalty = 0.0;
    fullScore = 0.0;

    if (!generated.loaded ||
        !background.loaded ||
        !foreground.loaded ||
        generated.width != background.width ||
        generated.height != background.height ||
        foreground.width != generated.width ||
        foreground.height != generated.height ||
        generated.bgra.size() != background.bgra.size() ||
        foreground.bgra.size() != generated.bgra.size())
    {
        return;
    }

    // We now know both foreground and background assets are
    // aligned 32x32 textures. No translation search.
    if (offsetX != 0 || offsetY != 0)
        return;

    const double centerX =
        (static_cast<double>(generated.width) - 1.0) / 2.0;

    const double centerY =
        (static_cast<double>(generated.height) - 1.0) / 2.0;

    const double maximumRadius =
        std::sqrt(
            centerX * centerX +
            centerY * centerY);

    double foregroundMatchedWeight = 0.0;
    double foregroundTotalWeight = 0.0;

    double spillAmount = 0.0;
    double spillPixelCount = 0.0;

    double fullSquaredError = 0.0;
    double fullChannelCount = 0.0;

    const std::size_t pixelCount =
        generated.bgra.size() / 4u;

    for (std::size_t i = 0;
         i < pixelCount;
         ++i)
    {
        const std::uint32_t x =
            static_cast<std::uint32_t>(
                i % generated.width);

        const std::uint32_t y =
            static_cast<std::uint32_t>(
                i / generated.width);

        const unsigned char* generatedPixel =
            &generated.bgra[i * 4u];

        const unsigned char* backgroundPixel =
            &background.bgra[i * 4u];

        const unsigned char* foregroundPixel =
            &foreground.bgra[i * 4u];

        const double residual =
            LiveResidualWeightForPixel(
                generatedPixel,
                backgroundPixel);

        unsigned char composite[4] = {};

        LiveCompositePixelOver(
            backgroundPixel,
            foregroundPixel,
            composite);

        const double dx =
            static_cast<double>(x) -
            centerX;

        const double dy =
            static_cast<double>(y) -
            centerY;

        const double radius =
            std::sqrt(
                dx * dx +
                dy * dy);

        const double normalizedRadius =
            maximumRadius > 0.0
                ? min(
                    1.0,
                    radius / maximumRadius)
                : 0.0;

        // Center-out weighting:
        //
        // center ~4x importance
        // extreme corners ~1x
        const double centerWeight =
            1.0 +
            3.0 *
            (1.0 -
             normalizedRadius);

        // ----------------------------------------------------
        // FOREGROUND PIXELS
        //
        // Only pixels not explained by the background can earn
        // positive foreground-match points.
        // ----------------------------------------------------

        if (residual >= 0.15)
        {
            const int diffBlue =
                std::abs(
                    static_cast<int>(
                        generatedPixel[0]) -
                    static_cast<int>(
                        composite[0]));

            const int diffGreen =
                std::abs(
                    static_cast<int>(
                        generatedPixel[1]) -
                    static_cast<int>(
                        composite[1]));

            const int diffRed =
                std::abs(
                    static_cast<int>(
                        generatedPixel[2]) -
                    static_cast<int>(
                        composite[2]));

            const int maximumDifference =
                max(
                    diffBlue,
                    max(
                        diffGreen,
                        diffRed));

            const double rmsDifference =
                std::sqrt(
                    (
                        static_cast<double>(
                            diffBlue * diffBlue +
                            diffGreen * diffGreen +
                            diffRed * diffRed)
                    ) /
                    3.0);

            double pixelMatch = 0.0;

            // Very close color = full credit.
            if (maximumDifference <= 4 &&
                rmsDifference <= 4.0)
            {
                pixelMatch = 1.0;
            }
            // Once average RGB error reaches ~30 levels,
            // treat this as the wrong color entirely.
            else if (rmsDifference >= 30.0 ||
                     maximumDifference >= 45)
            {
                pixelMatch = 0.0;
            }
            else
            {
                // Aggressive nonlinear falloff.
                //
                // A vaguely similar shape with gray pixels
                // where the target is rainbow-colored should
                // score very poorly.
                const double t =
                    max(
                        0.0,
                        min(
                            1.0,
                            (rmsDifference - 4.0) /
                            26.0));

                const double remaining =
                    1.0 - t;

                pixelMatch =
                    remaining *
                    remaining *
                    remaining *
                    remaining;
            }

            // Transparent source cannot explain a visible
            // foreground pixel.
            if (foregroundPixel[3] < 16)
            {
                pixelMatch = 0.0;
            }

            const double weight =
                residual *
                centerWeight;

            foregroundMatchedWeight +=
                pixelMatch *
                weight;

            foregroundTotalWeight +=
                weight;
        }

        // ----------------------------------------------------
        // BACKGROUND PIXELS
        //
        // They never increase foreground score.
        //
        // They only penalize candidates which draw over an
        // already-correct background pixel.
        // ----------------------------------------------------

        if (residual <= 0.08)
        {
            const int diffBlue =
                std::abs(
                    static_cast<int>(
                        composite[0]) -
                    static_cast<int>(
                        backgroundPixel[0]));

            const int diffGreen =
                std::abs(
                    static_cast<int>(
                        composite[1]) -
                    static_cast<int>(
                        backgroundPixel[1]));

            const int diffRed =
                std::abs(
                    static_cast<int>(
                        composite[2]) -
                    static_cast<int>(
                        backgroundPixel[2]));

            const int maximumDamage =
                max(
                    diffBlue,
                    max(
                        diffGreen,
                        diffRed));

            // Ignore tiny anti-aliasing/color-rounding noise.
            if (maximumDamage > 6)
            {
                const double normalizedDamage =
                    min(
                        1.0,
                        static_cast<double>(
                            maximumDamage - 6) /
                        64.0);

                spillAmount +=
                    normalizedDamage *
                    normalizedDamage;
            }

            spillPixelCount += 1.0;
        }

        // Whole-icon reconstruction is diagnostic only.
        for (int channel = 0;
             channel < 3;
             ++channel)
        {
            const double error =
                static_cast<double>(
                    static_cast<int>(
                        generatedPixel[channel]) -
                    static_cast<int>(
                        composite[channel]));

            fullSquaredError +=
                error *
                error;

            fullChannelCount +=
                1.0;
        }
    }

    if (foregroundTotalWeight > 0.0)
    {
        residualScore =
            100.0 *
            foregroundMatchedWeight /
            foregroundTotalWeight;
    }

    if (spillPixelCount > 0.0)
    {
        spillPenalty =
            100.0 *
            std::sqrt(
                spillAmount /
                spillPixelCount);
    }

    if (fullChannelCount > 0.0)
    {
        const double rmse =
            std::sqrt(
                fullSquaredError /
                fullChannelCount);

        fullScore =
            100.0 *
            (1.0 -
             rmse /
             255.0);
    }

    residualScore =
        max(
            0.0,
            min(
                100.0,
                residualScore));

    spillPenalty =
        max(
            0.0,
            min(
                100.0,
                spillPenalty));

    fullScore =
        max(
            0.0,
            min(
                100.0,
                fullScore));
}

static void LiveBackgroundCornerScore(
    const LiveTexturePixels& generated,
    const LiveTexturePixels& candidate,
    double& matchScore,
    double& meanError)
{
    // Robust four-corner BGRA background matcher.
    //
    // Each corner is scored independently. The worst corner is
    // discarded so foreground artwork overlapping one corner
    // cannot dominate background identification.
    //
    // Alpha is part of the comparison. Transparent pixels with
    // hidden RGB data are NOT allowed to masquerade as visible
    // background pixels.

    matchScore = 0.0;
    meanError = 255.0;

    if (!generated.loaded ||
        !candidate.loaded ||
        generated.width != candidate.width ||
        generated.height != candidate.height ||
        generated.bgra.size() != candidate.bgra.size() ||
        generated.width == 0 ||
        generated.height == 0)
    {
        return;
    }

    const int width =
        static_cast<int>(generated.width);

    const int height =
        static_cast<int>(generated.height);

    // For a 32x32 icon this examines an 8x8 block in each corner.
    const int cornerSize =
        min(
            8,
            min(width, height) / 2);

    struct CornerResult
    {
        double match = 0.0;
        double error = 255.0;
    };

    CornerResult corners[4];

    for (int corner = 0;
         corner < 4;
         ++corner)
    {
        const bool right =
            (corner & 1) != 0;

        const bool bottom =
            (corner & 2) != 0;

        double matchedWeight = 0.0;
        double totalWeight = 0.0;
        double weightedError = 0.0;

        for (int localY = 0;
             localY < cornerSize;
             ++localY)
        {
            const int y =
                bottom
                    ? height - 1 - localY
                    : localY;

            for (int localX = 0;
                 localX < cornerSize;
                 ++localX)
            {
                const int x =
                    right
                        ? width - 1 - localX
                        : localX;

                const std::size_t p =
                    (
                        static_cast<std::size_t>(y) *
                            generated.width +
                        static_cast<std::size_t>(x)
                    ) *
                    4u;

                const unsigned char* g =
                    &generated.bgra[p];

                const unsigned char* c =
                    &candidate.bgra[p];

                // Literal corner pixels count strongest.
                const int inward =
                    max(localX, localY);

                const double weight =
                    static_cast<double>(
                        cornerSize - inward);

                const int alphaDiff =
                    std::abs(
                        static_cast<int>(g[3]) -
                        static_cast<int>(c[3]));

                // Compare visible RGB, not hidden RGB underneath
                // transparent candidate pixels.
                const double generatedAlpha =
                    static_cast<double>(g[3]) /
                    255.0;

                const double candidateAlpha =
                    static_cast<double>(c[3]) /
                    255.0;

                const double gBlue =
                    static_cast<double>(g[0]) *
                    generatedAlpha;

                const double gGreen =
                    static_cast<double>(g[1]) *
                    generatedAlpha;

                const double gRed =
                    static_cast<double>(g[2]) *
                    generatedAlpha;

                const double cBlue =
                    static_cast<double>(c[0]) *
                    candidateAlpha;

                const double cGreen =
                    static_cast<double>(c[1]) *
                    candidateAlpha;

                const double cRed =
                    static_cast<double>(c[2]) *
                    candidateAlpha;

                const double blueDiff =
                    std::abs(
                        gBlue -
                        cBlue);

                const double greenDiff =
                    std::abs(
                        gGreen -
                        cGreen);

                const double redDiff =
                    std::abs(
                        gRed -
                        cRed);

                const double rgbError =
                    (
                        blueDiff +
                        greenDiff +
                        redDiff
                    ) /
                    3.0;

                // Alpha disagreement is deliberately expensive.
                const double visibleError =
                    rgbError * 0.75 +
                    static_cast<double>(
                        alphaDiff) *
                    0.25;

                double pixelMatch = 0.0;

                if (visibleError <= 4.0)
                {
                    pixelMatch = 1.0;
                }
                else if (visibleError < 40.0)
                {
                    const double t =
                        (visibleError - 4.0) /
                        36.0;

                    const double remaining =
                        1.0 - t;

                    pixelMatch =
                        remaining *
                        remaining;
                }

                matchedWeight +=
                    pixelMatch *
                    weight;

                totalWeight +=
                    weight;

                weightedError +=
                    visibleError *
                    weight;
            }
        }

        if (totalWeight > 0.0)
        {
            corners[corner].match =
                100.0 *
                matchedWeight /
                totalWeight;

            corners[corner].error =
                weightedError /
                totalWeight;
        }
    }

    // Sort corner results best -> worst.
    std::sort(
        &corners[0],
        &corners[4],
        [](const CornerResult& a,
           const CornerResult& b)
        {
            if (a.match != b.match)
                return a.match > b.match;

            return a.error < b.error;
        });

    // Ignore the single worst corner. This is the robust part:
    // a foreground item can overlap one corner without causing
    // the wrong background to win.
    matchScore =
        (
            corners[0].match +
            corners[1].match +
            corners[2].match
        ) /
        3.0;

    meanError =
        (
            corners[0].error +
            corners[1].error +
            corners[2].error
        ) /
        3.0;
}
static bool LiveFindGeneratedComposition(
    const LiveTexturePixels& generated)
{
    g_LiveGeneratedMatches.clear();

    g_LiveGeneratedResidual =
        LiveTexturePixels{};

    g_LiveGeneratedBackground =
        LiveDatMatch{};

    g_LiveHaveGeneratedBackground =
        false;

    // --------------------------------------------------------
    // PHASE 1: BACKGROUND
    // --------------------------------------------------------

    const CustomTab* backgroundTab = nullptr;

    for (const CustomTab& tab :
         g_CustomTabs)
    {
        if (tab.name ==
            "Background Textures")
        {
            backgroundTab =
                &tab;

            break;
        }
    }

    if (backgroundTab == nullptr ||
        backgroundTab->dids.empty())
    {
        if (g_LiveStateText != nullptr)
        {
            SetWindowTextW(
                g_LiveStateText,
                L"Background Textures custom tab is required.");
        }

        return false;
    }

    LiveTexturePixels backgroundPixels;

    double bestBackgroundScore = -1.0;
    double bestBackgroundError = 1000000.0;

    std::size_t bestBackgroundIndex =
        static_cast<std::size_t>(-1);

    std::size_t backgroundCount = 0;

    for (const TextureRecord& texture :
         g_Textures)
    {
        if (texture.width ==
                generated.width &&
            texture.height ==
                generated.height &&
            IsPreviewable(texture) &&
            backgroundTab->dids.find(
                texture.did) !=
                backgroundTab->dids.end())
        {
            ++backgroundCount;
        }
    }

    std::size_t backgroundProcessed = 0;

    for (std::size_t i = 0;
         i < g_Textures.size();
         ++i)
    {
        const TextureRecord& texture =
            g_Textures[i];

        if (texture.width !=
                generated.width ||
            texture.height !=
                generated.height ||
            !IsPreviewable(texture) ||
            backgroundTab->dids.find(
                texture.did) ==
                backgroundTab->dids.end())
        {
            continue;
        }

        ++backgroundProcessed;

        if (g_LiveStateText != nullptr &&
            (
                backgroundProcessed == 1 ||
                backgroundProcessed ==
                    backgroundCount ||
                (backgroundProcessed % 8u) == 0
            ))
        {
            wchar_t progress[256] = {};

            swprintf_s(
                progress,
                L"Finding composition...\r\n"
                L"Phase 1: corner background match\r\n"
                L"%u of %u",
                static_cast<unsigned int>(
                    backgroundProcessed),
                static_cast<unsigned int>(
                    backgroundCount));

            SetWindowTextW(
                g_LiveStateText,
                progress);

            UpdateWindow(
                g_LiveStateText);
        }

        LiveTexturePixels candidate;

        if (!LiveLoadDatPreviewPixels(
                texture,
                candidate))
        {
            continue;
        }

        double score = 0.0;
        double error = 255.0;

        LiveBackgroundCornerScore(
            generated,
            candidate,
            score,
            error);

        const bool betterScore =
            score >
            bestBackgroundScore +
            0.001;

        const bool tiedScore =
            std::abs(
                score -
                bestBackgroundScore) <=
            0.001;

        if (betterScore ||
            (
                tiedScore &&
                error <
                bestBackgroundError
            ))
        {
            bestBackgroundScore =
                score;

            bestBackgroundError =
                error;

            bestBackgroundIndex =
                i;

            backgroundPixels =
                std::move(
                    candidate);
        }
    }

    if (bestBackgroundIndex ==
            static_cast<std::size_t>(-1) ||
        !backgroundPixels.loaded)
    {
        return false;
    }

    g_LiveGeneratedBackground.textureIndex =
        bestBackgroundIndex;

    g_LiveGeneratedBackground.score =
        bestBackgroundScore;

    g_LiveHaveGeneratedBackground =
        true;

    // --------------------------------------------------------
    // Remove the known background.
    //
    // This is the image shown in the Foreground Icon preview.
    // --------------------------------------------------------

    g_LiveGeneratedResidual =
        LiveBuildResidualPixels(
            generated,
            backgroundPixels);

    // --------------------------------------------------------
    // PHASE 2: FOREGROUND
    //
    // We now know all relevant assets are the same dimensions,
    // so no translation search and no smaller texture search.
    // --------------------------------------------------------

    std::size_t foregroundCount = 0;

    for (const TextureRecord& texture :
         g_Textures)
    {
        if (!IsPreviewable(texture))
            continue;

        if (texture.width !=
                generated.width ||
            texture.height !=
                generated.height)
        {
            continue;
        }

        ++foregroundCount;
    }

    std::size_t foregroundProcessed = 0;

    for (std::size_t i = 0;
         i < g_Textures.size();
         ++i)
    {
        if (i == bestBackgroundIndex)
            continue;

        const TextureRecord& texture =
            g_Textures[i];

        if (!IsPreviewable(texture))
            continue;

        if (texture.width !=
                generated.width ||
            texture.height !=
                generated.height)
        {
            continue;
        }

        ++foregroundProcessed;

        if (g_LiveStateText != nullptr &&
            (
                foregroundProcessed == 1 ||
                foregroundProcessed ==
                    foregroundCount ||
                (foregroundProcessed % 32u) ==
                    0
            ))
        {
            wchar_t progress[256] = {};

            swprintf_s(
                progress,
                L"Finding composition...\r\n"
                L"Phase 2: center-out foreground pixels\r\n"
                L"%u of %u",
                static_cast<unsigned int>(
                    foregroundProcessed),
                static_cast<unsigned int>(
                    foregroundCount));

            SetWindowTextW(
                g_LiveStateText,
                progress);

            UpdateWindow(
                g_LiveStateText);
        }

        LiveTexturePixels foreground;

        if (!LiveLoadDatPreviewPixels(
                texture,
                foreground))
        {
            continue;
        }

        LiveLayerMatch match;

        match.foregroundIndex =
            i;

        LiveScoreForegroundPlacement(
            generated,
            backgroundPixels,
            foreground,
            0,
            0,
            match.residualScore,
            match.spillPenalty,
            match.fullScore);

        // Foreground score is almost entirely the strict
        // residual color match. Painting into the known
        // background subtracts from the result.
        match.score =
            match.residualScore -
            match.spillPenalty *
                0.75;

        match.score =
            max(
                0.0,
                min(
                    100.0,
                    match.score));

        match.improvement =
            match.fullScore -
            bestBackgroundScore;

        match.offsetX = 0;
        match.offsetY = 0;

        g_LiveGeneratedMatches.push_back(
            match);

        std::sort(
            g_LiveGeneratedMatches.begin(),
            g_LiveGeneratedMatches.end(),
            [](const LiveLayerMatch& a,
               const LiveLayerMatch& b)
            {
                if (a.score != b.score)
                    return a.score > b.score;

                if (a.residualScore !=
                    b.residualScore)
                {
                    return
                        a.residualScore >
                        b.residualScore;
                }

                if (a.spillPenalty !=
                    b.spillPenalty)
                {
                    return
                        a.spillPenalty <
                        b.spillPenalty;
                }

                if (a.fullScore !=
                    b.fullScore)
                {
                    return
                        a.fullScore >
                        b.fullScore;
                }

                return
                    g_Textures[
                        a.foregroundIndex].did <
                    g_Textures[
                        b.foregroundIndex].did;
            });

        if (g_LiveGeneratedMatches.size() >
            3)
        {
            g_LiveGeneratedMatches.resize(
                3);
        }
    }

    return true;
}
static void LiveSetGeneratedMatchButton(
    HWND button,
    std::size_t rank)
{
    if (button == nullptr)
        return;

    if (rank >=
        g_LiveGeneratedMatches.size())
    {
        wchar_t emptyLabel[32] = {};

        swprintf_s(
            emptyLabel,
            L"#%u --",
            static_cast<unsigned int>(
                rank + 1));

        SetWindowTextW(
            button,
            emptyLabel);

        EnableWindow(
            button,
            FALSE);

        return;
    }

    wchar_t label[64] = {};

    swprintf_s(
        label,
        L"#%u %.1f%%",
        static_cast<unsigned int>(
            rank + 1),
        g_LiveGeneratedMatches[rank].score);

    SetWindowTextW(
        button,
        label);

    EnableWindow(
        button,
        TRUE);
}

static void LiveLocateGeneratedMatch(
    std::size_t rank)
{
    if (rank >=
        g_LiveGeneratedMatches.size())
    {
        return;
    }

    const std::size_t textureIndex =
        g_LiveGeneratedMatches[rank]
            .foregroundIndex;

    if (textureIndex >=
        g_Textures.size())
    {
        return;
    }

    GoToTexture(
        textureIndex);

    if (g_MainWindow != nullptr)
    {
        ShowWindow(
            g_MainWindow,
            SW_RESTORE);

        SetForegroundWindow(
            g_MainWindow);
    }
}

static void LiveLocateGeneratedBackground()
{
    if (!g_LiveHaveGeneratedBackground)
        return;

    const std::size_t textureIndex =
        g_LiveGeneratedBackground
            .textureIndex;

    if (textureIndex >=
        g_Textures.size())
    {
        return;
    }

    GoToTexture(
        textureIndex);

    if (g_MainWindow != nullptr)
    {
        ShowWindow(
            g_MainWindow,
            SW_RESTORE);

        SetForegroundWindow(
            g_MainWindow);
    }
}
static void LiveUpdateGeneratedMatchPreviews()
{
    LiveClearGeneratedMatchPreviewBitmaps();

    for (std::size_t rank = 0;
         rank < 3;
         ++rank)
    {
        if (rank >=
            g_LiveGeneratedMatches.size())
        {
            continue;
        }

        const std::size_t textureIndex =
            g_LiveGeneratedMatches[rank]
                .foregroundIndex;

        if (textureIndex >=
            g_Textures.size())
        {
            continue;
        }

        g_LiveGeneratedMatchBitmaps[rank] =
            LoadPreviewBitmap(
                g_Textures[textureIndex],
                144);

        if (g_LiveGeneratedMatchBitmaps[rank] != nullptr &&
            g_LiveGeneratedMatchPreviews[rank] != nullptr)
        {
            SendMessageW(
                g_LiveGeneratedMatchPreviews[rank],
                STM_SETIMAGE,
                IMAGE_BITMAP,
                reinterpret_cast<LPARAM>(
                    g_LiveGeneratedMatchBitmaps[rank]));
        }
    }
}
static std::wstring LiveDescribeStateLine(
    const LiveControlState& control,
    const wchar_t* label,
    std::uint32_t state)
{
    std::wstring line = label;
    line += L": ";
    if (!LiveStatePresent(control, state))
    {
        line += L"inherit / not defined";
        return line;
    }
    const std::uint32_t did = LiveStateDid(control, state);
    if ((did & 0xFF000000u) == 0x06000000u)
        line += ToWide(LiveHex8(did));
    else
        line += L"present, no texture override";
    return line;
}

static std::wstring LiveBuildInspectorStateText()
{
    std::wstring stateText;
    if (g_LiveSelectedControl != 0)
    {
        const auto stateIt = g_LiveControlStates.find(g_LiveSelectedControl);
        if (stateIt != g_LiveControlStates.end())
        {
            const LiveControlState& control = stateIt->second;
            stateText += LiveDescribeStateLine(control, L"Normal", 1) + L"\r\n";
            stateText += LiveDescribeStateLine(control, L"Hover", 2) + L"\r\n";
            stateText += LiveDescribeStateLine(control, L"Pressed", 3) + L"\r\n";
            stateText += LiveDescribeStateLine(control, L"Selected", 6) + L"\r\n";
            stateText += LiveDescribeStateLine(control, L"Selected Hover", 7) + L"\r\n";
            stateText += LiveDescribeStateLine(control, L"Selected Pressed", 8);
        }
    }

    if (g_LiveSelectedGeneratedItem && g_LiveSelectedItemId != 0)
    {
        if (!stateText.empty()) stateText += L"\r\n\r\n";
        stateText += L"Runtime item icon -> editable DAT texture";
        const auto itemIt = g_LiveItemProvenance.find(g_LiveSelectedItemId);
        if (itemIt != g_LiveItemProvenance.end() &&
            !itemIt->second.componentDids.empty())
        {
            stateText += L"\r\nComponents: ";
            for (std::size_t i = 0; i < itemIt->second.componentDids.size(); ++i)
            {
                if (i) stateText += L", ";
                stateText += ToWide(LiveHex8(itemIt->second.componentDids[i]));
            }
        }
    }

    if (stateText.empty())
        stateText = L"No interactive state metadata for this texture.";
    return stateText;
}

static void LiveUpdateInspectorStateTextOnly()
{
    if (g_LiveStateText == nullptr)
        return;

    // Generated icons own this area while their DAT pixel
    // candidates are being displayed.
    if (g_LiveSelectedGeneratedItem)
        return;
    const std::wstring stateText = LiveBuildInspectorStateText();
    if (stateText != g_LiveLastStateText)
    {
        g_LiveLastStateText = stateText;
        SetWindowTextW(g_LiveStateText, stateText.c_str());
    }
}

static void LiveUpdateInspector()
{
    LiveClearInspectorBitmaps();

    if (g_LiveSelectedGeneratedItem &&
        g_LiveSelectedGeneratedSurface != 0)
    {
        ShowWindow(
            g_LiveForegroundTitle,
            SW_SHOW);

        ShowWindow(
            g_LiveForegroundPreview,
            SW_SHOW);

        for (int i = 0; i < 3; ++i)
        {
            ShowWindow(
                g_LiveGeneratedMatchPreviews[i],
                SW_SHOW);
        }
        const LiveTexturePixels* generated =
            LiveGetGenerated(g_LiveSelectedGeneratedSurface);

        if (generated == nullptr)
        {
            SetWindowTextW(
                g_LiveDetailsText,
                L"Generated item icon pixels are unavailable.");

            return;
        }

        g_LiveSelectedGeneratedHash =
            LiveHashGeneratedPixels(*generated);

        wchar_t details[1024] = {};

        swprintf_s(
            details,
            L"Generated Item Icon\r\n"
            L"Key: G_%s\r\n"
            L"Runtime Item: %08X\r\n"
            L"Surface: %08X\r\n"
            L"Dimensions: %u x %u\r\n"
            L"Format: BGRA32\r\n"
            L"DID: None",
            LiveGeneratedHashText(
                g_LiveSelectedGeneratedHash).c_str(),
            g_LiveSelectedItemId,
            g_LiveSelectedGeneratedSurface,
            generated->width,
            generated->height);

        SetWindowTextW(
            g_LiveDetailsText,
            details);

        g_LiveLastStateText =
            L"DID-less generated runtime item icon.\r\n"
            L"This is now selected independently of any DAT texture.";

        SetWindowTextW(
            g_LiveStateText,
            g_LiveLastStateText.c_str());

        SetWindowTextW(
            g_LiveOriginalTitle,
            L"Generated Icon");

        SetWindowTextW(
            g_LiveReplacementTitle,
            L"Finding composition...");

        g_LiveOriginalBitmap =
            LiveCreateGeneratedPreviewBitmap(
                *generated,
                144);

        if (g_LiveOriginalBitmap != nullptr)
        {
            SendMessageW(
                g_LiveOriginalPreview,
                STM_SETIMAGE,
                IMAGE_BITMAP,
                reinterpret_cast<LPARAM>(
                    g_LiveOriginalBitmap));
        }

        SetWindowTextW(
            g_LiveStateText,
            L"Finding composition...\r\n"
            L"Searching DAT textures.");

        UpdateWindow(
            g_LiveStateText);

        const bool foundComposition =
            LiveFindGeneratedComposition(
                *generated);

        std::wstring matchText;

        if (!foundComposition ||
            !g_LiveHaveGeneratedBackground)
        {
            matchText =
                L"No same-size DAT textures "
                L"could be compared.";

            SetWindowTextW(
                g_LiveReplacementTitle,
                L"No DAT composition");

            LiveSetGeneratedMatchButton(
                g_LiveReplaceButton,
                0);

            LiveSetGeneratedMatchButton(
                g_LiveRemoveButton,
                1);

            LiveSetGeneratedMatchButton(
                g_LiveLocateButton,
                2);
        }
        else
        {
            const TextureRecord& background =
                g_Textures[
                    g_LiveGeneratedBackground
                        .textureIndex];

            wchar_t backgroundLine[256] = {};

            swprintf_s(
                backgroundLine,
                L"Likely composition:\r\n\r\n"
                L"Background (corner match):\r\n"
                L"%6.2f%%   %S   %s\r\n"
                L"(middle preview = background)\r\n\r\n"
                L"Foreground center-out pixel matches:\r\n",
                g_LiveGeneratedBackground.score,
                background.did.c_str(),
                FormatName(
                    background.pixelFormat)
                    .c_str());

            matchText =
                backgroundLine;

            for (std::size_t i = 0;
                 i <
                    g_LiveGeneratedMatches.size();
                 ++i)
            {
                const LiveLayerMatch& match =
                    g_LiveGeneratedMatches[i];

                const TextureRecord& foreground =
                    g_Textures[
                        match.foregroundIndex];

                wchar_t line[256] = {};

                swprintf_s(
                    line,
                    L"%u. %6.2f%%  %S  %ux%u %s\r\n"
                    L"   strict color %.2f%% | spill %.2f%%\r\n",
                    static_cast<unsigned int>(
                        i + 1),
                    match.score,
                    foreground.did.c_str(),
                    foreground.width,
                    foreground.height,
                    FormatName(
                        foreground.pixelFormat)
                        .c_str(),
                    match.residualScore,
                    match.spillPenalty);

                matchText += line;
            }

            if (!g_LiveGeneratedMatches.empty())
            {
                const LiveLayerMatch& best =
                    g_LiveGeneratedMatches[0];

                const TextureRecord& foreground =
                    g_Textures[
                        best.foregroundIndex];

                wchar_t reconstruction[256] = {};

                swprintf_s(
                    reconstruction,
                    L"\r\nCombined reconstruction:\r\n"
                    L"%S + %S\r\n"
                    L"%6.2f%% full-icon match",
                    background.did.c_str(),
                    foreground.did.c_str(),
                    best.fullScore);

                matchText +=
                    reconstruction;
            }

            SetWindowTextW(
                g_LiveReplacementTitle,
                L"Background Icon");

            g_LiveReplacementBitmap =
                LoadPreviewBitmap(
                    background,
                    144);

            if (g_LiveReplacementBitmap != nullptr)
            {
                SendMessageW(
                    g_LiveReplacementPreview,
                    STM_SETIMAGE,
                    IMAGE_BITMAP,
                    reinterpret_cast<LPARAM>(
                        g_LiveReplacementBitmap));
            }

            g_LiveReplacementBitmap =
                LoadPreviewBitmap(
                    background,
                    144);

            SetWindowTextW(
                g_LiveForegroundTitle,
                L"Foreground Icon");

            // Show Generated - Background, not the currently
            // highest-ranked DAT candidate.
            g_LiveForegroundBitmap =
                LiveCreateGeneratedPreviewBitmap(
                    g_LiveGeneratedResidual,
                    144);

            if (g_LiveForegroundBitmap != nullptr)
            {
                SendMessageW(
                    g_LiveForegroundPreview,
                    STM_SETIMAGE,
                    IMAGE_BITMAP,
                    reinterpret_cast<LPARAM>(
                        g_LiveForegroundBitmap));
            }
            LiveSetGeneratedMatchButton(
                g_LiveReplaceButton,
                0);

            LiveSetGeneratedMatchButton(
                g_LiveRemoveButton,
                1);

            LiveSetGeneratedMatchButton(
                g_LiveLocateButton,
                2);
        }

        LiveUpdateGeneratedMatchPreviews();

        g_LiveLastStateText =
            matchText;

        SetWindowTextW(
            g_LiveStateText,
            matchText.c_str());

        return;
    }

    if (g_LiveSelectedTextureIndex == static_cast<std::size_t>(-1) ||
        g_LiveSelectedTextureIndex >= g_Textures.size())
    {
        SetWindowTextW(
            g_LiveDetailsText,
            L"Click a texture in the scene to inspect it.");
        g_LiveLastStateText =
            L"Hover/click states are simulated when the mirrored control "
            L"contains AC state-table metadata.";
        SetWindowTextW(g_LiveStateText, g_LiveLastStateText.c_str());
        SetWindowTextW(g_LiveOriginalTitle, L"Original");
        SetWindowTextW(g_LiveReplacementTitle, L"Replacement: None");
        return;
    }

    g_LiveGeneratedMatches.clear();
    g_LiveGeneratedBackground = LiveDatMatch{};
    g_LiveHaveGeneratedBackground = false;
    g_LiveGeneratedResidual = LiveTexturePixels{};

    ShowWindow(
        g_LiveForegroundTitle,
        SW_HIDE);

    ShowWindow(
        g_LiveForegroundPreview,
        SW_HIDE);

    for (int i = 0; i < 3; ++i)
    {
        ShowWindow(
            g_LiveGeneratedMatchPreviews[i],
            SW_HIDE);
    }

    SetWindowTextW(
        g_LiveReplaceButton,
        L"Replace PNG...");

    SetWindowTextW(
        g_LiveRemoveButton,
        L"Remove");

    SetWindowTextW(
        g_LiveLocateButton,
        L"Locate in Tabs");

    EnableWindow(
        g_LiveReplaceButton,
        TRUE);

    EnableWindow(
        g_LiveRemoveButton,
        TRUE);

    EnableWindow(
        g_LiveLocateButton,
        TRUE);

    const TextureRecord& texture =
        g_Textures[g_LiveSelectedTextureIndex];

    const bool hasReplacement = HasReplacement(texture);
    const std::wstring formatName = FormatName(texture.pixelFormat);

    wchar_t details[768] = {};
    swprintf_s(
        details,
        L"DID: %S\r\n"
        L"Dimensions: %u x %u\r\n"
        L"Format: %s (0x%08X)\r\n"
        L"Payload: %u bytes\r\n"
        L"Replacement: %s",
        texture.did.c_str(),
        texture.width,
        texture.height,
        formatName.c_str(),
        texture.pixelFormat,
        texture.imageSize,
        hasReplacement ? L"Yes" : L"No");

    SetWindowTextW(g_LiveDetailsText, details);

    LiveUpdateInspectorStateTextOnly();

    SetWindowTextW(g_LiveOriginalTitle, L"Original");
    SetWindowTextW(
        g_LiveReplacementTitle,
        hasReplacement ? L"Current Replacement" : L"Replacement: None");

    if (IsPreviewable(texture))
    {
        g_LiveOriginalBitmap = LoadPreviewBitmap(texture, 144);
        if (g_LiveOriginalBitmap != nullptr)
        {
            SendMessageW(
                g_LiveOriginalPreview,
                STM_SETIMAGE,
                IMAGE_BITMAP,
                reinterpret_cast<LPARAM>(g_LiveOriginalBitmap));
        }

        g_LiveReplacementBitmap =
            LoadReplacementRawBitmap(texture, 144);
        if (g_LiveReplacementBitmap != nullptr)
        {
            SendMessageW(
                g_LiveReplacementPreview,
                STM_SETIMAGE,
                IMAGE_BITMAP,
                reinterpret_cast<LPARAM>(g_LiveReplacementBitmap));
        }
    }
}

static void LiveUpdateStatus();

static LiveMirrorStatus LiveMirrorCurrentStatus()
{
    return static_cast<LiveMirrorStatus>(
        g_LiveMirrorStatus.load(std::memory_order_acquire));
}

static void LiveMirrorSetLastError(const std::wstring& error)
{
    std::lock_guard<std::mutex> lock(g_LiveMirrorErrorMutex);
    g_LiveMirrorLastError = error;
}

static std::wstring LiveMirrorGetLastError()
{
    std::lock_guard<std::mutex> lock(g_LiveMirrorErrorMutex);
    return g_LiveMirrorLastError;
}

static const wchar_t* LiveMirrorStatusLabel(LiveMirrorStatus status)
{
    switch (status)
    {
        case LiveMirrorStatus::Frozen: return L"Snapshot ready / idle";
        case LiveMirrorStatus::WaitingForAC: return L"Waiting for AC";
        case LiveMirrorStatus::Connected: return L"Connected";
        case LiveMirrorStatus::Capturing: return L"Capturing snapshot";
        case LiveMirrorStatus::VanillaRequired: return L"Restore Vanilla in AC";
        case LiveMirrorStatus::Busy: return L"AC capture busy";
        case LiveMirrorStatus::PathError: return L"LiveMirror path error";
        case LiveMirrorStatus::SceneUnavailable: return L"Scene unavailable";
        case LiveMirrorStatus::ProtocolError: return L"Bridge protocol error";
        default: return L"Unknown";
    }
}

static void LiveMirrorSetStatus(LiveMirrorStatus status)
{
    g_LiveMirrorStatus.store(static_cast<int>(status), std::memory_order_release);
    HWND window = g_LiveWindow;
    if (window != nullptr && IsWindow(window))
        PostMessageW(window, WM_APP_LIVE_MIRROR_STATUS, 0, 0);
}

static bool LiveMirrorBuildSnapshotPath(std::wstring& path)
{
    if (g_AppPaths.liveMirrorRoot.empty())
        return false;

    path =
        JoinPath(
            g_AppPaths.liveMirrorRoot,
            L"live_scene.json");

    return true;
}

static bool LiveMirrorPipeWriteLine(HANDLE pipe, const char* line)
{
    const std::string wire = std::string(line) + "\n";
    DWORD written = 0;
    return WriteFile(
               pipe,
               wire.data(),
               static_cast<DWORD>(wire.size()),
               &written,
               nullptr) != FALSE &&
           written == wire.size();
}

static bool LiveMirrorPipeReadLine(HANDLE pipe, std::string& line)
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

static void LiveMirrorSleepInterruptible(DWORD milliseconds)
{
    DWORD waited = 0;
    while (waited < milliseconds &&
           !g_LiveMirrorStop.load(std::memory_order_acquire))
    {
        const DWORD slice = std::min<DWORD>(100, milliseconds - waited);
        Sleep(slice);
        waited += slice;
    }
}

static DWORD WINAPI LiveMirrorWorkerThread(LPVOID parameter)
{
    HWND targetWindow = reinterpret_cast<HWND>(parameter);

    LiveMirrorSetStatus(LiveMirrorStatus::WaitingForAC);

    // Snapshot mode is intentionally one-shot. A button click performs one
    // bridge connection and one CAPTURE command, then the worker exits. This
    // leaves no background capture traffic running while the user changes the
    // in-game AC Customs theme.
    if (!WaitNamedPipeW(LIVE_MIRROR_PIPE, 1500))
    {
        LiveMirrorSetLastError(
            L"AC Customs is not available. Start/log into AC and try again.");
        g_LiveMirrorEnabled.store(false, std::memory_order_release);
        g_LiveMirrorStop.store(true, std::memory_order_release);
        g_LiveMirrorRunning.store(false, std::memory_order_release);
        LiveMirrorSetStatus(LiveMirrorStatus::WaitingForAC);
        return 0;
    }

    HANDLE pipe = CreateFileW(
        LIVE_MIRROR_PIPE,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        LiveMirrorSetLastError(
            L"Could not connect to the AC Customs Live UI bridge.");
        g_LiveMirrorEnabled.store(false, std::memory_order_release);
        g_LiveMirrorStop.store(true, std::memory_order_release);
        g_LiveMirrorRunning.store(false, std::memory_order_release);
        LiveMirrorSetStatus(LiveMirrorStatus::ProtocolError);
        return 0;
    }

    std::string response;
    if (!LiveMirrorPipeReadLine(pipe, response) ||
        response != "HELLO 1")
    {
        CloseHandle(pipe);
        LiveMirrorSetLastError(L"Unexpected bridge greeting.");
        g_LiveMirrorEnabled.store(false, std::memory_order_release);
        g_LiveMirrorStop.store(true, std::memory_order_release);
        g_LiveMirrorRunning.store(false, std::memory_order_release);
        LiveMirrorSetStatus(LiveMirrorStatus::ProtocolError);
        return 0;
    }

    LiveMirrorSetStatus(LiveMirrorStatus::Capturing);

    if (!LiveMirrorPipeWriteLine(pipe, "CAPTURE") ||
        !LiveMirrorPipeReadLine(pipe, response))
    {
        CloseHandle(pipe);
        LiveMirrorSetLastError(
            L"The Live UI snapshot connection was interrupted.");
        g_LiveMirrorEnabled.store(false, std::memory_order_release);
        g_LiveMirrorStop.store(true, std::memory_order_release);
        g_LiveMirrorRunning.store(false, std::memory_order_release);
        LiveMirrorSetStatus(LiveMirrorStatus::ProtocolError);
        return 0;
    }

    LiveMirrorStatus finalStatus =
        LiveMirrorStatus::Frozen;

    if (response.rfind("READY ", 0) == 0)
    {
        const std::uint32_t generation =
            static_cast<std::uint32_t>(
                std::strtoul(
                    response.c_str() + 6,
                    nullptr,
                    10));

        g_LiveMirrorGeneration.store(
            generation,
            std::memory_order_release);

        if (targetWindow != nullptr &&
            IsWindow(targetWindow))
        {
            PostMessageW(
                targetWindow,
                WM_APP_LIVE_MIRROR_READY,
                static_cast<WPARAM>(generation),
                0);
        }

        // Keep this one worker alive only long enough for the UI thread to
        // consume the completed scene file. It does NOT request another frame.
        DWORD applyWaited = 0;
        while (!g_LiveMirrorStop.load(
                   std::memory_order_acquire) &&
               g_LiveMirrorAppliedGeneration.load(
                   std::memory_order_acquire) < generation &&
               g_LiveMirrorFailedGeneration.load(
                   std::memory_order_acquire) < generation &&
               applyWaited < 10000)
        {
            Sleep(50);
            applyWaited += 50;
        }

        if (g_LiveMirrorFailedGeneration.load(
                std::memory_order_acquire) >= generation)
        {
            LiveMirrorSetLastError(
                L"The captured scene could not be loaded.");
            finalStatus =
                LiveMirrorStatus::SceneUnavailable;
        }
        else
        {
            LiveMirrorSetLastError(L"");
            finalStatus =
                LiveMirrorStatus::Frozen;
        }
    }
    else if (response == "VANILLA_REQUIRED")
    {
        LiveMirrorSetLastError(
            L"Live UI snapshots require Vanilla textures. "
            L"Switch AC Customs in-game to Vanilla, then take another snapshot.");

        finalStatus =
            LiveMirrorStatus::VanillaRequired;
    }
    else if (response == "BUSY")
    {
        LiveMirrorSetLastError(
            L"AC Customs is currently changing theme state or already capturing. "
            L"Try the snapshot again in a moment.");

        finalStatus =
            LiveMirrorStatus::Busy;
    }
    else if (response == "PATH_ERROR")
    {
        LiveMirrorSetLastError(
            L"AC could not create the LiveMirror directory.");

        finalStatus =
            LiveMirrorStatus::PathError;
    }
    else if (response == "EMPTY")
    {
        LiveMirrorSetLastError(
            L"AC produced no usable UI layers in this snapshot.");

        finalStatus =
            LiveMirrorStatus::SceneUnavailable;
    }
    else if (response == "CAPTURE_ERROR")
    {
        LiveMirrorSetLastError(
            L"AC could not finalize the Live UI snapshot.");

        finalStatus =
            LiveMirrorStatus::SceneUnavailable;
    }
    else
    {
        LiveMirrorSetLastError(
            L"Unexpected bridge response: " +
            ToWide(response));

        finalStatus =
            LiveMirrorStatus::ProtocolError;
    }

    CloseHandle(pipe);

    g_LiveMirrorEnabled.store(
        false,
        std::memory_order_release);

    g_LiveMirrorStop.store(
        true,
        std::memory_order_release);

    g_LiveMirrorRunning.store(
        false,
        std::memory_order_release);

    LiveMirrorSetStatus(finalStatus);
    return 0;
}

static void LiveMirrorUpdateButton()
{
    if (g_LiveMirrorButton == nullptr)
        return;

    SetWindowTextW(
        g_LiveMirrorButton,
        g_LiveMirrorRunning.load(std::memory_order_acquire)
            ? L"Capturing..."
            : L"Take Snapshot");
}

static void LiveMirrorStart(HWND window)
{
    // In snapshot mode this starts exactly one capture request.
    if (!LiveMirrorBuildSnapshotPath(g_LiveMirrorSnapshotPath))
    {
        g_LiveMirrorEnabled.store(false, std::memory_order_release);
        LiveMirrorSetStatus(LiveMirrorStatus::PathError);
        LiveMirrorUpdateButton();
        return;
    }

    g_LiveMirrorEnabled.store(true, std::memory_order_release);
    g_LiveMirrorStop.store(false, std::memory_order_release);
    g_LiveMirrorAppliedGeneration.store(0, std::memory_order_release);
    g_LiveMirrorFailedGeneration.store(0, std::memory_order_release);
    LiveMirrorSetLastError(L"");
    LiveMirrorUpdateButton();

    bool expected = false;
    if (!g_LiveMirrorRunning.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
    {
        return;
    }

    HANDLE worker = CreateThread(
        nullptr,
        0,
        &LiveMirrorWorkerThread,
        reinterpret_cast<LPVOID>(window),
        0,
        nullptr);

    if (!worker)
    {
        g_LiveMirrorRunning.store(false, std::memory_order_release);
        g_LiveMirrorEnabled.store(false, std::memory_order_release);
        LiveMirrorSetLastError(L"Could not create the Live Mirror worker thread.");
        LiveMirrorSetStatus(LiveMirrorStatus::ProtocolError);
        LiveMirrorUpdateButton();
        return;
    }

    CloseHandle(worker);
}

static void LiveMirrorFreeze()
{
    g_LiveMirrorEnabled.store(false, std::memory_order_release);
    g_LiveMirrorStop.store(true, std::memory_order_release);
    LiveMirrorSetStatus(LiveMirrorStatus::Frozen);
    LiveMirrorUpdateButton();
}

static bool LiveRestoreGeneratedSelectionAfterRefresh(
    std::uint32_t itemId,
    std::uint64_t previousHash)
{
    if (itemId == 0)
        return false;

    // Find the selected runtime item in the freshly loaded scene,
    // using the same topmost-first ordering as LiveHitTestScene.
    for (auto rootIt =
             g_LiveRootDrawOrder.rbegin();
         rootIt !=
             g_LiveRootDrawOrder.rend();
         ++rootIt)
    {
        if (*rootIt >=
            g_LiveRoots.size())
        {
            continue;
        }

        const LiveRootScene& root =
            g_LiveRoots[*rootIt];

        const int rootX =
            root.rect.l -
            g_LiveDesktopRect.l;

        const int rootY =
            root.rect.t -
            g_LiveDesktopRect.t;

        for (auto blitIt =
                 root.blitIndices.rbegin();
             blitIt !=
                 root.blitIndices.rend();
             ++blitIt)
        {
            if (*blitIt >=
                g_LiveBlits.size())
            {
                continue;
            }

            const LiveBlit& blit =
                g_LiveBlits[*blitIt];

            if (blit.itemId != itemId)
                continue;

            const bool generatedItem =
                (blit.sourceDid &
                 0xFF000000u) !=
                    0x06000000u &&
                blit.sourceSurface != 0 &&
                blit.itemId != 0;

            if (!generatedItem)
                continue;

            g_LiveSelectedItemId =
                itemId;

            g_LiveSelectedGeneratedItem =
                true;

            g_LiveSelectedTextureIndex =
                static_cast<std::size_t>(-1);

            g_LiveSelectedGeneratedSurface =
                blit.sourceSurface;

            g_LiveSelectedSceneRect.l =
                rootX +
                blit.destRect.l;

            g_LiveSelectedSceneRect.t =
                rootY +
                blit.destRect.t;

            g_LiveSelectedSceneRect.r =
                rootX +
                blit.destRect.r;

            g_LiveSelectedSceneRect.b =
                rootY +
                blit.destRect.b;

            g_LiveHaveSelectedSceneRect =
                true;

            const LiveTexturePixels* generated =
                LiveGetGenerated(
                    blit.sourceSurface);

            if (generated != nullptr)
            {
                const std::uint64_t newHash =
                    LiveHashGeneratedPixels(
                        *generated);

                g_LiveSelectedGeneratedHash =
                    newHash;

                // The composition only needs recomputing when
                // the actual generated pixels changed.
                // The modern UI schedules generated composition
                // matching asynchronously. Do not block Live Mirror
                // refreshes by running the matcher here.
            }
            else
            {
                g_LiveSelectedGeneratedHash =
                    previousHash;
            }

            return true;
        }
    }

    return false;
}


static void LiveMirrorApplyReadyScene()
{
    if (g_LiveMirrorSnapshotPath.empty())
        return;

    std::string selectedDid;
    if (g_LiveSelectedTextureIndex != static_cast<std::size_t>(-1) &&
        g_LiveSelectedTextureIndex < g_Textures.size())
    {
        selectedDid = g_Textures[g_LiveSelectedTextureIndex].did;
    }

    // Preserve the semantic selection across a live refresh. LiveLoadSnapshot
    // replaces all scene maps atomically and therefore clears these process-local
    // associations while loading the new generation. They remain valid for the
    // current AC process when the refreshed scene still contains them.
    const std::uint32_t selectedControl = g_LiveSelectedControl;
    const std::uint32_t selectedItemId = g_LiveSelectedItemId;
    const bool selectedGeneratedItem = g_LiveSelectedGeneratedItem;
    const std::uint64_t selectedGeneratedHash =
        g_LiveSelectedGeneratedHash;
    const LiveRectI selectedSceneRect =
        g_LiveSelectedSceneRect;
    const bool hadSelectedSceneRect =
        g_LiveHaveSelectedSceneRect;
    const std::uint32_t hoveredControl = g_LiveHoveredControl;
    const std::uint32_t pressedControl = g_LivePressedControl;
    const bool mouseDown = g_LiveMouseDown;

    const std::uint32_t generation =
        g_LiveMirrorGeneration.load(std::memory_order_acquire);

    std::wstring error;
    if (!LiveLoadSnapshot(g_LiveMirrorSnapshotPath, error))
    {
        LiveMirrorSetLastError(error);
        g_LiveMirrorFailedGeneration.store(generation, std::memory_order_release);
        LiveMirrorSetStatus(LiveMirrorStatus::SceneUnavailable);
        LiveUpdateStatus();
        return;
    }

    if (!selectedDid.empty())
    {
        std::uint32_t did = 0;
        if (ParseHex(selectedDid, did))
        {
            const int index = LiveFindTextureIndex(did);
            if (index >= 0)
                g_LiveSelectedTextureIndex = static_cast<std::size_t>(index);
        }
    }

    // Control addresses are process-local but stable during a session. Restore
    // them only when the refreshed scene still contains the same object/item.
    if (selectedControl != 0 &&
        g_LiveControlStates.find(selectedControl) != g_LiveControlStates.end())
    {
        g_LiveSelectedControl = selectedControl;
    }
    if (selectedGeneratedItem &&
        selectedItemId != 0)
    {
        // Generated selections need more than the item ID restored:
        // LiveLoadSnapshot cleared their surface/hash/scene rectangle.
        // Resolve the same runtime item against this new generation.
        const bool restoredGenerated =
            LiveRestoreGeneratedSelectionAfterRefresh(
                selectedItemId,
                selectedGeneratedHash);

        if (!restoredGenerated)
        {
            // Some capture generations can briefly omit the selected
            // item. Keep its semantic selection sticky rather than
            // dropping back to the previously selected DAT texture.
            g_LiveSelectedItemId =
                selectedItemId;

            g_LiveSelectedGeneratedItem =
                true;

            g_LiveSelectedTextureIndex =
                static_cast<std::size_t>(-1);

            g_LiveSelectedGeneratedSurface =
                0;

            g_LiveSelectedGeneratedHash =
                selectedGeneratedHash;

            if (hadSelectedSceneRect)
            {
                g_LiveSelectedSceneRect =
                    selectedSceneRect;

                g_LiveHaveSelectedSceneRect =
                    true;
            }
        }
    }
    else if (
        selectedItemId != 0 &&
        g_LiveItemProvenance.find(
            selectedItemId) !=
            g_LiveItemProvenance.end())
    {
        g_LiveSelectedItemId =
            selectedItemId;

        g_LiveSelectedGeneratedItem =
            false;
    }
    if (hoveredControl != 0 &&
        g_LiveControlStates.find(hoveredControl) != g_LiveControlStates.end())
    {
        g_LiveHoveredControl = hoveredControl;
    }
    if (mouseDown && pressedControl != 0 &&
        g_LiveControlStates.find(pressedControl) != g_LiveControlStates.end())
    {
        g_LiveMouseDown = true;
        g_LivePressedControl = pressedControl;
    }

    // Build the new frame only after restoring hover/press state. The visible
    // canvas is not cleared until this UI-thread transaction is ready to swap.
    LiveRebuildCanvas();

    LiveMirrorSetLastError(L"");
    g_LiveMirrorAppliedGeneration.store(generation, std::memory_order_release);
    // The selected texture previews are static workspace assets. Rebuilding
    // them on every mirror generation caused the side inspector to flash.
    // Only the lightweight state text can legitimately change with a refreshed
    // runtime control map.
    LiveUpdateInspectorStateTextOnly();
    LiveUpdateStatus();
    if (g_LiveWindow != nullptr)
        InvalidateRect(g_LiveWindow, nullptr, FALSE);
}

static void LiveUpdateStatus()
{
    if (g_LiveStatusText == nullptr)
        return;

    std::wstring text =
        L"AC: " + std::wstring(LiveMirrorStatusLabel(LiveMirrorCurrentStatus()));

    const std::wstring mirrorError = LiveMirrorGetLastError();
    if (!mirrorError.empty() &&
        (LiveMirrorCurrentStatus() == LiveMirrorStatus::SceneUnavailable ||
         LiveMirrorCurrentStatus() == LiveMirrorStatus::ProtocolError ||
         LiveMirrorCurrentStatus() == LiveMirrorStatus::PathError))
    {
        text += L" (" + mirrorError + L")";
    }

    text += L"   |   ";

    if (g_LiveSnapshotPath.empty())
    {
        text += L"No snapshot loaded. Click Load Snapshot...";
    }
    else
    {
        text +=
            L"Roots: " + std::to_wstring(g_LiveRootsRendered) +
            L"   Rendered: " + std::to_wstring(g_LiveRendered) +
            L"   Missing: " + std::to_wstring(g_LiveMissing) +
            L"   Generated rendered: " +
                std::to_wstring(g_LiveGeneratedRendered) +
            L"   Generated skipped: " +
                std::to_wstring(g_LiveGenerated);

        if (g_LiveHoveredDid != 0)
        {
            text += L"   Hover DID: " + ToWide(LiveHex8(g_LiveHoveredDid));
        }
    }

    if (text != g_LiveLastStatusText)
    {
        g_LiveLastStatusText = text;
        SetWindowTextW(g_LiveStatusText, text.c_str());
    }
}

static void LiveUpdateModeButton()
{
    if (g_LiveToggleReplacementsButton != nullptr)
    {
        SetWindowTextW(
            g_LiveToggleReplacementsButton,
            g_LiveUseReplacements
                ? L"Preview: Replacements"
                : L"Preview: Vanilla");
    }
}

static void LiveReloadScene()
{
    g_LiveTextureCache.clear();
    g_LiveGeneratedCache.clear();
    LiveRebuildCanvas();
    LiveUpdateInspector();
    LiveUpdateStatus();

    if (g_LiveWindow != nullptr)
        InvalidateRect(g_LiveWindow, nullptr, FALSE);
}

static bool LiveLoadAndShowSnapshot(
    HWND owner,
    const std::wstring& path)
{
    std::wstring error;
    if (!LiveLoadSnapshot(path, error))
    {
        MessageBoxW(
            owner,
            error.c_str(),
            L"Live UI Snapshot",
            MB_OK | MB_ICONERROR);
        return false;
    }

    LiveReloadScene();
    return true;
}

static bool LivePointInRect(
    const LiveRectI& rect,
    int x,
    int y)
{
    return x >= rect.l && x < rect.r &&
           y >= rect.t && y < rect.b;
}

static bool LiveBlitPixelVisible(
    const LiveBlit& blit,
    int localX,
    int localY)
{
    const int destinationWidth =
        blit.destRect.r - blit.destRect.l;
    const int destinationHeight =
        blit.destRect.b - blit.destRect.t;
    const int sourceWidth =
        blit.sourceRect.r - blit.sourceRect.l;
    const int sourceHeight =
        blit.sourceRect.b - blit.sourceRect.t;

    if (destinationWidth <= 0 || destinationHeight <= 0 ||
        sourceWidth <= 0 || sourceHeight <= 0)
    {
        return false;
    }

    const LiveTexturePixels* texture =
        LiveGetTexture(blit.sourceDid);

    if (texture == nullptr)
        return true;

    const int xInDestination = localX - blit.destRect.l;
    const int yInDestination = localY - blit.destRect.t;

    const int sourceX =
        blit.sourceRect.l +
        static_cast<int>(
            (static_cast<long long>(xInDestination) * sourceWidth) /
            destinationWidth);
    const int sourceY =
        blit.sourceRect.t +
        static_cast<int>(
            (static_cast<long long>(yInDestination) * sourceHeight) /
            destinationHeight);

    if (sourceX < 0 || sourceY < 0 ||
        sourceX >= static_cast<int>(texture->width) ||
        sourceY >= static_cast<int>(texture->height))
    {
        return false;
    }

    const unsigned char alpha =
        texture->bgra[
            (static_cast<std::size_t>(sourceY) * texture->width +
             static_cast<std::size_t>(sourceX)) * 4u + 3u];

    return alpha > 8;
}

static LiveHitResult LiveHitTestScene(int sceneX, int sceneY)
{
    LiveHitResult result;

    for (auto rootIt = g_LiveRootDrawOrder.rbegin();
         rootIt != g_LiveRootDrawOrder.rend();
         ++rootIt)
    {
        if (*rootIt >= g_LiveRoots.size())
            continue;

        const LiveRootScene& root = g_LiveRoots[*rootIt];
        const int rootX = root.rect.l - g_LiveDesktopRect.l;
        const int rootY = root.rect.t - g_LiveDesktopRect.t;
        const int rootWidth = root.rect.r - root.rect.l + 1;
        const int rootHeight = root.rect.b - root.rect.t + 1;

        if (sceneX < rootX || sceneY < rootY ||
            sceneX >= rootX + rootWidth ||
            sceneY >= rootY + rootHeight)
        {
            continue;
        }

        const int localX = sceneX - rootX;
        const int localY = sceneY - rootY;

        for (auto blitIt = root.blitIndices.rbegin();
             blitIt != root.blitIndices.rend();
             ++blitIt)
        {
            if (*blitIt >= g_LiveBlits.size())
                continue;

            const LiveBlit& blit = g_LiveBlits[*blitIt];

            if (!LivePointInRect(blit.destRect, localX, localY))
                continue;

            const bool generatedItem =
                (blit.sourceDid & 0xFF000000u) != 0x06000000u &&
                blit.sourceSurface != 0 &&
                blit.itemId != 0;

            std::uint32_t editableDid = 0;

            if (!generatedItem)
            {
                editableDid =
                    LiveEditableDidForBlit(blit);

                if (editableDid == 0 ||
                    LiveFindTextureIndex(editableDid) < 0)
                {
                    continue;
                }
            }

            const LiveTexturePixels* hitTexture = nullptr;
            const std::uint32_t effectiveDid =
                LiveEffectiveDidForBlit(blit);

            if (generatedItem)
            {
                hitTexture =
                    LiveGetGenerated(
                        blit.sourceSurface);
            }
            else if (
                (effectiveDid & 0xFF000000u) ==
                    0x06000000u)
            {
                hitTexture =
                    LiveGetTexture(effectiveDid);
            }

            if (hitTexture != nullptr)
            {
                const int destinationWidth = blit.destRect.r - blit.destRect.l;
                const int destinationHeight = blit.destRect.b - blit.destRect.t;
                const int sourceWidth = blit.sourceRect.r - blit.sourceRect.l;
                const int sourceHeight = blit.sourceRect.b - blit.sourceRect.t;
                if (destinationWidth > 0 && destinationHeight > 0 &&
                    sourceWidth > 0 && sourceHeight > 0)
                {
                    const int sourceX = blit.sourceRect.l + static_cast<int>(
                        (static_cast<long long>(localX - blit.destRect.l) * sourceWidth) /
                        destinationWidth);
                    const int sourceY = blit.sourceRect.t + static_cast<int>(
                        (static_cast<long long>(localY - blit.destRect.t) * sourceHeight) /
                        destinationHeight);
                    if (sourceX >= 0 && sourceY >= 0 &&
                        sourceX < static_cast<int>(hitTexture->width) &&
                        sourceY < static_cast<int>(hitTexture->height))
                    {
                        const unsigned char alpha = hitTexture->bgra[
                            (static_cast<std::size_t>(sourceY) * hitTexture->width +
                             static_cast<std::size_t>(sourceX)) * 4u + 3u];
                        if (alpha <= 8)
                            continue;
                    }
                }
            }

            result.hit = true;
            result.did = editableDid;
            result.sourceSurface = blit.sourceSurface;
            result.control = blit.control;
            result.itemId = blit.itemId;
            result.generatedItem = generatedItem;
            result.sceneRect.l = rootX + blit.destRect.l;
            result.sceneRect.t = rootY + blit.destRect.t;
            result.sceneRect.r = rootX + blit.destRect.r;
            result.sceneRect.b = rootY + blit.destRect.b;
            return result;
        }
    }

    return result;
}

static bool LiveClientToScene(
    int clientX,
    int clientY,
    int& sceneX,
    int& sceneY)
{
    if (g_LiveDrawW <= 0 || g_LiveDrawH <= 0 ||
        g_LiveCanvasWidth <= 0 || g_LiveCanvasHeight <= 0)
    {
        return false;
    }

    if (clientX < g_LiveDrawX ||
        clientY < g_LiveDrawY ||
        clientX >= g_LiveDrawX + g_LiveDrawW ||
        clientY >= g_LiveDrawY + g_LiveDrawH)
    {
        return false;
    }

    sceneX = static_cast<int>(
        (static_cast<long long>(clientX - g_LiveDrawX) *
         g_LiveCanvasWidth) /
        g_LiveDrawW);
    sceneY = static_cast<int>(
        (static_cast<long long>(clientY - g_LiveDrawY) *
         g_LiveCanvasHeight) /
        g_LiveDrawH);

    return true;
}

static void LiveSelectHit(const LiveHitResult& hit)
{
    if (!hit.hit)
        return;

    g_LiveSelectedControl = hit.control;
    g_LiveSelectedItemId = hit.itemId;
    g_LiveSelectedGeneratedItem = hit.generatedItem;
    g_LiveSelectedSceneRect = hit.sceneRect;
    g_LiveHaveSelectedSceneRect = true;

    if (hit.generatedItem)
    {
        g_LiveSelectedTextureIndex =
            static_cast<std::size_t>(-1);

        g_LiveSelectedGeneratedSurface =
            hit.sourceSurface;

        g_LiveSelectedGeneratedHash = 0;

        const LiveTexturePixels* generated =
            LiveGetGenerated(
                g_LiveSelectedGeneratedSurface);

        if (generated != nullptr)
        {
            g_LiveSelectedGeneratedHash =
                LiveHashGeneratedPixels(
                    *generated);
        }
    }
    else
    {
        const int textureIndex =
            LiveFindTextureIndex(hit.did);

        if (textureIndex < 0)
            return;

        g_LiveSelectedTextureIndex =
            static_cast<std::size_t>(
                textureIndex);

        g_LiveSelectedGeneratedSurface = 0;
        g_LiveSelectedGeneratedHash = 0;
    }

    LiveUpdateInspector();

    if (g_LiveWindow != nullptr)
        InvalidateRect(g_LiveWindow, nullptr, FALSE);
}

static void LiveDrawSceneRect(
    HDC dc,
    const LiveRectI& sceneRect,
    COLORREF color,
    int penWidth)
{
    if (g_LiveCanvasWidth <= 0 || g_LiveCanvasHeight <= 0 ||
        g_LiveDrawW <= 0 || g_LiveDrawH <= 0)
    {
        return;
    }

    const int left =
        g_LiveDrawX +
        static_cast<int>(
            (static_cast<long long>(sceneRect.l) * g_LiveDrawW) /
            g_LiveCanvasWidth);
    const int top =
        g_LiveDrawY +
        static_cast<int>(
            (static_cast<long long>(sceneRect.t) * g_LiveDrawH) /
            g_LiveCanvasHeight);
    const int right =
        g_LiveDrawX +
        static_cast<int>(
            (static_cast<long long>(sceneRect.r) * g_LiveDrawW) /
            g_LiveCanvasWidth);
    const int bottom =
        g_LiveDrawY +
        static_cast<int>(
            (static_cast<long long>(sceneRect.b) * g_LiveDrawH) /
            g_LiveCanvasHeight);

    HPEN pen = CreatePen(PS_SOLID, penWidth, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));

    Rectangle(dc, left, top, right, bottom);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void LiveResizeControls(HWND window)
{
    RECT client = {};
    GetClientRect(window, &client);

    const int margin = 10;
    const int toolbarHeight = 30;
    const int inspectorWidth = 540;
    const int inspectorLeft = max(margin, client.right - inspectorWidth - margin);
    const int buttonTop = margin;

    MoveWindow(g_LiveLoadButton, margin, buttonTop, 120, 26, TRUE);
    MoveWindow(g_LiveReloadButton, margin + 126, buttonTop, 82, 26, TRUE);
    MoveWindow(
        g_LiveToggleReplacementsButton,
        margin + 214,
        buttonTop,
        160,
        26,
        TRUE);
    MoveWindow(g_LiveBoundsCheck, margin + 382, buttonTop + 2, 130, 24, TRUE);
    MoveWindow(g_LiveMirrorButton, margin + 518, buttonTop, 116, 26, TRUE);

    int y = margin;
    MoveWindow(g_LiveDetailsText, inspectorLeft, y, inspectorWidth, 116, TRUE);
    y += 122;

    MoveWindow(g_LiveOriginalTitle, inspectorLeft, y, 170, 22, TRUE);
    MoveWindow(g_LiveReplacementTitle, inspectorLeft + 180, y, 170, 22, TRUE);
    MoveWindow(g_LiveForegroundTitle, inspectorLeft + 360, y, 180, 22, TRUE);
    y += 24;

    MoveWindow(g_LiveOriginalPreview, inspectorLeft, y, 170, 154, TRUE);
    MoveWindow(g_LiveReplacementPreview, inspectorLeft + 180, y, 170, 154, TRUE);
    MoveWindow(g_LiveForegroundPreview, inspectorLeft + 360, y, 180, 154, TRUE);
    y += 164;

    MoveWindow(g_LiveStateText, inspectorLeft, y, inspectorWidth, 300, TRUE);
    y += 306;

    MoveWindow(g_LiveReplaceButton, inspectorLeft, y, 170, 30, TRUE);
    MoveWindow(g_LiveRemoveButton, inspectorLeft + 180, y, 170, 30, TRUE);
    MoveWindow(g_LiveLocateButton, inspectorLeft + 360, y, 180, 30, TRUE);

    y += 36;

    MoveWindow(
        g_LiveGeneratedMatchPreviews[0],
        inspectorLeft,
        y,
        170,
        154,
        TRUE);

    MoveWindow(
        g_LiveGeneratedMatchPreviews[1],
        inspectorLeft + 180,
        y,
        170,
        154,
        TRUE);

    MoveWindow(
        g_LiveGeneratedMatchPreviews[2],
        inspectorLeft + 360,
        y,
        180,
        154,
        TRUE);

    MoveWindow(
        g_LiveStatusText,
        margin,
        client.bottom - margin - 24,
        max(1, inspectorLeft - margin * 2),
        24,
        TRUE);

    // Do not erase/repaint the entire parent here. With WS_CLIPCHILDREN the
    // canvas repaint is isolated from toolbar/inspector child controls.
    InvalidateRect(window, nullptr, FALSE);
}

static void LivePaint(HWND window)
{
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(window, &paint);

    RECT client = {};
    GetClientRect(window, &client);

    HDC paintDc = CreateCompatibleDC(dc);
    HBITMAP paintBitmap = nullptr;
    HGDIOBJ oldPaintBitmap = nullptr;
    HDC targetDc = dc;

    if (paintDc != nullptr)
    {
        paintBitmap = CreateCompatibleBitmap(
            dc, max(1, client.right), max(1, client.bottom));
        if (paintBitmap != nullptr)
        {
            oldPaintBitmap = SelectObject(paintDc, paintBitmap);
            targetDc = paintDc;
        }
    }

    FillRect(
        targetDc,
        &client,
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    const int margin = 10;
    const int top = 46;
    const int bottom = client.bottom - 42;
    const int inspectorWidth = 540;
    const int canvasRight = client.right - inspectorWidth - 22;
    const int availableWidth = max(1, canvasRight - margin);
    const int availableHeight = max(1, bottom - top);

    g_LiveDrawX = 0;
    g_LiveDrawY = 0;
    g_LiveDrawW = 0;
    g_LiveDrawH = 0;

    if (!g_LiveCanvas.empty() &&
        g_LiveCanvasWidth > 0 &&
        g_LiveCanvasHeight > 0)
    {
        const double scaleX =
            static_cast<double>(availableWidth) /
            static_cast<double>(g_LiveCanvasWidth);
        const double scaleY =
            static_cast<double>(availableHeight) /
            static_cast<double>(g_LiveCanvasHeight);
        const double scale = min(scaleX, scaleY);

        const int drawWidth = max(
            1,
            static_cast<int>(g_LiveCanvasWidth * scale));
        const int drawHeight = max(
            1,
            static_cast<int>(g_LiveCanvasHeight * scale));
        const int drawX =
            margin + (availableWidth - drawWidth) / 2;
        const int drawY =
            top + (availableHeight - drawHeight) / 2;

        g_LiveDrawX = drawX;
        g_LiveDrawY = drawY;
        g_LiveDrawW = drawWidth;
        g_LiveDrawH = drawHeight;

        BITMAPINFO bitmap = {};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = g_LiveCanvasWidth;
        bitmap.bmiHeader.biHeight = -g_LiveCanvasHeight;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(targetDc, HALFTONE);
        StretchDIBits(
            targetDc,
            drawX,
            drawY,
            drawWidth,
            drawHeight,
            0,
            0,
            g_LiveCanvasWidth,
            g_LiveCanvasHeight,
            g_LiveCanvas.data(),
            &bitmap,
            DIB_RGB_COLORS,
            SRCCOPY);

        HGDIOBJ oldBrush = SelectObject(targetDc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(
            targetDc,
            drawX - 1,
            drawY - 1,
            drawX + drawWidth + 1,
            drawY + drawHeight + 1);
        SelectObject(targetDc, oldBrush);

        if (g_LiveShowBounds && g_LiveHaveDesktopRect)
        {
            for (const LiveRootScene& root : g_LiveRoots)
            {
                LiveRectI rect;
                rect.l = root.rect.l - g_LiveDesktopRect.l;
                rect.t = root.rect.t - g_LiveDesktopRect.t;
                rect.r = root.rect.r - g_LiveDesktopRect.l + 1;
                rect.b = root.rect.b - g_LiveDesktopRect.t + 1;
                LiveDrawSceneRect(targetDc, rect, RGB(255, 0, 255), 1);
            }
        }

        if (g_LiveHaveHoveredSceneRect)
            LiveDrawSceneRect(targetDc, g_LiveHoveredSceneRect, RGB(0, 200, 255), 2);

        if (g_LiveHaveSelectedSceneRect)
            LiveDrawSceneRect(targetDc, g_LiveSelectedSceneRect, RGB(255, 215, 0), 2);
    }

    if (targetDc != dc)
    {
        BitBlt(
            dc, 0, 0,
            max(1, client.right), max(1, client.bottom),
            targetDc, 0, 0, SRCCOPY);
    }

    if (oldPaintBitmap != nullptr)
        SelectObject(paintDc, oldPaintBitmap);
    if (paintBitmap != nullptr)
        DeleteObject(paintBitmap);
    if (paintDc != nullptr)
        DeleteDC(paintDc);

    EndPaint(window, &paint);
}

static LRESULT CALLBACK LiveWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            g_LiveWindow = window;

            g_LiveLoadButton = CreateWindowExW(
                0, L"BUTTON", L"Load Snapshot...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_LOAD_SNAPSHOT)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveReloadButton = CreateWindowExW(
                0, L"BUTTON", L"Reload",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_RELOAD)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveToggleReplacementsButton = CreateWindowExW(
                0, L"BUTTON", L"Preview: Replacements",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_TOGGLE_REPLACEMENTS)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveBoundsCheck = CreateWindowExW(
                0, L"BUTTON", L"Root Bounds",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_SHOW_BOUNDS)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(
                g_LiveBoundsCheck,
                BM_SETCHECK,
                g_LiveShowBounds ? BST_CHECKED : BST_UNCHECKED,
                0);

            g_LiveMirrorButton = CreateWindowExW(
                0, L"BUTTON", L"Connect Live",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_MIRROR)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveDetailsText = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC",
                L"Click a texture in the scene to inspect it.",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            g_LiveOriginalTitle = CreateWindowExW(
                0, L"STATIC", L"Original",
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            g_LiveReplacementTitle = CreateWindowExW(
                0, L"STATIC", L"Replacement: None",
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            g_LiveOriginalPreview = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            g_LiveReplacementPreview = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE | SS_NOTIFY,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(
                        ID_LIVE_BG_LOCATE)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveForegroundTitle = CreateWindowExW(
                0, L"STATIC", L"Foreground Icon",
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            g_LiveForegroundPreview = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE | SS_NOTIFY,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(
                        ID_LIVE_FG_LOCATE)),
                GetModuleHandleW(nullptr), nullptr);

            for (int i = 0; i < 3; ++i)
            {
                g_LiveGeneratedMatchPreviews[i] =
                    CreateWindowExW(
                        WS_EX_CLIENTEDGE,
                        L"STATIC",
                        L"",
                        WS_CHILD |
                            WS_VISIBLE |
                            SS_BITMAP |
                            SS_CENTERIMAGE,
                        0, 0, 0, 0,
                        window,
                        nullptr,
                        GetModuleHandleW(nullptr),
                        nullptr);

                if (g_LiveGeneratedMatchPreviews[i] ==
                    nullptr)
                {
                    return -1;
                }
            }

            g_LiveStateText = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC",
                L"Interactive state metadata will appear here.",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            g_LiveReplaceButton = CreateWindowExW(
                0, L"BUTTON", L"Replace PNG...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_REPLACE)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveRemoveButton = CreateWindowExW(
                0, L"BUTTON", L"Remove",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_REMOVE)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveLocateButton = CreateWindowExW(
                0, L"BUTTON", L"Locate in Tabs",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_LIVE_LOCATE)),
                GetModuleHandleW(nullptr), nullptr);

            g_LiveStatusText = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"STATIC",
                L"No snapshot loaded.",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                0, 0, 0, 0, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);

            if (g_LiveLoadButton == nullptr ||
                g_LiveReloadButton == nullptr ||
                g_LiveToggleReplacementsButton == nullptr ||
                g_LiveBoundsCheck == nullptr ||
                g_LiveMirrorButton == nullptr ||
                g_LiveDetailsText == nullptr ||
                g_LiveStateText == nullptr ||
                g_LiveOriginalTitle == nullptr ||
                g_LiveOriginalPreview == nullptr ||
                g_LiveReplacementTitle == nullptr ||
                g_LiveReplacementPreview == nullptr ||
                g_LiveForegroundTitle == nullptr ||
                g_LiveForegroundPreview == nullptr ||
                g_LiveReplaceButton == nullptr ||
                g_LiveRemoveButton == nullptr ||
                g_LiveLocateButton == nullptr ||
                g_LiveStatusText == nullptr)
            {
                return -1;
            }

            LiveUpdateModeButton();
            LiveResizeControls(window);

            std::wstring latest;
            if (LiveFindLatestSnapshot(latest))
                LiveLoadAndShowSnapshot(window, latest);
            else
                LiveUpdateStatus();

            // Snapshot mode is user-driven. Opening the editor never starts
            // background capture traffic; the last snapshot remains visible
            // until the user explicitly requests another one.
            LiveUpdateStatus();

            return 0;
        }

        case WM_COMMAND:
        {
            const int command = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            if (notification == BN_CLICKED &&
                command == ID_LIVE_LOAD_SNAPSHOT)
            {
                LiveMirrorFreeze();
                std::wstring path;
                if (LiveChooseSnapshot(window, path))
                    LiveLoadAndShowSnapshot(window, path);
                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_RELOAD)
            {
                if (!g_LiveSnapshotPath.empty())
                {
                    std::wstring error;
                    if (!LiveLoadSnapshot(g_LiveSnapshotPath, error))
                    {
                        MessageBoxW(
                            window,
                            error.c_str(),
                            L"Live UI Snapshot",
                            MB_OK | MB_ICONERROR);
                    }
                    else
                    {
                        LiveReloadScene();
                    }
                }
                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_TOGGLE_REPLACEMENTS)
            {
                g_LiveUseReplacements = !g_LiveUseReplacements;
                LiveUpdateModeButton();
                LiveReloadScene();
                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_SHOW_BOUNDS)
            {
                g_LiveShowBounds =
                    SendMessageW(
                        g_LiveBoundsCheck,
                        BM_GETCHECK,
                        0,
                        0) == BST_CHECKED;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_MIRROR)
            {
                if (!g_LiveMirrorRunning.load(
                        std::memory_order_acquire))
                {
                    LiveMirrorStart(window);
                }

                LiveUpdateStatus();
                return 0;
            }

            if (notification == STN_CLICKED &&
                command == ID_LIVE_BG_LOCATE)
            {
                if (g_LiveSelectedGeneratedItem)
                {
                    LiveLocateGeneratedBackground();
                }

                return 0;
            }

            if (notification == STN_CLICKED &&
                command == ID_LIVE_FG_LOCATE)
            {
                if (g_LiveSelectedGeneratedItem)
                {
                    LiveLocateGeneratedMatch(0);
                }

                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_REPLACE)
            {
                if (g_LiveSelectedGeneratedItem)
                {
                    LiveLocateGeneratedMatch(0);
                    return 0;
                }

                if (g_LiveSelectedTextureIndex != static_cast<std::size_t>(-1) &&
                    g_LiveSelectedTextureIndex < g_Textures.size())
                {
                    const int previousSelectedRow = g_SelectedRow;
                    const int previousSelectedListRow = g_SelectedListRow;

                    g_SelectedRow =
                        static_cast<int>(g_LiveSelectedTextureIndex);
                    g_SelectedListRow = -1;
                    ReplaceSelectedTexture(window);

                    const int liveListRow =
                        FindListRowForTexture(g_LiveSelectedTextureIndex);
                    if (liveListRow >= 0)
                    {
                        SetSubItem(
                            liveListRow,
                            5,
                            HasReplacement(
                                g_Textures[g_LiveSelectedTextureIndex])
                                ? L"REPLACED"
                                : L"");
                    }

                    g_SelectedRow = previousSelectedRow;
                    g_SelectedListRow = previousSelectedListRow;
                    if (previousSelectedRow >= 0 &&
                        static_cast<std::size_t>(previousSelectedRow) <
                            g_Textures.size())
                    {
                        ShowTextureDetails(previousSelectedRow);
                        g_SelectedListRow = previousSelectedListRow;
                    }
                    else
                    {
                        ClearReplacementPreview();
                        SetWindowTextW(
                            g_ReplacementTitle,
                            L"Current Replacement: None");
                    }

                    LiveReloadScene();
                }
                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_REMOVE)
            {
                if (g_LiveSelectedGeneratedItem)
                {
                    LiveLocateGeneratedMatch(1);
                    return 0;
                }

                if (g_LiveSelectedTextureIndex != static_cast<std::size_t>(-1) &&
                    g_LiveSelectedTextureIndex < g_Textures.size())
                {
                    RemoveReplacementTexture(
                        window,
                        g_LiveSelectedTextureIndex);
                    LiveReloadScene();
                }
                return 0;
            }

            if (notification == BN_CLICKED &&
                command == ID_LIVE_LOCATE)
            {
                if (g_LiveSelectedGeneratedItem)
                {
                    LiveLocateGeneratedMatch(2);
                    return 0;
                }

                if (g_LiveSelectedTextureIndex != static_cast<std::size_t>(-1) &&
                    g_LiveSelectedTextureIndex < g_Textures.size())
                {
                    GoToTexture(g_LiveSelectedTextureIndex);
                    if (g_MainWindow != nullptr)
                    {
                        ShowWindow(g_MainWindow, SW_RESTORE);
                        SetForegroundWindow(g_MainWindow);
                    }
                }
                return 0;
            }

            break;
        }

        case WM_APP_LIVE_MIRROR_READY:
            LiveMirrorApplyReadyScene();
            return 0;

        case WM_APP_LIVE_MIRROR_STATUS:
            LiveMirrorUpdateButton();
            LiveUpdateStatus();
            return 0;

        case WM_MOUSEMOVE:
        {
            const int clientX = GET_X_LPARAM(lParam);
            const int clientY = GET_Y_LPARAM(lParam);
            int sceneX = 0;
            int sceneY = 0;

            LiveHitResult hit;
            if (LiveClientToScene(clientX, clientY, sceneX, sceneY))
                hit = LiveHitTestScene(sceneX, sceneY);

            const std::uint32_t newDid = hit.hit ? hit.did : 0;
            const std::uint32_t newControl = hit.hit ? hit.control : 0;
            const bool rectChanged =
                hit.hit != g_LiveHaveHoveredSceneRect ||
                (hit.hit &&
                 (hit.sceneRect.l != g_LiveHoveredSceneRect.l ||
                  hit.sceneRect.t != g_LiveHoveredSceneRect.t ||
                  hit.sceneRect.r != g_LiveHoveredSceneRect.r ||
                  hit.sceneRect.b != g_LiveHoveredSceneRect.b));
            const bool stateChanged = newControl != g_LiveHoveredControl;

            if (newDid != g_LiveHoveredDid || rectChanged || stateChanged)
            {
                g_LiveHoveredDid = newDid;
                g_LiveHoveredControl = newControl;
                g_LiveHaveHoveredSceneRect = hit.hit;
                if (hit.hit)
                    g_LiveHoveredSceneRect = hit.sceneRect;
                if (stateChanged)
                    LiveRebuildCanvas();
                LiveUpdateStatus();
                InvalidateRect(window, nullptr, FALSE);
            }

            if (!g_LiveTrackingMouseLeave)
            {
                TRACKMOUSEEVENT track = {};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                if (TrackMouseEvent(&track))
                    g_LiveTrackingMouseLeave = true;
            }
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        {
            const int clientX = GET_X_LPARAM(lParam);
            const int clientY = GET_Y_LPARAM(lParam);
            int sceneX = 0;
            int sceneY = 0;

            if (LiveClientToScene(clientX, clientY, sceneX, sceneY))
            {
                const LiveHitResult hit = LiveHitTestScene(sceneX, sceneY);
                if (hit.hit)
                {
                    g_LiveMouseDown = true;
                    g_LivePressedControl = hit.control;
                    if (hit.control != 0)
                        LiveRebuildCanvas();
                    SetCapture(window);
                    LiveSelectHit(hit);

                    if (message == WM_LBUTTONDBLCLK &&
                        g_LiveSelectedTextureIndex != static_cast<std::size_t>(-1))
                    {
                        GoToTexture(g_LiveSelectedTextureIndex);
                        if (g_MainWindow != nullptr)
                        {
                            ShowWindow(g_MainWindow, SW_RESTORE);
                            SetForegroundWindow(g_MainWindow);
                        }
                    }
                }
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_LiveMouseDown)
            {
                const std::uint32_t oldPressed = g_LivePressedControl;
                g_LiveMouseDown = false;
                g_LivePressedControl = 0;
                if (oldPressed != 0)
                    LiveRebuildCanvas();
                ReleaseCapture();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSELEAVE:
            g_LiveTrackingMouseLeave = false;
            if (g_LiveHoveredControl != 0 || g_LiveHoveredDid != 0)
            {
                g_LiveHoveredControl = 0;
                g_LiveHoveredDid = 0;
                g_LiveHaveHoveredSceneRect = false;
                LiveRebuildCanvas();
                LiveUpdateStatus();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            LiveResizeControls(window);
            return 0;

        case WM_PAINT:
            LivePaint(window);
            return 0;

        case WM_DESTROY:
            LiveMirrorFreeze();
            LiveClearInspectorBitmaps();
            g_LiveWindow = nullptr;
            g_LiveLoadButton = nullptr;
            g_LiveReloadButton = nullptr;
            g_LiveToggleReplacementsButton = nullptr;
            g_LiveBoundsCheck = nullptr;
            g_LiveDetailsText = nullptr;
            g_LiveStateText = nullptr;
            g_LiveOriginalTitle = nullptr;
            g_LiveOriginalPreview = nullptr;
            g_LiveReplacementTitle = nullptr;
            g_LiveReplacementPreview = nullptr;
            g_LiveForegroundTitle = nullptr;
            g_LiveForegroundPreview = nullptr;

            for (int i = 0; i < 3; ++i)
                g_LiveGeneratedMatchPreviews[i] = nullptr;
            g_LiveReplaceButton = nullptr;
            g_LiveRemoveButton = nullptr;
            g_LiveLocateButton = nullptr;
            g_LiveMirrorButton = nullptr;
            g_LiveStatusText = nullptr;
            return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

static void ShowLiveUiEditor(HWND owner)
{
    if (g_LiveWindow != nullptr && IsWindow(g_LiveWindow))
    {
        ShowWindow(g_LiveWindow, SW_RESTORE);
        SetForegroundWindow(g_LiveWindow);
        return;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = LiveWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = LIVE_WINDOW_CLASS;

    if (RegisterClassExW(&windowClass) == 0)
    {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(
                owner,
                L"Could not register the Live UI editor window class.",
                WINDOW_TITLE,
                MB_OK | MB_ICONERROR);
            return;
        }
    }

    g_LiveWindow = CreateWindowExW(
        0,
        LIVE_WINDOW_CLASS,
        L"ACModernUI Manager - Live UI Editor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1500,
        900,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_LiveWindow == nullptr)
    {
        MessageBoxW(
            owner,
            L"Could not create the Live UI editor window.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);
        return;
    }

    ShowWindow(g_LiveWindow, SW_SHOW);
    UpdateWindow(g_LiveWindow);
}


#include "ACModernUITemplateMapperDev.inl"
#include "ACModernUITemplateImporterDev.inl"
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
static void TemplateImportDrawUi();
#endif
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
static void TemplateImportWatchTick();
#endif
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
static void TemplateEditorDrawCanvas();
static void TemplateEditorDrawInspector();
#endif
#include "ACModernUIModern.inl"
#include "ACModernUITemplateImportUI.inl"
#include "ACModernUITemplateEditorUI.inl"

static void ResizeControls(
    HWND window)
{
    RECT client = {};

    GetClientRect(
        window,
        &client);

    const int margin = 12;
    const int statusHeight = 28;
    const int tabHeight = 34;
    const int infoHeight = 28;
    const int gap = 12;
    const int detailsWidth = 320;

    MoveWindow(
        g_StatusText,
        margin,
        margin,
        client.right - (margin * 2),
        statusHeight,
        TRUE);

    const int infoTop =
        client.bottom -
        margin -
        infoHeight;

    MoveWindow(
        g_InfoText,
        margin,
        infoTop,
        client.right - (margin * 2) - 190,
        infoHeight,
        TRUE);

    MoveWindow(
        g_ProgressBar,
        client.right - margin - 180,
        infoTop + 4,
        180,
        20,
        TRUE);

    MoveWindow(
        g_DisplayUnsupportedCheck,
        margin,
        margin + statusHeight,
        220,
        24,
        TRUE);

    MoveWindow(
        g_DisplayReplacedCheck,
        margin + 225,
        margin + statusHeight,
        190,
        24,
        TRUE);

    MoveWindow(
        g_ClearEncounteredButton,
        margin + 420,
        margin + statusHeight,
        150,
        24,
        TRUE);

    MoveWindow(
        g_AddCustomTabButton,
        margin + 575,
        margin + statusHeight,
        100,
        24,
        TRUE);

    const int filterTop = margin + statusHeight + 30;

    MoveWindow(g_FilterWidthEdit, margin, filterTop, 80, 24, TRUE);
    MoveWindow(g_FilterHeightEdit, margin + 86, filterTop, 80, 24, TRUE);
    MoveWindow(g_ApplySizeFilterButton, margin + 174, filterTop, 64, 24, TRUE);
    MoveWindow(g_ClearSizeFilterButton, margin + 244, filterTop, 64, 24, TRUE);
    MoveWindow(g_ImportPackButton, margin + 320, filterTop, 140, 24, TRUE);
    MoveWindow(g_ExportPackButton, margin + 466, filterTop, 140, 24, TRUE);
    MoveWindow(g_LiveUiButton, margin + 612, filterTop, 126, 24, TRUE);

    const int tabTop =
        filterTop + 30;

    MoveWindow(
        g_TabControl,
        margin,
        tabTop,
        client.right - (margin * 2),
        tabHeight,
        TRUE);

    const int contentTop =
        tabTop + tabHeight + 6;

    const int contentHeight =
        infoTop -
        contentTop -
        8;

    int listWidth =
        client.right -
        (margin * 2) -
        detailsWidth -
        gap;

    if (listWidth < 320)
        listWidth = 320;

    MoveWindow(
        g_ListView,
        margin,
        contentTop,
        listWidth,
        contentHeight,
        TRUE);

    const int detailsLeft =
        margin + listWidth + gap;

    MoveWindow(
        g_DetailsTitle,
        detailsLeft,
        contentTop,
        detailsWidth,
        28,
        TRUE);

    MoveWindow(
        g_LargePreview,
        detailsLeft + 64,
        contentTop + 36,
        192,
        192,
        TRUE);

    MoveWindow(
        g_DetailsText,
        detailsLeft,
        contentTop + 238,
        detailsWidth,
        132,
        TRUE);

    MoveWindow(
        g_CopyButton,
        detailsLeft,
        contentTop + 376,
        88,
        32,
        TRUE);

    MoveWindow(
        g_CopyTextureButton,
        detailsLeft + 94,
        contentTop + 376,
        108,
        32,
        TRUE);

    MoveWindow(
        g_ReplaceButton,
        detailsLeft + 208,
        contentTop + 376,
        112,
        32,
        TRUE);

    MoveWindow(
        g_ReplacementTitle,
        detailsLeft,
        contentTop + 418,
        detailsWidth,
        26,
        TRUE);

    MoveWindow(
        g_ReplacementPreview,
        detailsLeft + 64,
        contentTop + 448,
        192,
        192,
        TRUE);
}

static LRESULT CALLBACK WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (ModernHandleWin32Message(
            window,
            message,
            wParam,
            lParam))
    {
        return 1;
    }

    switch (message)
    {
        case WM_CREATE:
        {
            g_MainWindow = window;
            g_StatusText =
                CreateWindowExW(
                    0,
                    L"STATIC",
                    L"Loading texture catalog...",
                    WS_CHILD |
                        WS_VISIBLE,
                    0,
                    0,
                    0,
                    0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_DisplayUnsupportedCheck =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Display Unsupported Items",
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(ID_DISPLAY_UNSUPPORTED)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            SendMessageW(
                g_DisplayUnsupportedCheck,
                BM_SETCHECK,
                BST_UNCHECKED,
                0);

            g_DisplayReplacedCheck =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Display Replaced Items",
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(ID_DISPLAY_REPLACED)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            SendMessageW(
                g_DisplayReplacedCheck,
                BM_SETCHECK,
                BST_CHECKED,
                0);

            g_ClearEncounteredButton =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Clear Encountered",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(ID_CLEAR_ENCOUNTERED)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_AddCustomTabButton =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Add Tab...",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(ID_ADD_CUSTOM_TAB)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_FilterWidthEdit =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FILTER_WIDTH)),
                    GetModuleHandleW(nullptr), nullptr);

            g_FilterHeightEdit =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FILTER_HEIGHT)),
                    GetModuleHandleW(nullptr), nullptr);

            g_ApplySizeFilterButton =
                CreateWindowExW(
                    0, L"BUTTON", L"Apply",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_APPLY_SIZE_FILTER)),
                    GetModuleHandleW(nullptr), nullptr);

            g_ClearSizeFilterButton =
                CreateWindowExW(
                    0, L"BUTTON", L"Clear",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_CLEAR_SIZE_FILTER)),
                    GetModuleHandleW(nullptr), nullptr);

            g_ImportPackButton =
                CreateWindowExW(
                    0, L"BUTTON", L"Import UI Pack...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_IMPORT_UI_PACK)),
                    GetModuleHandleW(nullptr), nullptr);

            g_ExportPackButton =
                CreateWindowExW(
                    0, L"BUTTON", L"Export UI Pack...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EXPORT_UI_PACK)),
                    GetModuleHandleW(nullptr), nullptr);

            g_LiveUiButton =
                CreateWindowExW(
                    0, L"BUTTON", L"Live UI Editor...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OPEN_LIVE_UI)),
                    GetModuleHandleW(nullptr), nullptr);

            SendMessageW(g_FilterWidthEdit, EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(L"Width"));
            SendMessageW(g_FilterHeightEdit, EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(L"Height"));

            g_TabControl =
                CreateWindowExW(
                    0,
                    WC_TABCONTROLW,
                    L"",
                    WS_CHILD |
                        WS_VISIBLE |
                        WS_CLIPSIBLINGS,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_ListView =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    WC_LISTVIEWW,
                    L"",
                    WS_CHILD |
                        WS_VISIBLE |
                        LVS_REPORT |
                        LVS_SHOWSELALWAYS,
                    0,
                    0,
                    0,
                    0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_DetailsTitle =
                CreateWindowExW(
                    0,
                    L"STATIC",
                    L"Selected Texture (Original)",
                    WS_CHILD |
                        WS_VISIBLE,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_LargePreview =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"STATIC",
                    L"",
                    WS_CHILD |
                        WS_VISIBLE |
                        SS_BITMAP |
                        SS_CENTERIMAGE,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_DetailsText =
                CreateWindowExW(
                    0,
                    L"STATIC",
                    L"Select a texture to inspect it.",
                    WS_CHILD |
                        WS_VISIBLE |
                        SS_LEFT,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_ReplaceButton =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Replace PNG...",
                    WS_CHILD |
                        WS_VISIBLE |
                        BS_PUSHBUTTON,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(
                            ID_REPLACE_PNG)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_CopyButton =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Copy DID",
                    WS_CHILD |
                        WS_VISIBLE |
                        BS_PUSHBUTTON,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(
                            ID_COPY_DID)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_CopyTextureButton =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Copy Texture",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(ID_COPY_TEXTURE)),
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_ReplacementTitle =
                CreateWindowExW(
                    0,
                    L"STATIC",
                    L"Current Replacement: None",
                    WS_CHILD | WS_VISIBLE,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_ReplacementPreview =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"STATIC",
                    L"",
                    WS_CHILD |
                        WS_VISIBLE |
                        SS_BITMAP |
                        SS_CENTERIMAGE,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_InfoText =
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"STATIC",
                    L"Ready.",
                    WS_CHILD |
                        WS_VISIBLE |
                        SS_LEFT |
                        SS_CENTERIMAGE,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            g_ProgressBar =
                CreateWindowExW(
                    0,
                    PROGRESS_CLASSW,
                    L"",
                    WS_CHILD |
                        PBS_MARQUEE,
                    0, 0, 0, 0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);

            if (g_StatusText == nullptr ||
                g_DisplayUnsupportedCheck == nullptr ||
                g_DisplayReplacedCheck == nullptr ||
                g_ClearEncounteredButton == nullptr ||
                g_AddCustomTabButton == nullptr ||
                g_FilterWidthEdit == nullptr ||
                g_FilterHeightEdit == nullptr ||
                g_ApplySizeFilterButton == nullptr ||
                g_ClearSizeFilterButton == nullptr ||
                g_ImportPackButton == nullptr ||
                g_ExportPackButton == nullptr ||
                g_LiveUiButton == nullptr ||
                g_TabControl == nullptr ||
                g_ListView == nullptr ||
                g_DetailsTitle == nullptr ||
                g_LargePreview == nullptr ||
                g_DetailsText == nullptr ||
                g_ReplaceButton == nullptr ||
                g_CopyButton == nullptr ||
                g_CopyTextureButton == nullptr ||
                g_ReplacementTitle == nullptr ||
                g_ReplacementPreview == nullptr ||
                g_InfoText == nullptr ||
                g_ProgressBar == nullptr)
            {
                return -1;
            }

            ListView_SetExtendedListViewStyle(
                g_ListView,
                LVS_EX_FULLROWSELECT |
                    LVS_EX_DOUBLEBUFFER |
                    LVS_EX_GRIDLINES);

            g_ThumbnailList =
                ImageList_Create(
                    THUMBNAIL_SIZE,
                    THUMBNAIL_SIZE,
                    ILC_COLOR32,
                    64,
                    64);

            if (g_ThumbnailList == nullptr)
                return -1;

            ListView_SetImageList(
                g_ListView,
                g_ThumbnailList,
                LVSIL_SMALL);


            AddColumn(
                0,
                200,
                L"Texture / DID");

            AddColumn(
                1,
                100,
                L"Size");

            AddColumn(
                2,
                130,
                L"Format");

            AddColumn(
                3,
                120,
                L"Bytes");

            AddColumn(
                4,
                100,
                L"Preview");

            AddColumn(
                5,
                110,
                L"Replaced");
            AddColumn(6, 220, L"Groups");
            AddColumn(7, 240, L"Notes");

            LoadCustomTabs();
            LoadTextureNotes();

            if (!LoadTextureCatalog())
            {
                BuildTextureTabs();
                PopulateList();

                SetWindowTextW(
                    g_StatusText,
                    L"No client DAT loaded. Open a DAT from the Welcome page.");
            }
            else
            {
                BuildTextureTabs();
                PopulateList();

                SetWindowTextW(
                    g_StatusText,
                    ToWide(g_DatScanSummary).c_str());
            }

            g_StopPreviewWorker = false;
            g_PreviewWorker = std::thread(PreviewWorkerMain);
            QueueSmallPreviewsForActiveTab();
            QueueVisiblePreviews();
            SetTimer(window, PREVIEW_TIMER_ID, 150, nullptr);
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
            RegisterHotKey(window, TEMPLATE_DEV_HOTKEY_ID,
                           MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'M');
#endif

            ResizeControls(window);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_REPLACE_PNG &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ReplaceSelectedTexture(window);
                if (g_LiveWindow != nullptr && IsWindow(g_LiveWindow))
                    LiveReloadScene();
                return 0;
            }

            if (LOWORD(wParam) == ID_COPY_DID &&
                HIWORD(wParam) == BN_CLICKED)
            {
                CopySelectedDidToClipboard(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_COPY_TEXTURE &&
                HIWORD(wParam) == BN_CLICKED)
            {
                CopySelectedTextureToClipboard(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_APPLY_SIZE_FILTER &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ApplySizeFilter(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_CLEAR_SIZE_FILTER &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ClearSizeFilter();
                return 0;
            }

            if (LOWORD(wParam) == ID_IMPORT_UI_PACK &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ImportUiPack(window);
                if (g_LiveWindow != nullptr && IsWindow(g_LiveWindow))
                    LiveReloadScene();
                return 0;
            }

            if (LOWORD(wParam) == ID_EXPORT_UI_PACK &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ExportUiPack(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_OPEN_LIVE_UI &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ShowLiveUiEditor(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_DISPLAY_REPLACED &&
                HIWORD(wParam) == BN_CLICKED)
            {
                g_DisplayReplaced =
                    SendMessageW(
                        g_DisplayReplacedCheck,
                        BM_GETCHECK, 0, 0) == BST_CHECKED;

                ClearLargePreview();
                ClearReplacementPreview();
                SetWindowTextW(
                    g_DetailsText,
                    L"Select a texture to inspect it.");
                SetWindowTextW(
                    g_ReplacementTitle,
                    L"Current Replacement: None");
                PopulateList();
                QueueSmallPreviewsForActiveTab();
                QueueVisiblePreviews();
                return 0;
            }

            if (LOWORD(wParam) == ID_CLEAR_ENCOUNTERED &&
                HIWORD(wParam) == BN_CLICKED)
            {
                ClearEncounteredData(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_ADD_CUSTOM_TAB &&
                HIWORD(wParam) == BN_CLICKED)
            {
                AddCustomTab(window);
                return 0;
            }

            if (LOWORD(wParam) == ID_DISPLAY_UNSUPPORTED &&
                HIWORD(wParam) == BN_CLICKED)
            {
                g_DisplayUnsupported =
                    SendMessageW(
                        g_DisplayUnsupportedCheck,
                        BM_GETCHECK, 0, 0) == BST_CHECKED;

                ClearLargePreview();
                ClearReplacementPreview();
                SetWindowTextW(
                    g_DetailsText,
                    L"Select a texture to inspect it.");
                SetWindowTextW(
                    g_ReplacementTitle,
                    L"Current Replacement: None");
                PopulateList();
                QueueVisiblePreviews();
                return 0;
            }
            break;

        case WM_APP_LIVE_MIRROR_READY:
            // The integrated shell is now the Live Mirror target.
            LiveMirrorApplyReadyScene();
            ModernMarkLiveCanvasDirty();
            return 0;

        case WM_APP_LIVE_MIRROR_STATUS:
            // The ImGui header reads the atomic status directly.
            return 0;

#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
        case WM_HOTKEY:
            if (wParam == TEMPLATE_DEV_HOTKEY_ID)
            {
                TemplateDevStart(window);
                return 0;
            }
            break;
#endif

        case WM_TIMER:
            if (wParam == PREVIEW_TIMER_ID)
            {
                QueueVisiblePreviews();
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
                TemplateDevTick(window);
#endif
                return 0;
            }
            break;

        case WM_APP_PREVIEW_READY:
            ApplyReadyPreview(
                static_cast<std::size_t>(wParam),
                lParam != 0);
            return 0;

        case WM_NOTIFY:
        {
            const NMHDR* header =
                reinterpret_cast<const NMHDR*>(lParam);

            if (header != nullptr &&
                header->hwndFrom == g_TabControl &&
                header->code == TCN_SELCHANGE)
            {
                const int selected =
                    TabCtrl_GetCurSel(g_TabControl);

                if (selected >= 0 &&
                    static_cast<std::size_t>(selected) <
                        g_TabPrefixes.size())
                {
                    g_ActivePrefix =
                        g_TabPrefixes[
                            static_cast<std::size_t>(
                                selected)];
                    g_ReplacementsOnly = false;
                    g_EncounteredOnly = false;
                    g_ActiveCustomTab = -1;
                }
                else if (selected ==
                    static_cast<int>(g_TabPrefixes.size() + 1))
                {
                    g_ActivePrefix.clear();
                    g_ReplacementsOnly = true;
                    g_EncounteredOnly = false;
                    g_ActiveCustomTab = -1;
                }
                else if (selected ==
                    static_cast<int>(g_TabPrefixes.size() + 2))
                {
                    g_ActivePrefix.clear();
                    g_ReplacementsOnly = false;
                    g_EncounteredOnly = true;
                    g_ActiveCustomTab = -1;
                }
                else if (selected >= static_cast<int>(g_TabPrefixes.size() + 3))
                {
                    const int customIndex =
                        selected - static_cast<int>(g_TabPrefixes.size() + 3);
                    g_ActivePrefix.clear();
                    g_ReplacementsOnly = false;
                    g_EncounteredOnly = false;
                    g_ActiveCustomTab =
                        (customIndex >= 0 && static_cast<std::size_t>(customIndex) < g_CustomTabs.size())
                            ? customIndex : -1;
                }
                else
                {
                    // The tab immediately after the prefix tabs is All.
                    g_ActivePrefix.clear();
                    g_ReplacementsOnly = false;
                    g_EncounteredOnly = false;
                    g_ActiveCustomTab = -1;
                }

                ClearLargePreview();
                ClearReplacementPreview();

                SetWindowTextW(
                    g_DetailsText,
                    L"Select a texture to inspect it.");

                SetWindowTextW(
                    g_ReplacementTitle,
                    L"Current Replacement: None");

                PopulateList();
                QueueSmallPreviewsForActiveTab();
                QueueVisiblePreviews();
                return 0;
            }

            if (header != nullptr &&
                header->hwndFrom == g_TabControl &&
                header->code == NM_RCLICK)
            {
                POINT point = {};
                GetCursorPos(&point);
                POINT clientPoint = point;
                ScreenToClient(g_TabControl, &clientPoint);

                TCHITTESTINFO hit = {};
                hit.pt = clientPoint;
                const int tabIndex = TabCtrl_HitTest(g_TabControl, &hit);
                const int customIndex =
                    tabIndex - static_cast<int>(g_TabPrefixes.size() + 3);

                if (customIndex >= 0 && static_cast<std::size_t>(customIndex) < g_CustomTabs.size())
                {
                    HMENU menu = CreatePopupMenu();
                    if (menu != nullptr)
                    {
                        AppendMenuW(menu, MF_STRING, ID_RENAME_CUSTOM_TAB, L"Rename Tab...");
                        AppendMenuW(menu, MF_STRING, ID_DELETE_CUSTOM_TAB, L"Delete Tab");

                        const UINT command = TrackPopupMenu(
                            menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                            point.x, point.y, 0, window, nullptr);
                        DestroyMenu(menu);

                        if (command == ID_RENAME_CUSTOM_TAB)
                            RenameCustomTab(window, customIndex);
                        else if (command == ID_DELETE_CUSTOM_TAB)
                            DeleteCustomTab(window, customIndex);
                    }
                }
                return 0;
            }

            if (header != nullptr &&
                header->hwndFrom == g_ListView &&
                header->code == NM_RCLICK)
            {
                const NMITEMACTIVATE* click =
                    reinterpret_cast<const NMITEMACTIVATE*>(lParam);

                if (click->iItem >= 0)
                {
                    // Right-clicking an already selected row preserves the
                    // entire Ctrl/Shift multi-selection. Right-clicking an
                    // unselected row makes that row the sole selection.
                    if ((ListView_GetItemState(
                            g_ListView, click->iItem, LVIS_SELECTED) &
                         LVIS_SELECTED) == 0)
                    {
                        ListView_SetItemState(
                            g_ListView, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                        ListView_SetItemState(
                            g_ListView, click->iItem,
                            LVIS_SELECTED | LVIS_FOCUSED,
                            LVIS_SELECTED | LVIS_FOCUSED);
                    }

                    LVITEMW item = {};
                    item.mask = LVIF_PARAM;
                    item.iItem = click->iItem;

                    if (ListView_GetItem(g_ListView, &item))
                    {
                        const std::size_t textureIndex =
                            static_cast<std::size_t>(item.lParam);
                        HMENU menu = CreatePopupMenu();

                        if (menu != nullptr)
                        {
                            if (g_ReplacementsOnly)
                            {
                                AppendMenuW(menu, MF_STRING, ID_GO_TO_TEXTURE, L"Go To");
                                AppendMenuW(menu, MF_STRING, ID_REMOVE_REPLACEMENT, L"Remove Replacement");
                                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                            }

                            AppendMenuW(menu, MF_STRING, ID_EDIT_TEXTURE_NOTE, L"Edit Note...");
                            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

                            if (!g_CustomTabs.empty())
                            {
                                HMENU addMenu = CreatePopupMenu();
                                if (addMenu != nullptr)
                                {
                                    for (std::size_t i = 0; i < g_CustomTabs.size(); ++i)
                                    {
                                        const std::wstring label = ToWide(g_CustomTabs[i].name);
                                        UINT flags = MF_STRING;
                                        const std::vector<std::size_t> selected =
                                            GetSelectedTextureIndices();
                                        bool allInTab = !selected.empty();

                                        for (const std::size_t selectedIndex : selected)
                                        {
                                            if (selectedIndex >= g_Textures.size() ||
                                                g_CustomTabs[i].dids.find(
                                                    g_Textures[selectedIndex].did) ==
                                                    g_CustomTabs[i].dids.end())
                                            {
                                                allInTab = false;
                                                break;
                                            }
                                        }

                                        if (allInTab)
                                            flags |= MF_CHECKED;

                                        AppendMenuW(addMenu, flags,
                                            ID_CUSTOM_TAB_MENU_BASE + static_cast<UINT>(i),
                                            label.c_str());
                                    }

                                    AppendMenuW(menu, MF_POPUP,
                                        reinterpret_cast<UINT_PTR>(addMenu), L"Add to Tab");
                                }
                            }

                            if (g_ActiveCustomTab >= 0)
                            {
                                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                                AppendMenuW(menu, MF_STRING,
                                    ID_REMOVE_FROM_CUSTOM_TAB, L"Remove from This Tab");
                            }

                            POINT point = {};
                            GetCursorPos(&point);
                            const UINT command = TrackPopupMenu(
                                menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                point.x, point.y, 0, window, nullptr);
                            DestroyMenu(menu);

                            if (command == ID_GO_TO_TEXTURE)
                                GoToTexture(textureIndex);
                            else if (command == ID_EDIT_TEXTURE_NOTE)
                                EditTextureNote(window, textureIndex);
                            else if (command == ID_REMOVE_REPLACEMENT)
                            {
                                RemoveReplacementTexture(window, textureIndex);
                                if (g_LiveWindow != nullptr && IsWindow(g_LiveWindow))
                                    LiveReloadScene();
                            }
                            else if (command == ID_REMOVE_FROM_CUSTOM_TAB)
                                RemoveSelectedTexturesFromActiveCustomTab(textureIndex);
                            else if (command >= ID_CUSTOM_TAB_MENU_BASE &&
                                     command < ID_CUSTOM_TAB_MENU_BASE + g_CustomTabs.size())
                            {
                                AddSelectedTexturesToCustomTab(
                                    textureIndex,
                                    static_cast<std::size_t>(command - ID_CUSTOM_TAB_MENU_BASE));
                            }
                        }
                    }
                }

                return 0;
            }

            if (header != nullptr &&
                header->hwndFrom == g_ListView &&
                header->code == LVN_ITEMCHANGED)
            {
                const NMLISTVIEW* change =
                    reinterpret_cast<const NMLISTVIEW*>(
                        lParam);

                if ((change->uNewState & LVIS_SELECTED) != 0 &&
                    (change->uOldState & LVIS_SELECTED) == 0)
                {
                    LVITEMW item = {};
                    item.mask = LVIF_PARAM;
                    item.iItem = change->iItem;

                    if (ListView_GetItem(
                            g_ListView,
                            &item))
                    {
                        g_SelectedListRow =
                            change->iItem;

                        ShowTextureDetails(
                            static_cast<int>(
                                item.lParam));
                    }
                }
            }

            return 0;
        }

        case WM_ERASEBKGND:
            // Child controls own their pixels. Prevent an erase pass from
            // flashing through tabs/list/preview panels before they repaint.
            return 1;

        case WM_SIZE:
            if (g_ModernReady)
            {
                if (wParam != SIZE_MINIMIZED)
                {
                    ModernResize(
                        static_cast<UINT>(LOWORD(lParam)),
                        static_cast<UINT>(HIWORD(lParam)));
                }
            }
            else
            {
                ResizeControls(window);
            }

            return 0;

        case WM_DESTROY:
            KillTimer(window, PREVIEW_TIMER_ID);
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
            TemplateDevShutdown(window);
#endif
            {
                std::lock_guard<std::mutex> lock(g_PreviewMutex);
                g_StopPreviewWorker = true;
                g_PreviewQueue.clear();
            }
            g_PreviewCv.notify_all();
            if (g_PreviewWorker.joinable())
                g_PreviewWorker.join();

            if (g_LiveWindow != nullptr && IsWindow(g_LiveWindow))
                DestroyWindow(g_LiveWindow);

            g_MainWindow = nullptr;

            ClearLargePreview();
            ClearReplacementPreview();

            if (g_ThumbnailList != nullptr)
            {
                ImageList_Destroy(
                    g_ThumbnailList);
                g_ThumbnailList = nullptr;
            }

            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(
        window,
        message,
        wParam,
        lParam);
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    std::wstring pathError;

    if (!InitializeAppPaths(
            pathError))
    {
        MessageBoxW(
            nullptr,
            pathError.c_str(),
            WINDOW_TITLE,
            MB_OK |
                MB_ICONERROR);

        return 1;
    }

    // Prevent Windows from bitmap-scaling the entire application on
    // high-DPI monitors. Dear ImGui's Win32 backend selects the best
    // supported per-monitor DPI awareness mode at runtime.
    ImGui_ImplWin32_EnableDpiAwareness();

    HMONITOR initialMonitor =
        MonitorFromPoint(
            POINT{ 0, 0 },
            MONITOR_DEFAULTTOPRIMARY);

    const float initialDpiScale =
        ImGui_ImplWin32_GetDpiScaleForMonitor(
            initialMonitor);

    // Size the top-level Manager window against the monitor's *usable* work
    // area, not just a DPI-scaled fixed pixel size. A fixed 1500x900 multiplied
    // by 125-150% DPI can exceed a 1080p desktop. Keep the familiar roomy
    // layout where it fits, but cap the initial outer window to 90% of the work
    // area and center it. The user can still resize/maximize normally.
    RECT initialWork = {
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN)
    };

    MONITORINFO initialMonitorInfo = {};
    initialMonitorInfo.cbSize = sizeof(initialMonitorInfo);
    if (GetMonitorInfoW(
            initialMonitor,
            &initialMonitorInfo))
    {
        initialWork =
            initialMonitorInfo.rcWork;
    }
    const int initialWorkWidth =
        max(1, initialWork.right - initialWork.left);
    const int initialWorkHeight =
        max(1, initialWork.bottom - initialWork.top);

    const int desiredWindowWidth =
        static_cast<int>(1400.0f * initialDpiScale);
    const int desiredWindowHeight =
        static_cast<int>(820.0f * initialDpiScale);

    const int maximumInitialWidth =
        max(1, (initialWorkWidth * 9) / 10);
    const int maximumInitialHeight =
        max(1, (initialWorkHeight * 9) / 10);

    const int initialWindowWidth =
        min(desiredWindowWidth, maximumInitialWidth);
    const int initialWindowHeight =
        min(desiredWindowHeight, maximumInitialHeight);

    const int initialWindowX =
        initialWork.left +
        (initialWorkWidth - initialWindowWidth) / 2;
    const int initialWindowY =
        initialWork.top +
        (initialWorkHeight - initialWindowHeight) / 2;

    const HRESULT comResult =
        CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED);

    const bool comInitialized =
        SUCCEEDED(comResult);

    HRESULT wicResult =
        CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&g_WicFactory));

    if (FAILED(wicResult))
    {
        MessageBoxW(
            nullptr,
            L"Could not initialize Windows Imaging Component.",
            WINDOW_TITLE,
            MB_OK |
                MB_ICONERROR);

        if (comInitialized)
            CoUninitialize();

        return 1;
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize =
        sizeof(controls);
    controls.dwICC =
        ICC_LISTVIEW_CLASSES |
        ICC_TAB_CLASSES |
        ICC_PROGRESS_CLASS;

    InitCommonControlsEx(
        &controls);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize =
        sizeof(windowClass);

    windowClass.style =
        CS_HREDRAW |
        CS_VREDRAW;

    windowClass.lpfnWndProc =
        WindowProc;

    windowClass.hInstance =
        instance;

    windowClass.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW);

    windowClass.hIcon =
        LoadIconW(
            nullptr,
            IDI_APPLICATION);

    windowClass.hIconSm =
        windowClass.hIcon;

    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1);

    windowClass.lpszClassName =
        WINDOW_CLASS;

    if (!RegisterClassExW(
            &windowClass))
    {
        MessageBoxW(
            nullptr,
            L"Could not register the Manager window class.",
            WINDOW_TITLE,
            MB_OK |
                MB_ICONERROR);

        return 1;
    }

    HWND window =
        CreateWindowExW(
            0,
            WINDOW_CLASS,
            WINDOW_TITLE,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            initialWindowX,
            initialWindowY,
            initialWindowWidth,
            initialWindowHeight,
            nullptr,
            nullptr,
            instance,
            nullptr);

    if (window == nullptr)
    {
        MessageBoxW(
            nullptr,
            L"Could not create the Manager window.",
            WINDOW_TITLE,
            MB_OK |
                MB_ICONERROR);

        return 1;
    }

    if (!ModernInitialize(window))
    {
        MessageBoxW(
            window,
            L"Could not initialize the modern DirectX 11 / Dear ImGui interface.",
            WINDOW_TITLE,
            MB_OK | MB_ICONERROR);

        DestroyWindow(window);

        ModernShutdown();
        CloseDatFile();

    if (g_WicFactory != nullptr)
        {
            g_WicFactory->Release();
            g_WicFactory = nullptr;
        }

        if (comInitialized)
            CoUninitialize();

        return 1;
    }

    ModernApplyNativeFrameTheme(
        window);

    ModernHideLegacyControls();

    ShowWindow(
        window,
        showCommand);

    UpdateWindow(
        window);

    MSG message = {};
    bool done = false;

    while (!done)
    {
        while (PeekMessageW(
                   &message,
                   nullptr,
                   0,
                   0,
                   PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                done = true;
                break;
            }

            TranslateMessage(
                &message);

            DispatchMessageW(
                &message);
        }

        if (done)
            break;

        if (IsIconic(window))
        {
            Sleep(10);
            continue;
        }

        ModernRenderFrame();
    }

    ModernShutdown();
    CloseDatFile();

    if (g_WicFactory != nullptr)
    {
        g_WicFactory->Release();
        g_WicFactory = nullptr;
    }

    if (comInitialized)
        CoUninitialize();

    return static_cast<int>(
        message.wParam);
}
