#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <windows.ui.core.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.input.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <vector>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <atomic>
#include <cstdio>
#include "window_test_helpers.h"
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
static int failures = 0;
static thread_local int startupStorage = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
class Outer final : public IInspectable {
    std::atomic<ULONG> refs_{1};

public:
    ComPtr<IInspectable> inner;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(iid, out) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** ids) override {
        *count = 0;
        *ids = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        *name = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trust) override {
        *trust = BaseTrust;
        return S_OK;
    }
};
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    startupStorage = 17;
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    CHECK(dll);
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IApplicationFactory> compose;
    CHECK(SUCCEEDED(factory.As(&compose)));
    if (!compose) return 1;
    auto* outer = new Outer;
    ComPtr<IApplication> app;
    CHECK(SUCCEEDED(compose->CreateInstance(outer, &outer->inner, &app)));
    if (!app) return 1;
    ComPtr<IUnknown> identity, innerIdentity;
    CHECK(SUCCEEDED(app.As(&identity)));
    CHECK(identity.Get() == static_cast<IInspectable*>(outer));
    CHECK(SUCCEEDED(outer->inner.As(&innerIdentity)));
    CHECK(innerIdentity.Get() != identity.Get());
    ComPtr<IApplication> queried;
    CHECK(SUCCEEDED(outer->QueryInterface(IID_PPV_ARGS(&queried))));
    CHECK(queried.Get() == app.Get());
    queried.Reset();
    identity.Reset();
    innerIdentity.Reset();
    app.Reset();
    CHECK(outer->Release() == 0);
    ComPtr<IApplicationStatics> statics;
    CHECK(SUCCEEDED(factory.As(&statics)));
    if (!statics) return 1;
    int loadedCount = 0;
    int activated = 0;
    bool activating = false;
    int reentrantActivation = 0;
    ComPtr<ABI::Windows::UI::Core::ICoreWindow> activationWindow;
    EventRegistrationToken activationToken{};
    int invoked = 0;
    std::vector<int> order;
    DWORD startupThread = GetCurrentThreadId(), uiThread = 0;
    auto callback = Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams* params)
                                                                     -> HRESULT {
        CHECK(params != nullptr);
        ++invoked;
        uiThread = GetCurrentThreadId();
        CHECK(uiThread != startupThread);
        APTTYPE apartment{};
        APTTYPEQUALIFIER qualifier{};
        CHECK(SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) &&
              (apartment == APTTYPE_STA || apartment == APTTYPE_MAINSTA));
        CHECK(startupStorage == 0);
        startupStorage = 99;
        CHECK(SUCCEEDED(compose->CreateInstance(nullptr, nullptr, &app)));
        ComPtr<IApplication> current;
        CHECK(SUCCEEDED(statics->get_Current(&current)));
        CHECK(current.Get() == app.Get());
        ComPtr<IActivationFactory> windowFactory;
        CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Window").Get(), &windowFactory)));
        if (windowFactory) {
            ComPtr<IWindowStatics> windows;
            CHECK(SUCCEEDED(windowFactory.As(&windows)));
            if (windows) {
                ComPtr<IWindow> window;
                CHECK(SUCCEEDED(windows->get_Current(&window)));
                if (window) {
                    ABI::Windows::Foundation::Rect bounds{};
                    CHECK(SUCCEEDED(window->get_Bounds(&bounds)));
                    SIZE expected{};
                    CHECK(DefaultClientSize(TestWindow(), expected));
                    CHECK(bounds.Width == expected.cx && bounds.Height == expected.cy);
                    ComPtr<ABI::Windows::UI::Core::ICoreWindow> coreWindow;
                    CHECK(SUCCEEDED(window->get_CoreWindow(&coreWindow)));
                    if (coreWindow) {
                        auto activation = Callback<ABI::Windows::Foundation::ITypedEventHandler<
                            ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::WindowActivatedEventArgs*>>(
                            [&](ABI::Windows::UI::Core::ICoreWindow* sender,
                                ABI::Windows::UI::Core::IWindowActivatedEventArgs* args) -> HRESULT {
                                CHECK(sender != nullptr && args != nullptr);
                                if (activating) ++reentrantActivation;
                                ABI::Windows::UI::Core::CoreWindowActivationState state{};
                                CHECK(SUCCEEDED(args->get_WindowActivationState(&state)));
                                if (state != ABI::Windows::UI::Core::CoreWindowActivationState_Deactivated) ++activated;
                                return S_OK;
                            });
                        CHECK(SUCCEEDED(coreWindow->add_Activated(activation.Get(), &activationToken)));
                        activationWindow = coreWindow;
                        HWND host{};
                        EnumThreadWindows(
                            GetCurrentThreadId(),
                            [](HWND hwnd, LPARAM data) -> BOOL {
                                wchar_t name[64]{};
                                GetClassNameW(hwnd, name, 64);
                                if (wcscmp(name, L"CTR2ShimWindow") == 0) {
                                    *reinterpret_cast<HWND*>(data) = hwnd;
                                    return FALSE;
                                }
                                return TRUE;
                            },
                            reinterpret_cast<LPARAM>(&host));
                        CHECK(host != nullptr);
                        SendMessageW(host, WM_ACTIVATE, WA_ACTIVE, 0);
                        ComPtr<IActivationFactory> pageActivation;
                        CHECK(
                            SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &pageActivation)));
                        ComPtr<Controls::IPageFactory> pages;
                        pageActivation.As(&pages);
                        ComPtr<Controls::IPage> page;
                        pages->CreateInstance(nullptr, nullptr, &page);
                        ComPtr<IFrameworkElement> element;
                        page.As(&element);
                        ComPtr<IInspectable> panel;
                        element->FindName(HStringReference(L"swapChainPanel").Get(), &panel);
                        ComPtr<IUIElement> panelUi;
                        panel.As(&panelUi);
                        CHECK(SUCCEEDED(window->put_Content(panelUi.Get())));
                        ComPtr<IFrameworkElement> panelElement;
                        panel.As(&panelElement);
                        auto loaded = Callback<IRoutedEventHandler>([&](IInspectable*, IRoutedEventArgs*) -> HRESULT {
                            ++loadedCount;
                            CHECK(activated > 0);
                            return S_OK;
                        });
                        EventRegistrationToken loadedToken{};
                        CHECK(SUCCEEDED(panelElement->add_Loaded(loaded.Get(), &loadedToken)));
                        CHECK(loadedCount == 0);
                        ComPtr<Controls::IPage> inactivePage;
                        pages->CreateInstance(nullptr, nullptr, &inactivePage);
                        ComPtr<IFrameworkElement> inactiveElement;
                        inactivePage.As(&inactiveElement);
                        ComPtr<IInspectable> inactivePanel;
                        inactiveElement->FindName(HStringReference(L"swapChainPanel").Get(), &inactivePanel);
                        ComPtr<IUIElement> inactiveUi;
                        inactivePanel.As(&inactiveUi);
                        int inactiveCalls = 0;
                        auto inactiveHandler = Callback<Input::IPointerEventHandler>(
                            [&](IInspectable*, Input::IPointerRoutedEventArgs*) -> HRESULT {
                                ++inactiveCalls;
                                return S_OK;
                            });
                        EventRegistrationToken inactiveToken{};
                        inactiveUi->add_PointerPressed(inactiveHandler.Get(), &inactiveToken);
                        ComPtr<Input::IPointer> savedPointer;
                        std::vector<int> pointerEvents;
                        auto pointerHandler = Callback<Input::IPointerEventHandler>(
                            [&](IInspectable* sender, Input::IPointerRoutedEventArgs* args) -> HRESULT {
                                CHECK(sender != nullptr);
                                CHECK(GetCurrentThreadId() == uiThread);
                                ComPtr<Input::IPointer> pointer;
                                CHECK(SUCCEEDED(args->get_Pointer(&pointer)));
                                savedPointer = pointer;
                                UINT32 id{};
                                CHECK(SUCCEEDED(pointer->get_PointerId(&id)) && id == 1);
                                boolean contact{};
                                CHECK(SUCCEEDED(pointer->get_IsInContact(&contact)));
                                ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
                                CHECK(SUCCEEDED(args->GetCurrentPoint(panelUi.Get(), &point)));
                                ABI::Windows::Foundation::Point position{};
                                CHECK(SUCCEEDED(point->get_Position(&position)) && position.X == 37 &&
                                      position.Y == 59);
                                pointerEvents.push_back(contact ? 1 : 0);
                                if (pointerEvents.size() == 1) {
                                    boolean captured = false;
                                    CHECK(SUCCEEDED(panelUi->CapturePointer(pointer.Get(), &captured)) && captured);
                                }
                                if (!contact) CHECK(SUCCEEDED(panelUi->ReleasePointerCapture(pointer.Get())));
                                return S_OK;
                            });
                        EventRegistrationToken pressed{}, moved{}, released{};
                        CHECK(SUCCEEDED(panelUi->add_PointerPressed(pointerHandler.Get(), &pressed)));
                        CHECK(SUCCEEDED(panelUi->add_PointerMoved(pointerHandler.Get(), &moved)));
                        CHECK(SUCCEEDED(panelUi->add_PointerReleased(pointerHandler.Get(), &released)));
                        SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(37, 59));
                        SendMessageW(host, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(37, 59));
                        SendMessageW(host, WM_LBUTTONUP, 0, MAKELPARAM(37, 59));
                        CHECK((pointerEvents == std::vector<int>{1, 1, 0}));
                        CHECK(inactiveCalls == 0);
                        inactiveUi->remove_PointerPressed(inactiveToken);
                        int lostCapture = 0;
                        auto lostHandler = Callback<Input::IPointerEventHandler>(
                            [&](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT {
                                ++lostCapture;
                                ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
                                CHECK(SUCCEEDED(args->GetCurrentPoint(panelUi.Get(), &point)));
                                ABI::Windows::Foundation::Point position{};
                                CHECK(SUCCEEDED(point->get_Position(&position)) && position.X == 37 &&
                                      position.Y == 59);
                                return S_OK;
                            });
                        EventRegistrationToken lostToken{};
                        CHECK(SUCCEEDED(panelUi->add_PointerCaptureLost(lostHandler.Get(), &lostToken)));
                        boolean captured{};
                        CHECK(SUCCEEDED(panelUi->CapturePointer(savedPointer.Get(), &captured)) && captured);
                        CHECK(SUCCEEDED(inactiveUi->CapturePointer(savedPointer.Get(), &captured)) && captured);
                        CHECK(lostCapture == 1);
                        CHECK(SUCCEEDED(inactiveUi->ReleasePointerCaptures()));
                        CHECK(SUCCEEDED(panelUi->remove_PointerCaptureLost(lostToken)));
                        ComPtr<ISwapChainPanelNative> inactiveNative;
                        inactivePanel.As(&inactiveNative);
                        CHECK(SUCCEEDED(inactiveNative->SetSwapChain(nullptr)));
                        SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(37, 59));
                        CHECK(pointerEvents.size() == 4);
                        CHECK(SUCCEEDED(window->put_Content(panelUi.Get())));
                        CHECK(SUCCEEDED(window->put_Content(nullptr)));
                        const auto detachedCount = pointerEvents.size();
                        SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(37, 59));
                        CHECK(pointerEvents.size() == detachedCount);
                        CHECK(SUCCEEDED(window->put_Content(panelUi.Get())));
                        CHECK(SUCCEEDED(panelUi->remove_PointerPressed(pressed)));
                        CHECK(SUCCEEDED(panelUi->remove_PointerMoved(moved)));
                        CHECK(SUCCEEDED(panelUi->remove_PointerReleased(released)));
                        CHECK(activated == 0);
                        ComPtr<IActivationFactory> userActivation;
                        CHECK(SUCCEEDED(
                            get(HStringReference(L"Windows.UI.Xaml.Controls.UserControl").Get(), &userActivation)));
                        ComPtr<Controls::IUserControlFactory> userFactory;
                        userActivation.As(&userFactory);
                        ComPtr<Controls::IUserControl> user;
                        CHECK(SUCCEEDED(userFactory->CreateInstance(nullptr, nullptr, &user)));
                        CHECK(SUCCEEDED(user->put_Content(panelUi.Get())));
                        ComPtr<IUIElement> userUi;
                        user.As(&userUi);
                        CHECK(SUCCEEDED(window->put_Content(userUi.Get())));
                        activating = true;
                        CHECK(SUCCEEDED(window->Activate()));
                        activating = false;
                        CHECK(reentrantActivation == 0);
                    }
                    ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> dispatcher;
                    CHECK(SUCCEEDED(window->get_Dispatcher(&dispatcher)));
                    if (dispatcher) {
                        boolean access = false;
                        CHECK(SUCCEEDED(dispatcher->get_HasThreadAccess(&access)) && access);
                        for (int n = 0; n < 3; ++n) {
                            auto work = Callback<ABI::Windows::UI::Core::IDispatchedHandler>([&, n]() -> HRESULT {
                                CHECK(GetCurrentThreadId() == uiThread);
                                order.push_back(n);
                                if (order.size() == 3) PostQuitMessage(0);
                                return S_OK;
                            });
                            ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
                            CHECK(SUCCEEDED(
                                dispatcher->RunAsync(n == 1 ? ABI::Windows::UI::Core::CoreDispatcherPriority_High
                                                            : ABI::Windows::UI::Core::CoreDispatcherPriority_Normal,
                                                     work.Get(), &action)));
                        }
                    } else {
                        PostQuitMessage(0);
                    }
                }
            }
        }
        return S_OK;
    });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(invoked == 1);
    CHECK((order == std::vector<int>{1, 0, 2}));
    CHECK(activated > 0);
    CHECK(loadedCount == 1);
    CHECK(SUCCEEDED(activationWindow->remove_Activated(activationToken)));
    CHECK(startupStorage == 17);
    printf("xamltest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
