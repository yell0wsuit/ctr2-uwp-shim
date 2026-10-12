#pragma once
#include "generated/xaml.h"
#include "com/composable.h"
#include "host/host.h"
#include "host/pointer.h"
#include <wrl.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <map>
#include <functional>
namespace ctr2 {
HRESULT MakePanel(IInspectable** value);
HRESULT MakeGrid(IInspectable** value);
HRESULT MakeMediaElement(IInspectable** value);
HRESULT QueueElementLoaded(IInspectable* value);
HRESULT DeliverElementLoaded(IInspectable* object);
HRESULT CurrentApplication(ABI::Windows::UI::Xaml::IApplication** value);
inline std::map<ABI::Windows::UI::Xaml::IFrameworkElement*, std::function<HRESULT()>>& LoadedCallbacks() {
    static std::map<ABI::Windows::UI::Xaml::IFrameworkElement*, std::function<HRESULT()>> callbacks;
    return callbacks;
}
inline std::map<ABI::Windows::UI::Xaml::IFrameworkElement*, std::function<HRESULT()>>& SizeCallbacks() {
    static std::map<ABI::Windows::UI::Xaml::IFrameworkElement*, std::function<HRESULT()>> callbacks;
    return callbacks;
}
// Raises SizeChanged on loaded elements whose host-sized layout changed.
HRESULT RaiseElementSizeChanged();
class SizeArgs final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                                           ABI::Windows::UI::Xaml::ISizeChangedEventArgs,
                                                           ABI::Windows::UI::Xaml::IRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.SizeChangedEventArgs", BaseTrust);
    Microsoft::WRL::ComPtr<IInspectable> source_;
    ABI::Windows::Foundation::Size previous_, next_;

public:
    SizeArgs(IInspectable* source, ABI::Windows::Foundation::Size previous, ABI::Windows::Foundation::Size next)
        : source_(source), previous_(previous), next_(next) {}
    HRESULT STDMETHODCALLTYPE get_PreviousSize(ABI::Windows::Foundation::Size* value) override {
        if (!value) return E_POINTER;
        *value = previous_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_NewSize(ABI::Windows::Foundation::Size* value) override {
        if (!value) return E_POINTER;
        *value = next_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override {
        if (!value) return E_POINTER;
        return source_.CopyTo(value);
    }
};
class RoutedArgs final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRt>,
                                                             ABI::Windows::UI::Xaml::IRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.RoutedEventArgs", BaseTrust);
    Microsoft::WRL::ComPtr<IInspectable> source_;

public:
    explicit RoutedArgs(IInspectable* source) : source_(source) {}
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override {
        if (!value) return E_POINTER;
        return source_.CopyTo(value);
    }
};
template <class Derived, class Primary, class... Interfaces>
class Element : public Composable<Derived, Primary, ABI::Windows::UI::Xaml::DefaultIFrameworkElement,
                                  ABI::Windows::UI::Xaml::DefaultIUIElement,
                                  ABI::Windows::UI::Xaml::DefaultIDependencyObject, Interfaces...> {
    using Base = Composable<Derived, Primary, ABI::Windows::UI::Xaml::DefaultIFrameworkElement,
                            ABI::Windows::UI::Xaml::DefaultIUIElement, ABI::Windows::UI::Xaml::DefaultIDependencyObject,
                            Interfaces...>;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::IRoutedEventHandler> loaded_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::ISizeChangedEventHandler> resized_;
    Microsoft::WRL::ComPtr<IInspectable> panel_, grid_, media_;
    Microsoft::WRL::Wrappers::HString name_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerPressed_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerMoved_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerReleased_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerEntered_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerExited_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerCaptureLost_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerCanceled_;
    Microsoft::WRL::EventSource<ABI::Windows::UI::Xaml::Input::IPointerEventHandler> eventPointerWheelChanged_;
    double width_ = 1024, height_ = 768;
    bool loadedRaised_ = false;
    ABI::Windows::Foundation::Size laidOut_{0, 0};
    // XAML raises SizeChanged on first layout (from 0x0) and on every later
    // size change; every element here is laid out at the host client size.
    HRESULT RaiseSizeChanged() {
        ABI::Windows::Foundation::Size size{};
        auto hr = get_RenderSize(&size);
        if (FAILED(hr)) return hr;
        if (size.Width == laidOut_.Width && size.Height == laidOut_.Height) return S_OK;
        auto* source = static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this);
        auto args = Microsoft::WRL::Make<SizeArgs>(source, laidOut_, size);
        if (!args) return E_OUTOFMEMORY;
        laidOut_ = size;
        Log("TRACE", "%ls SizeChanged %gx%g", Derived::Name, size.Width, size.Height);
        return resized_.InvokeAll(source, args.Get());
    }

private:
    friend Derived;
    explicit Element(IInspectable* outer) : Base(outer) {
        RegisterPointerTarget(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this),
                              [this](UINT message, ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs* args) {
                                  auto* source = static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this);
                                  if (message == WM_LBUTTONDOWN) eventPointerPressed_.InvokeAll(source, args);
                                  if (message == WM_LBUTTONUP) eventPointerReleased_.InvokeAll(source, args);
                                  if (message == WM_MOUSEMOVE) eventPointerMoved_.InvokeAll(source, args);
                                  if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
                                      eventPointerWheelChanged_.InvokeAll(source, args);
                                  if (message == WM_CAPTURECHANGED) eventPointerCaptureLost_.InvokeAll(source, args);
                              });
        SizeCallbacks()[this] = [this] { return loadedRaised_ ? RaiseSizeChanged() : S_OK; };
        LoadedCallbacks()[this] = [this] {
            if (loadedRaised_) return S_OK;
            loadedRaised_ = true;
            auto hr = RaiseSizeChanged();
            if (FAILED(hr)) return hr;
            auto args = Microsoft::WRL::Make<RoutedArgs>(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this));
            hr = loaded_.InvokeAll(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this), args.Get());
            if (FAILED(hr)) return hr;
            // Named elements stand in for the page's XAML children (LoadComponent is a no-op).
            if (grid_) {
                hr = DeliverElementLoaded(grid_.Get());
                if (FAILED(hr)) return hr;
            }
            return panel_ ? DeliverElementLoaded(panel_.Get()) : S_OK;
        };
    }

public:
    // NOLINTNEXTLINE(bugprone-exception-escape): the constructor built both maps; erase() does not throw.
    ~Element() {
        UnregisterPointerTarget(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this));
        LoadedCallbacks().erase(this);
        SizeCallbacks().erase(this);
    }
    HRESULT STDMETHODCALLTYPE add_PointerPressed(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                 EventRegistrationToken* token) override {
        return eventPointerPressed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerPressed(EventRegistrationToken token) override {
        return eventPointerPressed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerMoved(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                               EventRegistrationToken* token) override {
        return eventPointerMoved_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerMoved(EventRegistrationToken token) override {
        return eventPointerMoved_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerReleased(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                  EventRegistrationToken* token) override {
        return eventPointerReleased_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerReleased(EventRegistrationToken token) override {
        return eventPointerReleased_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerEntered(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                 EventRegistrationToken* token) override {
        return eventPointerEntered_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerEntered(EventRegistrationToken token) override {
        return eventPointerEntered_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerExited(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                EventRegistrationToken* token) override {
        return eventPointerExited_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerExited(EventRegistrationToken token) override {
        return eventPointerExited_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerCaptureLost(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                     EventRegistrationToken* token) override {
        return eventPointerCaptureLost_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerCaptureLost(EventRegistrationToken token) override {
        return eventPointerCaptureLost_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerCanceled(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                  EventRegistrationToken* token) override {
        return eventPointerCanceled_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerCanceled(EventRegistrationToken token) override {
        return eventPointerCanceled_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerWheelChanged(ABI::Windows::UI::Xaml::Input::IPointerEventHandler* handler,
                                                      EventRegistrationToken* token) override {
        return eventPointerWheelChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerWheelChanged(EventRegistrationToken token) override {
        return eventPointerWheelChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE CapturePointer(ABI::Windows::UI::Xaml::Input::IPointer* pointer,
                                             boolean* result) override {
        return CaptureElementPointer(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this), pointer, result);
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCapture(ABI::Windows::UI::Xaml::Input::IPointer* pointer) override {
        if (!pointer) return E_INVALIDARG;
        return ReleaseElementPointer(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this));
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCaptures() override {
        return ReleaseElementPointer(static_cast<ABI::Windows::UI::Xaml::IFrameworkElement*>(this));
    }
    HRESULT STDMETHODCALLTYPE get_RenderSize(ABI::Windows::Foundation::Size* value) override {
        if (!value) return E_POINTER;
        double width, height;
        get_ActualWidth(&width);
        get_ActualHeight(&height);
        value->Width = static_cast<float>(width);
        value->Height = static_cast<float>(height);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ActualWidth(DOUBLE* value) override {
        if (!value) return E_POINTER;
        RECT r{};
        if (HostWindow()) GetClientRect(HostWindow(), &r);
        *value = HostWindow() ? r.right : width_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ActualHeight(DOUBLE* value) override {
        if (!value) return E_POINTER;
        RECT r{};
        if (HostWindow()) GetClientRect(HostWindow(), &r);
        *value = HostWindow() ? r.bottom : height_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Width(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = width_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Width(DOUBLE value) override {
        width_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Height(DOUBLE* value) override {
        if (!value) return E_POINTER;
        *value = height_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Height(DOUBLE value) override {
        height_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        return name_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Name(HSTRING value) override { return name_.Set(value); }
    HRESULT STDMETHODCALLTYPE add_Loaded(ABI::Windows::UI::Xaml::IRoutedEventHandler* handler,
                                         EventRegistrationToken* token) override {
        return loaded_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Loaded(EventRegistrationToken token) override { return loaded_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_SizeChanged(ABI::Windows::UI::Xaml::ISizeChangedEventHandler* handler,
                                              EventRegistrationToken* token) override {
        return resized_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SizeChanged(EventRegistrationToken token) override {
        return resized_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE FindName(HSTRING name, IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
        Log("TRACE", "FindName(%ls)", text);
        if (wcscmp(text, L"swapChainPanel") == 0) {
            if (!panel_) {
                auto hr = MakePanel(&panel_);
                if (FAILED(hr)) return hr;
            }
            return panel_.CopyTo(value);
        }
        if (wcscmp(text, L"gridForTextBlocks") == 0) {
            if (!grid_) {
                auto hr = MakeGrid(&grid_);
                if (FAILED(hr)) return hr;
            }
            return grid_.CopyTo(value);
        }
        // MovieMgrPage.InitializeComponent safe_casts this name to MediaElement^.
        if (wcscmp(text, L"mediaElement") == 0) {
            if (!media_) {
                auto hr = MakeMediaElement(&media_);
                if (FAILED(hr)) return hr;
            }
            return media_.CopyTo(value);
        }
        Log("FAIL", "unknown named element %ls", text);
        return S_OK;
    }
};
}
