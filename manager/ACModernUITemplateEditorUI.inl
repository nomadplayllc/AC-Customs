// AC Customs - visual master-template mapping editor (developer-only).
// Included AFTER ACModernUITemplateImportUI.inl and ACModernUIModern.inl.
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
static ModernGpuImage g_TemplateEditorAtlasGpu;
// The DX11 ImGui backend normally binds a linear sampler. The template is a
// pixel-authored atlas: point sampling keeps texel edges sharp at any zoom.
// Bind only for this one image draw, then restore the ImGui renderer state.
static ID3D11SamplerState* g_TemplateEditorPointSampler = nullptr;

static bool TemplateEditorEnsurePointSampler()
{
    if (g_TemplateEditorPointSampler != nullptr)
        return true;
    if (g_ModernDevice == nullptr)
        return false;

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    return SUCCEEDED(g_ModernDevice->CreateSamplerState(
        &sampler, &g_TemplateEditorPointSampler)) &&
        g_TemplateEditorPointSampler != nullptr;
}

static void TemplateEditorBindPointSampler(
    const ImDrawList*, const ImDrawCmd*)
{
    if (g_ModernDeviceContext == nullptr ||
        g_TemplateEditorPointSampler == nullptr)
        return;

    g_ModernDeviceContext->PSSetSamplers(
        0, 1, &g_TemplateEditorPointSampler);
}

static void TemplateEditorReleaseAtlasResources()
{
    ModernReleaseGpuImage(g_TemplateEditorAtlasGpu);
    if (g_TemplateEditorPointSampler != nullptr)
    {
        g_TemplateEditorPointSampler->Release();
        g_TemplateEditorPointSampler = nullptr;
    }
}

static TemplateDevImage g_TemplateEditorAtlasPixels;
// Empty means the immutable embedded original master. A non-empty path is the
// PNG currently shown in the editor; it may be either the saved default master
// or a temporary candidate that has not yet been promoted.
static std::wstring g_TemplateEditorImageSource;
static void TemplateEditorRefreshImport();

static bool TemplateEditorLoadAtlas(const std::wstring& source)
{
    TemplateDevImage decoded;
    std::wstring error;
    bool loadedFromEmbedded = false;
    if (source.empty())
    {
        loadedFromEmbedded = true;
        if (!TemplateImportDecodePngBytes(TemplateImportMasterBytes(), decoded,
            error, L"the embedded master PNG"))
        {
            g_TemplateEditorError = true;
            g_TemplateEditorStatus = "Template Editor PNG: " + FromWide(error);
            return false;
        }
    }
    else if (!TemplateDevLoadPng(source, decoded, error))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Template Editor PNG: " + FromWide(error);
        return false;
    }

    ModernGpuImage newGpu;
    if (!ModernCreateGpuImageFromBgra(decoded.bgra.data(),
        2000, 2000, newGpu))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not upload the selected PNG to the graphics device.";
        return false;
    }

    ModernReleaseGpuImage(g_TemplateEditorAtlasGpu);
    g_TemplateEditorAtlasGpu = newGpu;
    g_TemplateEditorAtlasPixels = std::move(decoded);
    g_TemplateEditorImageSource = source;
    if (!g_TemplateEditorAtlasPixels.bgra.empty())
        g_TemplateEditorMasterCrc32 = AcuiCrc32(
            g_TemplateEditorAtlasPixels.bgra.data(),
            g_TemplateEditorAtlasPixels.bgra.size());
    g_TemplateEditorError = false;
    if (loadedFromEmbedded)
        g_TemplateEditorStatus = "Showing the embedded original master template.";
    else if (_wcsicmp(source.c_str(), TemplateImportDefaultMasterPath().c_str()) == 0)
        g_TemplateEditorStatus = "Showing the current external default master template.";
    else
        g_TemplateEditorStatus = "Loaded 2000 x 2000 PNG. Click 'Set as Default Master' to adopt it.";
    return true;
}

static bool TemplateEditorLoadCurrentDefaultMaster()
{
    const std::wstring external = TemplateImportDefaultMasterPath();
    if (FileExists(external))
        return TemplateEditorLoadAtlas(external);
    return TemplateEditorLoadAtlas(L"");
}

static bool TemplateEditorChooseAtlas()
{
    wchar_t filename[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_MainWindow;
    dialog.lpstrFilter = L"PNG Images (*.png)\0*.png\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Load a 2000 x 2000 PNG in Template Editor";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return false;
    const bool loaded = TemplateEditorLoadAtlas(filename);
    if (g_MainWindow && IsWindow(g_MainWindow))
    {
        BringWindowToTop(g_MainWindow);
        SetForegroundWindow(g_MainWindow);
    }
    return loaded;
}

static bool TemplateEditorCopyFileAtomic(const std::wstring& source,
    const std::wstring& destination)
{
    std::vector<BYTE> bytes;
    if (!ReadBinaryFile(source, bytes) || bytes.empty())
        return false;
    const std::wstring staged = destination + L".tmp";
    if (!WriteBinaryFile(staged, bytes) ||
        !MoveFileExW(staged.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(staged.c_str());
        return false;
    }
    return true;
}

static bool TemplateEditorSetCurrentAsDefaultMaster()
{
    if (g_TemplateEditorImageSource.empty())
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Load a custom 2000 x 2000 PNG before setting a new default master.";
        return false;
    }
    const std::wstring destination = TemplateImportDefaultMasterPath();
    if (!EnsureDirectoryExists(g_AppPaths.managerRoot))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not create the Manager settings directory.";
        return false;
    }
    if (_wcsicmp(g_TemplateEditorImageSource.c_str(), destination.c_str()) != 0 &&
        !TemplateEditorCopyFileAtomic(g_TemplateEditorImageSource, destination))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not save the external default master PNG.";
        return false;
    }
    if (!TemplateEditorLoadAtlas(destination))
        return false;
    TemplateEditorRefreshImport();
    g_TemplateEditorError = false;
    g_TemplateEditorStatus = "Saved and activated a new external default master template.";
    return true;
}

static bool TemplateEditorRestoreOriginalMaster()
{
    const std::wstring external = TemplateImportDefaultMasterPath();
    if (FileExists(external) && !DeleteFileW(external.c_str()))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not remove the external default master PNG.";
        return false;
    }
    if (!TemplateEditorLoadAtlas(L""))
        return false;
    TemplateEditorRefreshImport();
    g_TemplateEditorError = false;
    g_TemplateEditorStatus = "Restored the embedded original master template.";
    return true;
}

static bool TemplateEditorExportMasterAndMappings(HWND owner)
{
    std::string json;
    if (!TemplateEditorBuildMappingJson(json)) return false;

    wchar_t filename[32768] = L"DefaultTemplate_2000x2000px.png";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG Images (*.png)\0*.png\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Export Current Default Master + Plugin Mapping";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return false;

    std::vector<BYTE> png;
    std::wstring error;
    if (!TemplateImportReadActiveMasterBytes(png, error) || png.empty())
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not read the current default master PNG: " + FromWide(error);
        return false;
    }

    const std::wstring pngPath = filename;
    const std::wstring slashChars = L"\/";
    const std::size_t cut = pngPath.find_last_of(slashChars);
    const std::wstring folder = cut == std::wstring::npos ? L"." : pngPath.substr(0, cut);
    const std::wstring mappingPath = JoinPath(folder, L"template_mapping.json");
    const std::wstring pngTemp = pngPath + L".tmp";
    if (!WriteBinaryFile(pngTemp, png) ||
        !MoveFileExW(pngTemp.c_str(), pngPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ||
        !TemplateEditorWriteJsonFile(mappingPath, json))
    {
        DeleteFileW(pngTemp.c_str());
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not export the master template and mapping JSON.";
        return false;
    }

    g_TemplateEditorError = false;
    g_TemplateEditorStatus = "Exported DefaultTemplate_2000x2000px.png and template_mapping.json together for the plugin.";
    if (owner && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
    }
    return true;
}

static int g_TemplateEditorSelected = -1;
static float g_TemplateEditorZoom = 1.0f;
static ImVec2 g_TemplateEditorPan(0,0);
static bool g_TemplateEditorFitPending = true;
static bool g_TemplateEditorShowAll = false;
static bool g_TemplateEditorBlackOut = false;
static char g_TemplateEditorDidDraft[24] = {};
static int g_TemplateEditorRectDraft[4] = {};
static int g_TemplateEditorDraftSelected = -2;
static char g_TemplateEditorAddDid[24] = {};
static bool g_TemplateEditorPlacing = false;
static TemplateEditorEntry g_TemplateEditorPending;

static bool TemplateEditorEnsureAtlas()
{
    if (g_TemplateEditorAtlasGpu.view) return true;
    return TemplateEditorLoadCurrentDefaultMaster();
}

static void TemplateEditorSelect(int index)
{
    g_TemplateEditorSelected = index;
    g_TemplateEditorDraftSelected = -2;
}

static void TemplateEditorSyncDraft()
{
    TemplateEditorEnsureLoaded();
    if (g_TemplateEditorDraftSelected == g_TemplateEditorSelected) return;
    g_TemplateEditorDraftSelected = g_TemplateEditorSelected;
    if (g_TemplateEditorSelected < 0 ||
        static_cast<std::size_t>(g_TemplateEditorSelected) >= g_TemplateEditorEntries.size())
        return;
    const auto& e = g_TemplateEditorEntries[static_cast<std::size_t>(g_TemplateEditorSelected)];
    strcpy_s(g_TemplateEditorDidDraft, e.did.c_str());
    g_TemplateEditorRectDraft[0] = static_cast<int>(e.x);
    g_TemplateEditorRectDraft[1] = static_cast<int>(e.y);
    g_TemplateEditorRectDraft[2] = static_cast<int>(e.w);
    g_TemplateEditorRectDraft[3] = static_cast<int>(e.h);
}

// Edits become immediately active for the existing live PNG watcher.
// There is no second replacement writer; reuse the v0.2 apply transaction.
static void TemplateEditorRefreshImport()
{
    TemplateImportResetPreview();
    g_TemplateImportSelectedIndex = 0;
    if (!g_TemplateImportSource.empty())
    {
        if (TemplateImportLoadEditedPng(g_TemplateImportSource) &&
            g_TemplateImportAutoApplyOnSave)
        {
            if (TemplateImportApplyAndRefresh(true))
                g_TemplateImportAppliedStamp =
                    TemplateImportStatFile(g_TemplateImportSource);
        }
    }
    else
        g_TemplateImportCandidates.clear();
}

static bool TemplateEditorCommitChange()
{
    if (TemplateEditorSave())
    {
        TemplateEditorRefreshImport();
        return true;
    }
    return false;
}

static bool TemplateEditorAddAt(unsigned x, unsigned y)
{
    TemplateEditorEntry entry = g_TemplateEditorPending;
    entry.x = x;
    entry.y = y;
    if (!TemplateEditorValidate(entry))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Texture does not fit at that position.";
        return false;
    }
    for (const auto& current : g_TemplateEditorEntries)
        if (current.did == entry.did)
        {
            g_TemplateEditorError = true;
            g_TemplateEditorStatus = "This DID is already mapped. Select it to edit its position.";
            return false;
        }
    g_TemplateEditorEntries.push_back(entry);
    if (!TemplateEditorCommitChange())
    {
        g_TemplateEditorEntries.pop_back();
        return false;
    }
    TemplateEditorSelect(static_cast<int>(g_TemplateEditorEntries.size()-1));
    g_TemplateEditorPlacing = false;
    return true;
}

static int TemplateEditorHitTest(float atlasX, float atlasY)
{
    int result = -1;
    unsigned bestArea = 0xFFFFFFFFu;
    const auto& all = g_TemplateEditorEntries;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const auto& entry = all[i];
        if (atlasX < static_cast<float>(entry.x) ||
            atlasY < static_cast<float>(entry.y) ||
            atlasX >= static_cast<float>(entry.x+entry.w) ||
            atlasY >= static_cast<float>(entry.y+entry.h)) continue;
        const unsigned area = entry.w * entry.h;
        if (area < bestArea)
        {
            bestArea = area;
            result = static_cast<int>(i);
        }
        // At identical bounds, prefer the currently selected alias.
        else if (area == bestArea && static_cast<int>(i) == g_TemplateEditorSelected)
            result = static_cast<int>(i);
    }
    return result;
}

static void TemplateEditorDrawCanvas()
{
    TemplateEditorEnsureLoaded();
    ImGui::TextColored(MODERN_GOLD, "Template Editor");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu mappings - 2000 x 2000 atlas", g_TemplateEditorEntries.size());
    ImGui::TextWrapped("Wheel: zoom at cursor  |  Right-drag: pan  |  Hover: outline  |  Click: select");
    if (ImGui::Button("Load 2000x2000 PNG..."))
        TemplateEditorChooseAtlas();
    ImGui::SameLine();
    if (ImGui::Button("Set as Default Master") && !g_TemplateEditorImageSource.empty())
        TemplateEditorSetCurrentAsDefaultMaster();
    ImGui::SameLine();
    if (ImGui::Button("Reload Default Master"))
        TemplateEditorLoadCurrentDefaultMaster();
    ImGui::SameLine();
    if (ImGui::Button("Restore Original Master"))
        TemplateEditorRestoreOriginalMaster();
    const bool externalDefault = FileExists(TemplateImportDefaultMasterPath());
    const bool currentIsExternal = !g_TemplateEditorImageSource.empty() &&
        _wcsicmp(g_TemplateEditorImageSource.c_str(),
            TemplateImportDefaultMasterPath().c_str()) == 0;
    ImGui::TextWrapped("Display: %s",
        g_TemplateEditorImageSource.empty() ? "Embedded original master" :
        FromWide(g_TemplateEditorImageSource).c_str());
    ImGui::TextWrapped("Default master: %s",
        externalDefault ? FromWide(TemplateImportDefaultMasterPath()).c_str() :
        "Embedded original master");
    ImGui::TextDisabled(currentIsExternal || g_TemplateEditorImageSource.empty()
        ? "Point sampling (nearest neighbor) | Display matches the active compile baseline."
        : "Point sampling (nearest neighbor) | Displayed PNG is a candidate only until you set it as default.");
    if (ImGui::Button("Fit to Window")) g_TemplateEditorFitPending = true;
    ImGui::SameLine();
    ImGui::Checkbox("Show all bounds", &g_TemplateEditorShowAll);
    ImGui::SameLine();
    ImGui::Checkbox("Black out", &g_TemplateEditorBlackOut);
    if (g_TemplateEditorPlacing)
    {
        ImGui::SameLine();
        if (ImGui::Button("Cancel placement (Esc)")) g_TemplateEditorPlacing = false;
        ImGui::TextColored(MODERN_GOLD, "Click in the image to place DID %s (%ux%u).",
            g_TemplateEditorPending.did.c_str(),
            g_TemplateEditorPending.w, g_TemplateEditorPending.h);
    }
    if (!TemplateEditorEnsureAtlas())
    {
        ImGui::TextDisabled("Could not load template PNG texture.");
        return;
    }
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 40.0f || size.y < 40.0f) return;
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##AtlasCanvas", size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 center(canvasMin.x + size.x * 0.5f,
                        canvasMin.y + size.y * 0.5f);
    if (g_TemplateEditorFitPending)
    {
        g_TemplateEditorZoom = max(0.05f,
            min((size.x-22.0f)/2000.0f, (size.y-22.0f)/2000.0f));
        g_TemplateEditorPan = ImVec2(0, 0);
        g_TemplateEditorFitPending = false;
    }
    if (hovered && io.MouseWheel != 0.0f)
    {
        const float before = g_TemplateEditorZoom;
        const float after = max(0.05f, min(16.0f,
            before * (io.MouseWheel > 0.0f ? 1.2f : 1.0f/1.2f)));
        const float ax = (mouse.x-center.x-g_TemplateEditorPan.x)/before;
        const float ay = (mouse.y-center.y-g_TemplateEditorPan.y)/before;
        g_TemplateEditorPan.x = mouse.x-center.x-ax*after;
        g_TemplateEditorPan.y = mouse.y-center.y-ay*after;
        g_TemplateEditorZoom = after;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f))
    {
        g_TemplateEditorPan.x += io.MouseDelta.x;
        g_TemplateEditorPan.y += io.MouseDelta.y;
    }
    if (g_TemplateEditorPlacing && ImGui::IsKeyPressed(ImGuiKey_Escape))
        g_TemplateEditorPlacing = false;
    const float zoom = g_TemplateEditorZoom;
    const ImVec2 origin(center.x+g_TemplateEditorPan.x-1000.0f*zoom,
                        center.y+g_TemplateEditorPan.y-1000.0f*zoom);
    const float ax = (mouse.x-origin.x)/zoom;
    const float ay = (mouse.y-origin.y)/zoom;
    const bool inside = hovered && ax >= 0 && ay >= 0 && ax < 2000 && ay < 2000;
    const int hit = inside ? TemplateEditorHitTest(ax, ay) : -1;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvasMin, ImVec2(canvasMin.x+size.x,canvasMin.y+size.y), true);
    draw->AddRectFilled(canvasMin, ImVec2(canvasMin.x+size.x,canvasMin.y+size.y),
        IM_COL32(55, 55, 55, 255));
    const ImTextureID imageId = static_cast<ImTextureID>(
        reinterpret_cast<std::uintptr_t>(g_TemplateEditorAtlasGpu.view));
    // This callback changes only the atlas draw command's pixel sampler.
    // ResetRenderState rebinds ImGui's default sampler for subsequent UI.
    const bool pointSampling = TemplateEditorEnsurePointSampler();
    if (pointSampling)
        draw->AddCallback(TemplateEditorBindPointSampler, nullptr);
    draw->AddImage(imageId, origin,
        ImVec2(origin.x+2000.0f*zoom,origin.y+2000.0f*zoom));
    if (pointSampling)
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    // Hide all currently mapped areas to make unmapped template art visible.
    // Keep the overlays beneath hover/selection outlines so editing still works.
    if (g_TemplateEditorBlackOut)
    {
        for (const auto& entry : g_TemplateEditorEntries)
        {
            draw->AddRectFilled(
                ImVec2(origin.x + entry.x * zoom, origin.y + entry.y * zoom),
                ImVec2(origin.x + (entry.x + entry.w) * zoom,
                       origin.y + (entry.y + entry.h) * zoom),
                IM_COL32(0, 0, 0, 255));
        }
    }
    auto outline = [&](const TemplateEditorEntry& entry, ImU32 color, float thick)
    {
        draw->AddRect(ImVec2(origin.x+entry.x*zoom,origin.y+entry.y*zoom),
            ImVec2(origin.x+(entry.x+entry.w)*zoom,origin.y+(entry.y+entry.h)*zoom),
            color, 0.0f, 0, thick);
    };
    if (g_TemplateEditorShowAll)
        for (const auto& entry : g_TemplateEditorEntries)
            outline(entry, entry.pixelFormat == 0x15u ?
                IM_COL32(0,220,240,125) : IM_COL32(250,0,210,125), 1.0f);
    if (g_TemplateEditorSelected >= 0 &&
        static_cast<std::size_t>(g_TemplateEditorSelected) < g_TemplateEditorEntries.size())
        outline(g_TemplateEditorEntries[g_TemplateEditorSelected],
            IM_COL32(255,205,70,255), 2.2f);
    if (hit >= 0)
        outline(g_TemplateEditorEntries[hit], IM_COL32(255,255,255,255), 2.0f);
    if (g_TemplateEditorPlacing && inside)
    {
        const unsigned x = static_cast<unsigned>(ax), y = static_cast<unsigned>(ay);
        if (x+g_TemplateEditorPending.w<=2000u && y+g_TemplateEditorPending.h<=2000u)
        {
            TemplateEditorEntry ghost = g_TemplateEditorPending;
            ghost.x=x; ghost.y=y;
            outline(ghost, IM_COL32(90,245,120,255), 2.0f);
        }
    }
    draw->PopClipRect();
    if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (g_TemplateEditorPlacing)
            TemplateEditorAddAt(static_cast<unsigned>(ax), static_cast<unsigned>(ay));
        else
            TemplateEditorSelect(hit);
    }
    if (hit >= 0 && !g_TemplateEditorPlacing)
    {
        const auto& e = g_TemplateEditorEntries[hit];
        ImGui::BeginTooltip();
        ImGui::Text("DID %s", e.did.c_str());
        ImGui::Text("X %u  Y %u  %u x %u", e.x,e.y,e.w,e.h);
        ImGui::TextDisabled("%s | %s", e.pixelFormat == 0x15u ? "BGRA" : "BGR",
            e.exact ? "exact" : "manual/alpha-aware");
        ImGui::EndTooltip();
    }
}

static void TemplateEditorDrawInspector()
{
    TemplateEditorEnsureLoaded();
    ImGui::TextColored(MODERN_GOLD, "Mapping Inspector");
    ImGui::Separator();
    ImGui::TextWrapped("Click a texture on the template to edit its DID or position. "
        "All changes save to a local JSON file, never to the embedded master.");
    if (!g_TemplateEditorStatus.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(g_TemplateEditorError ? MODERN_RED : MODERN_GREEN,
            "%s", g_TemplateEditorStatus.c_str());
    }
    ImGui::Spacing();
    ImGui::Text("Mappings: %zu", g_TemplateEditorEntries.size());
    ImGui::TextDisabled("Active: %s",g_TemplateEditorCustomized ? "Custom" : "Embedded defaults");
    ImGui::Separator();
    TemplateEditorSyncDraft();
    if (g_TemplateEditorSelected >= 0 &&
        static_cast<std::size_t>(g_TemplateEditorSelected) < g_TemplateEditorEntries.size())
    {
        const auto& entry = g_TemplateEditorEntries[g_TemplateEditorSelected];
        ImGui::Text("Selected DID: %s", entry.did.c_str());
        ImGui::TextDisabled("Texture format: %s (%s)",
            entry.pixelFormat == 0x15u ? "BGRA" : "BGR",
            entry.exact ? "original exact match" : "alpha-aware/manual");
        // Identical atlas rectangles can represent multiple distinct DIDs.
        // Keep each DID editable instead of silently hiding the second alias.
        for (std::size_t i=0;i<g_TemplateEditorEntries.size();++i)
        {
            if (static_cast<int>(i) == g_TemplateEditorSelected) continue;
            const auto& other = g_TemplateEditorEntries[i];
            if (other.x == entry.x && other.y == entry.y &&
                other.w == entry.w && other.h == entry.h)
            {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton((std::string("Select linked DID ")+other.did).c_str()))
                    TemplateEditorSelect(static_cast<int>(i));
                ImGui::PopID();
            }
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##EditorDid", g_TemplateEditorDidDraft,
            sizeof(g_TemplateEditorDidDraft));
        ImGui::TextDisabled("DID (8 hexadecimal digits)");
        static const char* names[4] = {"X", "Y", "Width", "Height"};
        for (int i=0;i<4;++i)
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputInt(names[i], &g_TemplateEditorRectDraft[i]);
        }
        if (ImGui::Button("Save DID / Bounds", ImVec2(-1,0)))
        {
            TemplateEditorEntry changed = entry;
            changed.did = TemplateEditorNormalizeDid(g_TemplateEditorDidDraft);
            bool dimensionsValid = true;
            for (int i=0;i<4;++i) if (g_TemplateEditorRectDraft[i] < 0) dimensionsValid = false;
            if (dimensionsValid)
            {
                changed.x=static_cast<unsigned>(g_TemplateEditorRectDraft[0]);
                changed.y=static_cast<unsigned>(g_TemplateEditorRectDraft[1]);
                changed.w=static_cast<unsigned>(g_TemplateEditorRectDraft[2]);
                changed.h=static_cast<unsigned>(g_TemplateEditorRectDraft[3]);
            }
            const TextureRecord* texture = TemplateEditorFindTexture(changed.did);
            // Pixel format belongs to the assigned DID; don't require users
            // to manually switch BGR/BGRA when reassigning a slot.
            if (texture) changed.pixelFormat = texture->pixelFormat;
            if (!dimensionsValid || !texture ||
                changed.w != texture->width || changed.h != texture->height ||
                !TemplateEditorValidate(changed))
            {
                g_TemplateEditorError = true;
                g_TemplateEditorStatus = "DID, format or bounds do not match the DAT's native texture size.";
            }
            else
            {
                bool repeated = false;
                for (std::size_t i=0; i<g_TemplateEditorEntries.size(); ++i)
                    if (i != static_cast<std::size_t>(g_TemplateEditorSelected) &&
                        g_TemplateEditorEntries[i].did == changed.did) repeated = true;
                if (repeated)
                {
                    g_TemplateEditorError = true;
                    g_TemplateEditorStatus = "That DID already has a mapping.";
                }
                else
                {
                    const TemplateEditorEntry before = entry;
                    changed.exact = before.exact && changed.did == before.did &&
                        changed.x == before.x && changed.y == before.y &&
                        changed.w == before.w && changed.h == before.h;
                    g_TemplateEditorEntries[g_TemplateEditorSelected] = changed;
                    if (!TemplateEditorCommitChange())
                        g_TemplateEditorEntries[g_TemplateEditorSelected] = before;
                    else g_TemplateEditorDraftSelected = -2;
                }
            }
        }
        if (ImGui::Button("Remove Selected Mapping", ImVec2(-1,0)))
        {
            const auto removed = g_TemplateEditorEntries[g_TemplateEditorSelected];
            const int index = g_TemplateEditorSelected;
            g_TemplateEditorEntries.erase(g_TemplateEditorEntries.begin()+index);
            if (!TemplateEditorCommitChange())
                g_TemplateEditorEntries.insert(g_TemplateEditorEntries.begin()+index,removed);
            else TemplateEditorSelect(-1);
        }
    }
    else ImGui::TextDisabled("No mapping selected. Hover and click on the atlas.");
    ImGui::Separator();
    ImGui::TextColored(MODERN_GOLD, "Add Mapping");
    ImGui::TextWrapped("Enter an existing DAT DID. Its width, height and format "
        "will be filled automatically; click the atlas to place it.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##NewAtlasDID", "06004CXX",g_TemplateEditorAddDid,
        sizeof(g_TemplateEditorAddDid));
    if (ImGui::Button("Add DID / Choose Position", ImVec2(-1,0)))
    {
        const std::string did = TemplateEditorNormalizeDid(g_TemplateEditorAddDid);
        const TextureRecord* tex = TemplateEditorFindTexture(did);
        bool duplicate = false;
        for (const auto& entry : g_TemplateEditorEntries)
            if (entry.did == did) duplicate = true;
        if (!TemplateEditorValidDid(did) || !tex || duplicate)
        {
            g_TemplateEditorError = true;
            g_TemplateEditorStatus = duplicate ? "DID already mapped. Edit the existing mapping." :
                "Enter an existing previewable 0x06 DAT texture DID.";
        }
        else
        {
            g_TemplateEditorPending = TemplateEditorEntry{did, 0, 0,
                tex->width,tex->height,tex->pixelFormat,false};
            g_TemplateEditorPlacing = true;
            g_TemplateEditorStatus = "Move the cursor onto the atlas, then click to place.";
            g_TemplateEditorError = false;
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Restore Embedded Mappings...",ImVec2(-1,0)))
        ImGui::OpenPopup("Reset template mappings");
    if (ImGui::BeginPopupModal("Reset template mappings", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Discard ALL mapping edits and restore the 293 original regions?");
        if (ImGui::Button("Restore"))
        {
            if (TemplateEditorResetToEmbedded())
            {
                TemplateEditorSelect(-1);
                TemplateEditorRefreshImport();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
    if (ImGui::Button("Export Master + Mappings...", ImVec2(-1, 0)))
        TemplateEditorExportMasterAndMappings(g_MainWindow);
    ImGui::TextWrapped("Exports the CURRENT default master PNG together with template_mapping.json. "
        "Use both files together in the Decal plugin so the mapping revision matches the template art.");
    ImGui::Spacing();
    if (ImGui::Button("Export Mappings for Plugin...", ImVec2(-1, 0)))
        TemplateEditorExportMappings(g_MainWindow);
    ImGui::TextWrapped("Exports only template_mapping.json for the current default master revision.");
    ImGui::Spacing();
    ImGui::TextDisabled("Local editable mapping JSON:");
    ImGui::TextWrapped("%s", FromWide(TemplateEditorMappingPath()).c_str());
    if (ImGui::Button("Copy JSON path"))
        ImGui::SetClipboardText(FromWide(TemplateEditorMappingPath()).c_str());
}
#endif
