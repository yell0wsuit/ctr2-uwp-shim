// The game tears down its renderer on Activated(Deactivated) and reloads on the
// next activation. Focus moving to another window must not deactivate a view
// that is still visible; minimizing and restoring must.
#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.core.h>
#include <windows.ui.xaml.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <functional>
#include <vector>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::UI::Xaml;
static int failures = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
static std::vector<int> states; // 1 = active, 0 = deactivated, for CoreWindow
static std::vector<int> xamlStates;
static std::vector<std::pair<float, float>> sizes; // CoreWindow SizeChanged
static ComPtr<ICoreDispatcher> dispatcher;
static HWND host;
// Runs `step` after the UI queue has drained the work posted before it.
static void Then(const std::function<void()>& step) {
    auto work = Callback<IDispatchedHandler>([step]() -> HRESULT {
        step();
        return S_OK;
    });
    ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
    CHECK(SUCCEEDED(dispatcher->RunAsync(CoreDispatcherPriority_Normal, work.Get(), &action)));
}
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    CHECK(dll);
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    ComPtr<IApplicationStatics> statics;
    ComPtr<IApplicationFactory> compose;
    ComPtr<IApplication> app;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory)));
    if (!factory || FAILED(factory.As(&statics)) || FAILED(factory.As(&compose))) return 1;
    auto callback = Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) {
        CHECK(SUCCEEDED(compose->CreateInstance(nullptr, nullptr, &app)));
        ComPtr<IActivationFactory> windowFactory;
        ComPtr<IWindowStatics> windows;
        ComPtr<IWindow> window;
        ComPtr<ICoreWindow> core;
        if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Window").Get(), &windowFactory)) ||
            FAILED(windowFactory.As(&windows)) || FAILED(windows->get_Current(&window)) ||
            FAILED(window->get_CoreWindow(&core)) || FAILED(window->get_Dispatcher(&dispatcher))) {
            CHECK(false);
            PostQuitMessage(0);
            return S_OK;
        }
        EventRegistrationToken token{};
        auto activated = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, WindowActivatedEventArgs*>>(
            [](ICoreWindow*, IWindowActivatedEventArgs* args) -> HRESULT {
                CoreWindowActivationState state{};
                args->get_WindowActivationState(&state);
                states.push_back(state == CoreWindowActivationState_Deactivated ? 0 : 1);
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_Activated(activated.Get(), &token)));
        auto resized = Callback<ABI::Windows::Foundation::ITypedEventHandler<CoreWindow*, WindowSizeChangedEventArgs*>>(
            [](ICoreWindow*, IWindowSizeChangedEventArgs* args) -> HRESULT {
                ABI::Windows::Foundation::Size size{};
                args->get_Size(&size);
                sizes.emplace_back(size.Width, size.Height);
                return S_OK;
            });
        CHECK(SUCCEEDED(core->add_SizeChanged(resized.Get(), &token)));
        auto xamlActivated =
            Callback<IWindowActivatedEventHandler>([](IInspectable*, IWindowActivatedEventArgs* args) -> HRESULT {
                CoreWindowActivationState state{};
                args->get_WindowActivationState(&state);
                xamlStates.push_back(state == CoreWindowActivationState_Deactivated ? 0 : 1);
                return S_OK;
            });
        CHECK(SUCCEEDED(window->add_Activated(xamlActivated.Get(), &token)));
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND hwnd, LPARAM) -> BOOL {
                wchar_t name[64]{};
                GetClassNameW(hwnd, name, 64);
                if (wcscmp(name, L"CTR2ShimWindow") != 0) return TRUE;
                host = hwnd;
                return FALSE;
            },
            0);
        CHECK(host != nullptr);
        CHECK(SUCCEEDED(window->Activate())); // Shows the window.
        Then([] {
            CHECK((states == std::vector<int>{1}));
            // Focus moves to another window; this one stays visible.
            SendMessageW(host, WM_ACTIVATE, WA_INACTIVE, 0);
            Then([] {
                CHECK((states == std::vector<int>{1}));
                SendMessageW(host, WM_ACTIVATE, WA_CLICKACTIVE, 0); // Back again: no second activation.
                ShowWindow(host, SW_MINIMIZE);
                Then([] {
                    CHECK((states == std::vector<int>{1, 0}));
                    ShowWindow(host, SW_RESTORE);
                    Then([] {
                        CHECK((states == std::vector<int>{1, 0, 1}));
                        CHECK(xamlStates == states);
                        // Dragging the frame: the game rebuilds its renderer (and shows
                        // Loading) on every size change, so it hears only the final size.
                        sizes.clear();
                        SendMessageW(host, WM_ENTERSIZEMOVE, 0, 0);
                        for (int width : {700, 800, 900})
                            SetWindowPos(host, nullptr, 0, 0, width, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                        Then([] {
                            CHECK(sizes.empty());
                            SendMessageW(host, WM_EXITSIZEMOVE, 0, 0);
                            Then([] {
                                RECT client{};
                                GetClientRect(host, &client);
                                CHECK(sizes.size() == 1);
                                CHECK(!sizes.empty() && sizes.back().first == static_cast<float>(client.right) &&
                                      sizes.back().second == static_cast<float>(client.bottom));
                                // A move without resizing changes nothing.
                                sizes.clear();
                                SendMessageW(host, WM_ENTERSIZEMOVE, 0, 0);
                                SendMessageW(host, WM_EXITSIZEMOVE, 0, 0);
                                Then([] {
                                    CHECK(sizes.empty());
                                    PostQuitMessage(0);
                                });
                            });
                        });
                    });
                });
            });
        });
        return S_OK;
    });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(states.size() == 3);
    printf("focustest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
