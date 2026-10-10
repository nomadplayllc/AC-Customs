// AC Customs - master-template auto-map v0.2 (collision audit), DEVELOPMENT BUILD ONLY.
// Included from ACModernUIManager.cpp after the DAT/ACUI helpers and before
// ACModernUIModern.inl. Disable AC_CUSTOMS_TEMPLATE_DEVTOOLS before release.
#if AC_CUSTOMS_TEMPLATE_DEVTOOLS

static const int TEMPLATE_DEV_HOTKEY_ID = 0x6A31;
static std::atomic<bool> g_TemplateDevCancel{false};
static std::atomic<bool> g_TemplateDevDone{false};
static std::atomic<std::size_t> g_TemplateDevProgress{0};
static std::thread g_TemplateDevThread;
static std::mutex g_TemplateDevReportMutex;
static std::wstring g_TemplateDevReport;

struct TemplateDevImage
{
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> bgra;
};

struct TemplateDevMatch
{
    int x = -1;
    int y = -1;
    int count = 0; // 0=none, 1=unique, >=2=ambiguous
    const char* mode = "none";
    const char* source = "none";
};

static bool TemplateDevLoadPng(const std::wstring& filename,
                               TemplateDevImage& image,
                               std::wstring& error)
{
    image = TemplateDevImage{};
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT hr = g_WicFactory->CreateDecoderFromFilename(
        filename.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = frame->GetSize(&image.width, &image.height);
    if (SUCCEEDED(hr) &&
        (image.width != 2000 || image.height != 2000))
    {
        error = L"This development mapper requires the current 2000 x 2000 template PNG.";
        hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr)) hr = g_WicFactory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(
        frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
    {
        const std::size_t bytes =
            static_cast<std::size_t>(image.width) * image.height * 4u;
        image.bgra.resize(bytes);
        hr = converter->CopyPixels(nullptr, image.width * 4u,
                                   static_cast<UINT>(bytes), image.bgra.data());
    }
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (FAILED(hr))
    {
        if (error.empty()) error = L"Could not decode the PNG as 32-bit BGRA.";
        image = TemplateDevImage{};
        return false;
    }
    return true;
}

static bool TemplateDevLoadReplacement(const TextureRecord& record,
                                      std::vector<BYTE>& pixels)
{
    pixels.clear();
    std::vector<BYTE> raw;
    if (!HasReplacement(record) ||
        !ReadBinaryFile(ReplacementRawPath(record), raw) ||
        raw.size() != record.imageSize)
        return false;
    const std::size_t n = static_cast<std::size_t>(record.width) * record.height;
    if (record.pixelFormat == 0x15u && raw.size() == n * 4u)
    {
        pixels = std::move(raw);
        return true;
    }
    if (record.pixelFormat != 0x14u || raw.size() != n * 3u)
        return false;
    pixels.resize(n * 4u);
    for (std::size_t i = 0; i < n; ++i)
    {
        pixels[i * 4u] = raw[i * 3u];
        pixels[i * 4u + 1u] = raw[i * 3u + 1u];
        pixels[i * 4u + 2u] = raw[i * 3u + 2u];
        pixels[i * 4u + 3u] = 255u;
    }
    return true;
}

static std::uint32_t TemplateDevRgb(const BYTE* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u);
}

// Index only visible pixels in the template. Avoids scanning 4M pixels for
// every candidate texture and excludes the transparent guide background.
using TemplateDevColorIndex =
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>;

static TemplateDevColorIndex TemplateDevBuildIndex(const TemplateDevImage& image)
{
    TemplateDevColorIndex index;
    index.reserve(80000u);
    const std::size_t count = image.bgra.size() / 4u;
    for (std::size_t i = 0; i < count; ++i)
    {
        const BYTE* p = &image.bgra[i * 4u];
        if (p[3] == 255u)
            index[TemplateDevRgb(p)].push_back(static_cast<std::uint32_t>(i));
    }
    return index;
}

static bool TemplateDevCompareAt(const TemplateDevImage& image,
                                 const TextureRecord& record,
                                 const std::vector<BYTE>& source,
                                 int left, int top,
                                 bool opaqueMask)
{
    const std::size_t width = record.width;
    const std::size_t height = record.height;
    const std::size_t srcStride = width * 4u;
    const std::size_t dstStride = static_cast<std::size_t>(image.width) * 4u;
    const bool bgraFormat = record.pixelFormat == 0x15u;
    for (std::size_t y = 0; y < height; ++y)
    {
        const BYTE* s = source.data() + y * srcStride;
        const BYTE* d = image.bgra.data() +
            (static_cast<std::size_t>(top) + y) * dstStride +
            static_cast<std::size_t>(left) * 4u;
        for (std::size_t x = 0; x < width; ++x, s += 4, d += 4)
        {
            if (bgraFormat && opaqueMask && s[3] != 255u)
                continue; // Fallback checks only completely opaque source pixels.
            if (s[0] != d[0] || s[1] != d[1] || s[2] != d[2])
            {
                if (!bgraFormat || !opaqueMask || s[3] != 0u)
                    return false;
            }
            if (bgraFormat && !opaqueMask && s[3] != d[3])
                return false;
            if (opaqueMask && d[3] != 255u)
                return false;
        }
    }
    return true;
}

static TemplateDevMatch TemplateDevSearch(const TemplateDevImage& image,
                                          const TemplateDevColorIndex& index,
                                          const TextureRecord& record,
                                          const std::vector<BYTE>& source,
                                          const char* sourceName)
{
    TemplateDevMatch result;
    if (record.width == 0 || record.height == 0 ||
        record.width > image.width || record.height > image.height ||
        source.size() != static_cast<std::size_t>(record.width) * record.height * 4u)
        return result;

    // Find a rare, completely opaque source pixel as an anchor. At least 8
    // opaque pixels and 3 distinct colors are required to reject trivial hits.
    std::size_t anchor = static_cast<std::size_t>(-1);
    std::size_t bestFrequency = static_cast<std::size_t>(-1);
    std::unordered_set<std::uint32_t> colors;
    std::size_t opaqueCount = 0;
    const std::size_t n = source.size() / 4u;
    for (std::size_t i = 0; i < n; ++i)
    {
        const BYTE* p = &source[i * 4u];
        if (p[3] != 255u) continue;
        ++opaqueCount;
        const std::uint32_t rgb = TemplateDevRgb(p);
        if (colors.size() < 3u) colors.insert(rgb);
        const auto found = index.find(rgb);
        if (found == index.end()) continue;
        if (found->second.size() < bestFrequency)
        {
            bestFrequency = found->second.size();
            anchor = i;
        }
    }
    if (opaqueCount < 8u || colors.size() < 3u ||
        anchor == static_cast<std::size_t>(-1))
        return result;

    const auto positions = index.find(TemplateDevRgb(&source[anchor * 4u]));
    if (positions == index.end()) return result;

    // Pass 1: exact pixels (including alpha for BGRA). For BGR, alpha is
    // deliberately ignored: those textures are opaque in the AC DAT format.
    // Pass 2: for BGRA only, compare opaque source pixels. This recovers
    // sprites composited onto the template but remains flagged as lower trust.
    const int passCount = record.pixelFormat == 0x15u ? 2 : 1;
    for (int pass = 0; pass < passCount; ++pass)
    {
        if (g_TemplateDevCancel.load()) return TemplateDevMatch{};
        TemplateDevMatch current;
        current.source = sourceName;
        current.mode = pass == 0 ? "exact" : "opaque-mask";
        // Masked comparisons must have enough opaque coverage to be distinctive.
        if (pass == 1 && opaqueCount < std::max<std::size_t>(16u, n / 10u))
            break;
        for (const std::uint32_t pixel : positions->second)
        {
            if (g_TemplateDevCancel.load()) return TemplateDevMatch{};
            const std::size_t x = static_cast<std::size_t>(pixel % image.width);
            const std::size_t y = static_cast<std::size_t>(pixel / image.width);
            const std::size_t anchorX = anchor % record.width;
            const std::size_t anchorY = anchor / record.width;
            if (x < anchorX || y < anchorY) continue;
            const int left = static_cast<int>(x - anchorX);
            const int top = static_cast<int>(y - anchorY);
            if (static_cast<std::size_t>(left) + record.width > image.width ||
                static_cast<std::size_t>(top) + record.height > image.height)
                continue;
            if (!TemplateDevCompareAt(image, record, source,
                                      left, top, pass != 0))
                continue;
            if (current.count == 0)
            {
                current.x = left;
                current.y = top;
            }
            ++current.count;
            if (current.count >= 2) break; // ambiguous; never choose first
        }
        if (current.count > 0)
            return current;
    }
    return result;
}

static std::string TemplateDevCsvEscape(const std::string& value)
{
    std::string escaped = "\"";
    for (char ch : value)
    {
        if (ch == '"') escaped += '"';
        escaped += ch;
    }
    return escaped + "\"";
}

// Scan results are collected before generating the mapping. A pattern can
// match exactly yet still overlap a different texture's expected rectangle.
// Do not silently choose between conflicting atlas assignments.
struct TemplateDevScanRow
{
    const TextureRecord* record = nullptr;
    TemplateDevMatch match;
    bool skipped = false;
    bool overlapConflict = false;
    std::string overlapsWith;
};

static bool TemplateDevRectanglesOverlap(
    const TemplateDevScanRow& a, const TemplateDevScanRow& b)
{
    const auto& ar = *a.record;
    const auto& br = *b.record;
    return a.match.x < b.match.x + static_cast<int>(br.width) &&
           b.match.x < a.match.x + static_cast<int>(ar.width) &&
           a.match.y < b.match.y + static_cast<int>(br.height) &&
           b.match.y < a.match.y + static_cast<int>(ar.height);
}

static bool TemplateDevSameRectangle(
    const TemplateDevScanRow& a, const TemplateDevScanRow& b)
{
    return a.match.x == b.match.x && a.match.y == b.match.y &&
           a.record->width == b.record->width &&
           a.record->height == b.record->height;
}

static void TemplateDevWorker(TemplateDevImage image)
{
    std::wstring report;
    const auto started = std::chrono::steady_clock::now();
    const TemplateDevColorIndex index = TemplateDevBuildIndex(image);
    std::vector<TemplateDevScanRow> rows;
    rows.reserve(g_Textures.size());
    for (const TextureRecord& record : g_Textures)
    {
        if (g_TemplateDevCancel.load()) break;
        ++g_TemplateDevProgress;
        TemplateDevScanRow row;
        row.record = &record;
        if (!IsPreviewable(record) || !record.width || !record.height ||
            record.width > image.width || record.height > image.height)
        {
            row.skipped = true;
            rows.push_back(std::move(row));
            continue;
        }
        std::vector<BYTE> source;
        // Prefer existing replacements; fall back to the unmodified DAT.
        if (TemplateDevLoadReplacement(record, source))
            row.match = TemplateDevSearch(
                image, index, record, source, "replacement");
        if (row.match.count == 0 && LoadDatTexturePixels(record, source))
            row.match = TemplateDevSearch(
                image, index, record, source, "original-dat");
        rows.push_back(std::move(row));
    }

    // Collision audit. Strict-exact aliases occupying the same rectangle are
    // legitimate: two DIDs may refer to pixel-identical source artwork.
    // A partially overlapping exact match, however, cannot represent two
    // independent template slots. Lower-trust opaque-mask matches are rejected
    // whenever they intersect any other candidate, even an exact match.
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].match.count != 1) continue;
        for (std::size_t j = i + 1; j < rows.size(); ++j)
        {
            if (rows[j].match.count != 1 ||
                !TemplateDevRectanglesOverlap(rows[i], rows[j]))
                continue;
            const bool aExact = std::strcmp(rows[i].match.mode, "exact") == 0;
            const bool bExact = std::strcmp(rows[j].match.mode, "exact") == 0;
            if (aExact && bExact && TemplateDevSameRectangle(rows[i], rows[j]))
                continue; // shared image slot (DID alias), record both
            if (!aExact || !bExact)
            {
                if (!aExact)
                {
                    rows[i].overlapConflict = true;
                    if (!rows[i].overlapsWith.empty()) rows[i].overlapsWith += ';';
                    rows[i].overlapsWith += rows[j].record->did;
                }
                if (!bExact)
                {
                    rows[j].overlapConflict = true;
                    if (!rows[j].overlapsWith.empty()) rows[j].overlapsWith += ';';
                    rows[j].overlapsWith += rows[i].record->did;
                }
            }
            else // two non-identical strict matches: both require review
            {
                rows[i].overlapConflict = rows[j].overlapConflict = true;
                if (!rows[i].overlapsWith.empty()) rows[i].overlapsWith += ';';
                if (!rows[j].overlapsWith.empty()) rows[j].overlapsWith += ';';
                rows[i].overlapsWith += rows[j].record->did;
                rows[j].overlapsWith += rows[i].record->did;
            }
        }
    }

    std::ostringstream json, csv;
    json << "{\n  \"formatVersion\": 1,\n"
         << "  \"width\": " << image.width << ",\n"
         << "  \"height\": " << image.height << ",\n"
         << "  \"pixelCrc32\": \"0x"
         << Hex8(AcuiCrc32(image.bgra.data(), image.bgra.size())) << "\",\n"
         << "  \"textures\": [\n";
    csv << "DID,Width,Height,PixelFormat,Status,Source,Method,X,Y,OverlapWith\n";
    std::size_t matched = 0, ambiguous = 0, unmatched = 0;
    std::size_t overlapsExcluded = 0, skipped = 0;
    for (const TemplateDevScanRow& row : rows)
    {
        const TextureRecord& record = *row.record;
        if (row.skipped) { ++skipped; continue; }
        std::string status;
        if (row.match.count == 1 && !row.overlapConflict)
        {
            if (matched++) json << ",\n";
            json << "    {\"did\": \"" << record.did << "\", "
                 << "\"x\": " << row.match.x << ", \"y\": " << row.match.y
                 << ", \"width\": " << record.width
                 << ", \"height\": " << record.height
                 << ", \"pixelFormat\": \"0x" << Hex8(record.pixelFormat)
                 << "\", \"source\": \"" << row.match.source
                 << "\", \"method\": \"" << row.match.mode << "\"}";
            status = "mapped";
        }
        else if (row.overlapConflict)
        {
            ++overlapsExcluded;
            status = "overlap-excluded";
        }
        else if (row.match.count >= 2)
        {
            ++ambiguous;
            status = "ambiguous";
        }
        else { ++unmatched; status = "unmatched"; }

        csv << record.did << ',' << record.width << ',' << record.height
            << ",0x" << Hex8(record.pixelFormat) << ',' << status << ','
            << row.match.source << ',' << row.match.mode << ',';
        // Record candidate positions for overlap-excluded results too: useful
        // for investigating false positives, but NOT present in the JSON map.
        if (row.match.count == 1)
            csv << row.match.x << ',' << row.match.y;
        else csv << ',';
        csv << ',' << TemplateDevCsvEscape(row.overlapsWith) << '\n';
    }
    json << "\n  ]\n}\n";

    // Non-overwriting rule applies only to protected app data. The two .dev
    // mapping report files are regenerated on each successful scan.
    const std::wstring jsonPath = JoinPath(
        g_AppPaths.managerRoot, L"template_auto_map.dev.json");
    const std::wstring csvPath = JoinPath(
        g_AppPaths.managerRoot, L"template_auto_map_report.dev.csv");
    bool wroteJson = false, wroteCsv = false;
    if (!g_TemplateDevCancel.load())
    {
        std::ofstream outJson(std::filesystem::path(jsonPath),
                              std::ios::binary | std::ios::trunc);
        if (outJson)
        {
            outJson << json.str();
            outJson.flush();
            wroteJson = outJson.good();
        }
        std::ofstream outCsv(std::filesystem::path(csvPath),
                             std::ios::binary | std::ios::trunc);
        if (outCsv)
        {
            outCsv << csv.str();
            outCsv.flush();
            wroteCsv = outCsv.good();
        }
    }
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started).count();
    if (g_TemplateDevCancel.load()) report = L"Template scan cancelled.";
    else
    {
        report = L"Developer template auto-map complete.\r\n\r\n"
            L"DIDs mapped: " + std::to_wstring(matched) +
            L"\r\nOverlap conflicts (excluded): " + std::to_wstring(overlapsExcluded) +
            L"\r\nAmbiguous (excluded): " + std::to_wstring(ambiguous) +
            L"\r\nUnmatched: " + std::to_wstring(unmatched) +
            L"\r\nSkipped: " + std::to_wstring(skipped) +
            L"\r\nElapsed: " + std::to_wstring(seconds) + L" seconds\r\n\r\n";
        report += wroteJson && wroteCsv
            ? (L"Mapping:\r\n" + jsonPath + L"\r\n\r\nReport:\r\n" + csvPath)
            : L"ERROR: Could not save one or both output files.";
    }
    {
        std::lock_guard<std::mutex> lock(g_TemplateDevReportMutex);
        g_TemplateDevReport = std::move(report);
    }
    g_TemplateDevDone.store(true);
}

static void TemplateDevStart(HWND owner)
{
    if (g_TemplateDevRunning.load())
    {
        MessageBoxW(owner, L"A master-template scan is already running.",
                    L"Template Auto-Map (Developer)", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (g_Textures.empty() || g_DatFile == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(owner, L"Load client_portal.dat before auto-mapping.",
                    L"Template Auto-Map (Developer)", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring filename;
    if (!ChooseReplacementPng(owner, filename)) return;
    TemplateDevImage image;
    std::wstring error;
    if (!TemplateDevLoadPng(filename, image, error))
    {
        MessageBoxW(owner, error.c_str(), L"Template Auto-Map (Developer)",
                    MB_OK | MB_ICONERROR);
        return;
    }
    g_TemplateDevCancel.store(false);
    g_TemplateDevDone.store(false);
    g_TemplateDevProgress.store(0);
    g_TemplateDevRunning.store(true);
    g_TemplateDevThread = std::thread(TemplateDevWorker, std::move(image));
    MessageBoxW(owner,
        L"Scanning the 2000 x 2000 PNG against known texture DIDs.\r\n\r\n"
        L"The scan runs in the background. Its progress is shown in the title bar.\r\n"
        L"No replacement files will be changed.",
        L"Template Auto-Map (Developer)", MB_OK | MB_ICONINFORMATION);
}

static void TemplateDevTick(HWND owner)
{
    if (!g_TemplateDevRunning.load()) return;
    if (!g_TemplateDevDone.exchange(false))
    {
        const std::wstring status = L"AC Customs - Auto-mapping: " +
            std::to_wstring(g_TemplateDevProgress.load()) + L" / " +
            std::to_wstring(g_Textures.size());
        SetWindowTextW(owner, status.c_str());
        return;
    }
    if (g_TemplateDevThread.joinable()) g_TemplateDevThread.join();
    g_TemplateDevRunning.store(false);
    SetWindowTextW(owner, WINDOW_TITLE);
    std::wstring report;
    {
        std::lock_guard<std::mutex> lock(g_TemplateDevReportMutex);
        report = g_TemplateDevReport;
    }
    MessageBoxW(owner, report.c_str(), L"Template Auto-Map (Developer)",
                MB_OK | MB_ICONINFORMATION);
}

static void TemplateDevShutdown(HWND owner)
{
    UnregisterHotKey(owner, TEMPLATE_DEV_HOTKEY_ID);
    g_TemplateDevCancel.store(true);
    if (g_TemplateDevThread.joinable()) g_TemplateDevThread.join();
    g_TemplateDevRunning.store(false);
}
#endif // AC_CUSTOMS_TEMPLATE_DEVTOOLS
