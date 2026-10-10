// Included AFTER ACModernUIModern.inl so the normal Modern GPU/image helpers
// and replacement cache invalidation functions are already defined.
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
static ModernGpuImage g_TemplateImportPreviewNew;
static ModernGpuImage g_TemplateImportPreviewOld;
static std::size_t g_TemplateImportPreviewIndex = static_cast<std::size_t>(-1);
static std::size_t g_TemplateImportSelectedIndex = 0;

static void TemplateImportResetPreview()
{
    ModernReleaseGpuImage(g_TemplateImportPreviewNew);
    ModernReleaseGpuImage(g_TemplateImportPreviewOld);
    g_TemplateImportPreviewIndex = static_cast<std::size_t>(-1);
}

static void TemplateImportMakePreview(std::size_t index)
{
    if (index == g_TemplateImportPreviewIndex) return;
    TemplateImportResetPreview();
    if (index >= g_TemplateImportCandidates.size()) return;
    const TemplateImportCandidate& c = g_TemplateImportCandidates[index];
    const std::vector<BYTE> modified = TemplateImportExtractBgra(c);
    ModernCreateGpuImageFromBgra(modified.data(),
        static_cast<int>(c.mapping.w), static_cast<int>(c.mapping.h),
        g_TemplateImportPreviewNew);
    TemplateDevImage original;
    std::wstring error;
    if (TemplateImportDecodeEmbedded(original, error))
    {
        std::vector<BYTE> old(static_cast<std::size_t>(c.mapping.w) * c.mapping.h * 4u);
        for (unsigned y = 0; y < c.mapping.h; ++y)
        {
            const BYTE* p = original.bgra.data() +
                ((static_cast<std::size_t>(c.mapping.y + y) * 2000u) +
                c.mapping.x) * 4u;
            memcpy(old.data() + static_cast<std::size_t>(y) * c.mapping.w * 4u,
                p, static_cast<std::size_t>(c.mapping.w) * 4u);
        }
        ModernCreateGpuImageFromBgra(old.data(),
            static_cast<int>(c.mapping.w), static_cast<int>(c.mapping.h),
            g_TemplateImportPreviewOld);
    }
    g_TemplateImportPreviewIndex = index;
}

// Update caches AND rebuild the Live UI compositor after .rgb files change.
// Merely marking the D3D texture dirty reuploads an old canvas and is not enough.
static bool TemplateImportApplyAndRefresh(bool automatic)
{
    if (g_TemplateImportApplying) return false;
    g_TemplateImportApplying = true;
    std::wstring error;
    const bool ok = TemplateImportApply(error);
    if (ok)
    {
        ModernRefreshReplacementSet();
        ModernInvalidateInspectorImages();
        g_LiveUseReplacements = true;
        LiveReloadScene();
        ModernMarkLiveCanvasDirty();
        std::size_t applied = 0;
        for (const auto& c : g_TemplateImportCandidates)
            if (c.selected) ++applied;
        g_TemplateImportNotice = std::string(automatic
            ? "Saved PNG applied automatically: "
            : "Applied: ") + std::to_string(applied) +
            " texture replacements. Live UI preview refreshed.";
        g_TemplateImportNoticeError = false;
    }
    else
    {
        g_TemplateImportNotice = FromWide(error);
        g_TemplateImportNoticeError = true;
    }
    g_TemplateImportApplying = false;
    return ok;
}

// Called from ModernRenderShell on every frame, including when Live UI is open.
// File timestamps and size must remain unchanged for 650 ms; this tolerates
// editors that rewrite the PNG in chunks or swap temp files on Save.
static void TemplateImportWatchTick()
{
    if (!g_TemplateImportAutoApplyOnSave ||
        g_TemplateImportSource.empty() ||
        g_TemplateImportApplying)
        return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_TemplateImportLastPollAt < 300u) return;
    g_TemplateImportLastPollAt = now;
    const TemplateImportFileStamp stamp =
        TemplateImportStatFile(g_TemplateImportSource);
    if (!stamp.valid) return; // Editors may rename the PNG during a save.
    if (!TemplateImportSameStamp(stamp, g_TemplateImportSeenStamp))
    {
        g_TemplateImportSeenStamp = stamp;
        g_TemplateImportStampChangedAt = now;
        return;
    }
    if (TemplateImportSameStamp(stamp, g_TemplateImportAppliedStamp) ||
        now - g_TemplateImportStampChangedAt < 650u ||
        now - g_TemplateImportLastAttemptAt < 1000u)
        return;

    g_TemplateImportLastAttemptAt = now;
    // Decode into a fresh image; if the editor is still writing it, retry.
    if (!TemplateImportLoadEditedPng(g_TemplateImportSource)) return;
    TemplateImportResetPreview();
    g_TemplateImportSelectedIndex = 0;
    if (TemplateImportApplyAndRefresh(true))
        g_TemplateImportAppliedStamp = stamp;
}

static void TemplateImportDrawUi()
{
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(MODERN_GOLD, "Master Template");
    ImGui::Spacing();

    ImGui::TextWrapped("1. Export the current default template, or generate one from an ACUI pack / your current workspace below.");
    ImGui::TextWrapped("2. Open the PNG in Photoshop, GIMP, Krita, or any image editor. "
        "Paint over the existing textures, ideally on a new layer.");
    ImGui::TextWrapped("3. Keep the canvas at 2000 x 2000 pixels, and keep "
        "each texture inside its cyan or magenta outline. Do not move texture slots.");
    ImGui::TextWrapped("4. Choose your edited PNG once below. Leave AC Customs open.");
    ImGui::TextWrapped("5. Save the PNG from your art editor. Changes are detected "
        "and applied automatically to the Live UI preview soon after saving.");
    ImGui::Spacing();
    ImGui::TextDisabled("Tips: Cyan textures support transparency; magenta textures are opaque. "
        "Saving a PSD/project file alone will NOT update the watched PNG. "
        "Export or save to the same PNG path to trigger a refresh.");
    ImGui::Spacing();

    if (ImGui::Button("Export Default Template..."))
        TemplateImportExportMaster(g_MainWindow);
    ImGui::SameLine();
    if (ImGui::Button("Choose Edited PNG / Start Watching..."))
    {
        TemplateImportResetPreview();
        if (TemplateImportPrepare(g_MainWindow))
        {
            g_TemplateImportSelectedIndex = 0;
            if (g_TemplateImportAutoApplyOnSave)
                TemplateImportApplyAndRefresh(true);
        }
    }
    ImGui::Spacing();
    ImGui::TextColored(MODERN_GOLD, "Generate Editable Template");
    ImGui::TextWrapped("Create a 2000 x 2000 PNG using this template's text, "
        "guides and sections. Replacement textures are placed into the "
        "CURRENT mapping bounds; everything else stays as the master artwork.");
    if (ImGui::Button("From Current Workspace..."))
        TemplateExportEditable(false);
    ImGui::SameLine();
    if (ImGui::Button("From ACUI Pack..."))
        TemplateExportEditable(true);
    ImGui::TextDisabled("Source files are read-only. The generated PNG can be opened "
        "in Photoshop and selected above for automatic Save-to-Live editing.");
    ImGui::Spacing();
    ImGui::Checkbox("Automatically apply when the PNG is saved",
        &g_TemplateImportAutoApplyOnSave);

    if (!g_TemplateImportSource.empty())
    {
        ImGui::TextWrapped("Watching: %s", FromWide(g_TemplateImportSource).c_str());
        ImGui::TextDisabled(g_TemplateImportAutoApplyOnSave
            ? "Live updates enabled. Save the PNG to refresh the preview."
            : "Auto-update paused. Use Apply Now when ready.");
    }
    if (!g_TemplateImportNotice.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text,
            g_TemplateImportNoticeError ? MODERN_RED : MODERN_GREEN);
        ImGui::TextWrapped("%s", g_TemplateImportNotice.c_str());
        ImGui::PopStyleColor();
    }
    if (g_TemplateImportArtwork.bgra.empty()) return;

    const std::size_t total = g_TemplateImportCandidates.size();
    std::size_t checked = 0, exact = 0, masked = 0;
    for (const auto& c : g_TemplateImportCandidates)
    {
        if (c.selected) ++checked;
        if (c.mapping.exact) ++exact; else ++masked;
    }
    ImGui::Text("Detected changes: %zu   Selected: %zu   Exact: %zu   Alpha-mask: %zu",
        total, checked, exact, masked);
    if (ImGui::Button("Apply Now"))
    {
        if (TemplateImportApplyAndRefresh(false))
            g_TemplateImportAppliedStamp = TemplateImportStatFile(g_TemplateImportSource);
    }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Review individual textures (optional)"))
    {
        ImGui::TextWrapped("Every changed texture is checked by default, "
            "including RGBA and alpha-mask mappings. Uncheck only what you want to skip. "
            "The next external Save automatically selects all changed textures again.");
        if (ImGui::Button("Select All"))
            for (auto& c : g_TemplateImportCandidates) c.selected = true;
        ImGui::SameLine();
        if (ImGui::Button("Deselect All"))
            for (auto& c : g_TemplateImportCandidates) c.selected = false;
        if (total == 0)
            ImGui::TextDisabled("Nothing changed compared with the embedded master.");
        else
        {
    const float paneWidth = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##TemplateImportEntries",
        ImVec2(paneWidth * 0.57f, 310.0f), true);
    for (std::size_t i = 0; i < total; ++i)
    {
        auto& c = g_TemplateImportCandidates[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Checkbox("##apply", &c.selected);
        ImGui::SameLine();
        char label[160] = {};
        sprintf_s(label, "%s  %ux%u  %s%s",
            c.mapping.did, c.mapping.w, c.mapping.h,
            c.mapping.exact ? "EXACT" : "ALPHA",
            c.riskyAlpha ? "  (opaque)" : "");
        if (ImGui::Selectable(label, i == g_TemplateImportSelectedIndex))
            g_TemplateImportSelectedIndex = i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##TemplateImportPreview",
        ImVec2(0, 310.0f), true);
    if (g_TemplateImportSelectedIndex < total)
    {
        const auto& c = g_TemplateImportCandidates[g_TemplateImportSelectedIndex];
        TemplateImportMakePreview(g_TemplateImportSelectedIndex);
        ImGui::Text("DID %s", c.mapping.did);
        ImGui::Text("Source: %d,%d  %ux%u", c.mapping.x, c.mapping.y,
            c.mapping.w, c.mapping.h);
        ImGui::Text("Changed pixels: %u", c.changedPixels);
        if (!c.mapping.exact)
            ImGui::TextDisabled("Alpha-mask mapped region");
        if (c.riskyAlpha)
            ImGui::TextDisabled("PNG appears opaque here; alpha saved as painted");
        auto drawImage = [](const char* heading, const ModernGpuImage& image)
        {
            ImGui::TextDisabled("%s", heading);
            if (!image.view) return;
            const float maxSize = 125.0f;
            const float scale = min(1.0f,
                min(maxSize / static_cast<float>(image.width),
                    maxSize / static_cast<float>(image.height)));
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float pw = image.width * scale;
            const float ph = image.height * scale;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (int y = 0; y < static_cast<int>(ph); y += 10)
                for (int x = 0; x < static_cast<int>(pw); x += 10)
                {
                    const float right = min(pw, static_cast<float>(x + 10));
                    const float bottom = min(ph, static_cast<float>(y + 10));
                    draw->AddRectFilled(
                        ImVec2(origin.x + x, origin.y + y),
                        ImVec2(origin.x + right, origin.y + bottom),
                        ((x / 10 + y / 10) % 2)
                            ? IM_COL32(90, 90, 90, 255)
                            : IM_COL32(145, 145, 145, 255));
                }
            const ImTextureID id = static_cast<ImTextureID>(
                reinterpret_cast<std::uintptr_t>(image.view));
            ImGui::Image(id, ImVec2(pw, ph));
        };
        drawImage("Original", g_TemplateImportPreviewOld);
        ImGui::SameLine();
        drawImage("Replacement", g_TemplateImportPreviewNew);
    }
    ImGui::EndChild();
        }
        if (ImGui::Button("Apply Checked Textures"))
        {
            if (TemplateImportApplyAndRefresh(false))
                g_TemplateImportAppliedStamp = TemplateImportStatFile(g_TemplateImportSource);
        }
    }
}
#endif // AC_CUSTOMS_TEMPLATE_DEVTOOLS
