// Desktop MediaElement backend: the Media Foundation Media Engine, which the
// Store MediaElement is built on, decodes frames that a render thread copies
// into a D3D11 swap chain on a child window. (MFPlay drew through EVR and
// Direct3D 9, which crashed inside D3D9.DLL on some systems.) Media Foundation
// is loaded on demand so systems without it (N editions, Wine without codecs)
// still take the game's MediaFailed path.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "element.h"
#include "generated/controls.h"
#include "log.h"
#include "video_layout.h"
#include <d3d10_1.h> // ID3D10Multithread, which D3D11 devices also implement.
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <shlwapi.h>
#include <atomic>
#include <memory>
#include <string>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
namespace ctr2 {
namespace {
struct MediaFoundation {
    decltype(&MFCreateAttributes) createAttributes = nullptr;
    decltype(&MFCreateDXGIDeviceManager) createDeviceManager = nullptr;
    HRESULT started = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
};
const MediaFoundation& LoadMediaFoundation() {
    static const MediaFoundation api = [] {
        MediaFoundation result;
        // Kept loaded and started: MF may still be unwinding worker callbacks
        // after Shutdown.
        HMODULE module = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return result;
        auto startup = reinterpret_cast<decltype(&MFStartup)>(GetProcAddress(module, "MFStartup"));
        result.createAttributes =
            reinterpret_cast<decltype(&MFCreateAttributes)>(GetProcAddress(module, "MFCreateAttributes"));
        result.createDeviceManager =
            reinterpret_cast<decltype(&MFCreateDXGIDeviceManager)>(GetProcAddress(module, "MFCreateDXGIDeviceManager"));
        result.started = startup && result.createAttributes && result.createDeviceManager
                             ? startup(MF_VERSION, MFSTARTUP_FULL)
                             : E_NOTIMPL;
        return result;
    }();
    return api;
}
// Engine events and render failures, delivered on the host thread.
struct MediaDispatch {
    std::function<void(DWORD, HRESULT)> deliver;
};
void Deliver(const std::weak_ptr<MediaDispatch>& weak, DWORD event, HRESULT hr) {
    PostHostWork(0, [weak, event, hr] {
        auto dispatch = weak.lock();
        if (dispatch && dispatch->deliver) dispatch->deliver(event, hr);
    });
}
class EngineNotify final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFMediaEngineNotify> {
    std::weak_ptr<MediaDispatch> dispatch_;

public:
    explicit EngineNotify(const std::weak_ptr<MediaDispatch>& dispatch) : dispatch_(dispatch) {}
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event, DWORD_PTR, DWORD param2) override {
        // MF calls this on its worker threads. Only the host thread touches the
        // element and its delegates. For an error, param2 is the HRESULT.
        if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA || event == MF_MEDIA_ENGINE_EVENT_ENDED ||
            event == MF_MEDIA_ENGINE_EVENT_ERROR)
            Deliver(dispatch_, event, event == MF_MEDIA_ENGINE_EVENT_ERROR ? static_cast<HRESULT>(param2) : S_OK);
        return S_OK;
    }
};
// Device, swap chain, and the thread that presents each new frame.
class VideoOutput {
    ComPtr<ID3D11Device> device_;
    ComPtr<IMFDXGIDeviceManager> manager_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<IMFMediaEngine> engine_;
    HWND surface_ = nullptr;
    HANDLE thread_ = nullptr, stop_ = nullptr;
    std::weak_ptr<MediaDispatch> dispatch_;
    std::atomic<VideoFit> fit_{VideoFit::Uniform};
    std::atomic<bool> relayout_{false};

    static DWORD WINAPI Run(void* self) {
        // The engine is free-threaded; join the MTA rather than rely on an implicit one.
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        static_cast<VideoOutput*>(self)->Render();
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    }
    // A fresh frame that cannot be copied fails playback; redrawing an older
    // one (after a resize, or once playback ended) only loses the picture.
    HRESULT Draw(bool picture, bool fresh) {
        ComPtr<ID3D11Texture2D> back;
        auto hr = swap_->GetBuffer(0, IID_PPV_ARGS(&back));
        if (FAILED(hr)) return hr;
        // Clear first: the letterbox bars must not keep an older picture.
        ComPtr<ID3D11RenderTargetView> view;
        hr = device_->CreateRenderTargetView(back.Get(), nullptr, &view);
        if (FAILED(hr)) return hr;
        ComPtr<ID3D11DeviceContext> context;
        device_->GetImmediateContext(&context);
        const float black[4] = {0, 0, 0, 1};
        context->ClearRenderTargetView(view.Get(), black);
        if (picture) {
            D3D11_TEXTURE2D_DESC desc{};
            back->GetDesc(&desc);
            DWORD width = 0, height = 0;
            engine_->GetNativeVideoSize(&width, &height);
            const auto layout =
                LayoutVideo(SIZE{static_cast<LONG>(width), static_cast<LONG>(height)},
                            SIZE{static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)}, fit_.load());
            const auto& crop = layout.source;
            const MFVideoNormalizedRect source{crop.left, crop.top, crop.right, crop.bottom};
            const MFARGB border{0, 0, 0, 255};
            hr = engine_->TransferVideoFrame(back.Get(), &source, &layout.target, &border);
            if (FAILED(hr) && fresh) return hr;
        }
        return swap_->Present(1, 0);
    }
    void Render() {
        SIZE size{};
        bool picture = false, drawn = false;
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        ComPtr<IDXGIOutput> output;
        while (WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT) {
            HRESULT hr = S_OK;
            RECT client{};
            GetClientRect(surface_, &client);
            const SIZE now{client.right, client.bottom};
            bool redraw = !drawn || relayout_.exchange(false);
            if (now.cx > 0 && now.cy > 0 && (now.cx != size.cx || now.cy != size.cy)) {
                size = now;
                output.Reset();
                hr = swap_->ResizeBuffers(0, static_cast<UINT>(size.cx), static_cast<UINT>(size.cy),
                                          DXGI_FORMAT_UNKNOWN, 0);
                redraw = true;
            }
            LONGLONG time = 0;
            const bool fresh = SUCCEEDED(hr) && engine_->OnVideoStreamTick(&time) == S_OK;
            if (fresh) picture = redraw = true;
            if (SUCCEEDED(hr) && redraw) {
                hr = Draw(picture, fresh);
                drawn = SUCCEEDED(hr);
            }
            if (FAILED(hr)) {
                Log("FAIL", "MediaElement video output hr=%08lx", hr);
                Deliver(dispatch_, MF_MEDIA_ENGINE_EVENT_ERROR, hr);
                return;
            }
            if (redraw) continue; // Present already waited for the display.
            // Poll for the next frame once per refresh. WaitForVBlank returns
            // at once where it is unsupported; sleep then instead of spinning.
            LARGE_INTEGER start{};
            LARGE_INTEGER end{};
            QueryPerformanceCounter(&start);
            if (!output) swap_->GetContainingOutput(&output);
            if (!output || FAILED(output->WaitForVBlank())) output.Reset();
            QueryPerformanceCounter(&end);
            if ((end.QuadPart - start.QuadPart) * 1000 < frequency.QuadPart) Sleep(1);
        }
    }

public:
    ~VideoOutput() { Stop(); }
    HRESULT Initialize(const MediaFoundation& api) {
        static const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
        };
        // Video support lets the engine decode on the GPU; without it the
        // engine may still decode in software.
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                    D3D11_SDK_VERSION, &device_, nullptr, nullptr);
        if (FAILED(hr)) {
            flags &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &device_, nullptr, nullptr);
        }
        if (FAILED(hr)) return hr;
        // The engine's threads and the render thread share the device.
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(device_.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
        UINT token = 0;
        hr = api.createDeviceManager(&token, &manager_);
        return FAILED(hr) ? hr : manager_->ResetDevice(device_.Get(), token);
    }
    IMFDXGIDeviceManager* Manager() const { return manager_.Get(); }
    void SetFit(VideoFit fit) {
        fit_ = fit;
        relayout_ = true;
    }
    // Called once the engine knows the stream has video.
    HRESULT Start(HWND surface, IMFMediaEngine* engine, const std::weak_ptr<MediaDispatch>& dispatch) {
        if (thread_) return S_OK;
        ComPtr<IDXGIDevice> dxgi;
        auto hr = device_.As(&dxgi);
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(hr)) hr = dxgi->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> factory;
        if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        hr = factory->CreateSwapChainForHwnd(device_.Get(), surface, &desc, nullptr, nullptr, &swap_);
        if (FAILED(hr)) {
            // As for the game's swap chains: older systems refuse flip model on a child window.
            desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = factory->CreateSwapChainForHwnd(device_.Get(), surface, &desc, nullptr, nullptr, &swap_);
        }
        if (FAILED(hr)) return hr;
        // The host window owns Alt+Enter.
        factory->MakeWindowAssociation(surface, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
        surface_ = surface;
        engine_ = engine;
        dispatch_ = dispatch;
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_) return HRESULT_FROM_WIN32(GetLastError());
        thread_ = CreateThread(nullptr, 0, Run, this, 0, nullptr);
        return thread_ ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    void Stop() {
        if (thread_) {
            SetEvent(stop_);
            // DXGI can send the surface window (this thread's) a message while
            // the render thread presents. Serve sent messages only, so posted
            // work cannot reenter the element while it closes.
            while (MsgWaitForMultipleObjects(1, &thread_, FALSE, INFINITE, QS_SENDMESSAGE) == WAIT_OBJECT_0 + 1) {
                MSG message;
                PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
            }
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (stop_) CloseHandle(stop_);
        stop_ = nullptr;
        engine_.Reset();
    }
    void Release() {
        Stop();
        swap_.Reset();
        manager_.Reset();
        device_.Reset();
    }
};
class FailureArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, IExceptionRoutedEventArgs, IRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.ExceptionRoutedEventArgs", BaseTrust);
    ComPtr<IInspectable> source_;
    Wrappers::HString message_;

public:
    FailureArgs(IInspectable* source, HRESULT hr) : source_(source) {
        wchar_t text[80]{};
        swprintf_s(text, L"Movie playback failed (0x%08lx)", hr);
        message_.Set(text);
    }
    HRESULT STDMETHODCALLTYPE get_ErrorMessage(HSTRING* value) override { return message_.CopyTo(value); }
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override { return source_.CopyTo(value); }
};
VideoFit FitFor(Stretch stretch) {
    if (stretch == Stretch_Fill) return VideoFit::Fill;
    return stretch == Stretch_UniformToFill ? VideoFit::UniformToFill : VideoFit::Uniform;
}
class MediaElement final : public Element<MediaElement, DefaultIMediaElement, DefaultIMediaElement2> {
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> source_;
    ComPtr<IMFMediaEngine> engine_;
    VideoOutput video_;
    ComPtr<IInspectable> previousPointer_;
    std::shared_ptr<MediaDispatch> dispatch_;
    EventSource<IRoutedEventHandler> opened_, ended_;
    EventSource<IExceptionRoutedEventHandler> failed_;
    HWND surface_ = nullptr;
    boolean autoPlay_ = true, muted_ = false;
    double volume_ = 1;
    bool ready_ = false, playRequested_ = false, failedOnce_ = false;
    MediaElementState state_ = MediaElementState_Closed;
    Stretch stretch_ = Stretch_Uniform;
    void Close() {
        // Invalidates callbacks already queued for the previous source.
        dispatch_.reset();
        ready_ = false;
        video_.Stop();
        if (engine_) engine_->Shutdown();
        engine_.Reset();
        video_.Release();
        if (CurrentPointerTarget() == static_cast<IFrameworkElement*>(this)) SetPointerTarget(previousPointer_.Get());
        previousPointer_.Reset();
        if (IsWindow(surface_)) DestroyWindow(surface_);
        surface_ = nullptr;
    }
    void Fail(HRESULT hr) {
        if (failedOnce_) return;
        failedOnce_ = true;
        Log("FAIL", "MediaElement playback hr=%08lx", hr);
        Close();
        state_ = MediaElementState_Closed;
        ComPtr<IInspectable> sender;
        QueryInterface(IID_PPV_ARGS(&sender)); // Handlers may navigate away and release this element.
        auto args = Make<FailureArgs>(sender.Get(), hr);
        failed_.InvokeAll(sender.Get(), args.Get());
    }
    void Raise(EventSource<IRoutedEventHandler>& event) {
        ComPtr<IInspectable> sender;
        QueryInterface(IID_PPV_ARGS(&sender));
        auto args = Make<RoutedArgs>(sender.Get());
        event.InvokeAll(sender.Get(), args.Get());
    }
    void OnEvent(DWORD event, HRESULT hr) {
        Log("TRACE", "MediaElement event=%lu hr=%08lx", event, hr);
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) {
            Fail(FAILED(hr) ? hr : E_FAIL);
            return;
        }
        if (!engine_) return;
        if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA) {
            ready_ = true;
            state_ = MediaElementState_Stopped;
            if (engine_->HasVideo()) hr = video_.Start(surface_, engine_.Get(), dispatch_);
            // Start before raising Opened; handlers can Stop or replace Source.
            if (SUCCEEDED(hr) && playRequested_) hr = Play();
            if (SUCCEEDED(hr)) {
                Raise(opened_);
                return;
            }
        } else if (event == MF_MEDIA_ENGINE_EVENT_ENDED) {
            state_ = MediaElementState_Stopped;
            Raise(ended_);
            return;
        }
        if (FAILED(hr)) Fail(hr);
    }
    HRESULT ResolvePath(std::wstring& path) {
        Wrappers::HString uri;
        auto hr = source_->get_AbsoluteUri(uri.GetAddressOf());
        if (FAILED(hr)) return hr;
        const wchar_t* text = uri.GetRawBuffer(nullptr);
        wchar_t buffer[32768]{};
        DWORD length = ARRAYSIZE(buffer);
        if (_wcsnicmp(text, L"file:", 5) == 0) {
            hr = PathCreateFromUrlW(text, buffer, &length, 0);
            if (SUCCEEDED(hr)) path = buffer;
            return hr;
        }
        constexpr wchar_t prefix[] = L"ms-appx:///";
        if (_wcsnicmp(text, prefix, ARRAYSIZE(prefix) - 1) != 0) return E_INVALIDARG;
        std::wstring encoded(text + ARRAYSIZE(prefix) - 1);
        hr = UrlUnescapeW(encoded.data(), buffer, &length, 0);
        if (FAILED(hr)) return hr;
        std::wstring relative(buffer);
        for (auto& c : relative)
            if (c == L'/') c = L'\\';
        if (relative.empty() || relative.front() == L'\\' || relative.find(L':') != std::wstring::npos ||
            (L"\\" + relative + L"\\").find(L"\\..\\") != std::wstring::npos)
            return E_INVALIDARG;
        if (!GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer))) return HRESULT_FROM_WIN32(GetLastError());
        path = buffer;
        path.resize(path.find_last_of(L"\\/") + 1);
        path += relative;
        return S_OK;
    }
    HRESULT Open() {
        std::wstring path;
        auto hr = ResolvePath(path);
        if (FAILED(hr)) return hr;
        wchar_t url[32768]{};
        DWORD length = ARRAYSIZE(url);
        hr = UrlCreateFromPathW(path.c_str(), url, &length, 0);
        if (FAILED(hr)) return hr;
        const auto& api = LoadMediaFoundation();
        if (FAILED(api.started)) return api.started;
        ComPtr<IMFMediaEngineClassFactory> factory;
        hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;
        hr = video_.Initialize(api);
        if (FAILED(hr)) return hr;
        surface_ = CreateSurfaceWindow();
        if (!surface_) return E_FAIL;
        previousPointer_ = CurrentPointerTarget();
        SetPointerTarget(static_cast<IFrameworkElement*>(this));
        dispatch_ = std::make_shared<MediaDispatch>();
        dispatch_->deliver = [this](DWORD event, HRESULT result) { OnEvent(event, result); };
        auto notify = Make<EngineNotify>(dispatch_);
        if (!notify) return E_OUTOFMEMORY;
        ComPtr<IMFAttributes> attributes;
        hr = api.createAttributes(&attributes, 3);
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.Get());
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, video_.Manager());
        // Frame-server mode: frames are copied out, never presented by the engine.
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(0, attributes.Get(), &engine_);
        if (FAILED(hr)) return hr;
        video_.SetFit(FitFor(stretch_));
        engine_->SetVolume(volume_);
        engine_->SetMuted(muted_);
        Log("TRACE", "MediaElement opening %ls", path.c_str());
        auto* source = SysAllocString(url);
        if (!source) return E_OUTOFMEMORY;
        hr = engine_->SetSource(source);
        SysFreeString(source);
        return hr;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.MediaElement";
    explicit MediaElement(IInspectable* outer) : Element(outer) {}
    ~MediaElement() { Close(); }
    HRESULT STDMETHODCALLTYPE get_Source(ABI::Windows::Foundation::IUriRuntimeClass** value) override {
        return source_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Source(ABI::Windows::Foundation::IUriRuntimeClass* value) override {
        Close();
        source_ = value;
        failedOnce_ = false;
        state_ = value ? MediaElementState_Opening : MediaElementState_Closed;
        playRequested_ = autoPlay_;
        if (value) {
            // Open asynchronously, after the page has armed its watchdog.
            dispatch_ = std::make_shared<MediaDispatch>();
            auto weak = std::weak_ptr<MediaDispatch>(dispatch_);
            auto hr = PostHostWork(0, [this, weak] {
                if (weak.expired()) return;
                auto result = Open();
                if (FAILED(result)) Fail(result);
            });
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AutoPlay(boolean* value) override {
        if (!value) return E_POINTER;
        *value = autoPlay_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_AutoPlay(boolean value) override {
        autoPlay_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CurrentState(MediaElementState* value) override {
        if (!value) return E_POINTER;
        *value = state_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Play() override {
        if (!dispatch_ && source_) {
            auto source = source_;
            auto hr = put_Source(source.Get());
            if (FAILED(hr)) return hr;
        }
        playRequested_ = true;
        if (!ready_) return S_OK;
        auto hr = engine_->Play();
        if (SUCCEEDED(hr)) {
            state_ = MediaElementState_Playing;
            ShowWindow(surface_, SW_SHOW);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Pause() override {
        playRequested_ = false;
        if (!ready_) return S_OK;
        auto hr = engine_->Pause();
        if (SUCCEEDED(hr)) state_ = MediaElementState_Paused;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Stop() override {
        playRequested_ = false;
        // Cancels an in-flight open as well as active playback. Replaying a
        // stopped element reopens Source on demand.
        Close();
        state_ = source_ ? MediaElementState_Stopped : MediaElementState_Closed;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Stretch(Stretch* value) override {
        if (!value) return E_POINTER;
        *value = stretch_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Stretch(Stretch value) override {
        if (value < Stretch_None || value > Stretch_UniformToFill) return E_INVALIDARG;
        stretch_ = value;
        video_.SetFit(FitFor(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsMuted(boolean* value) override {
        if (!value) return E_POINTER;
        *value = muted_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsMuted(boolean value) override {
        muted_ = value;
        return engine_ ? engine_->SetMuted(value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Volume(double* value) override {
        if (!value) return E_POINTER;
        *value = volume_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Volume(double value) override {
        if (!(value >= 0 && value <= 1)) return E_INVALIDARG;
        volume_ = value;
        return engine_ ? engine_->SetVolume(value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_MediaOpened(IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        return opened_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_MediaOpened(EventRegistrationToken token) override {
        return opened_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_MediaEnded(IRoutedEventHandler* handler, EventRegistrationToken* token) override {
        return ended_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_MediaEnded(EventRegistrationToken token) override { return ended_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_MediaFailed(IExceptionRoutedEventHandler* handler,
                                              EventRegistrationToken* token) override {
        return failed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_MediaFailed(EventRegistrationToken token) override {
        return failed_.Remove(token);
    }
};
}
HRESULT MakeMediaElement(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ComPtr<IMediaElement> media;
    auto hr = Compose<MediaElement>(nullptr, nullptr, media.GetAddressOf());
    return FAILED(hr) ? hr : media.CopyTo(value);
}
}
