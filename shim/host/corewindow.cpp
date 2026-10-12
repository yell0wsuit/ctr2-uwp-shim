#include "generated/uicore.h"
#include "host.h"
#include "log.h"
#include "event_args.h"
#include <wrl.h>
#include <wrl/event.h>
#include "host/dispatcher.h"
#include "platform/cursor.h"
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Core;
namespace ctr2 {
class CoreWindow final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICoreWindow> {
    InspectableClass(L"Windows.UI.Core.CoreWindow", BaseTrust);
    ComPtr<ICoreCursor> cursor_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs>
        eventActivated_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CAutomationProviderRequestedEventArgs>
        eventAutomationProviderRequested_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs>
        eventCharacterReceived_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs>
        eventClosed_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CInputEnabledEventArgs>
        eventInputEnabled_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs>
        eventKeyDown_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs>
        eventKeyUp_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerCaptureLost_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerEntered_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerExited_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerMoved_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerPressed_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerReleased_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CTouchHitTestingEventArgs>
        eventTouchHitTesting_;
    EventSource<__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs>
        eventPointerWheelChanged_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs>
        eventSizeChanged_;
    EventSource<
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs>
        eventVisibilityChanged_;
    boolean enabled_ = true;
    CoreWindowFlowDirection flow_ = CoreWindowFlowDirection_LeftToRight;

public:
    void OnMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_ACTIVATE) {
            Log("TRACE", "CoreWindow Activated deliver state=%u", LOWORD(wparam));
            auto args = Make<ActivationArgs>(LOWORD(wparam) == WA_INACTIVE ? CoreWindowActivationState_Deactivated
                                                                           : CoreWindowActivationState_CodeActivated);
            if (args) eventActivated_.InvokeAll(this, args.Get());
        }
        if (message == WM_SIZE) {
            auto args = Make<WindowSizeArgs>(
                ABI::Windows::Foundation::Size{static_cast<float>(LOWORD(lparam)), static_cast<float>(HIWORD(lparam))});
            if (args) eventSizeChanged_.InvokeAll(this, args.Get());
        }
        if (message == WM_SHOWWINDOW) {
            Log("TRACE", "CoreWindow VisibilityChanged deliver visible=%u", static_cast<unsigned>(wparam));
            auto args = Make<VisibilityArgs>(wparam != 0);
            if (args) eventVisibilityChanged_.InvokeAll(this, args.Get());
        }
    }
    HRESULT STDMETHODCALLTYPE get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value) override {
        return MakeDispatcher(value);
    }
    HRESULT STDMETHODCALLTYPE add_Activated(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowActivatedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventActivated_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) override {
        return eventActivated_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_AutomationProviderRequested(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CAutomationProviderRequestedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventAutomationProviderRequested_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_AutomationProviderRequested(EventRegistrationToken token) override {
        return eventAutomationProviderRequested_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_CharacterReceived(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventCharacterReceived_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CharacterReceived(EventRegistrationToken token) override {
        return eventCharacterReceived_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_Closed(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCoreWindowEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventClosed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken token) override {
        return eventClosed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_InputEnabled(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CInputEnabledEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventInputEnabled_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_InputEnabled(EventRegistrationToken token) override {
        return eventInputEnabled_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE
    add_KeyDown(__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs* handler,
                EventRegistrationToken* token) override {
        return eventKeyDown_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyDown(EventRegistrationToken token) override {
        return eventKeyDown_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE
    add_KeyUp(__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs* handler,
              EventRegistrationToken* token) override {
        return eventKeyUp_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_KeyUp(EventRegistrationToken token) override { return eventKeyUp_.Remove(token); }
    HRESULT STDMETHODCALLTYPE add_PointerCaptureLost(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerCaptureLost_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerCaptureLost(EventRegistrationToken token) override {
        return eventPointerCaptureLost_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerEntered(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerEntered_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerEntered(EventRegistrationToken token) override {
        return eventPointerEntered_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerExited(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerExited_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerExited(EventRegistrationToken token) override {
        return eventPointerExited_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerMoved(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerMoved_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerMoved(EventRegistrationToken token) override {
        return eventPointerMoved_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerPressed(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerPressed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerPressed(EventRegistrationToken token) override {
        return eventPointerPressed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerReleased(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerReleased_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerReleased(EventRegistrationToken token) override {
        return eventPointerReleased_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_TouchHitTesting(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CTouchHitTestingEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventTouchHitTesting_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TouchHitTesting(EventRegistrationToken token) override {
        return eventTouchHitTesting_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_PointerWheelChanged(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs* handler,
        EventRegistrationToken* token) override {
        return eventPointerWheelChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_PointerWheelChanged(EventRegistrationToken token) override {
        return eventPointerWheelChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_SizeChanged(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CWindowSizeChangedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventSizeChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_SizeChanged(EventRegistrationToken token) override {
        return eventSizeChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE add_VisibilityChanged(
        __FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CVisibilityChangedEventArgs*
            handler,
        EventRegistrationToken* token) override {
        return eventVisibilityChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_VisibilityChanged(EventRegistrationToken token) override {
        return eventVisibilityChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE get_Bounds(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        RECT r{0, 0, 1024, 768};
        if (HostWindow()) GetClientRect(HostWindow(), &r);
        *value = {0, 0, static_cast<float>(r.right), static_cast<float>(r.bottom)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerCursor(ICoreCursor** value) override {
        if (!value) return E_POINTER;
        return cursor_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_PointerCursor(ICoreCursor* value) override {
        cursor_ = value;
        // No cursor hides the pointer over the game.
        HCURSOR shown = value ? LoadCoreCursor(value) : nullptr;
        CoreCursorType type = CoreCursorType_Arrow;
        UINT32 id = 0;
        if (value) {
            value->get_Type(&type);
            value->get_Id(&id);
        }
        Log("TRACE", "PointerCursor type=%d id=%u cursor=%p", value ? static_cast<int>(type) : -1, id, shown);
        SetHostCursor(shown);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerPosition(ABI::Windows::Foundation::Point* value) override {
        if (!value) return E_POINTER;
        POINT p{};
        GetCursorPos(&p);
        if (HostWindow()) ScreenToClient(HostWindow(), &p);
        *value = {static_cast<float>(p.x), static_cast<float>(p.y)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInputEnabled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = enabled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsInputEnabled(boolean value) override {
        enabled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FlowDirection(CoreWindowFlowDirection* value) override {
        if (!value) return E_POINTER;
        *value = flow_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_FlowDirection(CoreWindowFlowDirection value) override {
        flow_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Visible(boolean* value) override {
        if (!value) return E_POINTER;
        *value = IsWindowVisible(HostWindow()) != FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Activate() override {
        ActivateHostWindow();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        PostMessageW(HostWindow(), WM_CLOSE, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPointerCapture() override {
        SetCapture(HostWindow());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReleasePointerCapture() override {
        ReleaseCapture();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAsyncKeyState(ABI::Windows::System::VirtualKey key,
                                               CoreVirtualKeyStates* value) override {
        if (!value) return E_POINTER;
        *value = (::GetAsyncKeyState(static_cast<int>(key)) & 0x8000) ? CoreVirtualKeyStates_Down
                                                                      : CoreVirtualKeyStates_None;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetKeyState(ABI::Windows::System::VirtualKey key, CoreVirtualKeyStates* value) override {
        if (!value) return E_POINTER;
        *value =
            (::GetKeyState(static_cast<int>(key)) & 0x8000) ? CoreVirtualKeyStates_Down : CoreVirtualKeyStates_None;
        return S_OK;
    }
};
static ComPtr<CoreWindow> coreWindow;
HRESULT MakeCoreWindow(ICoreWindow** value) {
    if (!value) return E_POINTER;
    if (!coreWindow) coreWindow = Make<CoreWindow>();
    return coreWindow ? coreWindow.CopyTo(value) : E_OUTOFMEMORY;
}
void CoreHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (coreWindow) coreWindow->OnMessage(message, wparam, lparam);
}
}
