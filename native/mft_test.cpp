#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <activation.h>
#include <winstring.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string_view>
#include "core.hpp"
#include "mft_ids.hpp"

using Microsoft::WRL::ComPtr;
static void check(HRESULT hr, const char* message)
{
    if (FAILED(hr)) {
        std::cerr << message << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << '\n';
        throw std::runtime_error(message);
    }
}
static void require(bool valid, const char* message)
{
    if (!valid) { throw std::runtime_error(message); }
}
static std::vector<uint8_t> bytes(IMFSample* sample)
{
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(&buffer), "Contiguous buffer");
    BYTE* data = nullptr;
    DWORD size = 0;
    check(buffer->Lock(&data, nullptr, &size), "Lock sample");
    std::vector<uint8_t> result;
    try {
        result.assign(data, data + size);
    } catch (...) {
        buffer->Unlock();
        throw;
    }
    check(buffer->Unlock(), "Unlock sample");
    return result;
}
static ComPtr<IMFSample> read(IMFSourceReader* reader, LONGLONG& time)
{
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        DWORD flags = 0, stream = 0;
        ComPtr<IMFSample> sample;
        check(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags, &time, &sample), "Read video");
        require(!(flags & MF_SOURCE_READERF_ERROR), "Source reader error flag");
        if (sample) { return sample; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { return {}; }
    }
    throw std::runtime_error("Too many empty video samples");
}
static void pixel_check(IMFSample* compressed, IMFSample* decoded, mjxl::Codec& codec, UINT32 width, UINT32 height)
{
    const auto packet = bytes(compressed);
    const auto expected = codec.rgb(packet, width, height);
    const auto actual = bytes(decoded);
    require(actual.size() == size_t(width) * height * 4, "RGB32 output size mismatch");
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        require(actual[i * 4] == expected[i * 3 + 2] &&
                actual[i * 4 + 1] == expected[i * 3 + 1] &&
                actual[i * 4 + 2] == expected[i * 3], "RGB32 pixels differ from core decoder");
    }
}
static void state_test(IClassFactory* factory, IMFMediaType* input)
{
    ComPtr<IMFTransform> transform;
    if (factory) {
        check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&transform)), "Local transform");
    } else {
        check(CoCreateInstance(kMjxlMft, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&transform)), "Registered transform");
    }
    check(transform->SetInputType(0, input, 0), "Set test input");
    ComPtr<IMFMediaType> output;
    check(transform->GetOutputAvailableType(0, 0, &output), "Test output format");
    check(transform->SetOutputType(0, output.Get(), 0), "Set test output");
    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateSample(&sample), "Test sample");
    check(MFCreateMemoryBuffer(16, &buffer), "Test buffer");
    BYTE* data = nullptr;
    check(buffer->Lock(&data, nullptr, nullptr), "Test lock");
    ZeroMemory(data, 16);
    check(buffer->Unlock(), "Test unlock");
    check(buffer->SetCurrentLength(16), "Test length");
    check(sample->AddBuffer(buffer.Get()), "Test add buffer");
    check(transform->ProcessInput(0, sample.Get(), 0), "Queue test sample");
    require(transform->ProcessInput(0, sample.Get(), 0) == MF_E_NOTACCEPTING, "Queued input must reject next input");
    check(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0), "Flush test");
    DWORD status = 0;
    check(transform->GetOutputStatus(&status), "Flushed status");
    require(status == 0, "Flush must discard pending input");
    check(transform->ProcessInput(0, sample.Get(), 0), "Queue malformed input");
    MFT_OUTPUT_STREAM_INFO info{};
    check(transform->GetOutputStreamInfo(0, &info), "Test buffer requirement");
    ComPtr<IMFSample> target;
    ComPtr<IMFMediaBuffer> targetBuffer;
    check(MFCreateSample(&target), "Test target");
    check(MFCreateMemoryBuffer(info.cbSize, &targetBuffer), "Test target buffer");
    check(target->AddBuffer(targetBuffer.Get()), "Test target add");
    MFT_OUTPUT_DATA_BUFFER out{0, target.Get(), 0, nullptr};
    require(FAILED(transform->ProcessOutput(0, 1, &out, &status)), "Malformed JXL must be rejected");
    check(transform->GetOutputStatus(&status), "Rejected status");
    require(status == 0, "Malformed input must not remain queued");
}
static void run(const wchar_t* filename, IClassFactory* factory)
{
    ComPtr<IMFSourceReader> native, decoded;
    check(MFCreateSourceReaderFromURL(filename, nullptr, &native), "Create native reader");
    check(native->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE), "Disable native streams");
    check(native->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE), "Select native video");
    ComPtr<IMFMediaType> input;
    check(native->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &input), "Native video type");
    GUID subtype{};
    check(input->GetGUID(MF_MT_SUBTYPE, &subtype), "Native subtype");
    require(subtype == kMjxlMfSubtype, "Expected MJXL AVI");
    check(native->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, input.Get()), "Compressed passthrough");
    state_test(factory, input.Get());
    UINT32 width = 0, height = 0;
    check(MFGetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, &width, &height), "Video dimensions");
    check(MFCreateSourceReaderFromURL(filename, nullptr, &decoded), "Create decoded reader");
    check(decoded->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE), "Disable decoded streams");
    check(decoded->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE), "Select decoded video");
    ComPtr<IMFMediaType> output;
    check(MFCreateMediaType(&output), "Create RGB type");
    check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set video major");
    check(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 subtype");
    check(decoded->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, output.Get()), "Automatic RGB32 decoder selection");
    mjxl::Codec codec;
    unsigned count = 0;
    LONGLONG previous = -1, last = 0;
    for (;;) {
        LONGLONG nativeTime = 0, decodedTime = 0;
        auto original = read(native.Get(), nativeTime);
        auto pixels = read(decoded.Get(), decodedTime);
        require(bool(original) == bool(pixels), "Frame count mismatch");
        if (!original) { break; }
        require(decodedTime == nativeTime && decodedTime > previous, "Frame timestamps differ");
        pixel_check(original.Get(), pixels.Get(), codec, width, height);
        previous = last = decodedTime;
        ++count;
    }
    require(count > 1, "Not enough decoded frames");
    PROPVARIANT position{};
    position.vt = VT_I8;
    position.hVal.QuadPart = last / 2;
    check(native->SetCurrentPosition(GUID_NULL, position), "Seek native");
    check(decoded->SetCurrentPosition(GUID_NULL, position), "Seek decoded");
    LONGLONG nativeTime = 0, decodedTime = 0;
    auto original = read(native.Get(), nativeTime);
    auto pixels = read(decoded.Get(), decodedTime);
    require(original && pixels && nativeTime == decodedTime, "Seek timestamp mismatch");
    pixel_check(original.Get(), pixels.Get(), codec, width, height);
    std::cout << "PASS Media Foundation: " << count << " exact RGB frames; timestamps; seek; flush; malformed input rejection\n";
}
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc != 2 && argc != 4) { std::cerr << "Usage: mjxl_mft_test file.avi [--local decoder.dll]\n"; return 1; }
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM startup");
    check(MFStartup(MF_VERSION), "MF startup");
    HMODULE module = nullptr;
    bool local = false;
    int result = 0;
    {
        ComPtr<IClassFactory> factory;
        try {
            if (argc == 4) {
                require(std::wstring(argv[2]) == L"--local", "Unknown option");
                module = LoadLibraryExW(std::filesystem::absolute(argv[3]).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                require(module != nullptr, "Load local decoder");
                using Create = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
                auto create = reinterpret_cast<Create>(GetProcAddress(module, "DllGetClassObject"));
                require(create != nullptr, "Factory export");
                check(create(kMjxlMft, IID_PPV_ARGS(&factory)), "Create local factory");
                using Activate = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
                auto activate = reinterpret_cast<Activate>(GetProcAddress(module, "DllGetActivationFactory"));
                require(activate != nullptr, "WinRT factory export");
                HSTRING name = nullptr;
                check(WindowsCreateString(L"MJXL.MediaFoundationDecoder", 27, &name), "WinRT class name");
                ComPtr<IActivationFactory> activation;
                HRESULT activationResult = activate(name, &activation);
                WindowsDeleteString(name);
                check(activationResult, "WinRT activation factory");
                ComPtr<IInspectable> instance;
                check(activation->ActivateInstance(&instance), "Activate WinRT decoder");
                ComPtr<IMFTransform> winrtTransform;
                check(instance.As(&winrtTransform), "WinRT transform interface");
                HSTRING runtimeName = nullptr;
                check(instance->GetRuntimeClassName(&runtimeName), "Runtime class name");
                UINT32 runtimeSize = 0;
                const wchar_t* runtimeText = WindowsGetStringRawBuffer(runtimeName, &runtimeSize);
                bool validName = std::wstring_view(runtimeText, runtimeSize) == L"MJXL.MediaFoundationDecoder";
                WindowsDeleteString(runtimeName);
                require(validName, "Runtime class name mismatch");

                MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, kMjxlMfSubtype};
                MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_RGB32};
                check(MFTRegisterLocal(factory.Get(), MFT_CATEGORY_VIDEO_DECODER, L"MJXL local test", MFT_ENUM_FLAG_SYNCMFT, 1, &input, 1, &output), "Register local decoder");
                local = true;
            }
            run(std::filesystem::absolute(argv[1]).c_str(), factory.Get());
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            result = 1;
        }
        if (local) { MFTUnregisterLocal(factory.Get()); }
    }
    MFShutdown();
    CoUninitialize();
    if (module) { FreeLibrary(module); }
    return result;
}
