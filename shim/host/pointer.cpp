#include "pointer.h"
#include "host.h"
#include "generated/pointer.h"
#include "generated/pointer_args.h"
#include <windowsx.h>
#include <wrl.h>
#include <map>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI;
using ABI::Windows::Foundation::Point;
namespace ctr2 {
class MousePointer final : public RuntimeClass<RuntimeClassFlags<WinRt>, Xaml::Input::IPointer> {
    InspectableClass(L"Windows.UI.Xaml.Input.Pointer", BaseTrust);
    boolean contact_;

public:
    explicit MousePointer(boolean contact) : contact_(contact) {}
    HRESULT STDMETHODCALLTYPE get_PointerId(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(ABI::Windows::Devices::Input::PointerDeviceType* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Devices::Input::PointerDeviceType_Mouse;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInContact(boolean* value) override {
        if (!value) return E_POINTER;
        *value = contact_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInRange(boolean* value) override {
        if (!value) return E_POINTER;
        *value = true;
        return S_OK;
    }
};
class MouseProperties final : public RuntimeClass<RuntimeClassFlags<WinRt>, Input::DefaultIPointerPointProperties> {
    InspectableClass(L"Windows.UI.Input.PointerPointProperties", BaseTrust);
    WPARAM buttons_;
    UINT message_;
    Point point_;

public:
    MouseProperties(WPARAM buttons, UINT message, Point point) : buttons_(buttons), message_(message), point_(point) {}
#define BOOL_PROPERTY(Name, expression)                                                                                \
    HRESULT STDMETHODCALLTYPE get_##Name(boolean* value) override {                                                    \
        if (!value) return E_POINTER;                                                                                  \
        *value = (expression);                                                                                         \
        return S_OK;                                                                                                   \
    }
    BOOL_PROPERTY(IsLeftButtonPressed, (buttons_ & MK_LBUTTON) != 0)
    BOOL_PROPERTY(IsRightButtonPressed, (buttons_ & MK_RBUTTON) != 0)
    BOOL_PROPERTY(IsMiddleButtonPressed, (buttons_ & MK_MBUTTON) != 0)
    BOOL_PROPERTY(IsXButton1Pressed, (buttons_ & MK_XBUTTON1) != 0)
    BOOL_PROPERTY(IsXButton2Pressed, (buttons_ & MK_XBUTTON2) != 0)
    BOOL_PROPERTY(IsPrimary, true)
    BOOL_PROPERTY(IsInRange, true)
    BOOL_PROPERTY(TouchConfidence, true)
    BOOL_PROPERTY(IsCanceled, message_ == WM_CAPTURECHANGED)
    BOOL_PROPERTY(IsInverted, false)
    BOOL_PROPERTY(IsEraser, false)
    BOOL_PROPERTY(IsBarrelButtonPressed, false)
    BOOL_PROPERTY(IsHorizontalMouseWheel, message_ == WM_MOUSEHWHEEL)
#undef BOOL_PROPERTY
#define FLOAT_PROPERTY(Name, expression)                                                                               \
    HRESULT STDMETHODCALLTYPE get_##Name(FLOAT* value) override {                                                      \
        if (!value) return E_POINTER;                                                                                  \
        *value = (expression);                                                                                         \
        return S_OK;                                                                                                   \
    }
    FLOAT_PROPERTY(Pressure, (buttons_ & MK_LBUTTON) ? 0.5f : 0.0f)
    FLOAT_PROPERTY(Orientation, 0)
    FLOAT_PROPERTY(XTilt, 0)
    FLOAT_PROPERTY(YTilt, 0)
    FLOAT_PROPERTY(Twist, 0)
#undef FLOAT_PROPERTY
    HRESULT STDMETHODCALLTYPE get_MouseWheelDelta(INT32* value) override {
        if (!value) return E_POINTER;
        *value = (message_ == WM_MOUSEWHEEL || message_ == WM_MOUSEHWHEEL) ? GET_WHEEL_DELTA_WPARAM(buttons_) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContactRect(ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        *value = {point_.X, point_.Y, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContactRectRaw(ABI::Windows::Foundation::Rect* value) override {
        return get_ContactRect(value);
    }
    HRESULT STDMETHODCALLTYPE get_PointerUpdateKind(Input::PointerUpdateKind* value) override {
        if (!value) return E_POINTER;
        *value = message_ == WM_LBUTTONDOWN ? Input::PointerUpdateKind_LeftButtonPressed
                 : message_ == WM_LBUTTONUP ? Input::PointerUpdateKind_LeftButtonReleased
                                            : Input::PointerUpdateKind_Other;
        return S_OK;
    }
};
class MousePoint final : public RuntimeClass<RuntimeClassFlags<WinRt>, Input::DefaultIPointerPoint> {
    InspectableClass(L"Windows.UI.Input.PointerPoint", BaseTrust);
    Point position_;
    WPARAM buttons_;
    UINT frame_;
    UINT64 timestamp_;
    ComPtr<MouseProperties> properties_;

public:
    MousePoint(Point position, WPARAM buttons, UINT message)
        : position_(position), buttons_(buttons), frame_(GetTickCount()), timestamp_(GetTickCount64() * 1000),
          properties_(Make<MouseProperties>(buttons, message, position)) {}
    HRESULT STDMETHODCALLTYPE get_Position(Point* value) override {
        if (!value) return E_POINTER;
        *value = position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RawPosition(Point* value) override { return get_Position(value); }
    HRESULT STDMETHODCALLTYPE get_PointerId(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FrameId(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = frame_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Timestamp(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = timestamp_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInContact(boolean* value) override {
        if (!value) return E_POINTER;
        *value = (buttons_ & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Properties(Input::IPointerPointProperties** value) override {
        if (!value) return E_POINTER;
        return properties_.CopyTo(value);
    }
};
class MouseArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, Xaml::Input::DefaultIPointerRoutedEventArgs,
                                            Xaml::IRoutedEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Input.PointerRoutedEventArgs", BaseTrust);
    ComPtr<IInspectable> source_;
    ComPtr<MousePointer> pointer_;
    ComPtr<MousePoint> point_;
    boolean handled_ = false;
    ABI::Windows::System::VirtualKeyModifiers modifiers_;

public:
    MouseArgs(IInspectable* source, Point position, WPARAM buttons, UINT message)
        : source_(source), pointer_(Make<MousePointer>((buttons & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0)),
          point_(Make<MousePoint>(position, buttons, message)) {
        modifiers_ = static_cast<ABI::Windows::System::VirtualKeyModifiers>(
            ((buttons & MK_SHIFT) ? ABI::Windows::System::VirtualKeyModifiers_Shift : 0) |
            ((buttons & MK_CONTROL) ? ABI::Windows::System::VirtualKeyModifiers_Control : 0));
    }
    HRESULT STDMETHODCALLTYPE get_OriginalSource(IInspectable** value) override {
        if (!value) return E_POINTER;
        return source_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Pointer(Xaml::Input::IPointer** value) override {
        if (!value) return E_POINTER;
        return pointer_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_KeyModifiers(ABI::Windows::System::VirtualKeyModifiers* value) override {
        if (!value) return E_POINTER;
        *value = modifiers_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentPoint(Xaml::IUIElement*, Input::IPointerPoint** value) override {
        if (!value) return E_POINTER;
        return point_.CopyTo(value);
    }
};
// NOLINTNEXTLINE(bugprone-throwing-static-initialization): allocation failure at DLL load is fatal anyway.
static std::map<IInspectable*, PointerCallback> targets;
static IInspectable* captureOwner;
static IInspectable* activeTarget;
IInspectable* CurrentPointerTarget() noexcept { return activeTarget; }
void SetPointerTarget(IInspectable* target) {
    ComPtr<Xaml::IFrameworkElement> element;
    activeTarget = target && SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&element))) ? element.Get() : nullptr;
}
void ClearPointerTarget(IInspectable* target) {
    ComPtr<Xaml::IFrameworkElement> element;
    if (target && SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&element))) && activeTarget == element.Get())
        activeTarget = nullptr;
}
static POINT lastPosition{};
static WPARAM lastButtons{};
HRESULT CaptureElementPointer(IInspectable* target, Xaml::Input::IPointer* pointer, boolean* result) {
    if (!result) return E_POINTER;
    *result = false;
    if (!pointer) return E_INVALIDARG;
    UINT32 id{};
    auto hr = pointer->get_PointerId(&id);
    if (FAILED(hr)) return hr;
    if (id != 1 || !HostWindow()) return S_OK;
    SetCapture(HostWindow());
    if (GetCapture() == HostWindow()) {
        captureOwner = target;
        *result = true;
    }
    return S_OK;
}
HRESULT ReleaseElementPointer(IInspectable* target) {
    if (captureOwner == target) {
        ReleaseCapture();
        captureOwner = nullptr;
    }
    return S_OK;
}
void RegisterPointerTarget(IInspectable* target, PointerCallback callback) { targets[target] = std::move(callback); }
void UnregisterPointerTarget(IInspectable* target) noexcept {
    if (captureOwner == target) {
        captureOwner = nullptr;
        ReleaseCapture();
    }
    if (activeTarget == target) activeTarget = nullptr;
    targets.erase(target);
}
void PointerHostMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message != WM_LBUTTONDOWN && message != WM_LBUTTONUP && message != WM_MOUSEMOVE && message != WM_MOUSEWHEEL &&
        message != WM_MOUSEHWHEEL && message != WM_CAPTURECHANGED)
        return;
    POINT position{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    IInspectable* lostOwner = nullptr;
    if (message == WM_CAPTURECHANGED) {
        lostOwner = captureOwner;
        captureOwner = nullptr;
        position = lastPosition;
        wparam = lastButtons;
    }
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) ScreenToClient(HostWindow(), &position);
    if (message != WM_CAPTURECHANGED) {
        lastPosition = position;
        lastButtons = wparam;
    }
    // Retain the controlling COM identities while handlers can mutate the element tree.
    std::vector<std::pair<ComPtr<IInspectable>, PointerCallback>> snapshot;
    for (auto& item : targets) {
        if (message == WM_CAPTURECHANGED && item.first != lostOwner) continue;
        if (message != WM_CAPTURECHANGED && item.first != (captureOwner ? captureOwner : activeTarget)) continue;
        snapshot.emplace_back(ComPtr<IInspectable>(item.first), item.second);
    }
    for (auto& item : snapshot) {
        auto args = Make<MouseArgs>(
            item.first.Get(), Point{static_cast<float>(position.x), static_cast<float>(position.y)}, wparam, message);
        if (args) item.second(message, args.Get());
    }
}
}
