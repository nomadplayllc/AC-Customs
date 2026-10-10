// AC Customs - developer-only editable coordinate registry.
// Included by ACModernUITemplateImporterDev.inl immediately after the
// generated, immutable ACModernUITemplateEmbedded.inl.
#include <iomanip>
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS
struct TemplateEditorEntry
{
    std::string did;
    unsigned x = 0, y = 0, w = 0, h = 0, pixelFormat = 0x14u;
    bool exact = false;
};

static std::vector<TemplateEditorEntry> g_TemplateEditorEntries;
static std::vector<TemplateImportMapping> g_TemplateEditorRuntime;
static bool g_TemplateEditorInitialized = false;
static bool g_TemplateEditorCustomized = false;
static std::string g_TemplateEditorStatus;
static bool g_TemplateEditorError = false;
// Updated by the active master-template loader. The embedded fallback keeps
// the historical CRC until an external default master is adopted.
static std::uint32_t g_TemplateEditorMasterCrc32 = 0x8436C7CCu;

static std::wstring TemplateEditorMappingPath()
{
    return JoinPath(g_AppPaths.managerRoot, L"template_mapping_overrides.dev.json");
}

static bool TemplateEditorValidDid(const std::string& did)
{
    if (did.size() != 8u || did.substr(0, 2) != "06") return false;
    for (char c : did)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            return false;
    return true;
}

static std::string TemplateEditorNormalizeDid(std::string did)
{
    if (did.size() == 10u && did.substr(0, 2) == "0x") did.erase(0, 2);
    for (char& c : did) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return did;
}

static bool TemplateEditorValidate(const TemplateEditorEntry& entry)
{
    return TemplateEditorValidDid(entry.did) &&
        (entry.pixelFormat == 0x14u || entry.pixelFormat == 0x15u) &&
        entry.w > 0 && entry.h > 0 &&
        entry.x < 2000 && entry.y < 2000 &&
        entry.w <= 2000u - entry.x && entry.h <= 2000u - entry.y;
}

static const TextureRecord* TemplateEditorFindTexture(const std::string& did)
{
    for (const TextureRecord& texture : g_Textures)
        if (texture.did == did && IsPreviewable(texture))
            return &texture;
    return nullptr;
}

static bool TemplateEditorValidateAgainstDat(const TemplateEditorEntry& entry)
{
    const TextureRecord* texture = TemplateEditorFindTexture(entry.did);
    return texture && texture->width == entry.w &&
        texture->height == entry.h && texture->pixelFormat == entry.pixelFormat;
}

static void TemplateEditorBuildRuntime()
{
    g_TemplateEditorRuntime.clear();
    g_TemplateEditorRuntime.reserve(g_TemplateEditorEntries.size());
    for (const TemplateEditorEntry& entry : g_TemplateEditorEntries)
        g_TemplateEditorRuntime.push_back(TemplateImportMapping{
            entry.did.c_str(), entry.x, entry.y, entry.w, entry.h,
            entry.pixelFormat, entry.exact});
}

static void TemplateEditorRestoreEmbedded()
{
    g_TemplateEditorEntries.clear();
    for (const TemplateImportMapping& map : kTemplateImportMappings)
        g_TemplateEditorEntries.push_back(TemplateEditorEntry{
            map.did, map.x, map.y, map.w, map.h, map.pixelFormat, map.exact});
    TemplateEditorBuildRuntime();
    g_TemplateEditorCustomized = false;
}

static bool TemplateEditorParseFile(const std::vector<BYTE>& bytes,
                                    std::vector<TemplateEditorEntry>& result)
{
    const std::string json(bytes.begin(), bytes.end());
    std::uint32_t formatVersion = 0, width = 0, height = 0, crc = 0;
    if (!JsonUIntField(json, "formatVersion", formatVersion) || formatVersion != 1u ||
        !JsonUIntField(json, "width", width) || width != 2000u ||
        !JsonUIntField(json, "height", height) || height != 2000u ||
        !JsonHexField(json, "pixelCrc32", crc) || crc == 0u)
        return false;
    std::size_t pos = 0;
    if (!FindJsonFieldValue(json, "textures", pos) ||
        pos >= json.size() || json[pos++] != '[') return false;
    std::unordered_set<std::string> used;
    result.clear();
    for (;;)
    {
        JsonSkipWhitespace(json, pos);
        if (pos >= json.size()) return false;
        if (json[pos] == ']') { ++pos; break; }
        if (json[pos] == ',') { ++pos; continue; }
        std::string object;
        if (!ExtractJsonObject(json, pos, object)) return false;
        TemplateEditorEntry entry;
        std::uint32_t x = 0, y = 0, w = 0, h = 0, pixelFormat = 0;
        std::string did, method;
        if (!JsonStringField(object, "did", did) ||
            !JsonUIntField(object, "x", x) || !JsonUIntField(object, "y", y) ||
            !JsonUIntField(object, "width", w) || !JsonUIntField(object, "height", h) ||
            !JsonHexField(object, "pixelFormat", pixelFormat) ||
            !JsonStringField(object, "method", method, false))
            return false;
        entry.did = TemplateEditorNormalizeDid(did);
        entry.x = x; entry.y = y; entry.w = w; entry.h = h;
        entry.pixelFormat = pixelFormat;
        entry.exact = (method == "exact");
        if (!TemplateEditorValidate(entry) || !used.insert(entry.did).second)
            return false;
        result.push_back(std::move(entry));
        if (result.size() > 50000u) return false;
    }
    JsonSkipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '}') return false;
    return true;
}

static void TemplateEditorEnsureLoaded()
{
    if (g_TemplateEditorInitialized) return;
    g_TemplateEditorInitialized = true;
    TemplateEditorRestoreEmbedded();
    const std::wstring path = TemplateEditorMappingPath();
    if (!FileExists(path)) return;
    std::vector<BYTE> bytes;
    std::vector<TemplateEditorEntry> parsed;
    if (!ReadBinaryFile(path, bytes) || !TemplateEditorParseFile(bytes, parsed))
    {
        g_TemplateEditorStatus = "Custom mapping JSON is invalid; using embedded mappings. File not overwritten.";
        g_TemplateEditorError = true;
        return;
    }
    g_TemplateEditorEntries = std::move(parsed);
    TemplateEditorBuildRuntime();
    g_TemplateEditorCustomized = true;
    g_TemplateEditorStatus = "Loaded editable mapping overrides.";
}

static const std::vector<TemplateImportMapping>& TemplateEditorActiveMappings()
{
    TemplateEditorEnsureLoaded();
    return g_TemplateEditorRuntime;
}

static bool TemplateEditorBuildMappingJson(std::string& json)
{
    TemplateEditorEnsureLoaded();
    if (g_TemplateEditorEntries.empty())
    {
        g_TemplateEditorStatus = "At least one mapped texture is required to export a usable plugin mapping.";
        g_TemplateEditorError = true;
        return false;
    }
    std::unordered_set<std::string> seen;
    for (const auto& entry : g_TemplateEditorEntries)
        if (!TemplateEditorValidate(entry) || !seen.insert(entry.did).second)
        {
            g_TemplateEditorStatus = "Duplicate DID or invalid mapping coordinates.";
            g_TemplateEditorError = true;
            return false;
        }
    std::ostringstream out;
    out << "{\n  \"formatVersion\": 1,\n  \"width\": 2000,\n"
        << "  \"height\": 2000,\n  \"pixelCrc32\": \"0x"
        << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << g_TemplateEditorMasterCrc32
        << std::dec << "\",\n"
        << "  \"textures\": [\n";
    for (std::size_t i = 0; i < g_TemplateEditorEntries.size(); ++i)
    {
        const auto& e = g_TemplateEditorEntries[i];
        out << "    {\"did\": \"" << e.did << "\", \"x\": " << e.x
            << ", \"y\": " << e.y << ", \"width\": " << e.w
            << ", \"height\": " << e.h << ", \"pixelFormat\": \"0x"
            << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
            << e.pixelFormat << std::dec << "\", \"method\": \""
            << (e.exact ? "exact" : "manual") << "\"}"
            << (i + 1 == g_TemplateEditorEntries.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    json = out.str();
    return true;
}

static bool TemplateEditorWriteJsonFile(const std::wstring& destination,
                                        const std::string& json)
{
    const std::vector<BYTE> bytes(json.begin(), json.end());
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

static bool TemplateEditorSave()
{
    std::string json;
    if (!TemplateEditorBuildMappingJson(json)) return false;
    const std::wstring path = TemplateEditorMappingPath();
    if (!EnsureDirectoryExists(g_AppPaths.managerRoot))
    {
        g_TemplateEditorStatus = "Failed to create the Manager settings directory.";
        g_TemplateEditorError = true;
        return false;
    }
    if (FileExists(path))
        CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
    if (!TemplateEditorWriteJsonFile(path, json))
    {
        g_TemplateEditorStatus = "Could not replace mapping JSON. Check permissions.";
        g_TemplateEditorError = true;
        return false;
    }
    TemplateEditorBuildRuntime();
    g_TemplateEditorCustomized = true;
    g_TemplateEditorError = false;
    g_TemplateEditorStatus = "Mapping changes saved. The live importer now uses them.";
    return true;
}

// Export the complete CURRENT mapping set. This JSON has the same schema as
// the Manager's local override and the Decal plugin's template_mapping.json.
static bool TemplateEditorExportMappings(HWND owner)
{
    std::string json;
    if (!TemplateEditorBuildMappingJson(json)) return false;

    wchar_t filename[32768] = L"template_mapping.json";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"AC Customs Mapping JSON (*.json)\0*.json\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrTitle = L"Export Complete Template Mapping for AC Customs Plugin";
    dialog.lpstrDefExt = L"json";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT |
        OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return false;

    const std::wstring destination = filename;
    if (!TemplateEditorWriteJsonFile(destination, json))
    {
        g_TemplateEditorError = true;
        g_TemplateEditorStatus = "Could not export mapping JSON. Check destination permissions.";
        return false;
    }
    g_TemplateEditorError = false;
    g_TemplateEditorStatus = "Exported " +
        std::to_string(g_TemplateEditorEntries.size()) +
        " mappings for the current master template.";
    if (owner && IsWindow(owner))
    {
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
    }
    return true;
}

static bool TemplateEditorResetToEmbedded()
{
    TemplateEditorEnsureLoaded();
    const std::wstring path = TemplateEditorMappingPath();
    if (FileExists(path) && !DeleteFileW(path.c_str()))
    {
        g_TemplateEditorStatus = "Could not delete custom map. Nothing reset.";
        g_TemplateEditorError = true;
        return false;
    }
    TemplateEditorRestoreEmbedded();
    g_TemplateEditorError = false;
    g_TemplateEditorStatus = "Restored all embedded mappings.";
    return true;
}
#endif
