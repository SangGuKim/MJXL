#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include "filter_ids.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

static void check(HRESULT status, const char* message)
{
    if (FAILED(status)) {
        std::cerr << message << " HRESULT=0x" << std::hex << static_cast<unsigned long>(status) << std::dec << '\n';
        throw std::runtime_error(message);
    }
}

static ComPtr<IPin> pin(IBaseFilter* filter, PIN_DIRECTION direction, bool video = false, bool audio = false)
{
    ComPtr<IEnumPins> enumerator;
    check(filter->EnumPins(&enumerator), "EnumPins");
    ComPtr<IPin> result;
    while (enumerator->Next(1, &result, nullptr) == S_OK) {
        PIN_DIRECTION actual;
        check(result->QueryDirection(&actual), "QueryDirection");
        if (actual == direction) {
            if (!video && !audio) { return result; }
            ComPtr<IEnumMediaTypes> types;
            check(result->EnumMediaTypes(&types), "EnumMediaTypes");
            AM_MEDIA_TYPE* type = nullptr;
            while (types->Next(1, &type, nullptr) == S_OK) {
                const bool matches = audio ? type->majortype == MEDIATYPE_Audio : type->majortype == MEDIATYPE_Video && type->subtype == kMjxlSubtype;
                if (type->cbFormat) { CoTaskMemFree(type->pbFormat); }
                if (type->pUnk) { type->pUnk->Release(); }
                CoTaskMemFree(type);
                if (matches) { return result; }
            }
        }
        result.Reset();
    }
    if (audio) { return {}; }
    throw std::runtime_error("Required pin not found");
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
    if (argc != 3) { std::cerr << "Usage: mjxl_graph_test decoder.ax video.avi\n"; return 1; }
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
    HMODULE module = nullptr;
    int exitCode = 0;
    try {
        const auto path = std::filesystem::absolute(argv[1]);
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) { throw std::runtime_error("Cannot load decoder filter"); }
        using Factory = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
        using Counter = unsigned long long(WINAPI*)();
        auto create = reinterpret_cast<Factory>(GetProcAddress(module, "DllGetClassObject"));
        auto counter = reinterpret_cast<Counter>(GetProcAddress(module, "MJXLDecodedFrames"));
        if (!create || !counter) { throw std::runtime_error("Missing decoder exports"); }
        ComPtr<IClassFactory> factory;
        check(create(kMjxlDecoder, IID_PPV_ARGS(&factory)), "GetClassObject");
        ComPtr<IBaseFilter> decoder;
        check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&decoder)), "Create decoder");
        ComPtr<IGraphBuilder> graph;
        check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), "Create graph");
        ComPtr<IBaseFilter> source, splitter, sink;
        check(CoCreateInstance(CLSID_AsyncReader, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&source)), "Create file source");
        ComPtr<IFileSourceFilter> loader;
        check(source.As(&loader), "File source interface");
        check(loader->Load(std::filesystem::absolute(argv[2]).c_str(), nullptr), "Load AVI");
        check(CoCreateInstance(CLSID_AviSplitter, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&splitter)), "Create AVI splitter");
        check(CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sink)), "Create null renderer");
        check(graph->AddFilter(source.Get(), L"File"), "Add source");
        check(graph->AddFilter(splitter.Get(), L"AVI Splitter"), "Add splitter");
        check(graph->AddFilter(decoder.Get(), L"MJXL Decoder"), "Add decoder");
        check(graph->AddFilter(sink.Get(), L"Null Renderer"), "Add renderer");
        check(graph->ConnectDirect(pin(source.Get(), PINDIR_OUTPUT).Get(), pin(splitter.Get(), PINDIR_INPUT).Get(), nullptr), "Connect AVI source");
        check(graph->ConnectDirect(pin(splitter.Get(), PINDIR_OUTPUT, true).Get(), pin(decoder.Get(), PINDIR_INPUT).Get(), nullptr), "Connect MJXL input");
        check(graph->ConnectDirect(pin(decoder.Get(), PINDIR_OUTPUT).Get(), pin(sink.Get(), PINDIR_INPUT).Get(), nullptr), "Connect RGB renderer");
        auto audioPin = pin(splitter.Get(), PINDIR_OUTPUT, false, true);
        ComPtr<IBaseFilter> audioSink;
        if (audioPin) {
            check(CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&audioSink)), "Create audio null renderer");
            check(graph->AddFilter(audioSink.Get(), L"Audio Null Renderer"), "Add audio renderer");
            check(graph->ConnectDirect(audioPin.Get(), pin(audioSink.Get(), PINDIR_INPUT).Get(), nullptr), "Connect PCM renderer");
        }
        ComPtr<IMediaFilter> clock;
        check(graph.As(&clock), "Clock interface");
        check(clock->SetSyncSource(nullptr), "Disable playback clock");
        ComPtr<IMediaControl> control;
        ComPtr<IMediaEvent> events;
        ComPtr<IMediaSeeking> seeking;
        check(graph.As(&control), "Control interface");
        check(graph.As(&events), "Events interface");
        check(graph.As(&seeking), "Seeking interface");
        const auto initial = counter();
        check(control->Run(), "Run graph");
        long code = 0;
        check(events->WaitForCompletion(30000, &code), "Wait for video end");
        if (code != EC_COMPLETE || counter() <= initial) { throw std::runtime_error("No decoded frames or playback failed"); }
        const auto first = counter() - initial;
        check(control->Stop(), "Stop graph");
        LONGLONG duration = 0;
        check(seeking->GetDuration(&duration), "Read duration");
        LONGLONG midpoint = duration / 2;
        check(seeking->SetPositions(&midpoint, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning), "Seek to midpoint");
        check(control->Run(), "Run after seeking");
        check(events->WaitForCompletion(30000, &code), "Wait after seeking");
        check(control->Stop(), "Stop after seeking");
        if (code != EC_COMPLETE || counter() <= initial + first) { throw std::runtime_error("Seek produced no frames"); }
        std::cout << "PASS DirectShow playback: " << first << " decoded frames; seek: " << counter() - initial - first << " additional frames\n";
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        exitCode = 1;
    }
    if (module) { FreeLibrary(module); }
    CoUninitialize();
    return exitCode;
}
