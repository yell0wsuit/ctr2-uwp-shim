#include "generated/xaml.h"
#include "com/factory.h"
#include "host/host.h"
#include "host/pointer.h"
#include <windows.ui.xaml.media.dxinterop.h>
#include "host/event_args.h"
#include "registry.h"
#include <wrl/event.h>
#include "host/dispatcher.h"
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
namespace ctr2 {
HRESULT QueueElementLoaded(IInspectable* value);
HRESULT RaiseElementSizeChanged();
HRESULT MakeCoreWindow(ABI::Windows::UI::Core::ICoreWindow** value);
class Window final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIWindow> {
    InspectableClass(L"Windows.UI.Xaml.Window", BaseTrust);
    ComPtr<IUIElement> content_;
    EventSource<IWindowSizeChangedEventHandler> resized_;
    EventSource<IWindowActivatedEventHandler> activated_;
    EventSource<IWindowClosedEventHandler> closed_;
    EventSource<IWindowVisibilityChangedEventHandler> visibility_;

public:
    void OnMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_ACTIVATE) {
            auto args = Make<ActivationArgs>(LOWORD(wparam) == WA_INACTIVE
                                                 ? ABI::Windows::UI::Core::CoreWindowActivationState_Deactivated
                                                 : ABI::Windows::UI::Core::CoreWindowActivationState_CodeActivated);
            if (args) activated_.InvokeAll(this, args.Get());
        }
        if (message == WM_SIZE) {
            auto args = Make<WindowSizeArgs>(
                ABI::Windows::Foundation::Size{static_cast<float>(LOWORD(lparam)), static_cast<float>(HIWORD(lparam))});
            if (args) resized_.InvokeAll(this, args.Get());
            auto hr = RaiseElementSizeChanged();
            if (FAILED(hr)) Log("FAIL", "SizeChanged handler hr=%08lx", hr);
        }
        if (message == WM_SHOWWINDOW) {
            auto args = Make<VisibilityArgs>(wparam != 0);
            if (args) visibility_.InvokeAll(this, args.Get());
        }
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value) override {
        return MakeDispatcher(value);
    }
    HRESULT STDMETHODCALLTYPE get_CoreWindow(ABI::Windows::UI::Core::ICoreWindow** value) override {
        return MakeCoreWindow(value);
    }
    HRESULT STDMETHODCALLTYPE get_Bounds(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        RECT r{0, 0, 1024, 768};
        if (HostWindow()) GetClientRect(HostWindow(), &r);
        *value = {0, 0, static_cast<float>(r.right), static_cast<float>(r.bottom)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = IsWindowVisible(HostWindow()) != FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Content(IUIElement** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IUIElement* value) override {
        SetPointerTarget(value);
        content_ = value;
        return S_OK;
    }
#define WINDOW_EVENT(Name, Handler, field)                                                                             \
    HRESULT STDMETHODCALLTYPE add_##Name(Handler* handler, EventRegistrationToken* token) override {                   \
        return field.Add(handler, token);                                                                              \
    }                                                                                                                  \
    HRESULT STDMETHODCALLTYPE remove_##Name(EventRegistrationToken token) override { return field.Remove(token); }
    WINDOW_EVENT(SizeChanged, IWindowSizeChangedEventHandler, resized_)
    WINDOW_EVENT(Activated, IWindowActivatedEventHandler, activated_)
    WINDOW_EVENT(Closed, IWindowClosedEventHandler, closed_)
    WINDOW_EVENT(VisibilityChanged, IWindowVisibilityChangedEventHandler, visibility_)
#undef WINDOW_EVENT
    HRESULT STDMETHODCALLTYPE Activate() override {
        if (!HostWindow()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "Window Activate begin");
        ActivateHostWindow();
        UpdateWindow(HostWindow());
        Log("TRACE", "Window Activate returned");
        return QueueElementLoaded(content_.Get());
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
};
static ComPtr<Window> xamlWindow;
void XamlHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (xamlWindow) xamlWindow->OnMessage(message, wparam, lparam);
}
class WindowFactory final : public Factory<DefaultIWindowStatics> {

public:
    WindowFactory() : Factory(L"Windows.UI.Xaml.Window") {}
    HRESULT STDMETHODCALLTYPE get_Current(IWindow** value) override {
        if (!value) return E_POINTER;
        if (!xamlWindow) xamlWindow = Make<Window>();
        return xamlWindow ? xamlWindow.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeWindowFactory(IActivationFactory** value) {
    static auto factory = Make<WindowFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
