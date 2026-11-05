#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <windows.media.h>
#include <activation.h>
#include <winstring.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include "core.hpp"
#include "mft_ids.hpp"

using Microsoft::WRL::ComPtr;
static std::atomic<long> objects{0}, locks{0};
static HMODULE moduleHandle = nullptr;

class Decoder final : public IMFTransform, public ABI::Windows::Media::IMediaExtension {
    std::atomic<ULONG> refs_{1};
    std::recursive_mutex mutex_;
    mjxl::Codec codec_;
    ComPtr<IMFAttributes> attributes_;
    ComPtr<IMFMediaType> input_, output_;
    ComPtr<IMFSample> pending_;
    UINT32 width_ = 0, height_ = 0;
    bool draining_ = false;
public:
    Decoder() : codec_(4) {
        if (FAILED(MFCreateAttributes(&attributes_, 4))) { throw std::bad_alloc(); }
        attributes_->SetUINT32(MF_TRANSFORM_ASYNC, FALSE);
        attributes_->SetUINT32(MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE, FALSE);
        ++objects;
    }
    ~Decoder() { --objects; }
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IMFTransform)) {
            *result = static_cast<IMFTransform*>(this); AddRef(); return S_OK;
        }
        if (iid == __uuidof(IInspectable) || iid == __uuidof(ABI::Windows::Media::IMediaExtension)) {
            *result = static_cast<ABI::Windows::Media::IMediaExtension*>(this); AddRef(); return S_OK;
        }
        if (iid == __uuidof(IAgileObject)) {
            *result = static_cast<IMFTransform*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --refs_;
        if (!count) { delete this; }
        return count;
    }
    STDMETHODIMP GetIids(ULONG* count, IID** ids) override {
        if (!count || !ids) { return E_POINTER; }
        *count = 0; *ids = nullptr;
        auto list = static_cast<IID*>(CoTaskMemAlloc(sizeof(IID) * 2));
        if (!list) { return E_OUTOFMEMORY; }
        list[0] = __uuidof(IMFTransform);
        list[1] = __uuidof(ABI::Windows::Media::IMediaExtension);
        *count = 2; *ids = list; return S_OK;
    }
    STDMETHODIMP GetRuntimeClassName(HSTRING* name) override {
        return WindowsCreateString(L"MJXL.MediaFoundationDecoder", static_cast<UINT32>(wcslen(L"MJXL.MediaFoundationDecoder")), name);
    }
    STDMETHODIMP GetTrustLevel(TrustLevel* level) override {
        if (!level) { return E_POINTER; }
        *level = BaseTrust; return S_OK;
    }
    STDMETHODIMP SetProperties(ABI::Windows::Foundation::Collections::IPropertySet*) override { return S_OK; }
    STDMETHODIMP GetStreamLimits(DWORD* a, DWORD* b, DWORD* c, DWORD* d) override {
        if (!a || !b || !c || !d) { return E_POINTER; }
        *a = *b = *c = *d = 1; return S_OK;
    }
    STDMETHODIMP GetStreamCount(DWORD* a, DWORD* b) override {
        if (!a || !b) { return E_POINTER; }
        *a = *b = 1; return S_OK;
    }
    STDMETHODIMP GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP GetInputStreamInfo(DWORD id, MFT_INPUT_STREAM_INFO* info) override {
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!info) { return E_POINTER; }
        *info = {};
        info->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;
        return S_OK;
    }
    STDMETHODIMP GetOutputStreamInfo(DWORD id, MFT_OUTPUT_STREAM_INFO* info) override {
        std::lock_guard guard(mutex_);
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!info) { return E_POINTER; }
        *info = {};
        info->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES | MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER | MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE;
        info->cbSize = width_ * height_ * 4;
        return S_OK;
    }
    STDMETHODIMP GetAttributes(IMFAttributes** result) override { return attributes_.CopyTo(result); }
    STDMETHODIMP GetInputStreamAttributes(DWORD id, IMFAttributes** result) override {
        if (result) { *result = nullptr; }
        return id ? MF_E_INVALIDSTREAMNUMBER : E_NOTIMPL;
    }
    STDMETHODIMP GetOutputStreamAttributes(DWORD id, IMFAttributes** result) override {
        if (result) { *result = nullptr; }
        return id ? MF_E_INVALIDSTREAMNUMBER : E_NOTIMPL;
    }
    STDMETHODIMP DeleteInputStream(DWORD) override { return E_NOTIMPL; }
    STDMETHODIMP AddInputStreams(DWORD, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP GetInputAvailableType(DWORD id, DWORD index, IMFMediaType** result) override {
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (index) { return MF_E_NO_MORE_TYPES; }
        ComPtr<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) { hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); }
        if (SUCCEEDED(hr)) { hr = type->SetGUID(MF_MT_SUBTYPE, kMjxlMfSubtype); }
        if (SUCCEEDED(hr)) { hr = type.CopyTo(result); }
        return hr;
    }
    STDMETHODIMP GetOutputAvailableType(DWORD id, DWORD index, IMFMediaType** result) override;
    STDMETHODIMP SetInputType(DWORD id, IMFMediaType* type, DWORD flags) override;
    STDMETHODIMP SetOutputType(DWORD id, IMFMediaType* type, DWORD flags) override;
    STDMETHODIMP GetInputCurrentType(DWORD id, IMFMediaType** result) override {
        std::lock_guard guard(mutex_);
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!result) { return E_POINTER; }
        *result = nullptr;
        return input_ ? input_.CopyTo(result) : MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    STDMETHODIMP GetOutputCurrentType(DWORD id, IMFMediaType** result) override {
        std::lock_guard guard(mutex_);
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!result) { return E_POINTER; }
        *result = nullptr;
        return output_ ? output_.CopyTo(result) : MF_E_TRANSFORM_TYPE_NOT_SET;
    }
    STDMETHODIMP GetInputStatus(DWORD id, DWORD* flags) override {
        std::lock_guard guard(mutex_);
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!flags) { return E_POINTER; }
        *flags = input_ && output_ && !pending_ && !draining_ ? MFT_INPUT_STATUS_ACCEPT_DATA : 0;
        return S_OK;
    }
    STDMETHODIMP GetOutputStatus(DWORD* flags) override {
        std::lock_guard guard(mutex_);
        if (!flags) { return E_POINTER; }
        *flags = pending_ ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
        return S_OK;
    }
    STDMETHODIMP SetOutputBounds(LONGLONG, LONGLONG) override { return E_NOTIMPL; }
    STDMETHODIMP ProcessEvent(DWORD id, IMFMediaEvent*) override { return id ? MF_E_INVALIDSTREAMNUMBER : E_NOTIMPL; }
    STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE message, ULONG_PTR) override {
        std::lock_guard guard(mutex_);
        if (message == MFT_MESSAGE_COMMAND_FLUSH || message == MFT_MESSAGE_NOTIFY_START_OF_STREAM || message == MFT_MESSAGE_NOTIFY_END_STREAMING) {
            pending_.Reset(); draining_ = false;
        } else if (message == MFT_MESSAGE_COMMAND_DRAIN) {
            draining_ = true;
        } else if (message == MFT_MESSAGE_SET_D3D_MANAGER) {
            return E_NOTIMPL;
        }
        return S_OK;
    }
    STDMETHODIMP ProcessInput(DWORD id, IMFSample* sample, DWORD flags) override {
        std::lock_guard guard(mutex_);
        if (id) { return MF_E_INVALIDSTREAMNUMBER; }
        if (!sample) { return E_POINTER; }
        if (flags) { return E_INVALIDARG; }
        if (!input_ || !output_) { return MF_E_TRANSFORM_TYPE_NOT_SET; }
        if (pending_ || draining_) { return MF_E_NOTACCEPTING; }
        DWORD size = 0;
        HRESULT hr = sample->GetTotalLength(&size);
        if (FAILED(hr)) { return hr; }
        if (!size || size > 256U * 1024 * 1024) { return MF_E_INVALID_STREAM_DATA; }
        pending_ = sample;
        return S_OK;
    }
    STDMETHODIMP ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*) override;
};

STDMETHODIMP Decoder::GetOutputAvailableType(DWORD id, DWORD index, IMFMediaType** result)
{
    std::lock_guard guard(mutex_);
    if (id) { return MF_E_INVALIDSTREAMNUMBER; }
    if (!result) { return E_POINTER; }
    *result = nullptr;
    if (index) { return MF_E_NO_MORE_TYPES; }
    if (!input_) { return MF_E_TRANSFORM_TYPE_NOT_SET; }
    ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) { hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); }
    if (SUCCEEDED(hr)) { hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32); }
    if (SUCCEEDED(hr)) { hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width_, height_); }
    UINT32 numerator = 0, denominator = 0;
    if (SUCCEEDED(hr) && SUCCEEDED(MFGetAttributeRatio(input_.Get(), MF_MT_FRAME_RATE, &numerator, &denominator))) {
        hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, numerator, denominator);
    }
    if (SUCCEEDED(hr)) { hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1); }
    if (SUCCEEDED(hr)) { hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive); }
    if (SUCCEEDED(hr)) { hr = type->SetUINT32(MF_MT_DEFAULT_STRIDE, width_ * 4); }
    if (SUCCEEDED(hr)) { hr = type->SetUINT32(MF_MT_SAMPLE_SIZE, width_ * height_ * 4); }
    if (SUCCEEDED(hr)) { hr = type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE); }
    if (SUCCEEDED(hr)) { hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE); }
    if (SUCCEEDED(hr)) { hr = type.CopyTo(result); }
    return hr;
}
STDMETHODIMP Decoder::SetInputType(DWORD id, IMFMediaType* type, DWORD flags)
{
    std::lock_guard guard(mutex_);
    if (id) { return MF_E_INVALIDSTREAMNUMBER; }
    if (flags & ~MFT_SET_TYPE_TEST_ONLY) { return E_INVALIDARG; }
    if (pending_) { return MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING; }
    UINT32 width = 0, height = 0;
    if (type) {
        GUID major{}, subtype{};
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video ||
            FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != kMjxlMfSubtype ||
            FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
            !width || !height || width > 16384 || height > 16384 || uint64_t(width) * height > 64ULL * 1024 * 1024) {
            return MF_E_INVALIDMEDIATYPE;
        }
    }
    if (!(flags & MFT_SET_TYPE_TEST_ONLY)) {
        ComPtr<IMFMediaType> copy;
        if (type) {
            HRESULT hr = MFCreateMediaType(&copy);
            if (FAILED(hr)) { return hr; }
            hr = type->CopyAllItems(copy.Get());
            if (FAILED(hr)) { return hr; }
        }
        input_ = copy; output_.Reset(); width_ = width; height_ = height; draining_ = false;
    }
    return S_OK;
}
STDMETHODIMP Decoder::SetOutputType(DWORD id, IMFMediaType* type, DWORD flags)
{
    std::lock_guard guard(mutex_);
    if (id) { return MF_E_INVALIDSTREAMNUMBER; }
    if (flags & ~MFT_SET_TYPE_TEST_ONLY) { return E_INVALIDARG; }
    if (pending_) { return MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING; }
    if (type) {
        if (!input_) { return MF_E_TRANSFORM_TYPE_NOT_SET; }
        GUID major{}, subtype{};
        UINT32 width = 0, height = 0;
        LONG stride = static_cast<LONG>(MFGetAttributeUINT32(type, MF_MT_DEFAULT_STRIDE, width_ * 4));
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video ||
            FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_RGB32 ||
            FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
            width != width_ || height != height_ || stride != static_cast<LONG>(width_ * 4)) {
            return MF_E_INVALIDMEDIATYPE;
        }
    }
    if (!(flags & MFT_SET_TYPE_TEST_ONLY)) {
        ComPtr<IMFMediaType> copy;
        if (type) {
            HRESULT hr = MFCreateMediaType(&copy);
            if (FAILED(hr)) { return hr; }
            hr = type->CopyAllItems(copy.Get());
            if (FAILED(hr)) { return hr; }
        }
        output_ = copy;
    }
    return S_OK;
}
STDMETHODIMP Decoder::ProcessOutput(DWORD flags, DWORD count, MFT_OUTPUT_DATA_BUFFER* buffers, DWORD* status)
{
    std::lock_guard guard(mutex_);
    if (!buffers || !status) { return E_POINTER; }
    *status = 0;
    if (flags || count != 1 || buffers[0].dwStreamID) { return E_INVALIDARG; }
    buffers[0].dwStatus = 0; buffers[0].pEvents = nullptr;
    if (!input_ || !output_) { return MF_E_TRANSFORM_TYPE_NOT_SET; }
    if (!pending_) { return MF_E_TRANSFORM_NEED_MORE_INPUT; }
    if (!buffers[0].pSample) { return E_INVALIDARG; }
    try {
        ComPtr<IMFMediaBuffer> source, destination;
        HRESULT hr = pending_->ConvertToContiguousBuffer(&source);
        if (FAILED(hr)) { return hr; }
        hr = buffers[0].pSample->ConvertToContiguousBuffer(&destination);
        if (FAILED(hr)) { return hr; }
        DWORD capacity = 0;
        hr = destination->GetMaxLength(&capacity);
        if (FAILED(hr)) { return hr; }
        DWORD size = width_ * height_ * 4;
        if (capacity < size) { return MF_E_BUFFERTOOSMALL; }
        BYTE* encoded = nullptr;
        DWORD length = 0;
        hr = source->Lock(&encoded, nullptr, &length);
        if (FAILED(hr)) { return hr; }
        std::vector<uint8_t> rgb;
        try {
            rgb = codec_.rgb({encoded, length}, width_, height_);
        } catch (...) {
            source->Unlock(); pending_.Reset(); throw;
        }
        source->Unlock();
        BYTE* pixels = nullptr;
        hr = destination->Lock(&pixels, nullptr, nullptr);
        if (FAILED(hr)) { return hr; }
        // 양의 스트라이드로 맨 위 행부터 BGRX 픽셀을 출력한다.
        for (size_t i = 0; i < size_t(width_) * height_; ++i) {
            pixels[i * 4] = rgb[i * 3 + 2]; pixels[i * 4 + 1] = rgb[i * 3 + 1];
            pixels[i * 4 + 2] = rgb[i * 3]; pixels[i * 4 + 3] = 255;
        }
        destination->Unlock();
        hr = destination->SetCurrentLength(size);
        if (FAILED(hr)) { return hr; }
        hr = pending_->CopyAllItems(buffers[0].pSample);
        if (FAILED(hr)) { return hr; }
        LONGLONG value = 0;
        if (SUCCEEDED(pending_->GetSampleTime(&value))) { buffers[0].pSample->SetSampleTime(value); }
        if (SUCCEEDED(pending_->GetSampleDuration(&value))) { buffers[0].pSample->SetSampleDuration(value); }
        buffers[0].pSample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
        pending_.Reset();
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (const std::exception&) { return MF_E_INVALID_STREAM_DATA; }
}

class Factory final : public IClassFactory, public IActivationFactory {
    std::atomic<ULONG> refs_{1};
public:
    Factory() { ++objects; }
    ~Factory() { --objects; }
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *result = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
        }
        if (iid == __uuidof(IInspectable) || iid == __uuidof(IActivationFactory)) {
            *result = static_cast<IActivationFactory*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP GetIids(ULONG* count, IID** ids) override {
        if (!count || !ids) { return E_POINTER; }
        *count = 0; *ids = nullptr; return S_OK;
    }
    STDMETHODIMP GetRuntimeClassName(HSTRING* name) override {
        return WindowsCreateString(L"MJXL.MediaFoundationDecoder", static_cast<UINT32>(wcslen(L"MJXL.MediaFoundationDecoder")), name);
    }
    STDMETHODIMP GetTrustLevel(TrustLevel* level) override {
        if (!level) { return E_POINTER; }
        *level = BaseTrust; return S_OK;
    }
    STDMETHODIMP ActivateInstance(IInspectable** result) override {
        return CreateInstance(nullptr, __uuidof(IInspectable), reinterpret_cast<void**>(result));
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --refs_;
        if (!count) { delete this; }
        return count;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (!result) { return E_POINTER; }
        *result = nullptr;
        if (outer) { return CLASS_E_NOAGGREGATION; }
        try {
            auto decoder = new Decoder;
            HRESULT hr = decoder->QueryInterface(iid, result); decoder->Release(); return hr;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) { ++locks; } else { --locks; }
        return S_OK;
    }
};
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result)
{
    if (!result) { return E_POINTER; }
    *result = nullptr;
    if (clsid != kMjxlMft) { return CLASS_E_CLASSNOTAVAILABLE; }
    auto factory = new (std::nothrow) Factory;
    if (!factory) { return E_OUTOFMEMORY; }
    HRESULT hr = factory->QueryInterface(iid, result); factory->Release(); return hr;
}
STDAPI DllGetActivationFactory(HSTRING name, IActivationFactory** result)
{
    if (!result) { return E_POINTER; }
    *result = nullptr;
    UINT32 size = 0;
    const wchar_t* text = WindowsGetStringRawBuffer(name, &size);
    if (std::wstring_view(text, size) != L"MJXL.MediaFoundationDecoder") { return CLASS_E_CLASSNOTAVAILABLE; }
    auto factory = new (std::nothrow) Factory;
    if (!factory) { return E_OUTOFMEMORY; }
    HRESULT hr = factory->QueryInterface(__uuidof(IActivationFactory), reinterpret_cast<void**>(result));
    factory->Release();
    return hr;
}
STDAPI DllCanUnloadNow()
{
    return objects == 0 && locks == 0 ? S_OK : S_FALSE;
}
static std::wstring class_key()
{
    wchar_t guid[40]{};
    StringFromGUID2(kMjxlMft, guid, 40);
    return std::wstring(L"Software\\Classes\\CLSID\\") + guid;
}
static HRESULT set_string(HKEY key, const wchar_t* name, const wchar_t* value)
{
    return HRESULT_FROM_WIN32(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t))));
}
STDAPI DllRegisterServer()
{
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(moduleHandle, path, 32768);
    if (!length || length >= 32768) { return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); }
    HKEY key = nullptr;
    auto keyPath = class_key();
    LSTATUS error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, (keyPath + L"\\InprocServer32").c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) { return HRESULT_FROM_WIN32(error); }
    HRESULT hr = set_string(key, nullptr, path);
    if (SUCCEEDED(hr)) { hr = set_string(key, L"ThreadingModel", L"Both"); }
    RegCloseKey(key);
    if (FAILED(hr)) { return hr; }
    MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, kMjxlMfSubtype}, output{MFMediaType_Video, MFVideoFormat_RGB32};
    wchar_t name[] = L"MJXL JPEG XL Media Foundation Decoder";
    return MFTRegister(kMjxlMft, MFT_CATEGORY_VIDEO_DECODER, name, MFT_ENUM_FLAG_SYNCMFT, 1, &input, 1, &output, nullptr);
}
STDAPI DllUnregisterServer()
{
    HRESULT hr = MFTUnregister(kMjxlMft);
    if (FAILED(hr)) { return hr; }
    LSTATUS error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, class_key().c_str());
    return error == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(error);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        moduleHandle = instance; DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
