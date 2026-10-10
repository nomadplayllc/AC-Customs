// AC Customs - template importer proof-of-concept, DEVELOPMENT ONLY.
// Include after ACModernUITemplateMapperDev.inl, before ACModernUIModern.inl.
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
#include <stdexcept>
#include <tuple>
#include "ACModernUITemplateEmbedded.inl"
#include "ACModernUITemplateEditorData.inl"

struct TemplateImportCandidate
{
    TemplateImportMapping mapping{};
    std::size_t catalogIndex = 0;
    bool selected = true;
    bool riskyAlpha = false;
    unsigned changedPixels = 0;
};

static TemplateDevImage g_TemplateImportArtwork;
static std::vector<TemplateImportCandidate> g_TemplateImportCandidates;
static std::wstring g_TemplateImportSource;
static std::string g_TemplateImportNotice;
static bool g_TemplateImportNoticeError = false;

// The editable artwork path remains watched while the manager is open.
// A saved PNG is reloaded only after its size and last-write stamp have settled.
static bool g_TemplateImportAutoApplyOnSave = true;
struct TemplateImportFileStamp
{
    FILETIME modified = {};
    ULONGLONG bytes = 0;
    bool valid = false;
};
static TemplateImportFileStamp g_TemplateImportSeenStamp;
static TemplateImportFileStamp g_TemplateImportAppliedStamp;
static ULONGLONG g_TemplateImportStampChangedAt = 0;
static ULONGLONG g_TemplateImportLastPollAt = 0;
static ULONGLONG g_TemplateImportLastAttemptAt = 0;
static bool g_TemplateImportApplying = false;

struct TemplateImportPriorReplacement
{
    bool existed = false;
    std::vector<BYTE> raw;
};
// In-session originals allow Undo/erase back to the master to restore the
// prior user replacement, instead of leaving the previously painted texture.
static std::unordered_map<std::string, TemplateImportPriorReplacement>
    g_TemplateImportPreviousFiles;
static std::unordered_set<std::string> g_TemplateImportManagedDids;

static std::wstring TemplateImportDefaultMasterPath()
{
    return JoinPath(g_AppPaths.managerRoot, L"DefaultMaster.png");
}

static TemplateImportFileStamp TemplateImportStatFile(const std::wstring& path)
{
    TemplateImportFileStamp result;
    if (path.empty()) return result;
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return result;
    result.valid = true;
    result.modified = data.ftLastWriteTime;
    result.bytes = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) |
        data.nFileSizeLow;
    return result;
}

static bool TemplateImportSameStamp(const TemplateImportFileStamp& a,
    const TemplateImportFileStamp& b)
{
    return a.valid == b.valid && (!a.valid ||
        (a.bytes == b.bytes && CompareFileTime(&a.modified, &b.modified) == 0));
}


// Decode the embedded PNG once. Encoding it as base64 instead of hundreds of
// thousands of C++ integer initializers keeps the MSVC translation unit lean.
static const std::vector<BYTE>& TemplateImportMasterBytes()
{
    static const std::vector<BYTE> bytes = []
    {
        std::vector<BYTE> decoded;
        decoded.reserve(810000u);
        unsigned int accumulator = 0;
        unsigned bits = 0;
        for (const char ch : kTemplateMasterPngBase64)
        {
            if (ch == '\0' || ch == '=') break;
            unsigned digit = 64u;
            if (ch >= 'A' && ch <= 'Z') digit = static_cast<unsigned>(ch - 'A');
            else if (ch >= 'a' && ch <= 'z') digit = static_cast<unsigned>(ch - 'a' + 26);
            else if (ch >= '0' && ch <= '9') digit = static_cast<unsigned>(ch - '0' + 52);
            else if (ch == '+') digit = 62u;
            else if (ch == '/') digit = 63u;
            if (digit >= 64u) continue;
            accumulator = (accumulator << 6u) | digit;
            bits += 6u;
            if (bits >= 8u)
            {
                bits -= 8u;
                decoded.push_back(static_cast<BYTE>((accumulator >> bits) & 255u));
            }
        }
        return decoded;
    }();
    return bytes;
}

static bool TemplateImportReadActiveMasterBytes(std::vector<BYTE>& bytes,
    std::wstring& error, std::wstring* sourcePath = nullptr,
    bool* external = nullptr)
{
    bytes.clear();
    const std::wstring defaultPath = TemplateImportDefaultMasterPath();
    if (FileExists(defaultPath))
    {
        if (!ReadBinaryFile(defaultPath, bytes) || bytes.empty())
        {
            error = L"Could not read the external default master PNG.";
            return false;
        }
        if (sourcePath) *sourcePath = defaultPath;
        if (external) *external = true;
        return true;
    }
    bytes = TemplateImportMasterBytes();
    if (bytes.empty())
    {
        error = L"Embedded master PNG was not decoded.";
        return false;
    }
    if (sourcePath) sourcePath->clear();
    if (external) *external = false;
    return true;
}

static bool TemplateImportDecodePngBytes(const std::vector<BYTE>& png,
    TemplateDevImage& image, std::wstring& error, const wchar_t* sourceLabel)
{
    image = TemplateDevImage{};
    if (g_WicFactory == nullptr) { error = L"WIC is not initialized."; return false; }
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    if (png.empty() || png.size() > 0xFFFFFFFFull)
    {
        error = std::wstring(L"Could not decode ") + sourceLabel + L".";
        return false;
    }
    HRESULT hr = g_WicFactory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(
            const_cast<BYTE*>(png.data()), static_cast<DWORD>(png.size()));
    if (SUCCEEDED(hr))
        hr = g_WicFactory->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = frame->GetSize(&image.width, &image.height);
    if (SUCCEEDED(hr) && (image.width != 2000u || image.height != 2000u))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = g_WicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
    {
        image.bgra.resize(static_cast<std::size_t>(image.width) * image.height * 4u);
        hr = converter->CopyPixels(nullptr, image.width * 4u,
            static_cast<UINT>(image.bgra.size()), image.bgra.data());
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (FAILED(hr))
    {
        error = std::wstring(L"Could not decode ") + sourceLabel +
            L". Ensure it is a valid 2000 x 2000 PNG.";
        image = TemplateDevImage{};
        return false;
    }
    return true;
}

// Historical name retained because other template-dev code already calls it.
// It now resolves the ACTIVE default master: external override first,
// embedded fallback otherwise.
static bool TemplateImportDecodeEmbedded(TemplateDevImage& image, std::wstring& error)
{
    std::vector<BYTE> png;
    bool external = false;
    if (!TemplateImportReadActiveMasterBytes(png, error, nullptr, &external))
    {
        image = TemplateDevImage{};
        return false;
    }
    if (!TemplateImportDecodePngBytes(png, image, error,
        external ? L"the external default master PNG" : L"the embedded master PNG"))
        return false;
    if (!image.bgra.empty())
        g_TemplateEditorMasterCrc32 = AcuiCrc32(image.bgra.data(), image.bgra.size());
    return true;
}

static bool TemplateImportSelectPng(HWND owner, std::wstring& path)
{
    wchar_t filename[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG Images (*.png)\0*.png\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Select edited AC Customs Master Template";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return false;
    path = filename;
    if (owner && IsWindow(owner)) { BringWindowToTop(owner); SetForegroundWindow(owner); }
    return true;
}

static bool TemplateImportExportMaster(HWND owner)
{
    wchar_t filename[32768] = L"DefaultTemplate_2000x2000px.png";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG Images (*.png)\0*.png\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Export Current Default Master Template";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return false;
    std::vector<BYTE> png;
    std::wstring error;
    bool external = false;
    if (!TemplateImportReadActiveMasterBytes(png, error, nullptr, &external) ||
        !WriteBinaryFile(filename, png))
    {
        g_TemplateImportNotice = png.empty() ? FromWide(error) : "Could not export master template.";
        g_TemplateImportNoticeError = true;
        return false;
    }
    g_TemplateImportNotice = std::string("Exported a copy of the current ") +
        (external ? "external" : "embedded") + " default master template.";
    g_TemplateImportNoticeError = false;
    return true;
}

// Reverse-template export: preserve the embedded master template, while
// stamping only valid, mapped replacement payloads over their native slots.
// Both sources are read-only; no .rgb workspace files are changed.
struct TemplateExportSummary
{
    std::size_t mapped = 0;
    std::size_t filled = 0;
    std::size_t missing = 0;
    std::size_t mismatched = 0;
    std::size_t sharedConflicts = 0;
    std::size_t sharedIdentical = 0;
};

static bool TemplateExportChooseDestination(HWND owner,
    const wchar_t* suggestedName, std::wstring& path)
{
    wchar_t filename[32768] = {};
    wcscpy_s(filename, _countof(filename), suggestedName);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"PNG Images (*.png)\0*.png\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Save Editable AC Customs Template";
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return false;
    path = filename;
    if (owner && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
    }
    return true;
}

static bool TemplateExportEncodePngAtomic(const std::wstring& path,
    const TemplateDevImage& atlas, std::wstring& error)
{
    if (g_WicFactory == nullptr || atlas.width != 2000u ||
        atlas.height != 2000u || atlas.bgra.size() != 16000000u)
    {
        error = L"Cannot encode an invalid 2000 x 2000 atlas.";
        return false;
    }

    // RGBA is a native PNG encoder format. Explicit channel order avoids the
    // often-confused BGRA/BGR conversion and preserves straight alpha bytes.
    std::vector<BYTE> rgba(atlas.bgra.size());
    for (std::size_t i = 0; i < rgba.size(); i += 4u)
    {
        rgba[i] = atlas.bgra[i+2u];
        rgba[i+1u] = atlas.bgra[i+1u];
        rgba[i+2u] = atlas.bgra[i];
        rgba[i+3u] = atlas.bgra[i+3u];
    }

    const std::wstring temporary = path + L".ac_customs_tmp";
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    HRESULT hr = g_WicFactory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(
        temporary.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = g_WicFactory->CreateEncoder(
        GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(hr)) hr = frame->Initialize(properties);
    if (SUCCEEDED(hr)) hr = frame->SetSize(atlas.width, atlas.height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    const BYTE* encodedPixels = rgba.data();
    if (SUCCEEDED(hr) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        encodedPixels = atlas.bgra.data();
    else if (SUCCEEDED(hr) && !IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA))
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(atlas.height,
        atlas.width * 4u, static_cast<UINT>(rgba.size()),
        const_cast<BYTE*>(encodedPixels));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (properties) properties->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (FAILED(hr))
    {
        DeleteFileW(temporary.c_str());
        error = L"Windows Imaging Component could not encode the PNG.";
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        error = L"Could not save the generated PNG. Check the destination's permissions.";
        return false;
    }
    return true;
}

static bool TemplateExportBuildAtlas(
    const std::unordered_map<std::string, std::vector<BYTE>>* packFiles,
    const AcuiManifest* packManifest, TemplateDevImage& atlas,
    TemplateExportSummary& summary, std::wstring& error)
{
    if (g_Textures.empty() || g_DatFile == INVALID_HANDLE_VALUE)
    {
        error = L"Load the client DAT first.";
        return false;
    }
    if (!TemplateImportDecodeEmbedded(atlas, error)) return false;
    const auto& mappings = TemplateEditorActiveMappings();
    summary.mapped = mappings.size();

    // Avoid a per-DID linear catalog search when the user has a large DAT.
    std::unordered_map<std::string, const TextureRecord*> catalog;
    catalog.reserve(g_Textures.size());
    for (const TextureRecord& tex : g_Textures)
        catalog.emplace(tex.did, &tex);

    std::unordered_map<std::string, std::string> packNames;
    if (packFiles != nullptr)
    {
        if (packManifest == nullptr) { error = L"Missing ACUI pack manifest."; return false; }
        for (const AcuiTextureEntry& entry : packManifest->textures)
            packNames.emplace(entry.did, entry.file);
    }

    // Shared rectangle aliases are resolved deterministically in mapping order.
    // If two DIDs provide distinct payloads for the same slot, the first wins.
    typedef std::tuple<unsigned,unsigned,unsigned,unsigned> TemplateSlotKey;
    std::map<TemplateSlotKey, std::vector<BYTE>> paintedSlots;
    for (const TemplateImportMapping& mapping : mappings)
    {
        if (mapping.w == 0u || mapping.h == 0u ||
            mapping.x >= 2000u || mapping.y >= 2000u ||
            mapping.w > 2000u - mapping.x ||
            mapping.h > 2000u - mapping.y)
        {
            ++summary.mismatched;
            continue;
        }
        const auto found = catalog.find(mapping.did);
        if (found == catalog.end() || !IsPreviewable(*found->second) ||
            found->second->width != mapping.w ||
            found->second->height != mapping.h ||
            found->second->pixelFormat != mapping.pixelFormat)
        {
            ++summary.mismatched;
            continue;
        }
        const TextureRecord& texture = *found->second;
        std::vector<BYTE> raw;
        if (packFiles != nullptr)
        {
            const auto name = packNames.find(mapping.did);
            if (name == packNames.end()) { ++summary.missing; continue; }
            const auto contents = packFiles->find(name->second);
            if (contents == packFiles->end()) { ++summary.missing; continue; }
            raw = contents->second;
        }
        else
        {
            const std::wstring replacement = ReplacementRawPath(texture);
            if (!FileExists(replacement)) { ++summary.missing; continue; }
            if (!ReadBinaryFile(replacement, raw))
            {
                ++summary.mismatched;
                continue;
            }
        }
        const unsigned stride = mapping.pixelFormat == 0x15u ? 4u : 3u;
        const std::size_t count = static_cast<std::size_t>(mapping.w) * mapping.h;
        if ((mapping.pixelFormat != 0x14u && mapping.pixelFormat != 0x15u) ||
            raw.size() != count * stride || raw.size() != texture.imageSize)
        {
            ++summary.mismatched;
            continue;
        }
        std::vector<BYTE> bgra(count * 4u);
        for (std::size_t i = 0; i < count; ++i)
        {
            const BYTE* pixel = raw.data() + i * stride;
            BYTE* out = bgra.data() + i * 4u;
            out[0] = pixel[0]; out[1] = pixel[1]; out[2] = pixel[2];
            out[3] = stride == 4u ? pixel[3] : 255u;
        }
        const TemplateSlotKey key(mapping.x, mapping.y, mapping.w, mapping.h);
        const auto existing = paintedSlots.find(key);
        if (existing != paintedSlots.end())
        {
            if (existing->second == bgra) ++summary.sharedIdentical;
            else ++summary.sharedConflicts;
            continue;
        }
        paintedSlots.emplace(key, bgra);
        for (unsigned y = 0; y < mapping.h; ++y)
        {
            BYTE* destination = atlas.bgra.data() +
                (static_cast<std::size_t>(mapping.y+y)*2000u+mapping.x)*4u;
            const BYTE* source = bgra.data() + static_cast<std::size_t>(y)*mapping.w*4u;
            memcpy(destination, source, static_cast<std::size_t>(mapping.w)*4u);
        }
        ++summary.filled;
    }
    if (summary.filled == 0u)
    {
        error = L"No compatible mapped replacement textures were found. No PNG was written.";
        return false;
    }
    return true;
}

static bool TemplateExportEditable(bool fromPack)
{
    std::unordered_map<std::string, std::vector<BYTE>> packFiles;
    AcuiManifest manifest;
    if (fromPack)
    {
        std::wstring source;
        if (!ChooseAcuiOpen(g_MainWindow, source)) return false;
        std::wstring error;
        if (!LoadStoredZip(source, packFiles, error))
        {
            g_TemplateImportNotice = FromWide(error);
            g_TemplateImportNoticeError = true;
            return false;
        }
        const auto index = packFiles.find("manifest.json");
        if (index == packFiles.end() ||
            !ParseAcuiManifest(index->second, manifest, error) ||
            !ValidateAcuiPack(manifest, packFiles, error))
        {
            g_TemplateImportNotice = index == packFiles.end() ?
                "The selected ACUI pack has no manifest.json." : FromWide(error);
            g_TemplateImportNoticeError = true;
            return false;
        }
    }

    TemplateDevImage atlas;
    TemplateExportSummary summary;
    std::wstring error;
    if (!TemplateExportBuildAtlas(fromPack ? &packFiles : nullptr,
        fromPack ? &manifest : nullptr, atlas, summary, error))
    {
        g_TemplateImportNotice = FromWide(error);
        g_TemplateImportNoticeError = true;
        return false;
    }
    std::wstring destination;
    if (!TemplateExportChooseDestination(g_MainWindow,
        fromPack ? L"ACUI_Pack_EditableTemplate.png" :
                   L"Current_Workspace_EditableTemplate.png", destination))
        return false;
    if (!TemplateExportEncodePngAtomic(destination, atlas, error))
    {
        g_TemplateImportNotice = FromWide(error);
        g_TemplateImportNoticeError = true;
        return false;
    }
    g_TemplateImportNoticeError = false;
    g_TemplateImportNotice = "Editable PNG saved: " + FromWide(destination) +
        " | Slots filled: " + std::to_string(summary.filled) +
        " / " + std::to_string(summary.mapped) +
        " | Missing: " + std::to_string(summary.missing) +
        " | Mismatched: " + std::to_string(summary.mismatched) +
        " | Shared-slot conflicts: " + std::to_string(summary.sharedConflicts) +
        ". The source pack/workspace was not changed.";
    return true;
}

static unsigned TemplateImportCountDifferences(
    const TemplateDevImage& oldImage, const TemplateDevImage& newImage,
    const TemplateImportMapping& map)
{
    unsigned differences = 0;
    const bool compareAlpha = map.pixelFormat == 0x15u;
    for (unsigned y = 0; y < map.h; ++y)
    {
        for (unsigned x = 0; x < map.w; ++x)
        {
            const std::size_t p = ((static_cast<std::size_t>(map.y + y) *
                oldImage.width) + map.x + x) * 4u;
            const BYTE* a = oldImage.bgra.data() + p;
            const BYTE* b = newImage.bgra.data() + p;
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] ||
                (compareAlpha && a[3] != b[3]))
                ++differences;
        }
    }
    return differences;
}

// The compiled reference and compiled registry are read-only. Imported files
// never update them, the DAT, or the mapper's report files.
static bool TemplateImportLoadEditedPng(const std::wstring& path)
{
    if (g_Textures.empty() || g_DatFile == INVALID_HANDLE_VALUE)
    {
        g_TemplateImportNotice = "Load the client DAT first.";
        g_TemplateImportNoticeError = true;
        return false;
    }
    TemplateDevImage edited;
    std::wstring error;
    if (!TemplateDevLoadPng(path, edited, error))
    {
        g_TemplateImportNotice = FromWide(error);
        g_TemplateImportNoticeError = true;
        return false;
    }
    TemplateDevImage master;
    if (!TemplateImportDecodeEmbedded(master, error))
    {
        g_TemplateImportNotice = FromWide(error);
        g_TemplateImportNoticeError = true;
        return false;
    }
    std::unordered_map<std::string, std::size_t> catalog;
    catalog.reserve(g_Textures.size());
    for (std::size_t i = 0; i < g_Textures.size(); ++i)
        catalog.emplace(g_Textures[i].did, i);
    std::vector<TemplateImportCandidate> candidates;
    for (const TemplateImportMapping& map : TemplateEditorActiveMappings())
    {
        if (map.x + map.w > 2000u || map.y + map.h > 2000u) continue;
        const auto found = catalog.find(map.did);
        if (found == catalog.end()) continue;
        const TextureRecord& tex = g_Textures[found->second];
        if (tex.width != map.w || tex.height != map.h ||
            tex.pixelFormat != map.pixelFormat || !IsPreviewable(tex))
            continue;
        TemplateImportCandidate c;
        c.mapping = map;
        c.catalogIndex = found->second;
        c.changedPixels = TemplateImportCountDifferences(master, edited, map);
        if (!c.changedPixels) continue;
        c.selected = true;
        if (map.pixelFormat == 0x15u)
        {
            unsigned originalTransparent = 0;
            unsigned newOpaque = 0;
            for (unsigned y = 0; y < map.h; ++y)
                for (unsigned x = 0; x < map.w; ++x)
                {
                    const std::size_t p = ((static_cast<std::size_t>(map.y + y) *
                        2000u) + map.x + x) * 4u + 3u;
                    if (master.bgra[p] < 255u) ++originalTransparent;
                    if (edited.bgra[p] == 255u) ++newOpaque;
                }
            c.riskyAlpha = (originalTransparent != 0 &&
                newOpaque == map.w * map.h);
        }
        candidates.push_back(c);
    }
    g_TemplateImportArtwork = std::move(edited);
    g_TemplateImportSource = std::move(path);
    g_TemplateImportCandidates = std::move(candidates);
    g_TemplateImportNoticeError = false;
    g_TemplateImportNotice = "Template loaded. All changed textures are selected.";
    return true;
}

// Choosing a file is the only manual step after exporting the original.
// Subsequent saves use the same path, without another file-picker dialog.
static bool TemplateImportPrepare(HWND owner)
{
    std::wstring path;
    if (!TemplateImportSelectPng(owner, path)) return false;
    if (!TemplateImportLoadEditedPng(path)) return false;
    g_TemplateImportSeenStamp = TemplateImportStatFile(path);
    g_TemplateImportAppliedStamp = g_TemplateImportSeenStamp;
    g_TemplateImportStampChangedAt = GetTickCount64();
    return true;
}

static std::vector<BYTE> TemplateImportExtractBgra(const TemplateImportCandidate& c)
{
    std::vector<BYTE> result(static_cast<std::size_t>(c.mapping.w) *
        c.mapping.h * 4u);
    for (unsigned y = 0; y < c.mapping.h; ++y)
    {
        const BYTE* src = g_TemplateImportArtwork.bgra.data() +
            ((static_cast<std::size_t>(c.mapping.y + y) * 2000u) +
            c.mapping.x) * 4u;
        memcpy(result.data() + static_cast<std::size_t>(y) * c.mapping.w * 4u,
            src, static_cast<std::size_t>(c.mapping.w) * 4u);
    }
    return result;
}

static std::vector<BYTE> TemplateImportExtractRaw(const TemplateImportCandidate& c)
{
    const std::vector<BYTE> bgra = TemplateImportExtractBgra(c);
    if (c.mapping.pixelFormat == 0x15u) return bgra;
    const std::size_t count = bgra.size() / 4u;
    std::vector<BYTE> bgr(count * 3u);
    for (std::size_t p = 0; p < count; ++p)
        memcpy(&bgr[p * 3u], &bgra[p * 4u], 3u);
    return bgr;
}

// Prepare a complete replacement workspace in a staging folder, then swap it
// with directory renames. Preserve unrelated DIDs and keep a rollback copy.
// Regions painted back to the original master restore the replacement they had
// when this editing session first modified them (or remove our new .rgb file).
static bool TemplateImportApply(std::wstring& error)
{
    std::vector<const TemplateImportCandidate*> selected;
    std::unordered_set<std::string> selectedDids;
    for (const auto& c : g_TemplateImportCandidates)
        if (c.selected)
        {
            selected.push_back(&c);
            selectedDids.insert(c.mapping.did);
        }
    if (selected.empty() && g_TemplateImportManagedDids.empty())
        return true; // A clean template is not an error.

    const std::wstring stage = JoinPath(g_AppPaths.managerRoot,
        L"TemplateImportStage");
    const std::wstring backup = JoinPath(g_AppPaths.managerRoot,
        L"TemplateImportBackup");
    const std::wstring workspace = g_AppPaths.replacementDirectory;
    if (!DeleteDirectoryTree(stage) || !EnsureDirectoryExists(stage))
    { error = L"Cannot initialize template import staging folder."; return false; }
    try
    {
        const std::filesystem::path workPath(workspace);
        const std::filesystem::path stagePath(stage);
        if (std::filesystem::exists(workPath))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(workPath))
            {
                const auto relative = std::filesystem::relative(entry.path(), workPath);
                const auto dest = stagePath / relative;
                if (entry.is_symlink())
                    throw std::runtime_error("Symlink in replacement directory");
                if (entry.is_directory()) std::filesystem::create_directories(dest);
                else if (entry.is_regular_file())
                {
                    std::filesystem::create_directories(dest.parent_path());
                    std::filesystem::copy_file(entry.path(), dest,
                        std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
    }
    catch (...)
    {
        DeleteDirectoryTree(stage);
        error = L"Could not stage existing replacement workspace. Nothing changed.";
        return false;
    }

    // Don't mutate remembered originals unless the workspace switch succeeds.
    auto remembered = g_TemplateImportPreviousFiles;
    for (const auto* item : selected)
    {
        const TextureRecord& tex = g_Textures[item->catalogIndex];
        if (remembered.find(tex.did) == remembered.end())
        {
            TemplateImportPriorReplacement before;
            const std::wstring oldFile = JoinPath(workspace, ToWide(tex.did) + L".rgb");
            before.existed = FileExists(oldFile);
            if (before.existed && !ReadBinaryFile(oldFile, before.raw))
            {
                DeleteDirectoryTree(stage);
                error = L"Could not back up the existing replacement for DID " + ToWide(tex.did);
                return false;
            }
            remembered.emplace(tex.did, std::move(before));
        }
        const std::vector<BYTE> raw = TemplateImportExtractRaw(*item);
        if (raw.size() != tex.imageSize ||
            !WriteBinaryFile(JoinPath(stage, ToWide(tex.did) + L".rgb"), raw))
        {
            DeleteDirectoryTree(stage);
            error = L"Could not stage texture DID " + ToWide(tex.did) + L". Nothing changed.";
            return false;
        }
    }

    for (const std::string& did : g_TemplateImportManagedDids)
    {
        if (selectedDids.find(did) != selectedDids.end()) continue;
        const auto original = remembered.find(did);
        if (original == remembered.end()) continue;
        const std::wstring stagedFile = JoinPath(stage, ToWide(did) + L".rgb");
        if (original->second.existed)
        {
            if (!WriteBinaryFile(stagedFile, original->second.raw))
            {
                DeleteDirectoryTree(stage);
                error = L"Could not restore the prior replacement for DID " + ToWide(did);
                return false;
            }
        }
        else if (FileExists(stagedFile) && !DeleteFileW(stagedFile.c_str()))
        {
            DeleteDirectoryTree(stage);
            error = L"Could not remove reverted replacement for DID " + ToWide(did);
            return false;
        }
        remembered.erase(original);
    }

    if (!DeleteDirectoryTree(backup))
    {
        DeleteDirectoryTree(stage);
        error = L"Could not clear previous template-import backup.";
        return false;
    }
    const bool hadWorkspace = DirectoryExists(workspace);
    if (hadWorkspace && !MoveFileExW(workspace.c_str(), backup.c_str(),
        MOVEFILE_WRITE_THROUGH))
    {
        DeleteDirectoryTree(stage);
        error = L"Could not back up the existing replacement workspace.";
        return false;
    }
    if (!MoveFileExW(stage.c_str(), workspace.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        if (hadWorkspace)
            MoveFileExW(backup.c_str(), workspace.c_str(), MOVEFILE_WRITE_THROUGH);
        error = L"Could not activate staged artwork. Check the backup folder.";
        return false;
    }
    g_TemplateImportPreviousFiles = std::move(remembered);
    g_TemplateImportManagedDids = std::move(selectedDids);
    return true;
}
#endif // AC_CUSTOMS_TEMPLATE_DEVTOOLS
