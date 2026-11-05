#include <streams.h>
#include <dvdmedia.h>
#include "core.hpp"
#include "filter_ids.hpp"
#include <algorithm>
#include <atomic>
#include <new>
#include <string_view>

static std::atomic<unsigned long long> decodedFrames{0};

static bool video_info(const CMediaType& type, LONG& width, LONG& height, REFERENCE_TIME& duration)
{
    if (!type.pbFormat) { return false; }
    const BITMAPINFOHEADER* bitmap = nullptr;
    if (type.formattype == FORMAT_VideoInfo && type.cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto info = reinterpret_cast<const VIDEOINFOHEADER*>(type.pbFormat);
        bitmap = &info->bmiHeader; duration = info->AvgTimePerFrame;
    } else if (type.formattype == FORMAT_VideoInfo2 && type.cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        auto info = reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat);
        bitmap = &info->bmiHeader; duration = info->AvgTimePerFrame;
    }
    if (!bitmap || bitmap->biWidth <= 0 || bitmap->biWidth > 16384 || bitmap->biHeight == 0 || bitmap->biHeight < -16384 || bitmap->biHeight > 16384) {
        return false;
    }
    width = bitmap->biWidth; height = bitmap->biHeight;
    return uint64_t(width) * std::abs(height) <= 64ULL * 1024 * 1024;
}

class Decoder final : public CTransformFilter {
    mjxl::Codec codec_;
public:
    explicit Decoder(LPUNKNOWN outer) : CTransformFilter(NAME("MJXL Decoder"), outer, kMjxlDecoder), codec_(4)
    {
    }
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN outer, HRESULT* status)
    {
        try {
            auto result = new Decoder(outer);
            *status = S_OK;
            return result;
        } catch (...) {
            *status = E_OUTOFMEMORY;
            return nullptr;
        }
    }
    HRESULT CheckInputType(const CMediaType* input) override
    {
        LONG w, h; REFERENCE_TIME time;
        return input && input->majortype == MEDIATYPE_Video && input->subtype == kMjxlSubtype && video_info(*input, w, h, time) ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
    }
    HRESULT GetMediaType(int position, CMediaType* output) override
    {
        if (!output) { return E_POINTER; }
        if (position < 0) { return E_INVALIDARG; }
        if (position > 0) { return VFW_S_NO_MORE_ITEMS; }
        if (!m_pInput->IsConnected()) { return E_UNEXPECTED; }
        LONG w, h; REFERENCE_TIME duration;
        if (!video_info(m_pInput->CurrentMediaType(), w, h, duration)) { return VFW_E_TYPE_NOT_ACCEPTED; }
        auto info = reinterpret_cast<VIDEOINFOHEADER*>(output->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
        if (!info) { return E_OUTOFMEMORY; }
        ZeroMemory(info, sizeof(*info));
        info->AvgTimePerFrame = duration;
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biWidth = w; info->bmiHeader.biHeight = std::abs(h);
        info->bmiHeader.biPlanes = 1; info->bmiHeader.biBitCount = 24;
        info->bmiHeader.biCompression = BI_RGB;
        info->bmiHeader.biSizeImage = ((w * 3 + 3) & ~3) * std::abs(h);
        output->SetType(&MEDIATYPE_Video); output->SetSubtype(&MEDIASUBTYPE_RGB24);
        output->SetFormatType(&FORMAT_VideoInfo); output->SetTemporalCompression(FALSE);
        output->SetSampleSize(info->bmiHeader.biSizeImage);
        return S_OK;
    }
    HRESULT CheckTransform(const CMediaType* input, const CMediaType* output) override
    {
        if (CheckInputType(input) != S_OK || !output || output->majortype != MEDIATYPE_Video || output->subtype != MEDIASUBTYPE_RGB24 || output->formattype != FORMAT_VideoInfo || output->cbFormat < sizeof(VIDEOINFOHEADER)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        LONG iw, ih, ow, oh; REFERENCE_TIME time;
        if (!video_info(*input, iw, ih, time) || !video_info(*output, ow, oh, time) || iw != ow || std::abs(ih) != std::abs(oh)) {
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        const auto& bitmap = reinterpret_cast<const VIDEOINFOHEADER*>(output->pbFormat)->bmiHeader;
        return bitmap.biBitCount == 24 && bitmap.biCompression == BI_RGB ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
    }
    HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* requested) override
    {
        if (!allocator || !requested) { return E_POINTER; }
        LONG w, h; REFERENCE_TIME time;
        if (!video_info(m_pOutput->CurrentMediaType(), w, h, time)) { return E_UNEXPECTED; }
        requested->cBuffers = std::max<LONG>(requested->cBuffers, 2);
        requested->cbBuffer = std::max<LONG>(requested->cbBuffer, ((w * 3 + 3) & ~3) * std::abs(h));
        requested->cbAlign = std::max<LONG>(requested->cbAlign, 1);
        ALLOCATOR_PROPERTIES actual{};
        auto result = allocator->SetProperties(requested, &actual);
        if (FAILED(result)) { return result; }
        return actual.cbBuffer >= requested->cbBuffer ? S_OK : E_FAIL;
    }
    HRESULT Transform(IMediaSample* input, IMediaSample* output) override
    {
        if (!input || !output) { return E_POINTER; }
        try {
            LONG w, h; REFERENCE_TIME duration;
            if (!video_info(m_pOutput->CurrentMediaType(), w, h, duration)) { return E_UNEXPECTED; }
            BYTE* encoded = nullptr; BYTE* destination = nullptr;
            if (FAILED(input->GetPointer(&encoded)) || FAILED(output->GetPointer(&destination))) { return E_FAIL; }
            const auto length = input->GetActualDataLength();
            if (length <= 0 || length > 256 * 1024 * 1024) { return E_INVALIDARG; }
            const auto rgb = codec_.rgb({encoded, static_cast<size_t>(length)}, w, std::abs(h));
            const LONG stride = (w * 3 + 3) & ~3;
            const LONG bytes = stride * std::abs(h);
            if (output->GetSize() < bytes) { return E_FAIL; }
            for (LONG y = 0; y < std::abs(h); ++y) {
                auto source = rgb.data() + size_t(y) * w * 3;
                auto target = destination + size_t(h > 0 ? h - 1 - y : y) * stride;
                for (LONG x = 0; x < w; ++x) {
                    target[x * 3] = source[x * 3 + 2];
                    target[x * 3 + 1] = source[x * 3 + 1];
                    target[x * 3 + 2] = source[x * 3];
                }
                std::fill(target + w * 3, target + stride, BYTE{0});
            }
            output->SetActualDataLength(bytes);
            REFERENCE_TIME start, end;
            if (SUCCEEDED(input->GetTime(&start, &end))) { output->SetTime(&start, &end); }
            output->SetSyncPoint(TRUE);
            output->SetDiscontinuity(input->IsDiscontinuity() == S_OK);
            ++decodedFrames;
            return S_OK;
        } catch (const std::exception& error) {
            OutputDebugStringA(error.what());
            return E_FAIL;
        }
    }
};

static const AMOVIESETUP_MEDIATYPE inputType{&MEDIATYPE_Video, &kMjxlSubtype};
static const AMOVIESETUP_MEDIATYPE outputType{&MEDIATYPE_Video, &MEDIASUBTYPE_RGB24};
static wchar_t inputName[] = L"Input";
static wchar_t outputName[] = L"Output";
static const AMOVIESETUP_PIN pins[] = {
    {inputName, FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, 1, &inputType},
    {outputName, FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, nullptr, 1, &outputType}
};
static const AMOVIESETUP_FILTER setup{&kMjxlDecoder, L"MJXL JPEG XL Decoder", MERIT_NORMAL + 1, 2, pins};
CFactoryTemplate g_Templates[] = {{L"MJXL JPEG XL Decoder", &kMjxlDecoder, Decoder::CreateInstance, nullptr, &setup}};
int g_cTemplates = 1;

static HRESULT register_filter(bool install, bool allUsers = false)
{
    HKEY classes = nullptr;
    // Open the system Classes root without write access; check permissions on the registration subkey.
    auto status = allUsers
        ? RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Classes", 0, KEY_READ, &classes)
        : RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &classes, nullptr);
    if (status != ERROR_SUCCESS) { return HRESULT_FROM_WIN32(status); }
    status = RegOverridePredefKey(HKEY_CLASSES_ROOT, classes);
    if (status != ERROR_SUCCESS) { RegCloseKey(classes); return HRESULT_FROM_WIN32(status); }
    const auto result = AMovieDllRegisterServer2(install ? TRUE : FALSE);
    RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
    RegCloseKey(classes);
    return result;
}

STDAPI DllRegisterServer()
{
    return register_filter(true);
}
STDAPI DllUnregisterServer()
{
    return register_filter(false);
}
STDAPI DllInstall(BOOL install, LPCWSTR command)
{
    const std::wstring_view scope = command ? command : L"";
    if (scope != L"machine" && scope != L"user") {
        return E_INVALIDARG;
    }
    return register_filter(install != FALSE, scope == L"machine");
}
extern "C" unsigned long long __stdcall MJXLDecodedFrames()
{
    return decodedFrames.load();
}
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);
BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return DllEntryPoint(instance, reason, reserved);
}
