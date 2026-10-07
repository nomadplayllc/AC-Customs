// AC Customs PNG -> raw texture converter
//
// The Manager launches this helper out-of-process when a replacement PNG is
// selected. Public builds use the explicit `replace` command, which receives
// texture metadata and the destination path from the Manager. This intentionally
// avoids any dependency on the old development-only dat_textures.csv catalog.

#include <Windows.h>

#include <wincodec.h>



#include <cstdint>

#include <cwctype>

#include <fstream>

#include <iomanip>

#include <iostream>

#include <sstream>

#include <string>

#include <vector>



#pragma comment(lib, "windowscodecs.lib")

#pragma comment(lib, "ole32.lib")



static const wchar_t* METADATA_PATH =

    L"dat_textures.csv";



static const wchar_t* REPLACEMENT_DIRECTORY =

    L"textures";



static const wchar_t* CAPTURE_DIRECTORY =

    L"captured";



enum class OutputFormat

{

    BGR,

    BGRA

};



struct TextureMetadata

{

    std::uint32_t did = 0;

    std::uint32_t width = 0;

    std::uint32_t height = 0;

    std::uint32_t imageSize = 0;

    std::uint32_t pixelFormat = 0;

    std::uint32_t formatInfo = 0;

    std::uint32_t paletteDID = 0;

};



static std::wstring DIDName(

    std::uint32_t did)

{

    std::wostringstream out;



    out << std::uppercase

        << std::hex

        << std::setfill(L'0')

        << std::setw(8)

        << did;



    return out.str();

}



static bool ParseHex32(

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



static bool ParseDecimal32(

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



static std::vector<std::string> SplitCSV(

    const std::string& line)

{

    std::vector<std::string> fields;

    std::stringstream input(line);

    std::string field;



    while (std::getline(input, field, ','))

        fields.push_back(field);



    return fields;

}



static bool LoadTextureMetadata(

    std::uint32_t requestedDID,

    TextureMetadata& metadata)

{

    std::ifstream input(METADATA_PATH);



    if (!input.is_open())

    {

        std::wcerr

            << L"ERROR: Could not open texture catalog:\n  "

            << METADATA_PATH

            << L"\n";



        return false;

    }



    std::string line;



    // Skip header.

    if (!std::getline(input, line))

    {

        std::wcerr

            << L"ERROR: Texture catalog is empty.\n";



        return false;

    }



    while (std::getline(input, line))

    {

        if (line.empty())

            continue;



        const std::vector<std::string> fields =

            SplitCSV(line);



        if (fields.size() != 7)

            continue;



        TextureMetadata candidate;



        if (!ParseHex32(fields[0], candidate.did) ||

            !ParseDecimal32(fields[1], candidate.width) ||

            !ParseDecimal32(fields[2], candidate.height) ||

            !ParseDecimal32(fields[3], candidate.imageSize) ||

            !ParseHex32(fields[4], candidate.pixelFormat) ||

            !ParseHex32(fields[6], candidate.paletteDID))

        {

            continue;

        }



        if (candidate.did == requestedDID)

        {

            metadata = candidate;

            return true;

        }

    }



    std::wcerr

        << L"ERROR: DID "

        << DIDName(requestedDID)

        << L" was not found in dat_textures.csv.\n";



    return false;

}





static bool LoadAllTextureMetadata(

    std::vector<TextureMetadata>& textures)

{

    std::ifstream input(METADATA_PATH);



    if (!input.is_open())

    {

        std::wcerr

            << L"ERROR: Could not open texture catalog:\n  "

            << METADATA_PATH

            << L"\n";

        return false;

    }



    std::string line;



    // Skip header.

    if (!std::getline(input, line))

    {

        std::wcerr << L"ERROR: Texture catalog is empty.\n";

        return false;

    }



    while (std::getline(input, line))

    {

        if (line.empty())

            continue;



        const std::vector<std::string> fields = SplitCSV(line);



        if (fields.size() != 7)

            continue;



        TextureMetadata candidate;



        if (!ParseHex32(fields[0], candidate.did) ||

            !ParseDecimal32(fields[1], candidate.width) ||

            !ParseDecimal32(fields[2], candidate.height) ||

            !ParseDecimal32(fields[3], candidate.imageSize) ||

            !ParseHex32(fields[4], candidate.pixelFormat) ||

            !ParseHex32(fields[6], candidate.paletteDID))

        {

            continue;

        }



        textures.push_back(candidate);

    }



    return true;

}





static bool ParseDID(

    const wchar_t* text,

    std::uint32_t& did)

{

    if (text == nullptr)

        return false;



    std::wstring value(text);



    if (value.size() >= 2 &&

        value[0] == L'0' &&

        (value[1] == L'x' ||

         value[1] == L'X'))

    {

        value.erase(0, 2);

    }



    if (value.empty() ||

        value.size() > 8)

    {

        return false;

    }



    for (wchar_t c : value)

    {

        if (!std::iswxdigit(c))

            return false;

    }



    try

    {

        const unsigned long parsed =

            std::stoul(

                value,

                nullptr,

                16);



        did =

            static_cast<std::uint32_t>(

                parsed);



        return true;

    }

    catch (...)

    {

        return false;

    }

}



static bool ParseUnsigned32(
    const wchar_t* text,
    std::uint32_t& value)
{
    if (text == nullptr || *text == L'\0')
        return false;

    try
    {
        const std::wstring input(text);
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(input, &consumed, 0);

        if (consumed != input.size())
            return false;

        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}


static bool ConvertPng(

    const wchar_t* inputPath,

    const std::wstring& outputPath,

    const TextureMetadata& metadata,

    OutputFormat format)

{

    HRESULT hr =

        CoInitializeEx(

            nullptr,

            COINIT_MULTITHREADED);



    const bool uninitialize =

        SUCCEEDED(hr);



    if (FAILED(hr) &&

        hr != RPC_E_CHANGED_MODE)

    {

        std::wcerr

            << L"ERROR: CoInitializeEx failed.\n";



        return false;

    }



    IWICImagingFactory* factory = nullptr;



    hr = CoCreateInstance(

        CLSID_WICImagingFactory,

        nullptr,

        CLSCTX_INPROC_SERVER,

        IID_PPV_ARGS(&factory));



    if (FAILED(hr) ||

        factory == nullptr)

    {

        std::wcerr

            << L"ERROR: Could not create WIC factory.\n";



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    IWICBitmapDecoder* decoder = nullptr;



    hr = factory->CreateDecoderFromFilename(

        inputPath,

        nullptr,

        GENERIC_READ,

        WICDecodeMetadataCacheOnLoad,

        &decoder);



    if (FAILED(hr) ||

        decoder == nullptr)

    {

        std::wcerr

            << L"ERROR: Could not open input PNG.\n";



        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    IWICBitmapFrameDecode* frame = nullptr;



    hr = decoder->GetFrame(

        0,

        &frame);



    if (FAILED(hr) ||

        frame == nullptr)

    {

        std::wcerr

            << L"ERROR: Could not decode PNG frame.\n";



        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    UINT width = 0;

    UINT height = 0;



    hr = frame->GetSize(

        &width,

        &height);



    if (FAILED(hr) ||

        width == 0 ||

        height == 0)

    {

        std::wcerr

            << L"ERROR: Invalid PNG dimensions.\n";



        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    if (width != metadata.width ||

        height != metadata.height)

    {

        std::wcerr

            << L"ERROR: PNG dimensions do not match the AC texture.\n"

            << L"  PNG:      "

            << width

            << L"x"

            << height

            << L"\n"

            << L"  Required: "

            << metadata.width

            << L"x"

            << metadata.height

            << L"\n";



        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    const WICPixelFormatGUID targetFormat =

        (format == OutputFormat::BGR)

            ? GUID_WICPixelFormat24bppBGR

            : GUID_WICPixelFormat32bppBGRA;



    const UINT bytesPerPixel =

        (format == OutputFormat::BGR)

            ? 3u

            : 4u;



    const std::uint64_t calculatedSize =

        static_cast<std::uint64_t>(width) *

        static_cast<std::uint64_t>(height) *

        bytesPerPixel;



    if (calculatedSize != metadata.imageSize)

    {

        std::wcerr

            << L"ERROR: Calculated payload size does not match "

            << L"the texture catalog.\n"

            << L"  Calculated: "

            << calculatedSize

            << L"\n"

            << L"  Expected:   "

            << metadata.imageSize

            << L"\n";



        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    IWICFormatConverter* converter = nullptr;



    hr = factory->CreateFormatConverter(

        &converter);



    if (FAILED(hr) ||

        converter == nullptr)

    {

        std::wcerr

            << L"ERROR: Could not create WIC format converter.\n";



        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    hr = converter->Initialize(

        frame,

        targetFormat,

        WICBitmapDitherTypeNone,

        nullptr,

        0.0,

        WICBitmapPaletteTypeCustom);



    if (FAILED(hr))

    {

        std::wcerr

            << L"ERROR: WIC pixel conversion failed.\n";



        converter->Release();

        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    const UINT stride =

        width * bytesPerPixel;



    const UINT byteCount =

        stride * height;



    std::vector<std::uint8_t>

        pixels(byteCount);



    hr = converter->CopyPixels(

        nullptr,

        stride,

        byteCount,

        pixels.data());



    if (FAILED(hr))

    {

        std::wcerr

            << L"ERROR: Could not read converted pixels.\n";



        converter->Release();

        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    CreateDirectoryW(

        REPLACEMENT_DIRECTORY,

        nullptr);



    std::ofstream output(

        outputPath,

        std::ios::binary);



    if (!output.is_open())

    {

        std::wcerr

            << L"ERROR: Could not create replacement file:\n  "

            << outputPath

            << L"\n";



        converter->Release();

        frame->Release();

        decoder->Release();

        factory->Release();



        if (uninitialize)

            CoUninitialize();



        return false;

    }



    output.write(

        reinterpret_cast<const char*>(

            pixels.data()),

        static_cast<std::streamsize>(

            pixels.size()));



    const bool writeOK =

        output.good();



    output.close();



    converter->Release();

    frame->Release();

    decoder->Release();

    factory->Release();



    if (uninitialize)

        CoUninitialize();



    if (!writeOK)

    {

        std::wcerr

            << L"ERROR: Failed writing replacement file.\n";



        return false;

    }



    std::wcout

        << L"DID:         "

        << DIDName(metadata.did)

        << L"\n"

        << L"Dimensions:  "

        << metadata.width

        << L"x"

        << metadata.height

        << L"\n"

        << L"PixelFormat: 0x"

        << std::uppercase

        << std::hex

        << metadata.pixelFormat

        << std::dec

        << L"\n"

        << L"Encoding:    "

        << ((format == OutputFormat::BGR)

                ? L"BGR"

                : L"BGRA")

        << L"\n"

        << L"Bytes:       "

        << byteCount

        << L"\n"

        << L"Output:      "

        << outputPath

        << L"\n"

        << L"Replacement created successfully.\n";



    return true;

}





static bool CreatePreview(

    const TextureMetadata& metadata,

    OutputFormat format)

{

    const UINT bytesPerPixel =

        (format == OutputFormat::BGR) ? 3u : 4u;



    const std::uint64_t expectedSize =

        static_cast<std::uint64_t>(metadata.width) *

        static_cast<std::uint64_t>(metadata.height) *

        bytesPerPixel;



    if (expectedSize != metadata.imageSize)

    {

        std::wcerr << L"ERROR: Catalog imageSize does not match the known format.\n";

        return false;

    }



    const std::wstring didName = DIDName(metadata.did);

    const std::wstring inputPath =

        std::wstring(CAPTURE_DIRECTORY) + L"\\" + didName + L".rgb";

    const std::wstring outputPath =

        std::wstring(CAPTURE_DIRECTORY) + L"\\" + didName + L".png";



    std::ifstream input(inputPath, std::ios::binary | std::ios::ate);

    if (!input.is_open())

    {

        std::wcerr << L"ERROR: Could not open captured texture:\n  "

                   << inputPath << L"\n";

        return false;

    }



    const std::streamoff fileSize = input.tellg();

    if (fileSize != static_cast<std::streamoff>(metadata.imageSize))

    {

        std::wcerr << L"ERROR: Captured texture size mismatch.\n"

                   << L"  Expected: " << metadata.imageSize << L"\n"

                   << L"  Actual:   " << fileSize << L"\n";

        return false;

    }



    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> pixels(metadata.imageSize);

    input.read(

        reinterpret_cast<char*>(pixels.data()),

        static_cast<std::streamsize>(pixels.size()));



    if (input.gcount() != static_cast<std::streamsize>(pixels.size()))

    {

        std::wcerr << L"ERROR: Could not read captured texture.\n";

        return false;

    }



    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const bool uninitialize = SUCCEEDED(hr);



    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)

    {

        std::wcerr << L"ERROR: CoInitializeEx failed.\n";

        return false;

    }



    IWICImagingFactory* factory = nullptr;

    IWICStream* stream = nullptr;

    IWICBitmapEncoder* encoder = nullptr;

    IWICBitmapFrameEncode* frame = nullptr;

    IPropertyBag2* properties = nullptr;



    hr = CoCreateInstance(

        CLSID_WICImagingFactory,

        nullptr,

        CLSCTX_INPROC_SERVER,

        IID_PPV_ARGS(&factory));



    if (SUCCEEDED(hr))

        hr = factory->CreateStream(&stream);



    if (SUCCEEDED(hr))

        hr = stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE);



    if (SUCCEEDED(hr))

        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);



    if (SUCCEEDED(hr))

        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);



    if (SUCCEEDED(hr))

        hr = encoder->CreateNewFrame(&frame, &properties);



    if (SUCCEEDED(hr))

        hr = frame->Initialize(properties);



    if (SUCCEEDED(hr))

        hr = frame->SetSize(metadata.width, metadata.height);



    WICPixelFormatGUID pixelFormat =

        (format == OutputFormat::BGR)

            ? GUID_WICPixelFormat24bppBGR

            : GUID_WICPixelFormat32bppBGRA;



    if (SUCCEEDED(hr))

        hr = frame->SetPixelFormat(&pixelFormat);



    const WICPixelFormatGUID expectedFormat =

        (format == OutputFormat::BGR)

            ? GUID_WICPixelFormat24bppBGR

            : GUID_WICPixelFormat32bppBGRA;



    if (SUCCEEDED(hr) && !IsEqualGUID(pixelFormat, expectedFormat))

    {

        std::wcerr << L"ERROR: PNG encoder changed the requested pixel format.\n";

        hr = E_FAIL;

    }



    const UINT stride = metadata.width * bytesPerPixel;



    if (SUCCEEDED(hr))

    {

        hr = frame->WritePixels(

            metadata.height,

            stride,

            static_cast<UINT>(pixels.size()),

            pixels.data());

    }



    if (SUCCEEDED(hr))

        hr = frame->Commit();



    if (SUCCEEDED(hr))

        hr = encoder->Commit();



    if (properties != nullptr)

        properties->Release();

    if (frame != nullptr)

        frame->Release();

    if (encoder != nullptr)

        encoder->Release();

    if (stream != nullptr)

        stream->Release();

    if (factory != nullptr)

        factory->Release();



    if (uninitialize)

        CoUninitialize();



    if (FAILED(hr))

    {

        DeleteFileW(outputPath.c_str());

        std::wcerr << L"ERROR: Could not create PNG preview.\n";

        return false;

    }



    std::wcout

        << L"DID:         " << didName << L"\n"

        << L"Dimensions:  " << metadata.width << L"x" << metadata.height << L"\n"

        << L"PixelFormat: 0x" << std::uppercase << std::hex

        << metadata.pixelFormat << std::dec << L"\n"

        << L"Encoding:    "

        << ((format == OutputFormat::BGR) ? L"BGR" : L"BGRA") << L"\n"

        << L"Input:       " << inputPath << L"\n"

        << L"Output:      " << outputPath << L"\n"

        << L"Preview created successfully.\n";



    return true;

}







static int CreateAllPreviews()

{

    std::vector<TextureMetadata> textures;



    if (!LoadAllTextureMetadata(textures))

        return 1;



    std::uint32_t created = 0;

    std::uint32_t skipped = 0;

    std::uint32_t missingRaw = 0;

    std::uint32_t failed = 0;



    for (const TextureMetadata& metadata : textures)

    {

        OutputFormat format;



        if (metadata.pixelFormat == 0x00000014u)

        {

            format = OutputFormat::BGR;

        }

        else if (metadata.pixelFormat == 0x00000015u)

        {

            format = OutputFormat::BGRA;

        }

        else

        {

            ++skipped;

            continue;

        }



        const std::wstring inputPath =

            std::wstring(CAPTURE_DIRECTORY) +

            L"\\" +

            DIDName(metadata.did) +

            L".rgb";



        const DWORD attributes =

            GetFileAttributesW(inputPath.c_str());



        if (attributes == INVALID_FILE_ATTRIBUTES ||

            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)

        {

            ++missingRaw;

            continue;

        }



        if (CreatePreview(metadata, format))

            ++created;

        else

            ++failed;

    }



    std::wcout

        << L"\nPreview generation complete.\n\n"

        << L"Created:     " << created << L"\n"

        << L"Skipped:     " << skipped << L"\n"

        << L"Missing raw: " << missingRaw << L"\n"

        << L"Failed:      " << failed << L"\n";



    return failed == 0 ? 0 : 1;

}





int wmain(

    int argc,

    wchar_t* argv[])

{
    // Public Manager path: all metadata comes from the Manager's direct DAT
    // scan, and the destination is under %LOCALAPPDATA%\ACCustoms\User\textures.
    // This keeps replacement conversion independent of generated CSV catalogs.
    const bool explicitReplacementMode =
        argc == 9 &&
        _wcsicmp(argv[1], L"replace") == 0;

    if (explicitReplacementMode)
    {
        TextureMetadata metadata;

        if (!ParseDID(argv[2], metadata.did) ||
            !ParseUnsigned32(argv[3], metadata.width) ||
            !ParseUnsigned32(argv[4], metadata.height) ||
            !ParseUnsigned32(argv[5], metadata.imageSize) ||
            !ParseUnsigned32(argv[6], metadata.pixelFormat))
        {
            std::wcerr << L"ERROR: Invalid replacement metadata.\n";
            return 1;
        }

        OutputFormat format;
        if (metadata.pixelFormat == 0x14u)
            format = OutputFormat::BGR;
        else if (metadata.pixelFormat == 0x15u)
            format = OutputFormat::BGRA;
        else
        {
            std::wcerr
                << L"ERROR: Unsupported pixel format 0x"
                << std::uppercase << std::hex << metadata.pixelFormat
                << std::dec << L".\n";
            return 1;
        }

        return ConvertPng(argv[7], argv[8], metadata, format) ? 0 : 1;
    }


    const bool previewAllMode =

        argc == 2 &&

        _wcsicmp(argv[1], L"preview-all") == 0;



    if (previewAllMode)

        return CreateAllPreviews();



    const bool previewMode =

        argc == 3 &&

        _wcsicmp(argv[1], L"preview") == 0;



    const bool replacementMode =

        argc == 3 &&

        !previewMode;



    if (!previewMode && !replacementMode)

    {

        std::wcout

            << L"ACModernUIConverter\n\n"

            << L"Manager replacement (preferred):\n"

            << L"  ACModernUIConverter.exe replace DID width height imageSize pixelFormat input.png output.rgb\n\n"

            << L"Legacy catalog replacement:\n"

            << L"  ACModernUIConverter.exe DID replacement.png\n\n"

            << L"Preview:\n"

            << L"  ACModernUIConverter.exe preview DID\n\n"

            << L"Batch previews:\n"

            << L"  ACModernUIConverter.exe preview-all\n\n"

            << L"Examples:\n"

            << L"  ACModernUIConverter.exe 06001365 \"C:\\replacement.png\"\n"

            << L"  ACModernUIConverter.exe preview 06001365\n"

            << L"  ACModernUIConverter.exe preview-all\n";



        return 1;

    }



    const wchar_t* didArgument =

        previewMode ? argv[2] : argv[1];



    std::uint32_t did = 0;



    if (!ParseDID(didArgument, did))

    {

        std::wcerr

            << L"ERROR: Invalid DID '"

            << didArgument

            << L"'.\n";

        return 1;

    }



    TextureMetadata metadata;



    if (!LoadTextureMetadata(did, metadata))

        return 1;



    OutputFormat format;



    switch (metadata.pixelFormat)

    {

        case 0x00000014u:

            format = OutputFormat::BGR;

            break;



        case 0x00000015u:

            format = OutputFormat::BGRA;

            break;



        default:

            std::wcerr

                << L"ERROR: Texture "

                << DIDName(did)

                << L" uses unsupported pixelFormat 0x"

                << std::uppercase

                << std::hex

                << metadata.pixelFormat

                << std::dec

                << L".\n"

                << L"Currently supported:\n"

                << L"  0x14 = BGR\n"

                << L"  0x15 = BGRA\n";

            return 1;

    }



    if (previewMode)

        return CreatePreview(metadata, format) ? 0 : 1;



    const std::wstring outputPath =

        std::wstring(REPLACEMENT_DIRECTORY) +

        L"\\" +

        DIDName(did) +

        L".rgb";



    return ConvertPng(

        argv[2],

        outputPath,

        metadata,

        format)

        ? 0

        : 1;

}
