// Desktop MediaElement backend. MFPlay is loaded on demand so unavailable
// codecs/backends (including Wine) still take the game's MediaFailed path.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "element.h"
#include "generated/controls.h"
#include <mfplay.h>
#include <shlwapi.h>
#include <memory>
#include <string>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
namespace ctr2 {
namespace {
struct MediaDispatch {
    std::function<void(MFP_EVENT_TYPE, HRESULT, IMFPMediaItem*)> deliver;
};
class MediaCallback final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFPMediaPlayerCallback> {
    std::weak_ptr<MediaDispatch> dispatch_;

public:
    explicit MediaCallback(const std::shared_ptr<MediaDispatch>& dispatch) : dispatch_(dispatch) {}
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override {
        auto weak = dispatch_;
        auto type = event->eEventType;
        auto hr = event->hrEvent;
        ComPtr<IMFPMediaItem> item;
        if (type == MFP_EVENT_TYPE_MEDIAITEM_CREATED) item = MFP_GET_MEDIAITEM_CREATED_EVENT(event)->pMediaItem;
        // MF callbacks run on worker threads. Only the host thread touches the
        // element, its delegates, and the borrowed pointer in deliver.
        PostHostWork(0, [weak, type, hr, item] {
            auto dispatch = weak.lock();
            if (dispatch && dispatch->deliver) dispatch->deliver(type, hr, item.Get());
        });
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
class MediaElement final : public Element<MediaElement, DefaultIMediaElement, DefaultIMediaElement2> {
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> source_;
    ComPtr<IMFPMediaPlayer> player_;
    ComPtr<IInspectable> previousPointer_;
    std::shared_ptr<MediaDispatch> dispatch_;
    EventSource<IRoutedEventHandler> opened_, ended_;
    EventSource<IExceptionRoutedEventHandler> failed_;
    HWND surface_ = nullptr;
    WNDPROC originalProc_ = nullptr;
    boolean autoPlay_ = true, muted_ = false;
    double volume_ = 1;
    bool ready_ = false, playRequested_ = false, failedOnce_ = false;
    MediaElementState state_ = MediaElementState_Closed;
    Stretch stretch_ = Stretch_Uniform;
    static LRESULT CALLBACK VideoProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<MediaElement*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->player_ && msg == WM_SIZE) {
            self->UpdateVideoLayout();
            self->player_->UpdateVideo();
        }
        if (self && self->player_ && msg == WM_PAINT) {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            self->player_->UpdateVideo();
            EndPaint(hwnd, &paint);
            return 0;
        }
        return self ? CallWindowProcW(self->originalProc_, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }
    void Close() {
        // Invalidates callbacks already queued for the previous source.
        dispatch_.reset();
        ready_ = false;
        if (player_) player_->Shutdown();
        player_.Reset();
        if (CurrentPointerTarget() == static_cast<IFrameworkElement*>(this)) SetPointerTarget(previousPointer_.Get());
        previousPointer_.Reset();
        if (IsWindow(surface_)) {
            SetWindowLongPtrW(surface_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProc_));
            SetWindowLongPtrW(surface_, GWLP_USERDATA, 0);
            DestroyWindow(surface_);
        }
        surface_ = nullptr;
    }
    void UpdateVideoLayout() {
        if (!player_) return;
        MFVideoNormalizedRect crop{0, 0, 1, 1};
        SIZE size{};
        RECT client{};
        if (stretch_ == Stretch_UniformToFill && SUCCEEDED(player_->GetNativeVideoSize(nullptr, &size)) &&
            GetClientRect(surface_, &client) && size.cx > 0 && size.cy > 0 && client.right > 0 && client.bottom > 0) {
            const double sourceAspect = static_cast<double>(size.cx) / size.cy;
            const double targetAspect = static_cast<double>(client.right) / client.bottom;
            if (sourceAspect > targetAspect) {
                crop.left = static_cast<float>((1 - targetAspect / sourceAspect) / 2);
                crop.right = 1 - crop.left;
            } else {
                crop.top = static_cast<float>((1 - sourceAspect / targetAspect) / 2);
                crop.bottom = 1 - crop.top;
            }
        }
        player_->SetVideoSourceRect(&crop);
        player_->SetAspectRatioMode(stretch_ == Stretch_Fill ? MFVideoARMode_None : MFVideoARMode_PreservePicture);
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
    void OnEvent(MFP_EVENT_TYPE type, HRESULT hr, IMFPMediaItem* item) {
        Log("TRACE", "MediaElement event=%u hr=%08lx", static_cast<unsigned>(type), hr);
        if (FAILED(hr)) {
            Fail(hr);
            return;
        }
        if (!player_) return;
        if (type == MFP_EVENT_TYPE_MEDIAITEM_CREATED) {
            hr = player_->SetMediaItem(item);
        } else if (type == MFP_EVENT_TYPE_MEDIAITEM_SET) {
            ready_ = true;
            UpdateVideoLayout();
            state_ = MediaElementState_Stopped;
            // Start before raising Opened; handlers can Stop or replace Source.
            if (playRequested_) hr = Play();
            if (SUCCEEDED(hr)) {
                Raise(opened_);
                return;
            }
        } else if (type == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
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
        // Keep the module loaded: MF may still be unwinding worker callbacks
        // after Shutdown. Loading it only here preserves the no-media fallback.
        static HMODULE module = LoadLibraryExW(L"mfplay.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        auto create = reinterpret_cast<decltype(&MFPCreateMediaPlayer)>(GetProcAddress(module, "MFPCreateMediaPlayer"));
        if (!create) return E_NOTIMPL;
        surface_ = CreateSurfaceWindow();
        if (!surface_) return E_FAIL;
        previousPointer_ = CurrentPointerTarget();
        SetPointerTarget(static_cast<IFrameworkElement*>(this));
        SetWindowLongPtrW(surface_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        originalProc_ =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrW(surface_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(VideoProc)));
        dispatch_ = std::make_shared<MediaDispatch>();
        dispatch_->deliver = [this](MFP_EVENT_TYPE type, HRESULT result, IMFPMediaItem* item) {
            OnEvent(type, result, item);
        };
        auto callback = Make<MediaCallback>(dispatch_);
        hr = create(nullptr, FALSE, MFP_OPTION_NONE, callback.Get(), surface_, &player_);
        if (FAILED(hr)) return hr;
        player_->SetVolume(static_cast<float>(volume_));
        player_->SetMute(muted_);
        player_->SetAspectRatioMode(stretch_ == Stretch_Fill ? MFVideoARMode_None : MFVideoARMode_PreservePicture);
        Log("TRACE", "MediaElement opening %ls", path.c_str());
        return player_->CreateMediaItemFromURL(path.c_str(), FALSE, 0, nullptr);
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
        auto hr = player_->Play();
        if (SUCCEEDED(hr)) {
            state_ = MediaElementState_Playing;
            ShowWindow(surface_, SW_SHOW);
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Pause() override {
        playRequested_ = false;
        if (!ready_) return S_OK;
        auto hr = player_->Pause();
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
        UpdateVideoLayout();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsMuted(boolean* value) override {
        if (!value) return E_POINTER;
        *value = muted_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsMuted(boolean value) override {
        muted_ = value;
        return player_ ? player_->SetMute(value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Volume(double* value) override {
        if (!value) return E_POINTER;
        *value = volume_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Volume(double value) override {
        if (!(value >= 0 && value <= 1)) return E_INVALIDARG;
        volume_ = value;
        return player_ ? player_->SetVolume(static_cast<float>(value)) : S_OK;
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
