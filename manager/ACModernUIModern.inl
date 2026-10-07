#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/backends/imgui_impl_win32.h"
#include "third_party/imgui/backends/imgui_impl_dx11.h"

// The Win32 backend intentionally leaves this declaration to the application.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam);

static ID3D11Device* g_ModernDevice = nullptr;
static ID3D11DeviceContext* g_ModernDeviceContext = nullptr;
static IDXGISwapChain* g_ModernSwapChain = nullptr;
static ID3D11RenderTargetView* g_ModernRenderTarget = nullptr;
static bool g_ModernReady = false;

enum class ModernSection
{
    Textures,
    Replacements,
    Encountered,
    LiveUi,
    Packs,
    Info
};

static ModernSection g_ModernSection = ModernSection::Textures;

static char g_ModernSearch[160] = {};
static int g_ModernWidthFilter = 0;
static int g_ModernHeightFilter = 0;

static std::unordered_set<std::string> g_ModernReplacementDids;
static ULONGLONG g_ModernLastReplacementRefresh = 0;
static std::size_t g_ModernVisibleCount = 0;

static const ImVec4 MODERN_GOLD =
    ImVec4(0.82f, 0.66f, 0.38f, 1.00f);

static const ImVec4 MODERN_GOLD_HOVER =
    ImVec4(0.92f, 0.76f, 0.46f, 1.00f);

static const ImVec4 MODERN_GREEN =
    ImVec4(0.25f, 0.85f, 0.55f, 1.00f);

static const ImVec4 MODERN_RED =
    ImVec4(0.95f, 0.32f, 0.34f, 1.00f);
static ID3D11Texture2D*
    g_ModernLiveTexture = nullptr;

static ID3D11ShaderResourceView*
    g_ModernLiveView = nullptr;

static int
    g_ModernLiveTextureWidth = 0;

static int
    g_ModernLiveTextureHeight = 0;

static bool
    g_ModernLiveDirty = true;

static std::uint32_t
    g_ModernLiveSeenGeneration = 0;

// Live UI viewport state. Zoom is relative to the normal fit-to-window size.
// Pan is stored in screen pixels from the centered image position.
static float
    g_ModernLiveZoom = 1.0f;

static ImVec2
    g_ModernLivePan = ImVec2(0.0f, 0.0f);

static bool
    g_ModernLivePanning = false;
struct ModernGpuImage
{
    ID3D11ShaderResourceView* view = nullptr;
    int width = 0;
    int height = 0;
};

static std::unordered_map<
    std::size_t,
    ModernGpuImage>
    g_ModernThumbnailCache;

static std::deque<std::size_t>
    g_ModernThumbnailOrder;

static std::unordered_set<std::size_t>
    g_ModernThumbnailRequested;

static const std::size_t
    MODERN_THUMBNAIL_CACHE_LIMIT = 192;

static ModernGpuImage
    g_ModernInspectorOriginal;

static ModernGpuImage
    g_ModernInspectorReplacement;

static std::size_t
    g_ModernInspectorTextureIndex =
        static_cast<std::size_t>(-1);

static bool
    g_ModernInspectorReplacementChecked = false;

static std::string
    g_ModernReplacementNotice;

static double
    g_ModernReplacementNoticeUntil = 0.0;


enum class ModernDialogKind
{
    None,
    CreateGroup,
    RenameGroup,
    EditNote,
    DeleteGroup,
    RemoveReplacement,
    ExportPack,
    ImportPackConfirm
};

static ModernDialogKind
    g_ModernDialogKind =
        ModernDialogKind::None;

static bool
    g_ModernDialogOpenPending = false;

static std::size_t
    g_ModernDialogTextureIndex =
        static_cast<std::size_t>(-1);

static std::vector<std::size_t>
    g_ModernDialogNoteTextureIndices;

static bool
    g_ModernDialogNotesWereMixed = false;

static std::size_t
    g_ModernDialogGroupIndex =
        static_cast<std::size_t>(-1);

static char
    g_ModernDialogText[1024] = {};

static std::string
    g_ModernDialogError;

static std::wstring
    g_ModernPendingImportSource;

static AcuiManifest
    g_ModernPendingImportManifest;

static std::unordered_map<
    std::string,
    std::vector<BYTE>>
    g_ModernPendingImportFiles;

static std::string
    g_ModernPackNotice;

static bool
    g_ModernPackNoticeError = false;

static double
    g_ModernPackNoticeUntil = 0.0;

static ModernGpuImage
    g_ModernGeneratedImage;

static ModernGpuImage
    g_ModernGeneratedBackgroundImage;

static ModernGpuImage
    g_ModernGeneratedResidualImage;

static std::uint32_t
    g_ModernGeneratedImageSurface = 0;

static std::uint64_t
    g_ModernGeneratedImageHash = 0;

static std::size_t
    g_ModernGeneratedBackgroundIndex =
        static_cast<std::size_t>(-1);

static float
    g_ModernAppliedDpiScale = 0.0f;

static std::unordered_set<std::size_t>
    g_ModernSelectedTextures;

static std::unordered_set<std::string>
    g_ModernExcludedCustomGroups;

static std::size_t
    g_ModernSelectionAnchor =
        static_cast<std::size_t>(-1);

static bool
    g_ModernNeedPreviewPrime = true;

static std::vector<std::size_t>
    g_ModernPreviewPrimeList;

static std::size_t
    g_ModernPreviewPrimeCursor = 0;

static void ModernInvalidateGeneratedImages();

struct ModernGeneratedCandidate
{
    std::size_t textureIndex =
        static_cast<std::size_t>(-1);

    TextureRecord texture;

    bool preferredBackground =
        false;
};

struct ModernGeneratedWorkRequest
{
    std::uint64_t requestId = 0;
    std::uint64_t generatedHash = 0;

    std::uint32_t surface = 0;
    std::uint32_t itemId = 0;

    LiveTexturePixels generated;

    std::vector<ModernGeneratedCandidate>
        candidates;
};

struct ModernGeneratedWorkResult
{
    std::uint64_t requestId = 0;
    std::uint64_t generatedHash = 0;

    std::uint32_t surface = 0;
    std::uint32_t itemId = 0;

    bool foundComposition = false;
    bool haveBackground = false;

    LiveDatMatch background;
    LiveTexturePixels residual;

    std::vector<LiveLayerMatch>
        matches;
};

struct ModernGeneratedCachedComposition
{
    bool foundComposition = false;
    bool haveBackground = false;

    LiveDatMatch background;
    LiveTexturePixels residual;

    std::vector<LiveLayerMatch>
        matches;
};

static std::unordered_map<
    std::uint64_t,
    ModernGeneratedCachedComposition>
    g_ModernGeneratedCompositionCache;

static std::deque<std::uint64_t>
    g_ModernGeneratedCompositionCacheOrder;

static const std::size_t
    MODERN_GENERATED_COMPOSITION_CACHE_LIMIT = 64;

// Built-in preferred background textures for generated-icon decomposition.
// These are deliberately independent of user custom groups. Beta users can
// freely edit/delete their visible groups without affecting matching quality.
//
// Background matching uses these first. If none reaches the acceptance score,
// the worker falls back to every other previewable texture of the same size.
static const double
    MODERN_PREFERRED_BACKGROUND_ACCEPT_SCORE = 80.0;

static const char* const
    MODERN_PREFERRED_BACKGROUND_DIDS[] =
{
    "060010F9",
    "06001102",
    "060010FA",
    "06001101",
    "060010FB",
    "060010FC",
    "06001100",
    "060010FD",
    "060010FE",
    "060010FF",
    "0600335C",
    "060011C5",
    "060074CF",
    "060011C6",
    "06003357",
    "060011CA",
    "06003354",
    "06005F48",
    "060011CB",
    "06003355",
    "06005F49",
    "060011CC",
    "060011CD",
    "06003353",
    "060011CE",
    "060011CF",
    "060011D0",
    "060011D1",
    "060011D2",
    "060011D3",
    "060011D4",
    "060011D5",
    "060011F3",
    "060011F4",
    "06005F43",
    "06003358",
    "06005F44",
    "06003359",
    "06005F45",
    "06005F46",
    "06005F4A",
    "06003356",
    "0600335A",
    "0600335B",
    "06005B67",
    "06005B0C",
};

static bool ModernIsPreferredGeneratedBackgroundDid(
    const std::string& did)
{
    for (const char* preferredDid :
         MODERN_PREFERRED_BACKGROUND_DIDS)
    {
        if (did == preferredDid)
            return true;
    }

    return false;
}

static std::mutex
    g_ModernGeneratedWorkMutex;

static std::condition_variable
    g_ModernGeneratedWorkCv;

static std::thread
    g_ModernGeneratedWorker;

static bool
    g_ModernGeneratedWorkerStop = false;

static bool
    g_ModernGeneratedHavePending = false;

static bool
    g_ModernGeneratedHaveResult = false;

static ModernGeneratedWorkRequest
    g_ModernGeneratedPending;

static ModernGeneratedWorkResult
    g_ModernGeneratedResult;

static std::atomic<std::uint64_t>
    g_ModernGeneratedEpoch { 0 };

static std::uint64_t
    g_ModernGeneratedActiveRequest = 0;

static std::uint64_t
    g_ModernGeneratedRequestedHash = 0;

// Hash of the composition currently applied to the Inspector. Unlike the
// in-flight request state, this intentionally survives navigation away from
// Live UI so returning to the same generated icon does not recompute it.
static std::uint64_t
    g_ModernGeneratedResolvedHash = 0;

static bool
    g_ModernGeneratedWorking = false;

static double
    g_ModernGeneratedAnimationStart = 0.0;


static bool ModernGeneratedRequestCancelled(
    std::uint64_t requestId)
{
    return
        g_ModernGeneratedEpoch.load(
            std::memory_order_acquire) !=
        requestId;
}


static bool ModernWorkerLoadDatPixels(
    IWICImagingFactory* /*factory*/,
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


static ModernGeneratedWorkResult
ModernComputeGeneratedComposition(
    const ModernGeneratedWorkRequest& request,
    IWICImagingFactory* factory)
{
    ModernGeneratedWorkResult result;

    result.requestId =
        request.requestId;

    result.generatedHash =
        request.generatedHash;

    result.surface =
        request.surface;

    result.itemId =
        request.itemId;

    if (!request.generated.loaded)
        return result;

    double bestBackgroundScore =
        -1.0;

    double bestBackgroundError =
        1000000.0;

    std::size_t bestBackgroundIndex =
        static_cast<std::size_t>(-1);

    LiveTexturePixels
        backgroundPixels;

    auto evaluateBackgroundCandidate =
        [&](const ModernGeneratedCandidate& candidate)
        {
            if (ModernGeneratedRequestCancelled(
                    request.requestId))
            {
                return false;
            }

            LiveTexturePixels pixels;

            if (!ModernWorkerLoadDatPixels(
                    factory,
                    candidate.texture,
                    pixels))
            {
                return true;
            }

            double score = 0.0;
            double error = 255.0;

            LiveBackgroundCornerScore(
                request.generated,
                pixels,
                score,
                error);

            const bool better =
                score >
                bestBackgroundScore +
                    0.001;

            const bool tied =
                std::abs(
                    score -
                    bestBackgroundScore) <=
                0.001;

            if (better ||
                (tied &&
                 error <
                    bestBackgroundError))
            {
                bestBackgroundScore =
                    score;

                bestBackgroundError =
                    error;

                bestBackgroundIndex =
                    candidate.textureIndex;

                backgroundPixels =
                    std::move(pixels);
            }

            return true;
        };

    // Fast path: try the internal, curated background list first.
    for (const ModernGeneratedCandidate&
         candidate :
         request.candidates)
    {
        if (!candidate.preferredBackground)
            continue;

        if (!evaluateBackgroundCandidate(
                candidate))
        {
            return result;
        }
    }

    // Fallback: if the curated list did not produce a convincing match,
    // widen the same corner-first algorithm to all other same-size textures.
    // Keep the best preferred result in contention while doing so.
    if (bestBackgroundIndex ==
            static_cast<std::size_t>(-1) ||
        bestBackgroundScore <
            MODERN_PREFERRED_BACKGROUND_ACCEPT_SCORE)
    {
        for (const ModernGeneratedCandidate&
             candidate :
             request.candidates)
        {
            if (candidate.preferredBackground)
                continue;

            if (!evaluateBackgroundCandidate(
                    candidate))
            {
                return result;
            }
        }
    }

    if (bestBackgroundIndex ==
            static_cast<std::size_t>(-1) ||
        !backgroundPixels.loaded)
    {
        return result;
    }

    result.background.textureIndex =
        bestBackgroundIndex;

    result.background.score =
        bestBackgroundScore;

    result.haveBackground =
        true;

    result.residual =
        LiveBuildResidualPixels(
            request.generated,
            backgroundPixels);

    for (const ModernGeneratedCandidate&
         candidate :
         request.candidates)
    {
        if (ModernGeneratedRequestCancelled(
                request.requestId))
        {
            return ModernGeneratedWorkResult{};
        }

        if (candidate.textureIndex ==
            bestBackgroundIndex)
        {
            continue;
        }

        LiveTexturePixels foreground;

        if (!ModernWorkerLoadDatPixels(
                factory,
                candidate.texture,
                foreground))
        {
            continue;
        }

        LiveLayerMatch match;

        match.foregroundIndex =
            candidate.textureIndex;

        LiveScoreForegroundPlacement(
            request.generated,
            backgroundPixels,
            foreground,
            0,
            0,
            match.residualScore,
            match.spillPenalty,
            match.fullScore);

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

        result.matches.push_back(
            match);

        std::sort(
            result.matches.begin(),
            result.matches.end(),
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
                    a.foregroundIndex <
                    b.foregroundIndex;
            });

        if (result.matches.size() > 3)
        {
            result.matches.resize(3);
        }
    }

    result.foundComposition =
        true;

    return result;
}


static void ModernGeneratedWorkerMain()
{
    const HRESULT comResult =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED);

    const bool comInitialized =
        SUCCEEDED(comResult);

    IWICImagingFactory* factory =
        nullptr;

    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));

    for (;;)
    {
        ModernGeneratedWorkRequest request;

        {
            std::unique_lock<std::mutex>
                lock(
                    g_ModernGeneratedWorkMutex);

            g_ModernGeneratedWorkCv.wait(
                lock,
                []
                {
                    return
                        g_ModernGeneratedWorkerStop ||
                        g_ModernGeneratedHavePending;
                });

            if (g_ModernGeneratedWorkerStop)
                break;

            request =
                std::move(
                    g_ModernGeneratedPending);

            g_ModernGeneratedHavePending =
                false;
        }

        if (ModernGeneratedRequestCancelled(
                request.requestId))
        {
            continue;
        }

        ModernGeneratedWorkResult result =
            ModernComputeGeneratedComposition(
                request,
                factory);

        if (ModernGeneratedRequestCancelled(
                request.requestId))
        {
            continue;
        }

        {
            std::lock_guard<std::mutex>
                lock(
                    g_ModernGeneratedWorkMutex);

            g_ModernGeneratedResult =
                std::move(result);

            g_ModernGeneratedHaveResult =
                true;
        }
    }

    if (factory != nullptr)
        factory->Release();

    if (comInitialized)
        CoUninitialize();
}


static void ModernStartGeneratedWorker()
{
    if (g_ModernGeneratedWorker.joinable())
        return;

    {
        std::lock_guard<std::mutex>
            lock(
                g_ModernGeneratedWorkMutex);

        g_ModernGeneratedWorkerStop =
            false;

        g_ModernGeneratedHavePending =
            false;

        g_ModernGeneratedHaveResult =
            false;
    }

    g_ModernGeneratedWorker =
        std::thread(
            ModernGeneratedWorkerMain);
}


static void ModernCancelGeneratedWork()
{
    g_ModernGeneratedEpoch.fetch_add(
        1,
        std::memory_order_acq_rel);

    {
        std::lock_guard<std::mutex>
            lock(
                g_ModernGeneratedWorkMutex);

        g_ModernGeneratedHavePending =
            false;

        g_ModernGeneratedHaveResult =
            false;
    }

    g_ModernGeneratedActiveRequest = 0;
    g_ModernGeneratedRequestedHash = 0;

    g_ModernGeneratedWorking =
        false;
}


static void ModernStopGeneratedWorker()
{
    ModernCancelGeneratedWork();

    {
        std::lock_guard<std::mutex>
            lock(
                g_ModernGeneratedWorkMutex);

        g_ModernGeneratedWorkerStop =
            true;

        g_ModernGeneratedHavePending =
            false;
    }

    g_ModernGeneratedWorkCv.notify_all();

    if (g_ModernGeneratedWorker.joinable())
    {
        g_ModernGeneratedWorker.join();
    }
}


static void ModernStoreGeneratedCompositionCache(
    const ModernGeneratedWorkResult& result)
{
    if (result.generatedHash == 0)
        return;

    ModernGeneratedCachedComposition cached;

    cached.foundComposition =
        result.foundComposition;

    cached.haveBackground =
        result.haveBackground;

    cached.background =
        result.background;

    cached.residual =
        result.residual;

    cached.matches =
        result.matches;

    const auto existing =
        g_ModernGeneratedCompositionCache.find(
            result.generatedHash);

    if (existing ==
        g_ModernGeneratedCompositionCache.end())
    {
        g_ModernGeneratedCompositionCacheOrder.push_back(
            result.generatedHash);
    }

    g_ModernGeneratedCompositionCache[
        result.generatedHash] =
        std::move(cached);

    while (g_ModernGeneratedCompositionCache.size() >
           MODERN_GENERATED_COMPOSITION_CACHE_LIMIT)
    {
        const std::uint64_t oldestHash =
            g_ModernGeneratedCompositionCacheOrder.front();

        g_ModernGeneratedCompositionCacheOrder.pop_front();

        g_ModernGeneratedCompositionCache.erase(
            oldestHash);
    }
}


static bool ModernApplyGeneratedCompositionCache(
    std::uint64_t generatedHash,
    bool animateScores)
{
    const auto found =
        g_ModernGeneratedCompositionCache.find(
            generatedHash);

    if (found ==
        g_ModernGeneratedCompositionCache.end())
    {
        return false;
    }

    const ModernGeneratedCachedComposition& cached =
        found->second;

    g_LiveGeneratedBackground =
        cached.background;

    g_LiveHaveGeneratedBackground =
        cached.haveBackground;

    g_LiveGeneratedResidual =
        cached.residual;

    g_LiveGeneratedMatches =
        cached.matches;

    g_ModernGeneratedActiveRequest = 0;
    g_ModernGeneratedRequestedHash =
        generatedHash;
    g_ModernGeneratedResolvedHash =
        generatedHash;
    g_ModernGeneratedWorking = false;

    g_ModernGeneratedAnimationStart =
        animateScores
            ? ImGui::GetTime()
            : 0.0;

    ModernInvalidateGeneratedImages();

    return true;
}


static void ModernRequestGeneratedComposition(
    const LiveTexturePixels& generated,
    std::uint32_t surface,
    std::uint64_t generatedHash,
    std::uint32_t itemId)
{
    if (!generated.loaded ||
        generated.width == 0 ||
        generated.height == 0 ||
        generatedHash == 0)
    {
        return;
    }

    // Advancing the epoch immediately cancels any older generated-icon job.
    // That includes a job for another icon the player just clicked away from.
    const std::uint64_t requestId =
        g_ModernGeneratedEpoch.fetch_add(
            1,
            std::memory_order_acq_rel) +
        1;

    {
        std::lock_guard<std::mutex>
            lock(
                g_ModernGeneratedWorkMutex);

        // A cached hit does not need whatever was previously waiting in the
        // worker queue, and stale completed work must not overwrite it later.
        g_ModernGeneratedHavePending =
            false;

        g_ModernGeneratedHaveResult =
            false;
    }

    if (ModernApplyGeneratedCompositionCache(
            generatedHash,
            true))
    {
        return;
    }

    ModernGeneratedWorkRequest request;

    request.requestId =
        requestId;

    request.generatedHash =
        generatedHash;

    request.surface =
        surface;

    request.itemId =
        itemId;

    request.generated =
        generated;

    // Snapshot every same-size previewable DAT texture into the worker job.
    // The worker first tests its built-in preferred background IDs, then only
    // broadens to the remaining candidates if the preferred pass is weak.
    // User custom groups are intentionally not consulted.
    request.candidates.reserve(
        1024);

    for (std::size_t i = 0;
         i < g_Textures.size();
         ++i)
    {
        const TextureRecord& texture =
            g_Textures[i];

        if (!IsPreviewable(texture) ||
            texture.width !=
                generated.width ||
            texture.height !=
                generated.height)
        {
            continue;
        }

        ModernGeneratedCandidate candidate;

        candidate.textureIndex =
            i;

        candidate.texture =
            texture;

        candidate.preferredBackground =
            ModernIsPreferredGeneratedBackgroundDid(
                texture.did);

        request.candidates.push_back(
            std::move(candidate));
    }

    {
        std::lock_guard<std::mutex>
            lock(
                g_ModernGeneratedWorkMutex);

        // Only keep the newest request. A currently running request observes
        // the changed epoch and aborts at its next cancellation check.
        g_ModernGeneratedPending =
            std::move(request);

        g_ModernGeneratedHavePending =
            true;

        g_ModernGeneratedHaveResult =
            false;
    }

    g_ModernGeneratedActiveRequest =
        requestId;

    g_ModernGeneratedRequestedHash =
        generatedHash;

    g_ModernGeneratedResolvedHash = 0;

    g_ModernGeneratedWorking =
        true;

    g_ModernGeneratedAnimationStart =
        0.0;

    // Clear the previous composition while this distinct generated icon is
    // being analyzed, but keep the generated source image itself available.
    g_LiveGeneratedMatches.clear();

    g_LiveGeneratedBackground =
        LiveDatMatch{};

    g_LiveHaveGeneratedBackground =
        false;

    g_LiveGeneratedResidual =
        LiveTexturePixels{};

    ModernInvalidateGeneratedImages();

    g_ModernGeneratedWorkCv.notify_one();
}


static void ModernEnsureGeneratedWorkRequested()
{
    if (!g_LiveSelectedGeneratedItem ||
        g_LiveSelectedGeneratedSurface == 0)
    {
        return;
    }

    const LiveTexturePixels* generated =
        LiveGetGenerated(
            g_LiveSelectedGeneratedSurface);

    if (generated == nullptr)
        return;

    const std::uint64_t hash =
        LiveHashGeneratedPixels(
            *generated);

    if (hash == 0)
        return;

    // The currently displayed composition already belongs to these exact
    // generated pixels. Navigation away from Live UI intentionally does not
    // invalidate this state.
    if (hash ==
        g_ModernGeneratedResolvedHash)
    {
        return;
    }

    if (hash ==
            g_ModernGeneratedRequestedHash &&
        g_ModernGeneratedWorking)
    {
        return;
    }

    ModernRequestGeneratedComposition(
        *generated,
        g_LiveSelectedGeneratedSurface,
        hash,
        g_LiveSelectedItemId);
}


static void ModernPollGeneratedWork()
{
    ModernGeneratedWorkResult result;
    bool haveResult = false;

    {
        std::lock_guard<std::mutex>
            lock(
                g_ModernGeneratedWorkMutex);

        if (g_ModernGeneratedHaveResult)
        {
            result =
                std::move(
                    g_ModernGeneratedResult);

            g_ModernGeneratedHaveResult =
                false;

            haveResult = true;
        }
    }

    if (!haveResult)
        return;

    if (result.requestId == 0 ||
        result.requestId !=
            g_ModernGeneratedActiveRequest ||
        result.requestId !=
            g_ModernGeneratedEpoch.load(
                std::memory_order_acquire))
    {
        return;
    }

    if (!g_LiveSelectedGeneratedItem ||
        result.generatedHash !=
            g_LiveSelectedGeneratedHash)
    {
        return;
    }

    // Save both successful and unsuccessful completed analyses. A negative
    // result is still useful cache data and should not be recomputed every
    // time the player returns to this generated icon.
    ModernStoreGeneratedCompositionCache(
        result);

    g_LiveGeneratedBackground =
        result.background;

    g_LiveHaveGeneratedBackground =
        result.haveBackground;

    g_LiveGeneratedResidual =
        std::move(
            result.residual);

    g_LiveGeneratedMatches =
        std::move(
            result.matches);

    g_ModernGeneratedActiveRequest = 0;

    g_ModernGeneratedRequestedHash =
        result.generatedHash;

    g_ModernGeneratedResolvedHash =
        result.generatedHash;

    g_ModernGeneratedWorking =
        false;

    g_ModernGeneratedAnimationStart =
        ImGui::GetTime();

    ModernInvalidateGeneratedImages();
}


static float ModernAnimatedGeneratedPercent(
    double target)
{
    if (g_ModernGeneratedAnimationStart <=
        0.0)
    {
        return
            static_cast<float>(
                target);
    }

    const double elapsed =
        ImGui::GetTime() -
        g_ModernGeneratedAnimationStart;

    float t =
        static_cast<float>(
            elapsed / 0.65);

    t =
        max(
            0.0f,
            min(
                1.0f,
                t));

    // Smooth ease-out instead of a linear mechanical sweep.
    const float eased =
        1.0f -
        (1.0f - t) *
        (1.0f - t) *
        (1.0f - t);

    return
        static_cast<float>(
            target) *
        eased;
}


static bool
    g_ModernLiveManualPaused = false;

static bool
    g_ModernLiveAutoPaused = false;

static ModernSection
    g_ModernObservedSection =
        ModernSection::Textures;

static std::size_t
    g_ModernScrollToTextureIndex =
        static_cast<std::size_t>(-1);



static void ModernApplyTheme();

static void ModernApplyScaledTheme(
    float dpiScale)
{
    dpiScale =
        max(
            0.5f,
            min(
                4.0f,
                dpiScale));

    // Start again from an unscaled ImGui style every time.
    // ScaleAllSizes() is cumulative, so repeatedly applying it
    // to the existing style would compound the dimensions.
    ImGui::GetStyle() =
        ImGuiStyle();

    ModernApplyTheme();

    ImGuiStyle& style =
        ImGui::GetStyle();

    style.FontSizeBase =
        16.0f;

    style.ScaleAllSizes(
        dpiScale);

    // ConfigDpiScaleFonts will keep this synchronized too,
    // but set the correct value immediately on a monitor change.
    style.FontScaleDpi =
        dpiScale;

    g_ModernAppliedDpiScale =
        dpiScale;
}


static float ModernButtonHeight(
    float requestedHeight)
{
    // Explicit button heights are not automatically DPI-scaled by ImGui.
    // Never let a requested height become smaller than the current natural
    // frame height (font + scaled vertical frame padding), otherwise labels
    // can be vertically squeezed/clipped on 125%/150% Windows scaling.
    return max(
        requestedHeight,
        ImGui::GetFrameHeight());
}


static void ModernUpdateDpiScale()
{
    if (g_MainWindow == nullptr)
        return;

    const float dpiScale =
        ImGui_ImplWin32_GetDpiScaleForHwnd(
            g_MainWindow);

    if (std::abs(
            dpiScale -
            g_ModernAppliedDpiScale) >
        0.01f)
    {
        ModernApplyScaledTheme(
            dpiScale);
    }
}
static void ModernReleaseGpuImage(
    ModernGpuImage& image)
{
    if (image.view != nullptr)
    {
        image.view->Release();
        image.view = nullptr;
    }

    image.width = 0;
    image.height = 0;
}


static bool ModernCreateGpuImageFromBgra(
    const unsigned char* pixels,
    int width,
    int height,
    ModernGpuImage& result)
{
    ModernReleaseGpuImage(result);

    if (pixels == nullptr ||
        width <= 0 ||
        height <= 0 ||
        g_ModernDevice == nullptr)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC description = {};

    description.Width =
        static_cast<UINT>(width);

    description.Height =
        static_cast<UINT>(height);

    description.MipLevels = 1;
    description.ArraySize = 1;

    description.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    description.SampleDesc.Count = 1;

    description.Usage =
        D3D11_USAGE_IMMUTABLE;

    description.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial = {};

    initial.pSysMem =
        pixels;

    initial.SysMemPitch =
        static_cast<UINT>(
            width * 4);

    ID3D11Texture2D* texture = nullptr;

    HRESULT hr =
        g_ModernDevice->CreateTexture2D(
            &description,
            &initial,
            &texture);

    if (FAILED(hr) ||
        texture == nullptr)
    {
        return false;
    }

    hr =
        g_ModernDevice->
            CreateShaderResourceView(
                texture,
                nullptr,
                &result.view);

    texture->Release();

    if (FAILED(hr) ||
        result.view == nullptr)
    {
        ModernReleaseGpuImage(result);
        return false;
    }

    result.width = width;
    result.height = height;

    return true;
}


static bool ModernCreateGpuImageFromPixels(
    const LiveTexturePixels& pixels,
    ModernGpuImage& result)
{
    if (!pixels.loaded ||
        pixels.width == 0 ||
        pixels.height == 0 ||
        pixels.bgra.empty())
    {
        ModernReleaseGpuImage(result);
        return false;
    }

    return ModernCreateGpuImageFromBgra(
        pixels.bgra.data(),
        static_cast<int>(pixels.width),
        static_cast<int>(pixels.height),
        result);
}


static bool ModernCreateGpuImageFromBitmap(
    HBITMAP bitmap,
    ModernGpuImage& result)
{
    ModernReleaseGpuImage(result);

    if (bitmap == nullptr)
        return false;

    BITMAP object = {};

    if (GetObjectW(
            bitmap,
            sizeof(object),
            &object) == 0)
    {
        return false;
    }

    const int width =
        std::abs(object.bmWidth);

    const int height =
        std::abs(object.bmHeight);

    if (width <= 0 ||
        height <= 0)
    {
        return false;
    }

    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        4u);

    BITMAPINFO info = {};

    info.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);

    info.bmiHeader.biWidth =
        width;

    info.bmiHeader.biHeight =
        -height;

    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;

    info.bmiHeader.biCompression =
        BI_RGB;

    HDC dc =
        GetDC(nullptr);

    const int copied =
        GetDIBits(
            dc,
            bitmap,
            0,
            static_cast<UINT>(height),
            pixels.data(),
            &info,
            DIB_RGB_COLORS);

    ReleaseDC(
        nullptr,
        dc);

    if (copied == 0)
        return false;

    // Some GDI-created 32-bit bitmaps have meaningful RGB but
    // leave every alpha byte at zero. Make those opaque while
    // preserving genuine alpha-bearing previews.
    bool anyAlpha = false;

    for (std::size_t i = 3;
         i < pixels.size();
         i += 4)
    {
        if (pixels[i] != 0)
        {
            anyAlpha = true;
            break;
        }
    }

    if (!anyAlpha)
    {
        for (std::size_t i = 3;
             i < pixels.size();
             i += 4)
        {
            pixels[i] = 255;
        }
    }

    return ModernCreateGpuImageFromBgra(
        pixels.data(),
        width,
        height,
        result);
}


static bool ModernCreateReplacementGpuImage(
    const TextureRecord& texture,
    ModernGpuImage& result)
{
    ModernReleaseGpuImage(result);

    if (!IsPreviewable(texture) ||
        texture.width == 0 ||
        texture.height == 0)
    {
        return false;
    }

    const std::uint32_t bytesPerPixel =
        texture.pixelFormat == 0x15 ? 4u : 3u;

    const std::size_t expectedSize =
        static_cast<std::size_t>(texture.width) *
        static_cast<std::size_t>(texture.height) *
        static_cast<std::size_t>(bytesPerPixel);

    if (expectedSize !=
        static_cast<std::size_t>(texture.imageSize))
    {
        return false;
    }

    std::ifstream input(
        ReplacementRawPath(texture),
        std::ios::binary);

    if (!input.is_open())
        return false;

    std::vector<unsigned char> raw(expectedSize);

    input.read(
        reinterpret_cast<char*>(raw.data()),
        static_cast<std::streamsize>(raw.size()));

    if (input.gcount() !=
        static_cast<std::streamsize>(raw.size()))
    {
        return false;
    }

    input.peek();
    if (!input.eof())
        return false;

    // AC's 0x15 replacement payload is already tightly-packed BGRA32.
    // Upload it directly so its alpha channel reaches the ImGui/D3D preview.
    // The old HBITMAP/StretchDIBits preview path could discard that alpha and
    // then make the whole image opaque, which did not match in-game rendering.
    if (bytesPerPixel == 4u)
    {
        return ModernCreateGpuImageFromBgra(
            raw.data(),
            static_cast<int>(texture.width),
            static_cast<int>(texture.height),
            result);
    }

    // 0x14 replacements are BGR24; expand them to opaque BGRA32 for D3D.
    std::vector<unsigned char> bgra(
        static_cast<std::size_t>(texture.width) *
        static_cast<std::size_t>(texture.height) *
        4u);

    const std::size_t pixelCount =
        static_cast<std::size_t>(texture.width) *
        static_cast<std::size_t>(texture.height);

    for (std::size_t i = 0;
         i < pixelCount;
         ++i)
    {
        const unsigned char* source =
            raw.data() + i * 3u;

        unsigned char* destination =
            bgra.data() + i * 4u;

        destination[0] = source[0];
        destination[1] = source[1];
        destination[2] = source[2];
        destination[3] = 255u;
    }

    return ModernCreateGpuImageFromBgra(
        bgra.data(),
        static_cast<int>(texture.width),
        static_cast<int>(texture.height),
        result);
}


static ModernGpuImage* ModernGetThumbnail(
    std::size_t textureIndex)
{
    const auto existing =
        g_ModernThumbnailCache.find(
            textureIndex);

    if (existing !=
        g_ModernThumbnailCache.end())
    {
        return &existing->second;
    }

    if (textureIndex >=
        g_Textures.size())
    {
        return nullptr;
    }

    const TextureRecord& texture =
        g_Textures[textureIndex];

    if (!IsPreviewable(texture))
        return nullptr;

    if (!PreviewFileExists(texture))
    {
        if (g_ModernThumbnailRequested.insert(
                textureIndex).second)
        {
            // This texture is actually visible now. QueuePreview()
            // promotes an already queued item to the front when
            // priority=true.
            QueuePreview(
                textureIndex,
                true);
        }

        return nullptr;
    }

    std::vector<unsigned char> previewPixels;

    if (!LoadDatPreviewCanvasPixels(
            texture,
            56,
            previewPixels))
    {
        return nullptr;
    }

    ModernGpuImage image;

    if (!ModernCreateGpuImageFromBgra(
            previewPixels.data(),
            56,
            56,
            image))
    {
        return nullptr;
    }

    g_ModernThumbnailCache.emplace(
        textureIndex,
        image);

    g_ModernThumbnailOrder.push_back(
        textureIndex);

    while (g_ModernThumbnailOrder.size() >
           MODERN_THUMBNAIL_CACHE_LIMIT)
    {
        const std::size_t oldest =
            g_ModernThumbnailOrder.front();

        g_ModernThumbnailOrder.pop_front();

        const auto old =
            g_ModernThumbnailCache.find(
                oldest);

        if (old !=
            g_ModernThumbnailCache.end())
        {
            ModernReleaseGpuImage(
                old->second);

            g_ModernThumbnailCache.erase(
                old);
        }
    }

    return &g_ModernThumbnailCache[
        textureIndex];
}


static void ModernInvalidateInspectorImages()
{
    ModernReleaseGpuImage(
        g_ModernInspectorOriginal);

    ModernReleaseGpuImage(
        g_ModernInspectorReplacement);

    g_ModernInspectorTextureIndex =
        static_cast<std::size_t>(-1);

    g_ModernInspectorReplacementChecked =
        false;
}


static void ModernEnsureInspectorImages(
    std::size_t textureIndex)
{
    if (textureIndex >=
        g_Textures.size())
    {
        ModernInvalidateInspectorImages();
        return;
    }

    const TextureRecord& texture =
        g_Textures[textureIndex];

    if (g_ModernInspectorTextureIndex !=
        textureIndex)
    {
        ModernReleaseGpuImage(
            g_ModernInspectorOriginal);

        ModernReleaseGpuImage(
            g_ModernInspectorReplacement);

        g_ModernInspectorTextureIndex =
            textureIndex;

        g_ModernInspectorReplacementChecked =
            false;
    }

    if (g_ModernInspectorOriginal.view ==
        nullptr)
    {
        if (PreviewFileExists(texture))
        {
            std::vector<unsigned char> previewPixels;

            if (LoadDatPreviewCanvasPixels(
                    texture,
                    192,
                    previewPixels))
            {
                ModernCreateGpuImageFromBgra(
                    previewPixels.data(),
                    192,
                    192,
                    g_ModernInspectorOriginal);
            }
        }
        else
        {
            QueuePreview(
                textureIndex,
                true);
        }
    }

    if (!g_ModernInspectorReplacementChecked)
    {
        g_ModernInspectorReplacementChecked =
            true;

        ModernCreateReplacementGpuImage(
            texture,
            g_ModernInspectorReplacement);
    }
}


static void ModernInvalidateGeneratedImages()
{
    ModernReleaseGpuImage(
        g_ModernGeneratedImage);

    ModernReleaseGpuImage(
        g_ModernGeneratedBackgroundImage);

    ModernReleaseGpuImage(
        g_ModernGeneratedResidualImage);

    g_ModernGeneratedImageSurface = 0;
    g_ModernGeneratedImageHash = 0;

    g_ModernGeneratedBackgroundIndex =
        static_cast<std::size_t>(-1);
}


static void ModernEnsureGeneratedImages()
{
    // During a live refresh the snapshot loader clears the old
    // surface before the fresh item/blit association is restored.
    // Keep the current GPU Inspector images through that transient
    // frame instead of flashing back to another texture.
    if (g_LiveSelectedGeneratedItem &&
        g_LiveSelectedGeneratedSurface == 0)
    {
        return;
    }

    const std::size_t backgroundIndex =
        g_LiveHaveGeneratedBackground
            ? g_LiveGeneratedBackground
                .textureIndex
            : static_cast<std::size_t>(-1);

    const bool changed =
        g_ModernGeneratedImageSurface !=
            g_LiveSelectedGeneratedSurface ||
        g_ModernGeneratedImageHash !=
            g_LiveSelectedGeneratedHash ||
        g_ModernGeneratedBackgroundIndex !=
            backgroundIndex;

    if (!changed)
        return;

    ModernInvalidateGeneratedImages();

    g_ModernGeneratedImageSurface =
        g_LiveSelectedGeneratedSurface;

    g_ModernGeneratedImageHash =
        g_LiveSelectedGeneratedHash;

    g_ModernGeneratedBackgroundIndex =
        backgroundIndex;

    const LiveTexturePixels* generated =
        LiveGetGenerated(
            g_LiveSelectedGeneratedSurface);

    if (generated != nullptr)
    {
        ModernCreateGpuImageFromPixels(
            *generated,
            g_ModernGeneratedImage);
    }

    if (backgroundIndex !=
            static_cast<std::size_t>(-1) &&
        backgroundIndex <
            g_Textures.size())
    {
        std::vector<unsigned char> previewPixels;

        if (LoadDatPreviewCanvasPixels(
                g_Textures[backgroundIndex],
                144,
                previewPixels))
        {
            ModernCreateGpuImageFromBgra(
                previewPixels.data(),
                144,
                144,
                g_ModernGeneratedBackgroundImage);
        }
    }

    if (g_LiveGeneratedResidual.loaded)
    {
        ModernCreateGpuImageFromPixels(
            g_LiveGeneratedResidual,
            g_ModernGeneratedResidualImage);
    }
}


static void ModernRenderGpuImageFit(
    const ModernGpuImage& image,
    const ImVec2& available)
{
    if (image.view == nullptr ||
        image.width <= 0 ||
        image.height <= 0)
    {
        ImGui::TextDisabled(
            "Preview unavailable");

        return;
    }

    const float scale =
        min(
            available.x /
                static_cast<float>(
                    image.width),
            available.y /
                static_cast<float>(
                    image.height));

    const float safeScale =
        max(
            0.01f,
            scale);

    const ImVec2 size(
        static_cast<float>(
            image.width) *
            safeScale,
        static_cast<float>(
            image.height) *
            safeScale);

    if (available.x > size.x)
    {
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            (available.x - size.x) *
                0.5f);
    }

    if (available.y > size.y)
    {
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            (available.y - size.y) *
                0.5f);
    }

    const ImTextureID id =
        static_cast<ImTextureID>(
            reinterpret_cast<std::uintptr_t>(
                image.view));

    ImGui::Image(
        id,
        size);
}


static void ModernRenderTableThumbnail(
    std::size_t textureIndex,
    const TextureRecord& texture)
{
    ModernGpuImage* image =
        ModernGetThumbnail(
            textureIndex);

    if (image != nullptr &&
        image->view != nullptr)
    {
        const ImTextureID id =
            static_cast<ImTextureID>(
                reinterpret_cast<
                    std::uintptr_t>(
                        image->view));

        ImGui::Image(
            id,
            ImVec2(
                32.0f,
                32.0f));
    }
    else
    {
        ImGui::TextDisabled(
            IsPreviewable(texture)
                ? "..."
                : "N/A");
    }
}


static bool ModernSnapshotButton(
    bool capturing)
{
    const ImVec2 size(
        152.0f,
        30.0f);

    ImGui::PushID(
        "ModernSnapshotButton");

    const ImVec2 position =
        ImGui::GetCursorScreenPos();

    const bool clicked =
        ImGui::InvisibleButton(
            "##button",
            size);

    const bool hovered =
        !capturing &&
        ImGui::IsItemHovered();

    const bool held =
        !capturing &&
        ImGui::IsItemActive();

    ImDrawList* draw =
        ImGui::GetWindowDrawList();

    ImVec4 background =
        ImGui::GetStyleColorVec4(
            ImGuiCol_Button);

    if (capturing)
    {
        background.w *= 0.70f;
    }
    else if (held)
    {
        background =
            ImGui::GetStyleColorVec4(
                ImGuiCol_ButtonActive);
    }
    else if (hovered)
    {
        background =
            ImGui::GetStyleColorVec4(
                ImGuiCol_ButtonHovered);
    }

    draw->AddRectFilled(
        position,
        ImVec2(
            position.x + size.x,
            position.y + size.y),
        ImGui::GetColorU32(background),
        4.0f);

    draw->AddRect(
        position,
        ImVec2(
            position.x + size.x,
            position.y + size.y),
        ImGui::GetColorU32(
            ImGuiCol_Border),
        4.0f);

    const float iconCenterX =
        position.x + 17.0f;

    const float iconCenterY =
        position.y +
        size.y * 0.5f;

    const ImU32 iconColor =
        ImGui::GetColorU32(
            capturing
                ? ImGui::GetStyleColorVec4(
                    ImGuiCol_TextDisabled)
                : MODERN_GOLD);

    // Small camera glyph.
    draw->AddRect(
        ImVec2(
            iconCenterX - 8.0f,
            iconCenterY - 5.5f),
        ImVec2(
            iconCenterX + 8.0f,
            iconCenterY + 5.5f),
        iconColor,
        2.0f,
        0,
        1.5f);

    draw->AddRectFilled(
        ImVec2(
            iconCenterX - 4.0f,
            iconCenterY - 8.0f),
        ImVec2(
            iconCenterX + 2.0f,
            iconCenterY - 5.0f),
        iconColor,
        1.0f);

    draw->AddCircle(
        ImVec2(
            iconCenterX,
            iconCenterY),
        3.4f,
        iconColor,
        16,
        1.5f);

    const char* label =
        capturing
            ? "Capturing..."
            : "Take Snapshot";

    const ImVec2 textSize =
        ImGui::CalcTextSize(
            label);

    draw->AddText(
        ImVec2(
            position.x + 34.0f,
            position.y +
                (size.y - textSize.y) *
                    0.5f),
        ImGui::GetColorU32(
            capturing
                ? ImGui::GetStyleColorVec4(
                    ImGuiCol_TextDisabled)
                : ImGui::GetStyleColorVec4(
                    ImGuiCol_Text)),
        label);

    ImGui::PopID();

    return clicked && !capturing;
}


static bool ModernReplacementSelector(
    bool* replacements)
{
    if (replacements == nullptr)
        return false;

    ImGui::PushID(
        "ModernReplacementSelector");

    const float em =
        ImGui::GetFontSize();

    const float height =
        ImGui::GetFrameHeight();

    // Font-relative instead of fixed pixels.
    const float leftWidth =
        max(
            5.6f * em,
            ImGui::CalcTextSize(
                "Vanilla").x +
                2.0f * em);

    const float rightWidth =
        max(
            7.2f * em,
            ImGui::CalcTextSize(
                "Replacements").x +
                2.0f * em);

    const float width =
        leftWidth +
        rightWidth;

    const ImVec2 position =
        ImGui::GetCursorScreenPos();

    const bool clicked =
        ImGui::InvisibleButton(
            "##selector",
            ImVec2(
                width,
                height));

    const bool oldValue =
        *replacements;

    if (clicked)
    {
        const float localX =
            ImGui::GetIO().MousePos.x -
            position.x;

        *replacements =
            localX >=
            leftWidth;
    }

    ImDrawList* draw =
        ImGui::GetWindowDrawList();

    const float rounding =
        ImGui::GetStyle().FrameRounding;

    const ImU32 frameColor =
        ImGui::GetColorU32(
            ImGuiCol_FrameBg);

    const ImU32 borderColor =
        ImGui::GetColorU32(
            ImGuiCol_Border);

    const ImU32 selectedColor =
        ImGui::GetColorU32(
            ImVec4(
                0.78f,
                0.62f,
                0.34f,
                1.0f));

    draw->AddRectFilled(
        position,
        ImVec2(
            position.x + width,
            position.y + height),
        frameColor,
        rounding);

    draw->AddRect(
        position,
        ImVec2(
            position.x + width,
            position.y + height),
        borderColor,
        rounding);

    const ImVec2 selectedMin(
        *replacements
            ? position.x + leftWidth
            : position.x,
        position.y);

    const ImVec2 selectedMax(
        *replacements
            ? position.x + width
            : position.x + leftWidth,
        position.y + height);

    draw->AddRectFilled(
        selectedMin,
        selectedMax,
        selectedColor,
        rounding);

    const char* vanillaText =
        "Vanilla";

    const char* replacementText =
        "Replacements";

    const ImVec2 vanillaSize =
        ImGui::CalcTextSize(
            vanillaText);

    const ImVec2 replacementSize =
        ImGui::CalcTextSize(
            replacementText);

    const float textY =
        position.y +
        (height -
         ImGui::GetFontSize()) *
            0.5f;

    draw->AddText(
        ImVec2(
            position.x +
                (leftWidth -
                 vanillaSize.x) *
                    0.5f,
            textY),
        ImGui::GetColorU32(
            !*replacements
                ? ImVec4(
                    0.07f,
                    0.06f,
                    0.04f,
                    1.0f)
                : ImGui::GetStyleColorVec4(
                    ImGuiCol_TextDisabled)),
        vanillaText);

    draw->AddText(
        ImVec2(
            position.x +
                leftWidth +
                (rightWidth -
                 replacementSize.x) *
                    0.5f,
            textY),
        ImGui::GetColorU32(
            *replacements
                ? ImVec4(
                    0.07f,
                    0.06f,
                    0.04f,
                    1.0f)
                : ImGui::GetStyleColorVec4(
                    ImGuiCol_TextDisabled)),
        replacementText);

    ImGui::PopID();

    return
        oldValue !=
        *replacements;
}


static void ModernHandleLiveSectionTransition()
{
    // Live UI is snapshot-driven. Entering/leaving the page does not start,
    // stop, or poll AC. A snapshot only happens when the user presses the
    // Take Snapshot button.
    g_ModernObservedSection =
        g_ModernSection;
}


static void ModernReleasePreviewTextures()
{
    for (auto& entry :
         g_ModernThumbnailCache)
    {
        ModernReleaseGpuImage(
            entry.second);
    }

    g_ModernThumbnailCache.clear();
    g_ModernThumbnailOrder.clear();
    g_ModernThumbnailRequested.clear();

    ModernInvalidateInspectorImages();
    ModernInvalidateGeneratedImages();
}



static void ModernApplyNativeFrameTheme(
    HWND window)
{
    if (window == nullptr)
        return;

    const BOOL useDark = TRUE;

    // DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(
        window,
        static_cast<DWMWINDOWATTRIBUTE>(20),
        &useDark,
        sizeof(useDark));

    const COLORREF border =
        RGB(35, 39, 44);

    const COLORREF caption =
        RGB(12, 15, 18);

    const COLORREF captionText =
        RGB(220, 205, 175);

    // Windows 11 attributes. Unsupported systems simply ignore them.
    DwmSetWindowAttribute(
        window,
        static_cast<DWMWINDOWATTRIBUTE>(34),
        &border,
        sizeof(border));

    DwmSetWindowAttribute(
        window,
        static_cast<DWMWINDOWATTRIBUTE>(35),
        &caption,
        sizeof(caption));

    DwmSetWindowAttribute(
        window,
        static_cast<DWMWINDOWATTRIBUTE>(36),
        &captionText,
        sizeof(captionText));
}


static void ModernLoadUiFont()
{
    ImGuiIO& io =
        ImGui::GetIO();

    wchar_t windowsDirectory[MAX_PATH] = {};

    const UINT length =
        GetWindowsDirectoryW(
            windowsDirectory,
            static_cast<UINT>(
                _countof(windowsDirectory)));

    bool loaded = false;

    if (length != 0 &&
        length < _countof(windowsDirectory))
    {
        const std::wstring fontPathWide =
            std::wstring(windowsDirectory) +
            L"\\Fonts\\segoeui.ttf";

        const std::string fontPath =
            FromWide(fontPathWide);

        if (!fontPath.empty())
        {
            loaded =
                io.Fonts->AddFontFromFileTTF(
                    fontPath.c_str()) != nullptr;
        }
    }

    if (!loaded)
        io.Fonts->AddFontDefault();
}


static void ModernReleaseLiveTexture()
{
    if (g_ModernLiveView != nullptr)
    {
        g_ModernLiveView->Release();
        g_ModernLiveView = nullptr;
    }

    if (g_ModernLiveTexture != nullptr)
    {
        g_ModernLiveTexture->Release();
        g_ModernLiveTexture = nullptr;
    }

    g_ModernLiveTextureWidth = 0;
    g_ModernLiveTextureHeight = 0;
}


static void ModernMarkLiveCanvasDirty()
{
    g_ModernLiveDirty = true;
}


static bool ModernUploadLiveCanvas()
{
    const std::uint32_t generation =
        g_LiveMirrorAppliedGeneration.load(
            std::memory_order_acquire);

    if (generation !=
        g_ModernLiveSeenGeneration)
    {
        g_ModernLiveSeenGeneration =
            generation;

        g_ModernLiveDirty = true;

        // A new snapshot represents a new scene. Start it fitted so the user
        // never inherits an unrelated zoom/pan position from the prior capture.
        g_ModernLiveZoom = 1.0f;
        g_ModernLivePan = ImVec2(0.0f, 0.0f);
        g_ModernLivePanning = false;
    }

    if (g_ModernDevice == nullptr ||
        g_ModernDeviceContext == nullptr ||
        g_LiveCanvas.empty() ||
        g_LiveCanvasWidth <= 0 ||
        g_LiveCanvasHeight <= 0)
    {
        ModernReleaseLiveTexture();
        return false;
    }

    const bool sizeChanged =
        g_ModernLiveTexture == nullptr ||
        g_ModernLiveTextureWidth !=
            g_LiveCanvasWidth ||
        g_ModernLiveTextureHeight !=
            g_LiveCanvasHeight;

    if (sizeChanged)
    {
        ModernReleaseLiveTexture();

        D3D11_TEXTURE2D_DESC description = {};

        description.Width =
            static_cast<UINT>(
                g_LiveCanvasWidth);

        description.Height =
            static_cast<UINT>(
                g_LiveCanvasHeight);

        description.MipLevels = 1;
        description.ArraySize = 1;

        // g_LiveCanvas is BGRA32.
        description.Format =
            DXGI_FORMAT_B8G8R8A8_UNORM;

        description.SampleDesc.Count = 1;

        description.Usage =
            D3D11_USAGE_DEFAULT;

        description.BindFlags =
            D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial = {};

        initial.pSysMem =
            g_LiveCanvas.data();

        initial.SysMemPitch =
            static_cast<UINT>(
                g_LiveCanvasWidth * 4);

        HRESULT result =
            g_ModernDevice->CreateTexture2D(
                &description,
                &initial,
                &g_ModernLiveTexture);

        if (FAILED(result) ||
            g_ModernLiveTexture == nullptr)
        {
            ModernReleaseLiveTexture();
            return false;
        }

        result =
            g_ModernDevice->
                CreateShaderResourceView(
                    g_ModernLiveTexture,
                    nullptr,
                    &g_ModernLiveView);

        if (FAILED(result) ||
            g_ModernLiveView == nullptr)
        {
            ModernReleaseLiveTexture();
            return false;
        }

        g_ModernLiveTextureWidth =
            g_LiveCanvasWidth;

        g_ModernLiveTextureHeight =
            g_LiveCanvasHeight;

        g_ModernLiveDirty = false;

        return true;
    }

    if (g_ModernLiveDirty)
    {
        g_ModernDeviceContext->
            UpdateSubresource(
                g_ModernLiveTexture,
                0,
                nullptr,
                g_LiveCanvas.data(),
                static_cast<UINT>(
                    g_LiveCanvasWidth * 4),
                0);

        g_ModernLiveDirty = false;
    }

    return g_ModernLiveView != nullptr;
}


static void ModernOpenTextureIndex(
    std::size_t textureIndex)
{
    ModernCancelGeneratedWork();
    if (textureIndex >=
        g_Textures.size())
    {
        return;
    }

    const TextureRecord& texture =
        g_Textures[
            textureIndex];

    g_ModernSection =
        ModernSection::Textures;

    g_ReplacementsOnly = false;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;

    if (texture.did.size() >= 5)
    {
        g_ActivePrefix =
            texture.did.substr(
                0,
                5);
    }
    else
    {
        g_ActivePrefix.clear();
    }

    g_ModernSearch[0] = '\0';
    g_ModernWidthFilter = 0;
    g_ModernHeightFilter = 0;
    g_DisplayReplaced = true;

    g_ModernSelectedTextures.clear();
    g_ModernSelectedTextures.insert(
        textureIndex);

    g_ModernSelectionAnchor =
        textureIndex;

    g_SelectedRow =
        static_cast<int>(
            textureIndex);

    g_SelectedListRow = -1;

    g_ModernScrollToTextureIndex =
        textureIndex;

    g_ModernNeedPreviewPrime =
        true;

    QueuePreview(
        textureIndex,
        true);

    ModernInvalidateInspectorImages();
}


static void ModernSelectLiveHit(
    const LiveHitResult& hit)
{
    if (!hit.hit)
        return;

    g_LiveSelectedControl =
        hit.control;

    g_LiveSelectedItemId =
        hit.itemId;

    g_LiveSelectedGeneratedItem =
        hit.generatedItem;

    g_LiveSelectedSceneRect =
        hit.sceneRect;

    g_LiveHaveSelectedSceneRect =
        true;

    if (hit.generatedItem)
    {
        g_LiveSelectedTextureIndex =
            static_cast<std::size_t>(-1);

        g_LiveSelectedGeneratedSurface =
            hit.sourceSurface;

        g_LiveSelectedGeneratedHash = 0;

        const LiveTexturePixels* generated =
            LiveGetGenerated(
                hit.sourceSurface);

        if (generated != nullptr)
        {
            g_LiveSelectedGeneratedHash =
                LiveHashGeneratedPixels(
                    *generated);

            ModernRequestGeneratedComposition(
                *generated,
                hit.sourceSurface,
                g_LiveSelectedGeneratedHash,
                hit.itemId);
        }
    }
    else
    {
        ModernCancelGeneratedWork();

        const int textureIndex =
            LiveFindTextureIndex(
                hit.did);

        if (textureIndex < 0)
            return;

        g_LiveSelectedTextureIndex =
            static_cast<std::size_t>(
                textureIndex);

        g_LiveSelectedGeneratedSurface = 0;
        g_LiveSelectedGeneratedHash = 0;

        g_SelectedRow =
            textureIndex;

        g_SelectedListRow = -1;

        QueuePreview(
            static_cast<std::size_t>(
                textureIndex),
            true);
    }
}


static void ModernRenderGeneratedInspector()
{
    ModernEnsureGeneratedWorkRequested();
    ModernEnsureGeneratedImages();

    const float em =
        ImGui::GetFontSize();

    ImGui::TextColored(
        MODERN_GOLD,
        "Inspector");

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(
        MODERN_GOLD,
        "Multiple Layered Textures");

    ImGui::Spacing();

    // Generated source preview.
    ImGui::BeginChild(
        "##GeneratedOutputCard",
        ImVec2(
            7.0f * em,
            8.0f * em),
        ImGuiChildFlags_Borders);

    ImGui::TextUnformatted(
        "Generated");

    ImGui::Separator();

    ModernRenderGpuImageFit(
        g_ModernGeneratedImage,
        ImGui::GetContentRegionAvail());

    ImGui::EndChild();

    ImGui::Spacing();

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImGui::GetStyleColorVec4(
            ImGuiCol_TextDisabled));

    ImGui::TextWrapped(
        "Generated icon matching is heuristic. "
        "Suggested background and foreground layers "
        "may be incomplete or incorrect.");

    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (g_ModernGeneratedWorking)
    {
        ImGui::TextColored(
            MODERN_GOLD,
            "Working...");

        ImGui::Spacing();

        ImGui::TextDisabled(
            "Scanning all same-size DAT textures for likely layers.");

        return;
    }

    if (!g_LiveHaveGeneratedBackground)
    {
        ImGui::TextDisabled(
            "No likely composition was identified.");

        return;
    }

    ImGui::TextColored(
        MODERN_GOLD,
        "Likely Composition");

    ImGui::Spacing();

    // --------------------------------------------------------
    // Background
    // --------------------------------------------------------

    ImGui::TextUnformatted(
        "Background");

    const std::size_t backgroundIndex =
        g_LiveGeneratedBackground
            .textureIndex;

    if (backgroundIndex <
        g_Textures.size())
    {
        const TextureRecord& texture =
            g_Textures[
                backgroundIndex];

        ImGui::PushID(
            "BackgroundResult");

        ImGui::BeginGroup();

        const float imageSize =
            3.2f * em;

        ModernGpuImage* thumbnail =
            ModernGetThumbnail(
                backgroundIndex);

        if (thumbnail != nullptr &&
            thumbnail->view != nullptr)
        {
            const ImTextureID id =
                static_cast<ImTextureID>(
                    reinterpret_cast<
                        std::uintptr_t>(
                            thumbnail->view));

            ImGui::Image(
                id,
                ImVec2(
                    imageSize,
                    imageSize));
        }
        else
        {
            ImGui::Dummy(
                ImVec2(
                    imageSize,
                    imageSize));
        }

        ImGui::SameLine();

        ImGui::BeginGroup();

        const float animatedScore =
            ModernAnimatedGeneratedPercent(
                g_LiveGeneratedBackground
                    .score);

        const float percentageWidth =
            4.0f * em;

        const float barWidth =
            max(
                4.0f * em,
                ImGui::GetContentRegionAvail().x -
                    percentageWidth -
                    ImGui::GetStyle()
                        .ItemSpacing.x);

        ImGui::ProgressBar(
            animatedScore /
                100.0f,
            ImVec2(
                barWidth,
                ImGui::GetFrameHeight()),
            "");

        ImGui::SameLine();

        ImGui::Text(
            "%.2f%%",
            animatedScore);

        ImGui::TextDisabled(
            "%s",
            texture.did.c_str());

        ImGui::EndGroup();
        ImGui::EndGroup();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(
                ImGuiMouseCursor_Hand);

            if (ImGui::IsMouseClicked(
                    ImGuiMouseButton_Left))
            {
                ModernOpenTextureIndex(
                    backgroundIndex);
            }
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // --------------------------------------------------------
    // Foregrounds
    // --------------------------------------------------------

    ImGui::TextUnformatted(
        "Foreground Candidates");

    ImGui::Spacing();

    for (std::size_t i = 0;
         i < g_LiveGeneratedMatches.size();
         ++i)
    {
        const LiveLayerMatch& match =
            g_LiveGeneratedMatches[i];

        if (match.foregroundIndex >=
            g_Textures.size())
        {
            continue;
        }

        const std::size_t textureIndex =
            match.foregroundIndex;

        const TextureRecord& texture =
            g_Textures[
                textureIndex];

        ImGui::PushID(
            static_cast<int>(i));

        ImGui::BeginGroup();

        const float imageSize =
            3.2f * em;

        ModernGpuImage* thumbnail =
            ModernGetThumbnail(
                textureIndex);

        if (thumbnail != nullptr &&
            thumbnail->view != nullptr)
        {
            const ImTextureID id =
                static_cast<ImTextureID>(
                    reinterpret_cast<
                        std::uintptr_t>(
                            thumbnail->view));

            ImGui::Image(
                id,
                ImVec2(
                    imageSize,
                    imageSize));
        }
        else
        {
            ImGui::Dummy(
                ImVec2(
                    imageSize,
                    imageSize));
        }

        ImGui::SameLine();

        ImGui::BeginGroup();

        const float animatedScore =
            ModernAnimatedGeneratedPercent(
                match.score);

        const float percentageWidth =
            4.0f * em;

        const float barWidth =
            max(
                4.0f * em,
                ImGui::GetContentRegionAvail().x -
                    percentageWidth -
                    ImGui::GetStyle()
                        .ItemSpacing.x);

        ImGui::ProgressBar(
            animatedScore /
                100.0f,
            ImVec2(
                barWidth,
                ImGui::GetFrameHeight()),
            "");

        ImGui::SameLine();

        ImGui::Text(
            "%.2f%%",
            animatedScore);

        ImGui::TextDisabled(
            "%s",
            texture.did.c_str());

        ImGui::EndGroup();
        ImGui::EndGroup();

        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(
                ImGuiMouseCursor_Hand);

            if (ImGui::IsMouseClicked(
                    ImGuiMouseButton_Left))
            {
                ModernOpenTextureIndex(
                    textureIndex);
            }
        }

        ImGui::Spacing();

        ImGui::PopID();
    }
}


static void ModernCleanupRenderTarget()
{
    if (g_ModernRenderTarget != nullptr)
    {
        g_ModernRenderTarget->Release();
        g_ModernRenderTarget = nullptr;
    }
}

static bool ModernCreateRenderTarget()
{
    if (g_ModernSwapChain == nullptr ||
        g_ModernDevice == nullptr)
    {
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;

    const HRESULT getResult =
        g_ModernSwapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backBuffer));

    if (FAILED(getResult) ||
        backBuffer == nullptr)
    {
        return false;
    }

    const HRESULT createResult =
        g_ModernDevice->CreateRenderTargetView(
            backBuffer,
            nullptr,
            &g_ModernRenderTarget);

    backBuffer->Release();

    return
        SUCCEEDED(createResult) &&
        g_ModernRenderTarget != nullptr;
}

static void ModernCleanupD3D()
{
    ModernReleaseLiveTexture();
    ModernCleanupRenderTarget();

    if (g_ModernSwapChain != nullptr)
    {
        g_ModernSwapChain->Release();
        g_ModernSwapChain = nullptr;
    }

    if (g_ModernDeviceContext != nullptr)
    {
        g_ModernDeviceContext->Release();
        g_ModernDeviceContext = nullptr;
    }

    if (g_ModernDevice != nullptr)
    {
        g_ModernDevice->Release();
        g_ModernDevice = nullptr;
    }
}

static bool ModernCreateD3D(HWND window)
{
    DXGI_SWAP_CHAIN_DESC swapDescription = {};
    swapDescription.BufferCount = 2;
    swapDescription.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.OutputWindow = window;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.Windowed = TRUE;
    swapDescription.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL requestedLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL createdLevel =
        D3D_FEATURE_LEVEL_10_0;

    const HRESULT result =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            requestedLevels,
            static_cast<UINT>(
                _countof(requestedLevels)),
            D3D11_SDK_VERSION,
            &swapDescription,
            &g_ModernSwapChain,
            &g_ModernDevice,
            &createdLevel,
            &g_ModernDeviceContext);

    if (FAILED(result))
    {
        ModernCleanupD3D();
        return false;
    }

    if (!ModernCreateRenderTarget())
    {
        ModernCleanupD3D();
        return false;
    }

    return true;
}

static void ModernApplyTheme()
{
    ImGuiStyle& style =
        ImGui::GetStyle();

    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.CellPadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);

    style.WindowRounding = 0.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;

    // Button rectangles are kept at least as tall as ImGui's DPI-aware
    // natural frame height, so normal metric centering works consistently.
    style.ButtonTextAlign =
        ImVec2(0.5f, 0.5f);

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] =
        ImVec4(0.88f, 0.89f, 0.91f, 1.00f);

    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.46f, 0.49f, 0.53f, 1.00f);

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.035f, 0.043f, 0.052f, 1.00f);

    colors[ImGuiCol_ChildBg] =
        ImVec4(0.050f, 0.060f, 0.071f, 1.00f);

    colors[ImGuiCol_PopupBg] =
        ImVec4(0.055f, 0.065f, 0.078f, 0.98f);

    colors[ImGuiCol_Border] =
        ImVec4(0.15f, 0.17f, 0.19f, 1.00f);

    colors[ImGuiCol_FrameBg] =
        ImVec4(0.075f, 0.087f, 0.102f, 1.00f);

    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.11f, 0.125f, 0.145f, 1.00f);

    colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.15f, 0.135f, 0.095f, 1.00f);

    colors[ImGuiCol_TitleBg] =
        ImVec4(0.040f, 0.048f, 0.058f, 1.00f);

    colors[ImGuiCol_TitleBgActive] =
        ImVec4(0.050f, 0.058f, 0.068f, 1.00f);

    colors[ImGuiCol_Button] =
        ImVec4(0.085f, 0.098f, 0.115f, 1.00f);

    colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.16f, 0.145f, 0.105f, 1.00f);

    colors[ImGuiCol_ButtonActive] =
        ImVec4(0.23f, 0.19f, 0.12f, 1.00f);

    colors[ImGuiCol_Header] =
        ImVec4(0.12f, 0.105f, 0.075f, 0.80f);

    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.19f, 0.16f, 0.10f, 0.90f);

    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.25f, 0.20f, 0.12f, 1.00f);

    colors[ImGuiCol_Separator] =
        ImVec4(0.16f, 0.18f, 0.20f, 1.00f);

    colors[ImGuiCol_SeparatorHovered] =
        MODERN_GOLD;

    colors[ImGuiCol_SeparatorActive] =
        MODERN_GOLD_HOVER;

    colors[ImGuiCol_CheckMark] =
        MODERN_GOLD;

    colors[ImGuiCol_SliderGrab] =
        MODERN_GOLD;

    colors[ImGuiCol_SliderGrabActive] =
        MODERN_GOLD_HOVER;

    colors[ImGuiCol_TableHeaderBg] =
        ImVec4(0.065f, 0.076f, 0.090f, 1.00f);

    colors[ImGuiCol_TableBorderStrong] =
        ImVec4(0.15f, 0.17f, 0.19f, 1.00f);

    colors[ImGuiCol_TableBorderLight] =
        ImVec4(0.10f, 0.12f, 0.14f, 1.00f);

    colors[ImGuiCol_TableRowBg] =
        ImVec4(0.045f, 0.054f, 0.064f, 1.00f);

    colors[ImGuiCol_TableRowBgAlt] =
        ImVec4(0.055f, 0.064f, 0.075f, 1.00f);
}

static void ModernRefreshReplacementSet()
{
    g_ModernReplacementDids.clear();

    const std::wstring pattern =
        g_AppPaths.replacementDirectory +
        L"\\*.rgb";

    WIN32_FIND_DATAW data = {};

    HANDLE handle =
        FindFirstFileW(
            pattern.c_str(),
            &data);

    if (handle == INVALID_HANDLE_VALUE)
    {
        g_ModernLastReplacementRefresh =
            GetTickCount64();

        return;
    }

    do
    {
        if ((data.dwFileAttributes &
             FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            continue;
        }

        std::wstring name =
            data.cFileName;

        if (name.size() != 12)
            continue;

        if (_wcsicmp(
                name.substr(8).c_str(),
                L".rgb") != 0)
        {
            continue;
        }

        std::string did =
            FromWide(
                name.substr(0, 8));

        std::transform(
            did.begin(),
            did.end(),
            did.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    std::toupper(c));
            });

        g_ModernReplacementDids.insert(
            did);
    }
    while (FindNextFileW(
        handle,
        &data));

    FindClose(handle);

    g_ModernLastReplacementRefresh =
        GetTickCount64();
}

static bool ModernHasReplacement(
    const TextureRecord& texture)
{
    return
        g_ModernReplacementDids.find(
            texture.did) !=
        g_ModernReplacementDids.end();
}

static bool ModernContainsInsensitive(
    const std::string& haystack,
    const std::string& needle)
{
    if (needle.empty())
        return true;

    const auto result =
        std::search(
            haystack.begin(),
            haystack.end(),
            needle.begin(),
            needle.end(),
            [](char left, char right)
            {
                return
                    std::tolower(
                        static_cast<unsigned char>(
                            left)) ==
                    std::tolower(
                        static_cast<unsigned char>(
                            right));
            });

    return result != haystack.end();
}

static const char* ModernFormatName(
    std::uint32_t pixelFormat)
{
    switch (pixelFormat)
    {
        case 0x14:
            return "BGR";

        case 0x15:
            return "BGRA";

        case 0x65:
            return "INDEX16";

        default:
            return "Other";
    }
}

static void ModernSyncLegacyBrowser()
{
    // The modern ImGui browser owns presentation now.
    // Do not rebuild/populate the hidden Win32 ListView whenever
    // a tab changes; that was one of the major large-tab stalls.
    g_ModernNeedPreviewPrime = true;
    g_ModernPreviewPrimeList.clear();
    g_ModernPreviewPrimeCursor = 0;
}

static void ModernSelectAllTextures()
{
    g_ModernSection =
        ModernSection::Textures;

    g_ActivePrefix.clear();
    g_ReplacementsOnly = false;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;

    ModernSyncLegacyBrowser();
}

static void ModernSelectReplacements()
{
    g_ModernSection =
        ModernSection::Replacements;

    g_ActivePrefix.clear();
    g_ReplacementsOnly = true;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;

    ModernSyncLegacyBrowser();
}

static void ModernSelectEncountered()
{
    g_ModernSection =
        ModernSection::Encountered;

    LoadEncounteredDIDs();

    g_ActivePrefix.clear();
    g_ReplacementsOnly = false;
    g_EncounteredOnly = true;
    g_ActiveCustomTab = -1;

    ModernSyncLegacyBrowser();
}

static void ModernSelectPrefix(
    const std::string& prefix)
{
    g_ModernSection =
        ModernSection::Textures;

    g_ActivePrefix = prefix;
    g_ReplacementsOnly = false;
    g_EncounteredOnly = false;
    g_ActiveCustomTab = -1;

    ModernSyncLegacyBrowser();
}

static void ModernSelectCustomGroup(
    int customIndex)
{
    if (customIndex < 0 ||
        static_cast<std::size_t>(
            customIndex) >=
            g_CustomTabs.size())
    {
        return;
    }

    g_ModernSection =
        ModernSection::Textures;

    g_ActivePrefix.clear();
    g_ReplacementsOnly = false;
    g_EncounteredOnly = false;
    g_ActiveCustomTab =
        customIndex;

    ModernSyncLegacyBrowser();
}


static bool ModernChooseAndLoadDat()
{
    // Stop any generated-icon composition work before the backing DAT changes.
    // The worker's requests own their TextureRecord copies, but their pixel
    // reads intentionally come from the currently open DAT.
    ModernCancelGeneratedWork();

    std::wstring error;

    if (!SelectAndLoadDat(
            g_MainWindow,
            error))
    {
        if (!error.empty())
        {
            g_DatLoadNotice =
                FromWide(error);

            g_DatLoadNoticeError =
                true;
        }

        return false;
    }

    // Texture indices can change when a different DAT is selected, so every
    // index-keyed GPU/cache/selection object must be reset.
    ModernReleasePreviewTextures();

    g_ModernSelectedTextures.clear();

    g_ModernSelectionAnchor =
        static_cast<std::size_t>(-1);

    g_ModernScrollToTextureIndex =
        static_cast<std::size_t>(-1);

    g_SelectedRow = -1;
    g_SelectedListRow = -1;

    g_ModernSearch[0] = '\0';
    g_ModernWidthFilter = 0;
    g_ModernHeightFilter = 0;

    ModernRefreshReplacementSet();
    ModernSelectAllTextures();

    BuildTextureTabs();
    PopulateList();

    LiveReloadScene();
    ModernMarkLiveCanvasDirty();

    if (g_StatusText != nullptr)
    {
        SetWindowTextW(
            g_StatusText,
            ToWide(g_DatScanSummary).c_str());
    }

    return true;
}

static std::string ModernCustomGroupsText(
    const TextureRecord& texture)
{
    return FromWide(
        CustomGroupsForTexture(
            texture));
}


static bool ModernTextureIsInExcludedGroup(
    const TextureRecord& texture)
{
    if (g_ModernExcludedCustomGroups.empty())
        return false;

    for (const CustomTab& group :
         g_CustomTabs)
    {
        if (g_ModernExcludedCustomGroups.find(
                group.name) ==
            g_ModernExcludedCustomGroups.end())
        {
            continue;
        }

        if (group.dids.find(
                texture.did) !=
            group.dids.end())
        {
            return true;
        }
    }

    return false;
}


static bool ModernTextureVisible(
    const TextureRecord& texture)
{
    if (g_ModernWidthFilter > 0 &&
        texture.width !=
            static_cast<std::uint32_t>(
                g_ModernWidthFilter))
    {
        return false;
    }

    if (g_ModernHeightFilter > 0 &&
        texture.height !=
            static_cast<std::uint32_t>(
                g_ModernHeightFilter))
    {
        return false;
    }

    if (!g_DisplayUnsupported &&
        !IsPreviewable(texture))
    {
        return false;
    }

    if (g_ModernSection ==
            ModernSection::Replacements &&
        !ModernHasReplacement(texture))
    {
        return false;
    }

    if (g_ModernSection ==
            ModernSection::Encountered &&
        !IsEncounteredTexture(texture))
    {
        return false;
    }

    if (g_ModernSection ==
            ModernSection::Textures)
    {
        if (!g_ActivePrefix.empty() &&
            !TextureMatchesActiveTab(texture))
        {
            return false;
        }

        if (g_ActiveCustomTab >= 0 &&
            !IsInActiveCustomTab(texture))
        {
            return false;
        }

        if (!g_DisplayReplaced &&
            ModernHasReplacement(texture))
        {
            return false;
        }
    }

    if (ModernTextureIsInExcludedGroup(
            texture))
    {
        return false;
    }

    const std::string search =
        g_ModernSearch;

    if (!search.empty())
    {
        const auto note =
            g_TextureNotes.find(
                texture.did);

        const std::string noteText =
            note == g_TextureNotes.end()
                ? std::string()
                : note->second;

        const std::string groupText =
            ModernCustomGroupsText(
                texture);

        if (!ModernContainsInsensitive(
                texture.did,
                search) &&
            !ModernContainsInsensitive(
                groupText,
                search) &&
            !ModernContainsInsensitive(
                noteText,
                search))
        {
            return false;
        }
    }

    return true;
}

static bool ModernNavButton(
    const char* label,
    bool selected)
{
    if (selected)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.22f,
                0.18f,
                0.10f,
                1.00f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.29f,
                0.23f,
                0.13f,
                1.00f));
    }
    else
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.055f,
                0.065f,
                0.078f,
                1.00f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.095f,
                0.108f,
                0.125f,
                1.00f));
    }

    const bool clicked =
        ImGui::Button(
            label,
            ImVec2(
                -1.0f,
                ModernButtonHeight(40.0f)));

    ImGui::PopStyleColor(2);

    return clicked;
}

static void ModernSelectSingleTexture(
    std::size_t textureIndex)
{
    g_ModernSelectedTextures.clear();

    if (textureIndex <
        g_Textures.size())
    {
        g_ModernSelectedTextures.insert(
            textureIndex);

        g_SelectedRow =
            static_cast<int>(
                textureIndex);

        g_ModernSelectionAnchor =
            textureIndex;

        QueuePreview(
            textureIndex,
            true);

        ModernInvalidateInspectorImages();
    }
}


static void ModernApplyTextureSelection(
    const std::vector<std::size_t>& visible,
    std::size_t visibleRow,
    std::size_t textureIndex)
{
    ModernCancelGeneratedWork();
    if (textureIndex >=
        g_Textures.size())
    {
        return;
    }

    const ImGuiIO& io =
        ImGui::GetIO();

    if (io.KeyShift &&
        g_ModernSelectionAnchor !=
            static_cast<std::size_t>(-1))
    {
        const auto anchorIt =
            std::find(
                visible.begin(),
                visible.end(),
                g_ModernSelectionAnchor);

        if (anchorIt != visible.end())
        {
            const std::size_t anchorRow =
                static_cast<std::size_t>(
                    std::distance(
                        visible.begin(),
                        anchorIt));

            const std::size_t first =
                min(
                    anchorRow,
                    visibleRow);

            const std::size_t last =
                max(
                    anchorRow,
                    visibleRow);

            if (!io.KeyCtrl)
                g_ModernSelectedTextures.clear();

            for (std::size_t i = first;
                 i <= last;
                 ++i)
            {
                g_ModernSelectedTextures.insert(
                    visible[i]);
            }
        }
        else
        {
            ModernSelectSingleTexture(
                textureIndex);
        }
    }
    else if (io.KeyCtrl)
    {
        const auto found =
            g_ModernSelectedTextures.find(
                textureIndex);

        if (found ==
            g_ModernSelectedTextures.end())
        {
            g_ModernSelectedTextures.insert(
                textureIndex);
        }
        else
        {
            g_ModernSelectedTextures.erase(
                found);
        }

        g_ModernSelectionAnchor =
            textureIndex;
    }
    else
    {
        ModernSelectSingleTexture(
            textureIndex);
    }

    g_SelectedRow =
        static_cast<int>(
            textureIndex);

    QueuePreview(
        textureIndex,
        true);

    ModernInvalidateInspectorImages();
}


static void ModernSetDialogText(
    const std::string& value)
{
    std::memset(
        g_ModernDialogText,
        0,
        sizeof(g_ModernDialogText));

    const std::size_t count =
        value.size() <
                sizeof(g_ModernDialogText) - 1
            ? value.size()
            : sizeof(g_ModernDialogText) - 1;

    if (count > 0)
    {
        std::memcpy(
            g_ModernDialogText,
            value.data(),
            count);
    }
}


static void ModernRequestDialog(
    ModernDialogKind kind)
{
    g_ModernDialogKind =
        kind;

    g_ModernDialogOpenPending =
        true;

    g_ModernDialogError.clear();
}


static void ModernCreateCustomGroup()
{
    g_ModernDialogGroupIndex =
        static_cast<std::size_t>(-1);

    ModernSetDialogText(
        "");

    ModernRequestDialog(
        ModernDialogKind::CreateGroup);
}


static void ModernRenameCustomGroup(
    std::size_t customIndex)
{
    if (customIndex >=
        g_CustomTabs.size())
    {
        return;
    }

    g_ModernDialogGroupIndex =
        customIndex;

    ModernSetDialogText(
        g_CustomTabs[
            customIndex].name);

    ModernRequestDialog(
        ModernDialogKind::RenameGroup);
}


static void ModernDeleteCustomGroup(
    std::size_t customIndex)
{
    if (customIndex >=
        g_CustomTabs.size())
    {
        return;
    }

    g_ModernDialogGroupIndex =
        customIndex;

    ModernSetDialogText(
        "");

    ModernRequestDialog(
        ModernDialogKind::DeleteGroup);
}


static void ModernEditTextureNote(
    std::size_t textureIndex)
{
    if (textureIndex >=
        g_Textures.size())
    {
        return;
    }

    g_ModernDialogTextureIndex =
        textureIndex;

    g_ModernDialogNoteTextureIndices.clear();

    const bool useSelection =
        g_ModernSelectedTextures.size() > 1 &&
        g_ModernSelectedTextures.find(
            textureIndex) !=
        g_ModernSelectedTextures.end();

    if (useSelection)
    {
        g_ModernDialogNoteTextureIndices.assign(
            g_ModernSelectedTextures.begin(),
            g_ModernSelectedTextures.end());

        std::sort(
            g_ModernDialogNoteTextureIndices.begin(),
            g_ModernDialogNoteTextureIndices.end());
    }
    else
    {
        g_ModernDialogNoteTextureIndices.push_back(
            textureIndex);
    }

    std::string commonNote;
    bool haveCommonNote = false;
    bool mixedNotes = false;

    for (const std::size_t selectedIndex :
         g_ModernDialogNoteTextureIndices)
    {
        if (selectedIndex >=
            g_Textures.size())
        {
            continue;
        }

        const auto found =
            g_TextureNotes.find(
                g_Textures[
                    selectedIndex].did);

        const std::string note =
            found == g_TextureNotes.end()
                ? std::string()
                : found->second;

        if (!haveCommonNote)
        {
            commonNote = note;
            haveCommonNote = true;
        }
        else if (note != commonNote)
        {
            mixedNotes = true;
            break;
        }
    }

    g_ModernDialogNotesWereMixed =
        mixedNotes;

    ModernSetDialogText(
        mixedNotes
            ? ""
            : commonNote);

    ModernRequestDialog(
        ModernDialogKind::EditNote);
}

static void ModernRequestRemoveReplacement(
    std::size_t textureIndex)
{
    if (textureIndex >=
        g_Textures.size())
    {
        return;
    }

    g_ModernDialogTextureIndex =
        textureIndex;

    ModernSetDialogText(
        "");

    ModernRequestDialog(
        ModernDialogKind::RemoveReplacement);
}


static void ModernAddSelectionToCustomGroup(
    std::size_t customIndex,
    std::size_t fallbackTextureIndex)
{
    if (customIndex >=
        g_CustomTabs.size())
    {
        return;
    }

    if (g_ModernSelectedTextures.empty() &&
        fallbackTextureIndex <
            g_Textures.size())
    {
        ModernSelectSingleTexture(
            fallbackTextureIndex);
    }

    CustomTab& group =
        g_CustomTabs[
            customIndex];

    for (const std::size_t textureIndex :
         g_ModernSelectedTextures)
    {
        if (textureIndex <
            g_Textures.size())
        {
            group.dids.insert(
                g_Textures[
                    textureIndex].did);
        }
    }

    SaveCustomTabs();
}


static void ModernRemoveSelectionFromActiveGroup(
    std::size_t fallbackTextureIndex)
{
    if (g_ActiveCustomTab < 0 ||
        static_cast<std::size_t>(
            g_ActiveCustomTab) >=
            g_CustomTabs.size())
    {
        return;
    }

    if (g_ModernSelectedTextures.empty() &&
        fallbackTextureIndex <
            g_Textures.size())
    {
        ModernSelectSingleTexture(
            fallbackTextureIndex);
    }

    CustomTab& group =
        g_CustomTabs[
            static_cast<std::size_t>(
                g_ActiveCustomTab)];

    for (const std::size_t textureIndex :
         g_ModernSelectedTextures)
    {
        if (textureIndex <
            g_Textures.size())
        {
            group.dids.erase(
                g_Textures[
                    textureIndex].did);
        }
    }

    SaveCustomTabs();
    ModernSyncLegacyBrowser();
}


static bool ModernSelectionAllInGroup(
    std::size_t customIndex)
{
    if (customIndex >=
            g_CustomTabs.size() ||
        g_ModernSelectedTextures.empty())
    {
        return false;
    }

    const CustomTab& group =
        g_CustomTabs[
            customIndex];

    for (const std::size_t textureIndex :
         g_ModernSelectedTextures)
    {
        if (textureIndex >=
            g_Textures.size() ||
            group.dids.find(
                g_Textures[
                    textureIndex].did) ==
                group.dids.end())
        {
            return false;
        }
    }

    return true;
}


static void ModernBeginPreviewPrime(
    const std::vector<std::size_t>& visible)
{
    g_ModernPreviewPrimeList =
        visible;

    g_ModernPreviewPrimeCursor = 0;

    // Hot-load the first 50 immediately. Existing PNGs are
    // decoded/uploaded now. Missing PNGs become high-priority
    // work on the existing preview worker.
    const std::size_t hotCount =
        min(
            static_cast<std::size_t>(50),
            g_ModernPreviewPrimeList.size());

    for (std::size_t i = 0;
         i < hotCount;
         ++i)
    {
        ModernGetThumbnail(
            g_ModernPreviewPrimeList[i]);
    }

    g_ModernPreviewPrimeCursor =
        hotCount;

    g_ModernNeedPreviewPrime =
        false;
}


static void ModernPumpPreviewPrimeQueue()
{
    // Do not stuff 20,000 jobs into the mutex-protected queue
    // in one frame. Feed the worker incrementally.
    const std::size_t batchSize = 128;

    std::size_t queued = 0;

    while (
        g_ModernPreviewPrimeCursor <
            g_ModernPreviewPrimeList.size() &&
        queued < batchSize)
    {
        const std::size_t textureIndex =
            g_ModernPreviewPrimeList[
                g_ModernPreviewPrimeCursor++];

        if (textureIndex >=
            g_Textures.size())
        {
            continue;
        }

        const TextureRecord& texture =
            g_Textures[
                textureIndex];

        if (!IsPreviewable(texture) ||
            PreviewFileExists(texture))
        {
            continue;
        }

        QueuePreview(
            textureIndex,
            false);

        ++queued;
    }
}

static void ModernRenderNavigation()
{
    ImGui::TextColored(
        MODERN_GOLD,
        "BROWSER");

    ImGui::Spacing();

    char label[128] = {};

    sprintf_s(
        label,
        "All Textures  (%zu)",
        g_Textures.size());

    const bool allSelected =
        g_ModernSection ==
            ModernSection::Textures &&
        g_ActivePrefix.empty() &&
        g_ActiveCustomTab < 0;

    if (ModernNavButton(
            label,
            allSelected))
    {
        ModernSelectAllTextures();
    }

    sprintf_s(
        label,
        "Replacements  (%zu)",
        g_ModernReplacementDids.size());

    if (ModernNavButton(
            label,
            g_ModernSection ==
                ModernSection::Replacements))
    {
        ModernSelectReplacements();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(
            "DID Groups"))
    {
        for (const std::string& prefix :
             g_TabPrefixes)
        {
            const bool selected =
                g_ModernSection ==
                    ModernSection::Textures &&
                g_ActivePrefix ==
                    prefix;

            const std::string display =
                prefix + "...";

            if (ImGui::Selectable(
                    display.c_str(),
                    selected))
            {
                ModernSelectPrefix(
                    prefix);
            }
        }
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader(
            "Custom Groups"))
    {
        int renameIndex = -1;
        int deleteIndex = -1;

        for (std::size_t i = 0;
             i < g_CustomTabs.size();
             ++i)
        {
            ImGui::PushID(
                static_cast<int>(i));

            const bool selected =
                g_ModernSection ==
                    ModernSection::Textures &&
                g_ActiveCustomTab ==
                    static_cast<int>(i);

            if (ImGui::Selectable(
                    g_CustomTabs[i]
                        .name.c_str(),
                    selected))
            {
                ModernSelectCustomGroup(
                    static_cast<int>(i));
            }

            if (ImGui::BeginPopupContextItem(
                    "##CustomGroupContext"))
            {
                if (ImGui::MenuItem(
                        "Rename..."))
                {
                    renameIndex =
                        static_cast<int>(i);
                }

                if (ImGui::MenuItem(
                        "Delete Group"))
                {
                    deleteIndex =
                        static_cast<int>(i);
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();
        }

        if (renameIndex >= 0)
        {
            ModernRenameCustomGroup(
                static_cast<std::size_t>(
                    renameIndex));
        }

        if (deleteIndex >= 0)
        {
            ModernDeleteCustomGroup(
                static_cast<std::size_t>(
                    deleteIndex));
        }

        ImGui::Spacing();

        if (ImGui::Button(
                "+ New Group",
                ImVec2(
                    -1.0f,
                    0.0f)))
        {
            ModernCreateCustomGroup();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ModernNavButton(
            "Live UI",
            g_ModernSection ==
                ModernSection::LiveUi))
    {
        g_ModernSection =
            ModernSection::LiveUi;
    }

    if (ModernNavButton(
            "Import / Export",
            g_ModernSection ==
                ModernSection::Packs))
    {
        g_ModernSection =
            ModernSection::Packs;
    }

    // Keep Help/Info visually anchored at the bottom of the navigation
    // panel when there is room, without overlapping a long group list.
    const float infoButtonHeight = 40.0f;
    const float infoBottomY =
        ImGui::GetWindowHeight() -
        ImGui::GetStyle().WindowPadding.y -
        infoButtonHeight;

    if (ImGui::GetCursorPosY() < infoBottomY)
    {
        ImGui::SetCursorPosY(
            infoBottomY);
    }

    if (ModernNavButton(
            "Info",
            g_ModernSection ==
                ModernSection::Info))
    {
        g_ModernSection =
            ModernSection::Info;
    }
}

static void ModernRenderWelcome()
{
    ImGui::TextColored(
        MODERN_GOLD,
        "Welcome");

    ImGui::Spacing();
    ImGui::Separator();

    const ImVec2 available =
        ImGui::GetContentRegionAvail();

    const float topPadding =
        max(
            36.0f,
            min(
                150.0f,
                available.y * 0.20f));

    ImGui::Dummy(
        ImVec2(
            0.0f,
            topPadding));

    const char* heading =
        "Open your Asheron's Call client DAT to get started";

    const float headingWidth =
        ImGui::CalcTextSize(
            heading).x;

    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            (ImGui::GetWindowWidth() -
             headingWidth) *
                0.5f));

    ImGui::TextColored(
        MODERN_GOLD,
        "%s",
        heading);

    ImGui::Spacing();
    ImGui::Spacing();

    const float contentWidth =
        min(
            620.0f,
            max(
                260.0f,
                ImGui::GetContentRegionAvail().x -
                    80.0f));

    const float contentLeft =
        max(
            ImGui::GetCursorPosX(),
            (ImGui::GetWindowWidth() -
             contentWidth) *
                0.5f);

    ImGui::SetCursorPosX(
        contentLeft);

    ImGui::PushTextWrapPos(
        contentLeft +
        contentWidth);

    ImGui::TextDisabled(
        "AC Customs UI Manager reads compatible 0x06 BGR/BGRA textures "
        "directly from your game DAT. The DAT is opened read-only and is "
        "never modified.");

    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    ImGui::Spacing();

    const float buttonWidth =
        220.0f;

    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            (ImGui::GetWindowWidth() -
             buttonWidth) *
                0.5f));

    if (ImGui::Button(
            "Open DAT...",
            ImVec2(
                buttonWidth,
                ModernButtonHeight(
                    40.0f))))
    {
        ModernChooseAndLoadDat();
    }

    ImGui::Spacing();

    const char* remembered =
        "Your DAT location will be remembered for future launches.";

    const float rememberedWidth =
        ImGui::CalcTextSize(
            remembered).x;

    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            (ImGui::GetWindowWidth() -
             rememberedWidth) *
                0.5f));

    ImGui::TextDisabled(
        "%s",
        remembered);

    if (!g_DatLoadNotice.empty())
    {
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::SetCursorPosX(
            contentLeft);

        ImGui::PushTextWrapPos(
            contentLeft +
            contentWidth);

        if (g_DatLoadNoticeError)
        {
            ImGui::TextColored(
                MODERN_RED,
                "%s",
                g_DatLoadNotice.c_str());
        }
        else
        {
            ImGui::TextDisabled(
                "%s",
                g_DatLoadNotice.c_str());
        }

        ImGui::PopTextWrapPos();
    }

    const float footerY =
        ImGui::GetWindowHeight() -
        ImGui::GetStyle().WindowPadding.y -
        ImGui::GetTextLineHeightWithSpacing() *
            2.0f;

    if (ImGui::GetCursorPosY() <
        footerY)
    {
        ImGui::SetCursorPosY(
            footerY);
    }

    ImGui::Separator();

    ImGui::TextDisabled(
        "Common location: C:\\Turbine\\Asheron's Call\\client_portal.dat");
}


static void ModernRenderTextureBrowser()
{
    ImGui::TextColored(
        MODERN_GOLD,
        "Texture Browser");

    ImGui::Spacing();

    bool filterChanged = false;

    const float filterEm =
        ImGui::GetFontSize();

    const float filterSpacing =
        ImGui::GetStyle()
            .ItemSpacing.x;

    const float clearWidth =
        ImGui::CalcTextSize(
            "Clear Filters").x +
        ImGui::GetStyle()
            .FramePadding.x *
            2.0f;

    const float searchWidth =
        max(
            8.0f * filterEm,
            ImGui::GetContentRegionAvail().x -
                clearWidth -
                filterSpacing);

    ImGui::SetNextItemWidth(
        searchWidth);

    if (ImGui::InputTextWithHint(
            "##TextureSearch",
            "Search DID, groups, or notes...",
            g_ModernSearch,
            sizeof(g_ModernSearch)))
    {
        filterChanged = true;
    }

    ImGui::SameLine();

    if (ImGui::Button(
            "Clear Filters"))
    {
        g_ModernSearch[0] = '\0';
        g_ModernWidthFilter = 0;
        g_ModernHeightFilter = 0;
        g_ModernExcludedCustomGroups.clear();
        filterChanged = true;
    }

    ImGui::TextUnformatted(
        "Width");

    ImGui::SameLine();

    ImGui::SetNextItemWidth(
        5.0f * filterEm);

    if (ImGui::InputInt(
            "##WidthFilter",
            &g_ModernWidthFilter,
            0,
            0))
    {
        filterChanged = true;
    }

    ImGui::SameLine();

    ImGui::TextUnformatted(
        "Height");

    ImGui::SameLine();

    ImGui::SetNextItemWidth(
        5.0f * filterEm);

    if (ImGui::InputInt(
            "##HeightFilter",
            &g_ModernHeightFilter,
            0,
            0))
    {
        filterChanged = true;
    }

    if (g_ModernWidthFilter < 0)
        g_ModernWidthFilter = 0;

    if (g_ModernHeightFilter < 0)
        g_ModernHeightFilter = 0;

    ImGui::SameLine();

    if (ImGui::Checkbox(
            "Show replaced",
            &g_DisplayReplaced))
    {
        filterChanged = true;
    }

    ImGui::Spacing();

    const std::size_t excludedGroupCount =
        g_ModernExcludedCustomGroups.size();

    std::string excludedPreview =
        "None";

    if (excludedGroupCount > 0)
    {
        excludedPreview =
            std::to_string(
                excludedGroupCount) +
            " selected";
    }

    ImGui::SetNextItemWidth(
        12.0f * filterEm);

    if (ImGui::BeginCombo(
            "Exclude custom groups",
            excludedPreview.c_str()))
    {
        if (ImGui::MenuItem(
                "Clear exclusions",
                nullptr,
                false,
                excludedGroupCount > 0))
        {
            g_ModernExcludedCustomGroups.clear();
            filterChanged = true;
        }

        if (excludedGroupCount > 0)
            ImGui::Separator();

        ImGui::PushID(
            "CustomGroupExclusions");

        if (g_CustomTabs.empty())
        {
            ImGui::TextDisabled(
                "No custom groups");
        }
        else
        {
            for (const CustomTab& group :
                 g_CustomTabs)
            {
                bool excluded =
                    g_ModernExcludedCustomGroups.find(
                        group.name) !=
                    g_ModernExcludedCustomGroups.end();

                if (ImGui::Checkbox(
                        group.name.c_str(),
                        &excluded))
                {
                    if (excluded)
                    {
                        g_ModernExcludedCustomGroups.insert(
                            group.name);
                    }
                    else
                    {
                        g_ModernExcludedCustomGroups.erase(
                            group.name);
                    }

                    filterChanged = true;
                }
            }
        }

        ImGui::PopID();

        ImGui::EndCombo();
    }

    if (filterChanged)
    {
        g_ModernNeedPreviewPrime = true;
        g_ModernPreviewPrimeList.clear();
        g_ModernPreviewPrimeCursor = 0;
    }

    ImGui::Spacing();

    std::vector<std::size_t> visible;

    visible.reserve(
        g_Textures.size());

    for (std::size_t i = 0;
         i < g_Textures.size();
         ++i)
    {
        if (ModernTextureVisible(
                g_Textures[i]))
        {
            visible.push_back(i);
        }
    }

    g_ModernVisibleCount =
        visible.size();

    if (g_ModernNeedPreviewPrime)
    {
        ModernBeginPreviewPrime(
            visible);
    }

    ModernPumpPreviewPrimeQueue();

    if (g_ModernSelectedTextures.size() > 1)
    {
        ImGui::TextDisabled(
            "%zu textures shown  |  %zu selected",
            visible.size(),
            g_ModernSelectedTextures.size());
    }
    else
    {
        ImGui::TextDisabled(
            "%zu textures shown",
            visible.size());
    }

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable(
            "##TextureTableV2",
            7,
            tableFlags,
            ImVec2(
                0.0f,
                0.0f)))
    {
        ImGui::TableSetupScrollFreeze(
            0,
            1);

        ImGui::TableSetupColumn(
            "Preview",
            ImGuiTableColumnFlags_WidthFixed,
            4.2f * filterEm);

        ImGui::TableSetupColumn(
            "DID",
            ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableSetupColumn(
            "Size",
            ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableSetupColumn(
            "Format",
            ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableSetupColumn(
            "Replaced",
            ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableSetupColumn(
            "Groups",
            ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableSetupColumn(
            "Notes",
            ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableHeadersRow();

        const float thumbnailSize =
            2.0f * filterEm;

        const float rowHeight =
            max(
                ImGui::GetFrameHeight(),
                thumbnailSize) +
            ImGui::GetStyle()
                .CellPadding.y *
                2.0f;

        int targetVisibleRow = -1;

        if (g_ModernScrollToTextureIndex !=
            static_cast<std::size_t>(-1))
        {
            const auto target =
                std::find(
                    visible.begin(),
                    visible.end(),
                    g_ModernScrollToTextureIndex);

            if (target !=
                visible.end())
            {
                targetVisibleRow =
                    static_cast<int>(
                        std::distance(
                            visible.begin(),
                            target));
            }
            else
            {
                // The requested texture is not part of the current filtered
                // view, so do not keep a stale scroll target.
                g_ModernScrollToTextureIndex =
                    static_cast<std::size_t>(-1);
            }
        }

        ImGuiListClipper clipper;

        // Let ImGui measure the actual rendered row height.
        clipper.Begin(
            static_cast<int>(
                visible.size()));

        // A navigation target can be far outside the current viewport. Force
        // that exact row to be submitted; SetScrollHereY() below will then use
        // its true rendered position instead of an accumulated row estimate.
        if (targetVisibleRow >= 0)
        {
            clipper.IncludeItemByIndex(
                targetVisibleRow);
        }

        while (clipper.Step())
        {
            for (int row =
                     clipper.DisplayStart;
                 row <
                     clipper.DisplayEnd;
                 ++row)
            {
                const std::size_t visibleRow =
                    static_cast<std::size_t>(
                        row);

                const std::size_t textureIndex =
                    visible[
                        visibleRow];

                const TextureRecord& texture =
                    g_Textures[
                        textureIndex];

                const bool selected =
                    g_ModernSelectedTextures.find(
                        textureIndex) !=
                    g_ModernSelectedTextures.end();

                ImGui::TableNextRow(
                    0,
                    rowHeight);

                ImGui::TableSetColumnIndex(0);

                ImGui::PushID(
                    static_cast<int>(
                        textureIndex));

                if (ImGui::Selectable(
                        "##TextureRow",
                        selected,
                        ImGuiSelectableFlags_SpanAllColumns,
                        ImVec2(
                            0.0f,
                            rowHeight -
                                ImGui::GetStyle()
                                    .CellPadding.y)))
                {
                    ModernApplyTextureSelection(
                        visible,
                        visibleRow,
                        textureIndex);
                }

                if (textureIndex ==
                    g_ModernScrollToTextureIndex)
                {
                    // We are now looking at the target's real rendered
                    // position, so there is no accumulated row-height error.
                    ImGui::SetScrollHereY(
                        0.40f);

                    g_ModernScrollToTextureIndex =
                        static_cast<std::size_t>(-1);
                }



                if (ImGui::BeginPopupContextItem(
                        "##TextureContext"))
                {
                    if (g_ModernSelectedTextures.find(
                            textureIndex) ==
                        g_ModernSelectedTextures.end())
                    {
                        ModernSelectSingleTexture(
                            textureIndex);
                    }

                    if (ImGui::MenuItem(
                            "Go to DID Group"))
                    {
                        ModernOpenTextureIndex(
                            textureIndex);
                    }

                    if (ImGui::BeginMenu(
                            "Add to Custom Group"))
                    {
                        if (g_CustomTabs.empty())
                        {
                            ImGui::TextDisabled(
                                "No custom groups");
                        }

                        for (std::size_t groupIndex = 0;
                             groupIndex <
                                g_CustomTabs.size();
                             ++groupIndex)
                        {
                            const bool allInGroup =
                                ModernSelectionAllInGroup(
                                    groupIndex);

                            if (ImGui::MenuItem(
                                    g_CustomTabs[
                                        groupIndex]
                                        .name.c_str(),
                                    nullptr,
                                    allInGroup))
                            {
                                ModernAddSelectionToCustomGroup(
                                    groupIndex,
                                    textureIndex);
                            }
                        }

                        ImGui::Separator();

                        if (ImGui::MenuItem(
                                "Create New Group..."))
                        {
                            ModernCreateCustomGroup();
                        }

                        ImGui::EndMenu();
                    }

                    if (g_ActiveCustomTab >= 0)
                    {
                        if (ImGui::MenuItem(
                                "Remove from This Group"))
                        {
                            ModernRemoveSelectionFromActiveGroup(
                                textureIndex);
                        }
                    }

                    ImGui::Separator();

                    const bool editMultipleNotes =
                        g_ModernSelectedTextures.size() > 1;

                    if (ImGui::MenuItem(
                            editMultipleNotes
                                ? "Edit Notes for Selection..."
                                : "Edit Note..."))
                    {
                        ModernEditTextureNote(
                            textureIndex);
                    }

                    if (ModernHasReplacement(
                            texture))
                    {
                        if (ImGui::MenuItem(
                                "Remove Replacement"))
                        {
                            ModernRequestRemoveReplacement(
                                textureIndex);
                        }
                    }

                    ImGui::EndPopup();
                }

                ImGui::SameLine();

                ModernRenderTableThumbnail(
                    textureIndex,
                    texture);

                ImGui::TableSetColumnIndex(1);

                ImGui::TextUnformatted(
                    texture.did.c_str());

                ImGui::TableSetColumnIndex(2);

                ImGui::Text(
                    "%u x %u",
                    texture.width,
                    texture.height);

                ImGui::TableSetColumnIndex(3);

                ImGui::TextUnformatted(
                    ModernFormatName(
                        texture.pixelFormat));

                ImGui::TableSetColumnIndex(4);

                if (ModernHasReplacement(
                        texture))
                {
                    ImGui::TextColored(
                        MODERN_GREEN,
                        "Yes");
                }
                else
                {
                    ImGui::TextColored(
                        MODERN_RED,
                        "No");
                }

                ImGui::TableSetColumnIndex(5);

                const std::string groups =
                    ModernCustomGroupsText(
                        texture);

                if (!groups.empty())
                {
                    ImGui::TextUnformatted(
                        groups.c_str());
                }

                ImGui::TableSetColumnIndex(6);

                const auto note =
                    g_TextureNotes.find(
                        texture.did);

                if (note !=
                    g_TextureNotes.end())
                {
                    ImGui::TextUnformatted(
                        note->second.c_str());
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }
}

static bool ModernRenderPreviewCard(
    const char* id,
    const char* title,
    const ModernGpuImage& image,
    float width)
{
    ImGui::BeginChild(
        id,
        ImVec2(
            width,
            190.0f),
        ImGuiChildFlags_Borders);

    ImGui::TextUnformatted(
        title);

    ImGui::Separator();
    ImGui::Spacing();

    const ImVec2 available(
        max(
            1.0f,
            ImGui::GetContentRegionAvail().x),
        max(
            1.0f,
            ImGui::GetContentRegionAvail().y));

    ModernRenderGpuImageFit(
        image,
        available);

    const bool hovered =
        image.view != nullptr &&
        ImGui::IsWindowHovered();

    const bool clicked =
        hovered &&
        ImGui::IsMouseClicked(
            ImGuiMouseButton_Left);

    if (hovered)
    {
        ImGui::SetMouseCursor(
            ImGuiMouseCursor_Hand);
    }

    ImGui::EndChild();

    return clicked;
}

static void ModernRenderInspector()
{
    if (g_ModernSection ==
            ModernSection::LiveUi &&
        g_LiveSelectedGeneratedItem)
    {
        ModernRenderGeneratedInspector();
        return;
    }

    if (g_ModernSection ==
            ModernSection::LiveUi &&
        g_LiveSelectedTextureIndex !=
            static_cast<std::size_t>(-1) &&
        g_LiveSelectedTextureIndex <
            g_Textures.size())
    {
        g_SelectedRow =
            static_cast<int>(
                g_LiveSelectedTextureIndex);
    }

    ImGui::TextColored(
        MODERN_GOLD,
        "Inspector");

    ImGui::Separator();
    ImGui::Spacing();

    if (g_SelectedRow < 0 ||
        static_cast<std::size_t>(
            g_SelectedRow) >=
            g_Textures.size())
    {
        ImGui::TextDisabled(
            "Select a texture to inspect it.");

        return;
    }

    const std::size_t textureIndex =
        static_cast<std::size_t>(
            g_SelectedRow);

    const TextureRecord& texture =
        g_Textures[textureIndex];

    ModernEnsureInspectorImages(
        textureIndex);

    ImGui::Text(
        "Texture %s",
        texture.did.c_str());

    ImGui::Spacing();

    // Calculate this ONCE before either child is created.
    // Previously the second card recalculated from the remaining
    // width and therefore became much narrower.
    const float previewCardWidth =
        max(
            80.0f,
            (ImGui::GetContentRegionAvail().x -
             8.0f) /
                2.0f);

    const bool originalClicked =
        ModernRenderPreviewCard(
            "##OriginalPreview",
            "Original",
            g_ModernInspectorOriginal,
            previewCardWidth);

    ImGui::SameLine();

    const bool replacementClicked =
        ModernRenderPreviewCard(
            "##ReplacementPreview",
            "Replacement",
            g_ModernInspectorReplacement,
            previewCardWidth);

    if (originalClicked ||
        replacementClicked)
    {
        ModernOpenTextureIndex(
            textureIndex);
    }

    if (ImGui::Button(
            "Copy Original",
            ImVec2(
                previewCardWidth,
                ModernButtonHeight(32.0f))))
    {
        CopySelectedTextureToClipboard(
            g_MainWindow);
    }

    ImGui::SameLine();

    const bool hasReplacement =
        ModernHasReplacement(
            texture);

    ImGui::BeginDisabled(
        !hasReplacement);

    if (ImGui::Button(
            "Copy Replacement",
            ImVec2(
                previewCardWidth,
                ModernButtonHeight(32.0f))))
    {
        CopySelectedReplacementTextureToClipboard(
            g_MainWindow);
    }

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled(
        "DID");

    ImGui::SameLine(
        105.0f);

    ImGui::TextUnformatted(
        texture.did.c_str());

    ImGui::TextDisabled(
        "Size");

    ImGui::SameLine(
        105.0f);

    ImGui::Text(
        "%u x %u",
        texture.width,
        texture.height);

    ImGui::TextDisabled(
        "Format");

    ImGui::SameLine(
        105.0f);

    ImGui::Text(
        "%s (0x%08X)",
        ModernFormatName(
            texture.pixelFormat),
        texture.pixelFormat);

    ImGui::TextDisabled(
        "Payload");

    ImGui::SameLine(
        105.0f);

    ImGui::Text(
        "%u bytes",
        texture.imageSize);

    ImGui::TextDisabled(
        "Replaced");

    ImGui::SameLine(
        105.0f);

    if (ModernHasReplacement(
            texture))
    {
        ImGui::TextColored(
            MODERN_GREEN,
            "Yes");
    }
    else
    {
        ImGui::TextColored(
            MODERN_RED,
            "No");
    }

    ImGui::Spacing();

    ImGui::TextDisabled(
        "Notes");

    const auto note =
        g_TextureNotes.find(
            texture.did);

    ImGui::TextWrapped(
        "%s",
        note == g_TextureNotes.end()
            ? "(none)"
            : note->second.c_str());

    const bool editMultipleNotes =
        g_ModernSelectedTextures.size() > 1 &&
        g_ModernSelectedTextures.find(
            textureIndex) !=
        g_ModernSelectedTextures.end();

    char editNoteLabel[96] = {};

    if (editMultipleNotes)
    {
        sprintf_s(
            editNoteLabel,
            "Edit Notes for %zu Selected",
            g_ModernSelectedTextures.size());
    }
    else
    {
        strcpy_s(
            editNoteLabel,
            sizeof(editNoteLabel),
            "Edit Note");
    }

    if (ImGui::Button(
            editNoteLabel,
            ImVec2(
                -1.0f,
                ModernButtonHeight(32.0f))))
    {
        ModernEditTextureNote(
            textureIndex);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(
            "Copy DID",
            ImVec2(
                -1.0f,
                ModernButtonHeight(36.0f))))
    {
        CopySelectedDidToClipboard(
            g_MainWindow);
    }

    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImVec4(
            0.64f,
            0.49f,
            0.24f,
            1.00f));

    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(
            0.78f,
            0.61f,
            0.31f,
            1.00f));

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(
            0.06f,
            0.055f,
            0.045f,
            1.00f));

    if (ImGui::Button(
            "Replace PNG...",
            ImVec2(
                -1.0f,
                ModernButtonHeight(40.0f))))
    {
        if (ReplaceSelectedTexture(
                g_MainWindow))
        {
            ModernRefreshReplacementSet();
            ModernInvalidateInspectorImages();

            g_ModernReplacementNotice =
                "Replacement applied.";

            g_ModernReplacementNoticeUntil =
                ImGui::GetTime() + 3.0;

            if (g_LiveWindow != nullptr &&
                IsWindow(g_LiveWindow))
            {
                LiveReloadScene();
            }

            if (g_ModernSection ==
                ModernSection::LiveUi)
            {
                LiveReloadScene();
                ModernMarkLiveCanvasDirty();
            }
        }
    }

    ImGui::PopStyleColor(3);

    if (ModernHasReplacement(
            texture))
    {
        if (ImGui::Button(
                "Revert to Original",
                ImVec2(
                    -1.0f,
                    ModernButtonHeight(36.0f))))
        {
            ModernRequestRemoveReplacement(
                textureIndex);
        }
    }
    if (!g_ModernReplacementNotice.empty() &&
        ImGui::GetTime() <
            g_ModernReplacementNoticeUntil)
    {
        ImGui::Spacing();

        ImGui::TextColored(
            MODERN_GREEN,
            "%s",
            g_ModernReplacementNotice.c_str());
    }

}

static void ModernRenderLiveUi()
{
    const bool capturing =
        g_LiveMirrorRunning.load(
            std::memory_order_acquire);

    const bool vanillaRequired =
        LiveMirrorCurrentStatus() ==
            LiveMirrorStatus::VanillaRequired;

    const bool snapshotAvailable =
        !g_LiveSnapshotPath.empty();

    ImGui::TextColored(
        MODERN_GOLD,
        "Live UI");

    ImGui::SameLine();

    ImGui::TextDisabled(
        capturing
            ? "- Capturing snapshot..."
            : vanillaRequired
                ? "- Vanilla required"
                : snapshotAvailable
                    ? "- Snapshot loaded"
                    : "- No snapshot");

    ImGui::SameLine();

    if (ModernSnapshotButton(
            capturing))
    {
        LiveMirrorStart(
            g_MainWindow);
    }

    // Right-aligned Vanilla/Replacements mode selector. This only controls
    // how the already-captured snapshot is rendered in the Manager.
    const float em =
        ImGui::GetFontSize();

    const float rightControlsWidth =
        13.5f * em;

    ImGui::SameLine();

    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX() +
                ImGui::GetStyle().ItemSpacing.x,
            ImGui::GetWindowWidth() -
                rightControlsWidth -
                ImGui::GetStyle().WindowPadding.x));

    bool replacements =
        g_LiveUseReplacements;

    if (ModernReplacementSelector(
            &replacements))
    {
        g_LiveUseReplacements =
            replacements;

        // Rebuild the current snapshot without asking AC for a new capture.
        g_LiveTextureCache.clear();
        g_LiveGeneratedCache.clear();

        LiveRebuildCanvas();

        ModernMarkLiveCanvasDirty();

        ModernInvalidateGeneratedImages();
        ModernInvalidateInspectorImages();

        if (g_LiveSelectedGeneratedItem)
        {
            g_ModernGeneratedRequestedHash = 0;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (vanillaRequired)
    {
        ImGui::TextColored(
            MODERN_GOLD,
            "Snapshot rejected - AC Customs is using a replacement theme.");

        ImGui::TextDisabled(
            "Switch AC Customs in-game to Vanilla, then click Take Snapshot. "
            "The previous snapshot remains available.");

        ImGui::Spacing();
    }
    else if (capturing)
    {
        ImGui::TextDisabled(
            "Capturing one UI snapshot from AC...");
        ImGui::Spacing();
    }

    if (g_ModernLiveView != nullptr ||
        !g_LiveCanvas.empty())
    {
        ImGui::TextDisabled(
            "Mouse wheel: zoom   Right-drag: pan");

        const float zoomButtonWidth =
            max(
                28.0f,
                ImGui::GetFrameHeight());

        const float fitButtonWidth =
            max(
                42.0f,
                ImGui::CalcTextSize("Fit").x +
                    ImGui::GetStyle().FramePadding.x * 2.0f);

        char zoomText[32] = {};
        snprintf(
            zoomText,
            sizeof(zoomText),
            "%.1fx",
            g_ModernLiveZoom);

        const float zoomTextWidth =
            ImGui::CalcTextSize(
                zoomText).x;

        const float controlsWidth =
            zoomButtonWidth * 2.0f +
            fitButtonWidth +
            zoomTextWidth +
            ImGui::GetStyle().ItemSpacing.x * 3.0f;

        ImGui::SameLine();

        ImGui::SetCursorPosX(
            max(
                ImGui::GetCursorPosX() +
                    ImGui::GetStyle().ItemSpacing.x,
                ImGui::GetWindowWidth() -
                    ImGui::GetStyle().WindowPadding.x -
                    controlsWidth));

        if (ImGui::Button(
                "-##LiveZoomOut",
                ImVec2(
                    zoomButtonWidth,
                    0.0f)))
        {
            const float oldZoom =
                g_ModernLiveZoom;

            g_ModernLiveZoom =
                max(
                    1.0f,
                    g_ModernLiveZoom /
                        1.25f);

            if (oldZoom > 0.0f)
            {
                const float ratio =
                    g_ModernLiveZoom /
                    oldZoom;

                g_ModernLivePan.x *= ratio;
                g_ModernLivePan.y *= ratio;
            }
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(
            zoomText);
        ImGui::SameLine();

        if (ImGui::Button(
                "+##LiveZoomIn",
                ImVec2(
                    zoomButtonWidth,
                    0.0f)))
        {
            const float oldZoom =
                g_ModernLiveZoom;

            g_ModernLiveZoom =
                min(
                    8.0f,
                    g_ModernLiveZoom *
                        1.25f);

            if (oldZoom > 0.0f)
            {
                const float ratio =
                    g_ModernLiveZoom /
                    oldZoom;

                g_ModernLivePan.x *= ratio;
                g_ModernLivePan.y *= ratio;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Fit##LiveZoomFit",
                ImVec2(
                    fitButtonWidth,
                    0.0f)))
        {
            g_ModernLiveZoom = 1.0f;
            g_ModernLivePan =
                ImVec2(0.0f, 0.0f);
            g_ModernLivePanning = false;
        }

        ImGui::Spacing();
    }

    ImGui::BeginChild(
        "##IntegratedLiveCanvas",
        ImVec2(
            0.0f,
            0.0f),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

    if (!ModernUploadLiveCanvas())
    {
        ImGui::Dummy(
            ImVec2(
                0.0f,
                40.0f));

        ImGui::TextColored(
            MODERN_GOLD,
            vanillaRequired
                ? "Snapshot unavailable."
                : capturing
                    ? "Capturing UI snapshot..."
                    : "No Live UI snapshot loaded.");

        ImGui::Spacing();

        if (vanillaRequired)
        {
            ImGui::TextWrapped(
                "Switch AC Customs in-game to Vanilla, then click "
                "Take Snapshot.");
        }
        else if (capturing)
        {
            ImGui::TextWrapped(
                "AC Customs is collecting the current UI. "
                "The snapshot will appear here when capture finishes.");
        }
        else
        {
            ImGui::TextWrapped(
                "1. Log in to Asheron's Call with AC Customs enabled and set "
                "to Vanilla.\n"
                "2. Open and arrange the in-game UI elements you want to "
                "inspect.\n"
                "3. Return to Live UI and click Take Snapshot.");
        }

        ImGui::Spacing();

        ImGui::TextDisabled(
            "Roots: %d   Textures: %d   Generated: %d",
            g_LiveRootsRendered,
            g_LiveRendered,
            g_LiveGeneratedRendered);

        ImGui::EndChild();
        return;
    }

    const ImVec2 viewportSize(
        max(
            1.0f,
            ImGui::GetContentRegionAvail().x),
        max(
            1.0f,
            ImGui::GetContentRegionAvail().y));

    const ImVec2 viewportMin =
        ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(
        "##LiveCanvasInteraction",
        viewportSize);

    const bool viewportHovered =
        ImGui::IsItemHovered();

    const float fitScaleX =
        viewportSize.x /
        static_cast<float>(
            g_LiveCanvasWidth);

    const float fitScaleY =
        viewportSize.y /
        static_cast<float>(
            g_LiveCanvasHeight);

    const float fitScale =
        max(
            0.01f,
            min(
                fitScaleX,
                fitScaleY));

    g_ModernLiveZoom =
        max(
            1.0f,
            min(
                8.0f,
                g_ModernLiveZoom));

    auto ImageSizeForZoom =
        [&](float zoom)
        {
            return ImVec2(
                static_cast<float>(
                    g_LiveCanvasWidth) *
                    fitScale * zoom,
                static_cast<float>(
                    g_LiveCanvasHeight) *
                    fitScale * zoom);
        };

    auto ClampPan =
        [&](const ImVec2& imageSize)
        {
            const float maxPanX =
                max(
                    0.0f,
                    (imageSize.x -
                     viewportSize.x) *
                        0.5f);

            const float maxPanY =
                max(
                    0.0f,
                    (imageSize.y -
                     viewportSize.y) *
                        0.5f);

            if (maxPanX <= 0.0f)
            {
                g_ModernLivePan.x = 0.0f;
            }
            else
            {
                g_ModernLivePan.x =
                    max(
                        -maxPanX,
                        min(
                            maxPanX,
                            g_ModernLivePan.x));
            }

            if (maxPanY <= 0.0f)
            {
                g_ModernLivePan.y = 0.0f;
            }
            else
            {
                g_ModernLivePan.y =
                    max(
                        -maxPanY,
                        min(
                            maxPanY,
                            g_ModernLivePan.y));
            }
        };

    ImVec2 imageSize =
        ImageSizeForZoom(
            g_ModernLiveZoom);

    ClampPan(
        imageSize);

    auto ImageMinForCurrentView =
        [&]()
        {
            return ImVec2(
                viewportMin.x +
                    (viewportSize.x -
                     imageSize.x) *
                        0.5f +
                    g_ModernLivePan.x,
                viewportMin.y +
                    (viewportSize.y -
                     imageSize.y) *
                        0.5f +
                    g_ModernLivePan.y);
        };

    ImVec2 imageMin =
        ImageMinForCurrentView();

    ImVec2 imageMax(
        imageMin.x + imageSize.x,
        imageMin.y + imageSize.y);

    const ImVec2 mouse =
        ImGui::GetMousePos();

    const bool mouseInsideImageBeforeInput =
        viewportHovered &&
        mouse.x >= imageMin.x &&
        mouse.y >= imageMin.y &&
        mouse.x < imageMax.x &&
        mouse.y < imageMax.y;

    const float wheel =
        ImGui::GetIO().MouseWheel;

    if (mouseInsideImageBeforeInput &&
        wheel != 0.0f)
    {
        const float normalizedX =
            (mouse.x - imageMin.x) /
            max(
                1.0f,
                imageSize.x);

        const float normalizedY =
            (mouse.y - imageMin.y) /
            max(
                1.0f,
                imageSize.y);

        const float oldZoom =
            g_ModernLiveZoom;

        if (wheel > 0.0f)
        {
            g_ModernLiveZoom =
                min(
                    8.0f,
                    g_ModernLiveZoom *
                        1.25f);
        }
        else
        {
            g_ModernLiveZoom =
                max(
                    1.0f,
                    g_ModernLiveZoom /
                        1.25f);
        }

        if (g_ModernLiveZoom != oldZoom)
        {
            imageSize =
                ImageSizeForZoom(
                    g_ModernLiveZoom);

            const ImVec2 centeredMin(
                viewportMin.x +
                    (viewportSize.x -
                     imageSize.x) *
                        0.5f,
                viewportMin.y +
                    (viewportSize.y -
                     imageSize.y) *
                        0.5f);

            const ImVec2 desiredMin(
                mouse.x -
                    normalizedX *
                        imageSize.x,
                mouse.y -
                    normalizedY *
                        imageSize.y);

            g_ModernLivePan.x =
                desiredMin.x -
                centeredMin.x;

            g_ModernLivePan.y =
                desiredMin.y -
                centeredMin.y;

            ClampPan(
                imageSize);

            imageMin =
                ImageMinForCurrentView();

            imageMax =
                ImVec2(
                    imageMin.x +
                        imageSize.x,
                    imageMin.y +
                        imageSize.y);
        }
    }

    if (viewportHovered &&
        ImGui::IsMouseClicked(
            ImGuiMouseButton_Right))
    {
        g_ModernLivePanning =
            g_ModernLiveZoom > 1.0f;
    }

    if (!ImGui::IsMouseDown(
            ImGuiMouseButton_Right))
    {
        g_ModernLivePanning = false;
    }

    if (g_ModernLivePanning)
    {
        const ImVec2 delta =
            ImGui::GetIO().MouseDelta;

        g_ModernLivePan.x +=
            delta.x;

        g_ModernLivePan.y +=
            delta.y;

        ClampPan(
            imageSize);

        imageMin =
            ImageMinForCurrentView();

        imageMax =
            ImVec2(
                imageMin.x +
                    imageSize.x,
                imageMin.y +
                    imageSize.y);
    }

    ImDrawList* draw =
        ImGui::GetWindowDrawList();

    const ImTextureID textureId =
        static_cast<ImTextureID>(
            reinterpret_cast<
                std::uintptr_t>(
                    g_ModernLiveView));

    draw->AddImage(
        textureId,
        imageMin,
        imageMax);

    const bool imageHovered =
        viewportHovered &&
        mouse.x >= imageMin.x &&
        mouse.y >= imageMin.y &&
        mouse.x < imageMax.x &&
        mouse.y < imageMax.y;

    LiveHitResult hoverHit;

    if (imageHovered)
    {
        const float normalizedX =
            (mouse.x - imageMin.x) /
            max(
                1.0f,
                imageSize.x);

        const float normalizedY =
            (mouse.y - imageMin.y) /
            max(
                1.0f,
                imageSize.y);

        const int sceneX =
            max(
                0,
                min(
                    g_LiveCanvasWidth - 1,
                    static_cast<int>(
                        normalizedX *
                        g_LiveCanvasWidth)));

        const int sceneY =
            max(
                0,
                min(
                    g_LiveCanvasHeight - 1,
                    static_cast<int>(
                        normalizedY *
                        g_LiveCanvasHeight)));

        hoverHit =
            LiveHitTestScene(
                sceneX,
                sceneY);

        const std::uint32_t newDid =
            hoverHit.hit
                ? hoverHit.did
                : 0;

        const std::uint32_t newControl =
            hoverHit.hit
                ? hoverHit.control
                : 0;

        const bool controlChanged =
            newControl !=
                g_LiveHoveredControl;

        g_LiveHoveredDid =
            newDid;

        g_LiveHoveredControl =
            newControl;

        g_LiveHaveHoveredSceneRect =
            hoverHit.hit;

        if (hoverHit.hit)
        {
            g_LiveHoveredSceneRect =
                hoverHit.sceneRect;
        }

        if (controlChanged)
        {
            LiveRebuildCanvas();
            ModernMarkLiveCanvasDirty();
        }

        if (hoverHit.hit &&
            !g_ModernLivePanning &&
            ImGui::IsMouseClicked(
                ImGuiMouseButton_Left))
        {
            ModernSelectLiveHit(
                hoverHit);

            ModernInvalidateGeneratedImages();

            if (!hoverHit.generatedItem)
                ModernInvalidateInspectorImages();
        }
    }
    else
    {
        const bool hadControl =
            g_LiveHoveredControl != 0;

        g_LiveHoveredDid = 0;
        g_LiveHoveredControl = 0;

        g_LiveHaveHoveredSceneRect =
            false;

        if (hadControl)
        {
            LiveRebuildCanvas();
            ModernMarkLiveCanvasDirty();
        }
    }

    if (g_ModernLivePanning)
    {
        ImGui::SetMouseCursor(
            ImGuiMouseCursor_Hand);
    }
    else if (imageHovered &&
             hoverHit.hit)
    {
        ImGui::SetMouseCursor(
            ImGuiMouseCursor_Hand);
    }

    auto DrawSceneRect =
        [&](const LiveRectI& rect,
            ImU32 color,
            float thickness)
        {
            const float left =
                imageMin.x +
                (
                    static_cast<float>(
                        rect.l) /
                    g_LiveCanvasWidth
                ) *
                imageSize.x;

            const float top =
                imageMin.y +
                (
                    static_cast<float>(
                        rect.t) /
                    g_LiveCanvasHeight
                ) *
                imageSize.y;

            const float right =
                imageMin.x +
                (
                    static_cast<float>(
                        rect.r) /
                    g_LiveCanvasWidth
                ) *
                imageSize.x;

            const float bottom =
                imageMin.y +
                (
                    static_cast<float>(
                        rect.b) /
                    g_LiveCanvasHeight
                ) *
                imageSize.y;

            draw->AddRect(
                ImVec2(left, top),
                ImVec2(right, bottom),
                color,
                0.0f,
                0,
                thickness);
        };

    if (g_LiveShowBounds &&
        g_LiveHaveDesktopRect)
    {
        for (const LiveRootScene& root :
             g_LiveRoots)
        {
            LiveRectI rect;

            rect.l =
                root.rect.l -
                g_LiveDesktopRect.l;

            rect.t =
                root.rect.t -
                g_LiveDesktopRect.t;

            rect.r =
                root.rect.r -
                g_LiveDesktopRect.l +
                1;

            rect.b =
                root.rect.b -
                g_LiveDesktopRect.t +
                1;

            DrawSceneRect(
                rect,
                IM_COL32(
                    215,
                    80,
                    210,
                    180),
                1.0f);
        }
    }

    if (g_LiveHaveHoveredSceneRect)
    {
        DrawSceneRect(
            g_LiveHoveredSceneRect,
            IM_COL32(
                50,
                190,
                235,
                255),
            2.0f);
    }

    if (g_LiveHaveSelectedSceneRect)
    {
        DrawSceneRect(
            g_LiveSelectedSceneRect,
            IM_COL32(
                218,
                175,
                95,
                255),
            2.5f);
    }

    ImGui::EndChild();
}

static void ModernRenderInfo()
{
    const float em =
        ImGui::GetFontSize();

    ImGui::TextColored(
        MODERN_GOLD,
        "AC Customs - Quick Guide");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped(
        "AC Customs lets you browse compatible BGR/BGRA texture resources "
        "directly from the selected client DAT, preview and replace them, "
        "inspect the currently visible game UI, organize textures into custom "
        "groups, and import or export UI packs.");

    ImGui::Spacing();

    if (!g_DatPathUtf8.empty())
    {
        ImGui::TextDisabled(
            "DAT source: %s",
            g_DatPathUtf8.c_str());

        if (!g_DatScanSummary.empty())
        {
            ImGui::TextDisabled(
                "%s",
                g_DatScanSummary.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button(
                "Change DAT...",
                ImVec2(
                    150.0f,
                    ModernButtonHeight(
                        34.0f))))
        {
            ModernChooseAndLoadDat();
        }
    }
    else
    {
        ImGui::TextDisabled(
            "No client DAT is currently loaded.");

        ImGui::Spacing();

        if (ImGui::Button(
                "Open DAT...",
                ImVec2(
                    150.0f,
                    ModernButtonHeight(
                        34.0f))))
        {
            ModernChooseAndLoadDat();
        }
    }

    if (g_DatLoadNoticeError &&
        !g_DatLoadNotice.empty())
    {
        ImGui::Spacing();

        ImGui::TextColored(
            MODERN_RED,
            "%s",
            g_DatLoadNotice.c_str());
    }

    ImGui::Spacing();
    ImGui::Spacing();

    auto Section =
        [&](const char* number,
            const char* title,
            const char* body)
        {
            ImGui::TextColored(
                MODERN_GOLD,
                "%s",
                number);

            ImGui::SameLine();

            ImGui::TextUnformatted(
                title);

            ImGui::Indent(
                2.0f * em);

            ImGui::PushTextWrapPos(
                ImGui::GetCursorPosX() +
                max(
                    12.0f * em,
                    ImGui::GetContentRegionAvail().x));

            ImGui::TextDisabled(
                "%s",
                body);

            ImGui::PopTextWrapPos();
            ImGui::Unindent(
                2.0f * em);

            ImGui::Spacing();
        };

    Section(
        "1.",
        "Browse textures",
        "Use All Textures or a DID group on the left. This experimental build "
        "indexes compatible 0x06 BGR/BGRA textures directly from the selected "
        "client DAT. Search by DID or notes, use the width/height filters, and "
        "click a texture row to inspect it on the right.");

    Section(
        "2.",
        "Replace a texture",
        "Select a texture, then use Replace PNG... in the Inspector. The "
        "Original and Replacement previews show the current state. Use Revert "
        "to Original to remove an existing replacement.");

    Section(
        "3.",
        "Use Live UI",
        "Live UI uses on-demand snapshots instead of continuous streaming. "
        "To capture the current interface: 1) Log in to Asheron's Call with "
        "AC Customs enabled and set to Vanilla. 2) Open and arrange the in-game "
        "UI elements you want to inspect. 3) Return to Live UI and click Take "
        "Snapshot. If a replacement theme is active, the snapshot is rejected "
        "without changing the current view. The Vanilla / Replacements selector "
        "in this app only changes how the captured snapshot is displayed; it "
        "does not change the in-game theme.");

    Section(
        "4.",
        "Inspect the live interface",
        "Click a texture directly in Live UI to populate the Inspector. Use the "
        "mouse wheel over the snapshot to zoom and right-drag to pan; Fit returns "
        "to the full captured view. For generated or layered icons, AC Customs "
        "analyzes the image in the "
        "background against same-size DAT textures and suggests likely source "
        "layers. These suggestions are heuristic and may not always be exact. "
        "Click a suggested texture to jump directly to it in the texture browser.");

    Section(
        "5.",
        "Create custom groups",
        "Use + New Group under Custom Groups. Ctrl-click individual textures or "
        "Shift-click a range, then right-click the selection and choose Add to "
        "Custom Group. Multi-selected textures can also share one note through "
        "Edit Notes for Selection. Use Exclude custom groups above the texture table "
        "to hide textures belonging to one or more custom groups. When viewing a "
        "custom group, right-click a texture to remove the current selection from "
        "that group or use Go to DID Group.");

    Section(
        "6.",
        "Import or export a UI pack",
        "Open Import / Export on the left to import an existing UI pack or "
        "export your current replacement workspace.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(
        MODERN_GREEN,
        "Tip:");

    ImGui::SameLine();

    ImGui::TextWrapped(
        "Use Live UI to identify what you want to change, then jump to that "
        "texture in the browser and make the replacement from the Inspector.");
}

static void ModernSetPackNotice(
    const std::string& text,
    bool error)
{
    g_ModernPackNotice =
        text;

    g_ModernPackNoticeError =
        error;

    g_ModernPackNoticeUntil =
        ImGui::GetTime() + 5.0;
}


static bool ModernCommitImportedPack()
{
    std::wstring error;

    if (!StageImportedWorkspace(
            g_ModernPendingImportManifest,
            g_ModernPendingImportFiles,
            error) ||
        !CommitImportedWorkspace(
            error))
    {
        DeleteDirectoryTree(
            g_AppPaths.importStageDirectory);

        ModernSetPackNotice(
            FromWide(error),
            true);

        return false;
    }

    g_CurrentPackName =
        g_ModernPendingImportManifest.name;

    g_CurrentPackAuthor =
        g_ModernPendingImportManifest.author;

    g_CurrentPackDescription =
        g_ModernPendingImportManifest.description;

    g_CurrentPackSource =
        g_ModernPendingImportSource;

    ModernRefreshReplacementSet();
    ModernInvalidateInspectorImages();
    ModernMarkLiveCanvasDirty();

    ModernSetPackNotice(
        "Imported " +
            g_CurrentPackName +
            " (" +
            std::to_string(
                g_ModernPendingImportManifest
                    .textures.size()) +
            " replacements).",
        false);

    g_ModernPendingImportFiles.clear();
    g_ModernPendingImportSource.clear();
    g_ModernPendingImportManifest =
        AcuiManifest();

    return true;
}


static void ModernBeginImportPack()
{
    std::wstring source;

    if (!ChooseAcuiOpen(
            g_MainWindow,
            source))
    {
        return;
    }

    std::unordered_map<
        std::string,
        std::vector<BYTE>>
        files;

    std::wstring error;

    if (!LoadStoredZip(
            source,
            files,
            error))
    {
        ModernSetPackNotice(
            FromWide(error),
            true);

        return;
    }

    const auto manifestIt =
        files.find(
            "manifest.json");

    if (manifestIt ==
        files.end())
    {
        ModernSetPackNotice(
            "The selected .acui does not contain manifest.json.",
            true);

        return;
    }

    AcuiManifest manifest;

    if (!ParseAcuiManifest(
            manifestIt->second,
            manifest,
            error) ||
        !ValidateAcuiPack(
            manifest,
            files,
            error))
    {
        ModernSetPackNotice(
            FromWide(error),
            true);

        return;
    }

    g_ModernPendingImportSource =
        source;

    g_ModernPendingImportManifest =
        manifest;

    g_ModernPendingImportFiles =
        std::move(files);

    const std::vector<std::size_t>
        existing =
            GetReplacementTextureIndices();

    if (!existing.empty())
    {
        ModernRequestDialog(
            ModernDialogKind::ImportPackConfirm);

        return;
    }

    ModernCommitImportedPack();
}


static bool ModernExportCurrentPack(
    const std::string& packName)
{
    const std::vector<std::size_t>
        textureIndices =
            GetReplacementTextureIndices();

    if (textureIndices.empty())
    {
        g_ModernDialogError =
            "There are no replacement textures to export.";

        return false;
    }

    std::wstring destination;

    if (!ChooseAcuiSave(
            g_MainWindow,
            packName,
            destination))
    {
        return false;
    }

    std::vector<StoredZipEntry>
        zipEntries;

    StoredZipEntry manifestEntry;
    manifestEntry.name =
        "manifest.json";

    const std::string manifestJson =
        BuildAcuiManifestJson(
            packName,
            g_CurrentPackAuthor,
            g_CurrentPackDescription,
            textureIndices);

    manifestEntry.data.assign(
        manifestJson.begin(),
        manifestJson.end());

    zipEntries.push_back(
        std::move(
            manifestEntry));

    for (std::size_t textureIndex :
         textureIndices)
    {
        const TextureRecord& texture =
            g_Textures[
                textureIndex];

        if (!IsPreviewable(
                texture))
        {
            g_ModernDialogError =
                "Cannot export unsupported replacement format for DID " +
                texture.did +
                ".";

            return false;
        }

        StoredZipEntry entry;
        entry.name =
            "textures/" +
            texture.did +
            ".rgb";

        if (!ReadBinaryFile(
                ReplacementRawPath(
                    texture),
                entry.data) ||
            entry.data.size() !=
                texture.imageSize)
        {
            g_ModernDialogError =
                "Replacement data is missing or has the wrong byte count for DID " +
                texture.did +
                ".";

            return false;
        }

        zipEntries.push_back(
            std::move(entry));
    }

    std::wstring error;

    if (!WriteStoredZip(
            destination,
            zipEntries,
            error))
    {
        g_ModernDialogError =
            FromWide(error);

        return false;
    }

    g_CurrentPackName =
        packName;

    g_CurrentPackSource =
        destination;

    ModernSetPackNotice(
        "Exported " +
            std::to_string(
                textureIndices.size()) +
            " replacements to " +
            packName +
            ".",
        false);

    return true;
}


static void ModernRenderPacks()
{
    ImGui::TextColored(
        MODERN_GOLD,
        "UI Packs");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped(
        "Import and export the ACUI replacement workspace. "
        "File selection uses the normal Windows picker; naming, "
        "confirmation, and status stay inside AC Customs.");

    ImGui::Spacing();

    if (ImGui::Button(
            "Import UI Pack...",
            ImVec2(
                190.0f,
                ModernButtonHeight(38.0f))))
    {
        ModernBeginImportPack();
    }

    ImGui::SameLine();

    if (ImGui::Button(
            "Export UI Pack...",
            ImVec2(
                190.0f,
                ModernButtonHeight(38.0f))))
    {
        const std::vector<std::size_t>
            replacements =
                GetReplacementTextureIndices();

        if (replacements.empty())
        {
            ModernSetPackNotice(
                "There are no replacement textures to export.",
                true);
        }
        else
        {
            ModernSetDialogText(
                g_CurrentPackName.empty()
                    ? "Untitled UI Pack"
                    : g_CurrentPackName);

            ModernRequestDialog(
                ModernDialogKind::ExportPack);
        }
    }

    if (!g_ModernPackNotice.empty() &&
        ImGui::GetTime() <
            g_ModernPackNoticeUntil)
    {
        ImGui::Spacing();

        ImGui::TextColored(
            g_ModernPackNoticeError
                ? MODERN_RED
                : MODERN_GREEN,
            "%s",
            g_ModernPackNotice.c_str());
    }
}


static void ModernRenderDialogs()
{
    const char* popupName =
        nullptr;

    switch (g_ModernDialogKind)
    {
        case ModernDialogKind::CreateGroup:
            popupName =
                "Create Custom Group";
            break;

        case ModernDialogKind::RenameGroup:
            popupName =
                "Rename Custom Group";
            break;

        case ModernDialogKind::EditNote:
            popupName =
                g_ModernDialogNoteTextureIndices.size() > 1
                    ? "Edit Texture Notes"
                    : "Edit Texture Note";
            break;

        case ModernDialogKind::DeleteGroup:
            popupName =
                "Delete Custom Group";
            break;

        case ModernDialogKind::RemoveReplacement:
            popupName =
                "Revert to Original";
            break;

        case ModernDialogKind::ExportPack:
            popupName =
                "Export UI Pack";
            break;

        case ModernDialogKind::ImportPackConfirm:
            popupName =
                "Import UI Pack";
            break;

        default:
            break;
    }

    if (popupName == nullptr)
        return;

    if (g_ModernDialogOpenPending)
    {
        ImGui::OpenPopup(
            popupName);

        g_ModernDialogOpenPending =
            false;
    }

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x +
                viewport->WorkSize.x *
                    0.5f,
            viewport->WorkPos.y +
                viewport->WorkSize.y *
                    0.5f),
        ImGuiCond_Appearing,
        ImVec2(
            0.5f,
            0.5f));

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(
            390.0f,
            0.0f),
        ImVec2(
            560.0f,
            460.0f));

    bool keepOpen =
        true;

    if (!ImGui::BeginPopupModal(
            popupName,
            &keepOpen,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!keepOpen)
        {
            g_ModernDialogKind =
                ModernDialogKind::None;
        }

        return;
    }

    const float buttonGap =
        ImGui::GetStyle().ItemSpacing.x;

    const float halfButton =
        (ImGui::GetContentRegionAvail().x -
         buttonGap) /
        2.0f;

    if (g_ModernDialogKind ==
            ModernDialogKind::CreateGroup ||
        g_ModernDialogKind ==
            ModernDialogKind::RenameGroup)
    {
        ImGui::TextWrapped(
            g_ModernDialogKind ==
                    ModernDialogKind::CreateGroup
                ? "Create a custom group for organizing textures."
                : "Rename this custom group.");

        ImGui::Spacing();

        ImGui::SetNextItemWidth(
            -1.0f);

        const bool submitByEnter =
            ImGui::InputText(
                "##GroupName",
                g_ModernDialogText,
                sizeof(
                    g_ModernDialogText),
                ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere(
                -1);
        }

        if (!g_ModernDialogError.empty())
        {
            ImGui::Spacing();

            ImGui::TextColored(
                MODERN_RED,
                "%s",
                g_ModernDialogError.c_str());
        }

        ImGui::Spacing();

        const bool save =
            submitByEnter ||
            ImGui::Button(
                g_ModernDialogKind ==
                        ModernDialogKind::CreateGroup
                    ? "Create"
                    : "Save",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f)));

        ImGui::SameLine();

        const bool cancel =
            ImGui::Button(
                "Cancel",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f)));

        if (save)
        {
            std::string name =
                SanitizeNoteForStorage(
                    g_ModernDialogText);

            if (name.empty())
            {
                g_ModernDialogError =
                    "Enter a group name.";
            }
            else if (
                name.find('\t') !=
                    std::string::npos ||
                name.find('\r') !=
                    std::string::npos ||
                name.find('\n') !=
                    std::string::npos)
            {
                g_ModernDialogError =
                    "Group names cannot contain tabs or line breaks.";
            }
            else
            {
                const int exceptIndex =
                    g_ModernDialogKind ==
                            ModernDialogKind::RenameGroup
                        ? static_cast<int>(
                            g_ModernDialogGroupIndex)
                        : -1;

                if (CustomTabNameExists(
                        name,
                        exceptIndex))
                {
                    g_ModernDialogError =
                        "A custom group with that name already exists.";
                }
                else if (
                    g_ModernDialogKind ==
                    ModernDialogKind::CreateGroup)
                {
                    CustomTab tab;
                    tab.name =
                        name;

                    g_CustomTabs.push_back(
                        std::move(tab));

                    SaveCustomTabs();

                    g_ActivePrefix.clear();
                    g_ReplacementsOnly =
                        false;
                    g_EncounteredOnly =
                        false;

                    g_ActiveCustomTab =
                        static_cast<int>(
                            g_CustomTabs.size() -
                            1);

                    g_ModernSection =
                        ModernSection::Textures;

                    ModernSyncLegacyBrowser();

                    g_ModernDialogKind =
                        ModernDialogKind::None;

                    ImGui::CloseCurrentPopup();
                }
                else if (
                    g_ModernDialogGroupIndex <
                    g_CustomTabs.size())
                {
                    const std::string oldName =
                        g_CustomTabs[
                            g_ModernDialogGroupIndex]
                            .name;

                    const bool wasExcluded =
                        g_ModernExcludedCustomGroups.erase(
                            oldName) > 0;

                    g_CustomTabs[
                        g_ModernDialogGroupIndex]
                        .name =
                            name;

                    if (wasExcluded)
                    {
                        g_ModernExcludedCustomGroups.insert(
                            name);
                    }

                    SaveCustomTabs();

                    g_ModernDialogKind =
                        ModernDialogKind::None;

                    ImGui::CloseCurrentPopup();
                }
            }
        }

        if (cancel)
        {
            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }
    }
    else if (
        g_ModernDialogKind ==
        ModernDialogKind::EditNote)
    {
        const std::size_t noteTargetCount =
            g_ModernDialogNoteTextureIndices.size();

        if (noteTargetCount > 1)
        {
            ImGui::TextDisabled(
                "%zu textures selected",
                noteTargetCount);
        }
        else if (g_ModernDialogTextureIndex <
                 g_Textures.size())
        {
            ImGui::TextDisabled(
                "Texture %s",
                g_Textures[
                    g_ModernDialogTextureIndex]
                    .did.c_str());
        }

        ImGui::Spacing();

        if (noteTargetCount > 1)
        {
            ImGui::TextWrapped(
                g_ModernDialogNotesWereMixed
                    ? "The selected textures currently have different notes. "
                      "Saving will replace all of them with the same note. "
                      "Leave the field blank to remove notes from all selected textures."
                    : "Edit the shared note for all selected textures. "
                      "Leave the field blank to remove the note from all of them.");
        }
        else
        {
            ImGui::TextWrapped(
                "Add an optional note for this texture. "
                "Leave it blank to remove the note.");
        }

        ImGui::Spacing();

        ImGui::SetNextItemWidth(
            -1.0f);

        ImGui::InputText(
            "##TextureNote",
            g_ModernDialogText,
            sizeof(
                g_ModernDialogText));

        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere(
                -1);
        }

        ImGui::Spacing();

        if (ImGui::Button(
                noteTargetCount > 1
                    ? "Apply to Selection"
                    : "Save",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            const std::string note =
                SanitizeNoteForStorage(
                    g_ModernDialogText);

            for (const std::size_t selectedIndex :
                 g_ModernDialogNoteTextureIndices)
            {
                if (selectedIndex >=
                    g_Textures.size())
                {
                    continue;
                }

                const std::string did =
                    g_Textures[
                        selectedIndex]
                        .did;

                if (note.empty())
                {
                    g_TextureNotes.erase(
                        did);
                }
                else
                {
                    g_TextureNotes[
                        did] =
                            note;
                }
            }

            SaveTextureNotes();

            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Cancel",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }
    }
    else if (
        g_ModernDialogKind ==
        ModernDialogKind::DeleteGroup)
    {
        if (g_ModernDialogGroupIndex <
            g_CustomTabs.size())
        {
            ImGui::TextWrapped(
                "Delete custom group \"%s\"?",
                g_CustomTabs[
                    g_ModernDialogGroupIndex]
                    .name.c_str());

            ImGui::Spacing();

            ImGui::TextDisabled(
                "Textures and replacement files are not deleted.");
        }

        ImGui::Spacing();

        if (ImGui::Button(
                "Delete Group",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            if (g_ModernDialogGroupIndex <
                g_CustomTabs.size())
            {
                const std::size_t customIndex =
                    g_ModernDialogGroupIndex;

                g_ModernExcludedCustomGroups.erase(
                    g_CustomTabs[
                        customIndex].name);

                g_CustomTabs.erase(
                    g_CustomTabs.begin() +
                    static_cast<
                        std::ptrdiff_t>(
                            customIndex));

                if (g_ActiveCustomTab ==
                    static_cast<int>(
                        customIndex))
                {
                    g_ActiveCustomTab =
                        -1;

                    g_ActivePrefix.clear();
                }
                else if (
                    g_ActiveCustomTab >
                    static_cast<int>(
                        customIndex))
                {
                    --g_ActiveCustomTab;
                }

                SaveCustomTabs();
                ModernSyncLegacyBrowser();
            }

            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Cancel",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }
    }
    else if (
        g_ModernDialogKind ==
        ModernDialogKind::RemoveReplacement)
    {
        if (g_ModernDialogTextureIndex <
            g_Textures.size())
        {
            ImGui::TextWrapped(
                "Revert DID %s to the original DAT texture?",
                g_Textures[
                    g_ModernDialogTextureIndex]
                    .did.c_str());

            ImGui::Spacing();

            ImGui::TextDisabled(
                "The replacement file will be removed from the current workspace.");
        }

        if (!g_ModernDialogError.empty())
        {
            ImGui::Spacing();

            ImGui::TextColored(
                MODERN_RED,
                "%s",
                g_ModernDialogError.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button(
                "Revert",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            if (g_ModernDialogTextureIndex <
                g_Textures.size())
            {
                const TextureRecord& texture =
                    g_Textures[
                        g_ModernDialogTextureIndex];

                const std::wstring path =
                    ReplacementRawPath(
                        texture);

                if (!DeleteFileW(
                        path.c_str()))
                {
                    const DWORD error =
                        GetLastError();

                    g_ModernDialogError =
                        "Could not remove the replacement file. Windows error " +
                        std::to_string(
                            error) +
                        ".";
                }
                else
                {
                    ModernRefreshReplacementSet();
                    ModernInvalidateInspectorImages();
                    ModernMarkLiveCanvasDirty();

                    g_ModernReplacementNotice =
                        "Reverted to original.";

                    g_ModernReplacementNoticeUntil =
                        ImGui::GetTime() +
                        3.0;

                    g_ModernDialogKind =
                        ModernDialogKind::None;

                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Cancel",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }
    }
    else if (
        g_ModernDialogKind ==
        ModernDialogKind::ExportPack)
    {
        ImGui::TextWrapped(
            "Name the UI pack to export.");

        ImGui::Spacing();

        ImGui::SetNextItemWidth(
            -1.0f);

        const bool submitByEnter =
            ImGui::InputText(
                "##PackName",
                g_ModernDialogText,
                sizeof(
                    g_ModernDialogText),
                ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere(
                -1);
        }

        if (!g_ModernDialogError.empty())
        {
            ImGui::Spacing();

            ImGui::TextColored(
                MODERN_RED,
                "%s",
                g_ModernDialogError.c_str());
        }

        ImGui::Spacing();

        const bool exportNow =
            submitByEnter ||
            ImGui::Button(
                "Choose Location...",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f)));

        ImGui::SameLine();

        const bool cancel =
            ImGui::Button(
                "Cancel",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f)));

        if (exportNow)
        {
            const std::string packName =
                SanitizeNoteForStorage(
                    g_ModernDialogText);

            if (packName.empty())
            {
                g_ModernDialogError =
                    "Enter a pack name.";
            }
            else if (
                ModernExportCurrentPack(
                    packName))
            {
                g_ModernDialogKind =
                    ModernDialogKind::None;

                ImGui::CloseCurrentPopup();
            }
        }

        if (cancel)
        {
            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }
    }
    else if (
        g_ModernDialogKind ==
        ModernDialogKind::ImportPackConfirm)
    {
        const std::vector<std::size_t>
            existing =
                GetReplacementTextureIndices();

        ImGui::TextWrapped(
            "Import \"%s\" and replace the current replacement workspace?",
            g_ModernPendingImportManifest
                .name.c_str());

        ImGui::Spacing();

        ImGui::TextDisabled(
            "Current replacements: %zu",
            existing.size());

        ImGui::TextDisabled(
            "Imported replacements: %zu",
            g_ModernPendingImportManifest
                .textures.size());

        ImGui::Spacing();

        ImGui::TextWrapped(
            "Export your current work first if you want to keep it.");

        ImGui::Spacing();

        if (ImGui::Button(
                "Import Pack",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            if (ModernCommitImportedPack())
            {
                g_ModernDialogKind =
                    ModernDialogKind::None;

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(
                "Cancel",
                ImVec2(
                    halfButton,
                    ModernButtonHeight(34.0f))))
        {
            g_ModernPendingImportFiles.clear();
            g_ModernPendingImportSource.clear();
            g_ModernPendingImportManifest =
                AcuiManifest();

            g_ModernDialogKind =
                ModernDialogKind::None;

            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();

    if (!keepOpen)
    {
        g_ModernDialogKind =
            ModernDialogKind::None;
    }
}


static void ModernRenderShell()
{
    ModernHandleLiveSectionTransition();
    ModernPollGeneratedWork();

    const ULONGLONG now =
        GetTickCount64();

    if (now -
            g_ModernLastReplacementRefresh >
        2000)
    {
        ModernRefreshReplacementSet();

        if (g_ModernSection ==
            ModernSection::Encountered)
        {
            LoadEncounteredDIDs();
        }
    }

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        viewport->WorkPos);

    ImGui::SetNextWindowSize(
        viewport->WorkSize);

    const ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin(
        "AC Customs##ModernRoot",
        nullptr,
        rootFlags);

    // Header
    ImGui::BeginChild(
        "##Header",
        ImVec2(
            0.0f,
            52.0f),
        ImGuiChildFlags_Borders);

    ImGui::SetCursorPosY(
        15.0f);

    ImGui::TextColored(
        MODERN_GOLD,
        "AC CUSTOMS");

    ImGui::SameLine();

    ImGui::TextDisabled(
        "   UI TEXTURE BROWSER  /  REPLACE  /  PREVIEW  /  CUSTOMIZE");

    const bool snapshotCapturing =
        g_LiveMirrorRunning.load(
            std::memory_order_acquire);

    const bool snapshotReady =
        !g_LiveSnapshotPath.empty();

    const bool snapshotNeedsVanilla =
        LiveMirrorCurrentStatus() ==
            LiveMirrorStatus::VanillaRequired;

    const char* liveStatusText =
        snapshotCapturing
            ? "Live UI: Capturing"
            : snapshotNeedsVanilla
                ? "Live UI: Vanilla Required"
                : snapshotReady
                    ? "Live UI: Snapshot Ready"
                    : "Live UI: No Snapshot";

    ImGui::SameLine();

    const float statusWidth =
        ImGui::CalcTextSize(
            liveStatusText).x;

    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() -
                statusWidth -
                24.0f));

    ImGui::TextColored(
        snapshotCapturing
            ? MODERN_GREEN
            : ImGui::GetStyleColorVec4(
                ImGuiCol_TextDisabled),
        "%s",
        liveStatusText);

    ImGui::EndChild();

    ImGui::Spacing();

    const float statusHeight =
        30.0f;

    ImGui::BeginChild(
        "##Body",
        ImVec2(
            0.0f,
            -statusHeight -
                8.0f),
        ImGuiChildFlags_None);

    const float navWidth =
        220.0f;

    const float inspectorWidth =
        360.0f;

    const float gaps =
        16.0f;

    const float availableWidth =
        ImGui::GetContentRegionAvail().x;

    const float centerWidth =
        max(
            320.0f,
            availableWidth -
                navWidth -
                inspectorWidth -
                gaps);

    ImGui::BeginChild(
        "##Navigation",
        ImVec2(
            navWidth,
            0.0f),
        ImGuiChildFlags_Borders);

    ModernRenderNavigation();

    ImGui::EndChild();

    ImGui::SameLine(
        0.0f,
        8.0f);

    ImGui::BeginChild(
        "##CenterWorkspace",
        ImVec2(
            centerWidth,
            0.0f),
        ImGuiChildFlags_Borders);

    if (g_DatPath.empty() &&
        g_ModernSection !=
            ModernSection::Info)
    {
        ModernRenderWelcome();
    }
    else
    {
        switch (g_ModernSection)
        {
            case ModernSection::Textures:
            case ModernSection::Replacements:
            case ModernSection::Encountered:
                ModernRenderTextureBrowser();
                break;

            case ModernSection::LiveUi:
                ModernRenderLiveUi();
                break;

            case ModernSection::Packs:
                ModernRenderPacks();
                break;

            case ModernSection::Info:
                ModernRenderInfo();
                break;
        }
    }

    ImGui::EndChild();

    ImGui::SameLine(
        0.0f,
        8.0f);

    ImGui::BeginChild(
        "##Inspector",
        ImVec2(
            0.0f,
            0.0f),
        ImGuiChildFlags_Borders);

    ModernRenderInspector();

    ImGui::EndChild();

    ImGui::EndChild();

    ImGui::Spacing();

    ImGui::BeginChild(
        "##StatusBar",
        ImVec2(
            0.0f,
            statusHeight),
        ImGuiChildFlags_Borders);

    ImGui::SetCursorPosY(
        7.0f);

    if (g_DatPath.empty())
    {
        ImGui::TextDisabled(
            "No DAT loaded");
    }
    else
    {
        ImGui::TextDisabled(
            "%zu textures shown  |  %zu replaced",
            g_ModernVisibleCount,
            g_ModernReplacementDids.size());
    }

    if (!g_DatPath.empty() &&
        g_DatScanStats.elapsedMs != 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(
            " |  DAT scan %llu ms",
            static_cast<unsigned long long>(
                g_DatScanStats.elapsedMs));
    }

    const std::uint64_t datLoads =
        g_DatPreviewLoadCount.load(
            std::memory_order_relaxed);

    if (!g_DatPath.empty() &&
        datLoads != 0)
    {
        const std::uint64_t totalMicros =
            g_DatPreviewTotalMicros.load(
                std::memory_order_relaxed);

        const std::uint64_t maximumMicros =
            g_DatPreviewMaxMicros.load(
                std::memory_order_relaxed);

        const double averageMs =
            static_cast<double>(totalMicros) /
            static_cast<double>(datLoads) /
            1000.0;

        const double maximumMs =
            static_cast<double>(maximumMicros) /
            1000.0;

        ImGui::SameLine();
        ImGui::TextDisabled(
            " |  DAT reads %llu  avg %.2f ms  max %.2f ms",
            static_cast<unsigned long long>(datLoads),
            averageMs,
            maximumMs);
    }

    ImGui::SameLine();

    const char* workspaceText =
        "Replacement workspace OK";

    const float workspaceWidth =
        ImGui::CalcTextSize(
            workspaceText).x;

    ImGui::SetCursorPosX(
        max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() -
                workspaceWidth -
                18.0f));

    ImGui::TextColored(
        MODERN_GREEN,
        "%s",
        workspaceText);

    ImGui::EndChild();

    ModernRenderDialogs();

    ImGui::End();
}

static void ModernHideLegacyControls()
{
    HWND controls[] =
    {
        g_TabControl,
        g_ListView,
        g_StatusText,
        g_DetailsTitle,
        g_DetailsText,
        g_LargePreview,
        g_ReplaceButton,
        g_CopyButton,
        g_CopyTextureButton,
        g_DisplayUnsupportedCheck,
        g_DisplayReplacedCheck,
        g_ClearEncounteredButton,
        g_AddCustomTabButton,
        g_FilterWidthEdit,
        g_FilterHeightEdit,
        g_ApplySizeFilterButton,
        g_ClearSizeFilterButton,
        g_ImportPackButton,
        g_ExportPackButton,
        g_LiveUiButton,
        g_ReplacementTitle,
        g_ReplacementPreview,
        g_InfoText,
        g_ProgressBar
    };

    for (HWND control : controls)
    {
        if (control != nullptr)
            ShowWindow(control, SW_HIDE);
    }
}

static bool ModernInitialize(
    HWND window)
{
    if (!ModernCreateD3D(window))
        return false;

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io =
        ImGui::GetIO();

    io.ConfigFlags |=
        ImGuiConfigFlags_DockingEnable;

    // Dear ImGui 1.92 docking branch can adjust font DPI as
    // the host window crosses monitors.
    io.ConfigDpiScaleFonts = true;

    io.ConfigWindowsMoveFromTitleBarOnly =
        true;

    io.IniFilename =
        "ac_customs_imgui.ini";

    const float dpiScale =
        ImGui_ImplWin32_GetDpiScaleForHwnd(
            window);

    ModernApplyScaledTheme(
        dpiScale);

    ModernLoadUiFont();

    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext();
        ModernCleanupD3D();
        return false;
    }

    if (!ImGui_ImplDX11_Init(
            g_ModernDevice,
            g_ModernDeviceContext))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        ModernCleanupD3D();
        return false;
    }

    LoadEncounteredDIDs();
    ModernRefreshReplacementSet();

    ModernStartGeneratedWorker();

    g_ModernReady = true;

    return true;
}

static bool ModernHandleWin32Message(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (!g_ModernReady)
        return false;

    return
        ImGui_ImplWin32_WndProcHandler(
            window,
            message,
            wParam,
            lParam) != 0;
}

static void ModernResize(
    UINT width,
    UINT height)
{
    if (!g_ModernReady ||
        g_ModernSwapChain == nullptr ||
        width == 0 ||
        height == 0)
    {
        return;
    }

    ModernCleanupRenderTarget();

    const HRESULT result =
        g_ModernSwapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0);

    if (SUCCEEDED(result))
        ModernCreateRenderTarget();
}

static void ModernRenderFrame()
{
    if (!g_ModernReady ||
        g_ModernRenderTarget == nullptr)
    {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ModernUpdateDpiScale();

    ImGui::NewFrame();

    ModernRenderShell();

    ImGui::Render();

    const float clearColor[4] =
    {
        0.025f,
        0.031f,
        0.038f,
        1.00f
    };

    g_ModernDeviceContext->
        OMSetRenderTargets(
            1,
            &g_ModernRenderTarget,
            nullptr);

    g_ModernDeviceContext->
        ClearRenderTargetView(
            g_ModernRenderTarget,
            clearColor);

    ImGui_ImplDX11_RenderDrawData(
        ImGui::GetDrawData());

    g_ModernSwapChain->Present(
        1,
        0);
}

static void ModernShutdown()
{
    LiveMirrorFreeze();

    ModernStopGeneratedWorker();

    ModernReleasePreviewTextures();

    if (!g_ModernReady)
    {
        ModernCleanupD3D();
        return;
    }

    g_ModernReady = false;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    ModernCleanupD3D();
}
