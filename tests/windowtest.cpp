// Separate-process round trips for normal, minimized/maximized, and fullscreen
// placement. Fixtures use isolated save folders and never touch player settings.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <filesystem>
#include <cstdio>
#include <string>
#include "window_test_helpers.h"
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::ViewManagement;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 1;
    const std::wstring action(argv[1]), mode(argv[2]);
    const bool full = mode == L"full" || mode == L"maxfull";
    const bool clamped = mode == L"offscreen" || mode == L"above" || mode == L"bottomright" || mode == L"oversized" ||
                         mode == L"negative";
    const bool maximized = mode == L"max" || mode == L"maxfull";
    wchar_t root[32768]{};
    if (!GetEnvironmentVariableW(L"CTR2SHIM_DATA", root, ARRAYSIZE(root))) return 1;
    auto local = std::filesystem::path(root) / L"Local";
    std::filesystem::create_directories(local);
    auto file = local / L"window.ini";
    if (action == L"invalid" || action == L"manual") {
        // Start fresh so earlier runs cannot leave valid keys in invalid fixtures.
        std::filesystem::remove(file);
        if (mode == L"legacy") {
            CHECK(WritePrivateProfileStringW(L"window", L"Placement", L"80,90,640,480,0", file.c_str()));
        } else {
            wchar_t edgeX[32]{}, edgeY[32]{};
            swprintf_s(edgeX, L"%d", GetSystemMetrics(SM_CXSCREEN) - 20);
            swprintf_s(edgeY, L"%d", GetSystemMetrics(SM_CYSCREEN) - 20);
            CHECK(WritePrivateProfileStringW(L"window", L"X",
                                             mode == L"offscreen"     ? L"50000"
                                             : mode == L"bottomright" ? edgeX
                                             : mode == L"negative"    ? L"-50000"
                                                                      : L"80",
                                             file.c_str()));
            CHECK(WritePrivateProfileStringW(L"window", L"Y",
                                             mode == L"offscreen"     ? L"50000"
                                             : mode == L"bottomright" ? edgeY
                                             : mode == L"above"       ? L"-20"
                                             : mode == L"negative"    ? L"-50000"
                                                                      : L"90",
                                             file.c_str()));
            const wchar_t* width = mode == L"oversized"     ? L"50000"
                                   : mode == L"overflow"    ? L"2147483647"
                                   : mode == L"invalidsize" ? L"0"
                                   : mode == L"corrupt"     ? L"640,trailing"
                                                            : L"640";
            CHECK(WritePrivateProfileStringW(L"window", L"Width", width, file.c_str()));
            CHECK(WritePrivateProfileStringW(L"window", L"Height", mode == L"oversized" ? L"50000" : L"480",
                                             file.c_str()));
            CHECK(WritePrivateProfileStringW(L"window", L"State",
                                             mode == L"invalidstate"   ? L"bogus"
                                             : mode == L"numericstate" ? L"1"
                                             : full                    ? L"FullScreen"
                                             : maximized               ? L"MAXIMIZED"
                                                                       : L"Windowed",
                                             file.c_str()));
        }
    }
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory)));
    ComPtr<IApplicationStatics> statics;
    ComPtr<IApplicationFactory> compose;
    CHECK(SUCCEEDED(factory.As(&statics)));
    CHECK(SUCCEEDED(factory.As(&compose)));
    ComPtr<IApplication> app;
    auto callback = Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*)
                                                                     -> HRESULT {
        CHECK(SUCCEEDED(compose->CreateInstance(nullptr, nullptr, &app)));
        ComPtr<IActivationFactory> windowFactory, viewFactory;
        ComPtr<IWindowStatics> windows;
        ComPtr<IWindow> window;
        ComPtr<IApplicationViewStatics2> views;
        ComPtr<IApplicationView> view;
        ComPtr<IApplicationView3> view3;
        CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Window").Get(), &windowFactory)));
        CHECK(SUCCEEDED(windowFactory.As(&windows)));
        CHECK(SUCCEEDED(windows->get_Current(&window)));
        CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.ViewManagement.ApplicationView").Get(), &viewFactory)));
        CHECK(SUCCEEDED(viewFactory.As(&views)));
        CHECK(SUCCEEDED(views->GetForCurrentView(&view)));
        CHECK(SUCCEEDED(view.As(&view3)));
        auto* host = TestWindow();
        CHECK(host);
        CHECK(!IsWindowVisible(host)); // Restore must not show the host during construction.
        CHECK(SUCCEEDED(window->Activate()));
        boolean fullscreen = false;
        CHECK(SUCCEEDED(view3->get_IsFullScreenMode(&fullscreen)));
        if (action == L"write") {
            CHECK(SUCCEEDED(view3->ExitFullScreenMode()));
            ShowWindow(host, SW_RESTORE);
            WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
            GetWindowPlacement(host, &placement);
            RECT outer{0, 0, 640, 480};
            CHECK(AdjustWindowRectEx(&outer, static_cast<DWORD>(GetWindowLongPtrW(host, GWL_STYLE)), FALSE,
                                     static_cast<DWORD>(GetWindowLongPtrW(host, GWL_EXSTYLE))));
            placement.rcNormalPosition = {80, 90, 80 + outer.right - outer.left, 90 + outer.bottom - outer.top};
            placement.showCmd = SW_SHOWNORMAL;
            placement.flags = 0;
            CHECK(SetWindowPlacement(host, &placement));
            if (maximized) ShowWindow(host, SW_MAXIMIZE);
            if (full) {
                boolean success = false;
                CHECK(SUCCEEDED(view3->TryEnterFullScreenMode(&success)) && success);
                CHECK(SUCCEEDED(view3->get_IsFullScreenMode(&fullscreen)) && fullscreen);
                CHECK((GetWindowLongPtrW(host, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == 0);
            } else {
                // Minimized is transient; remember the previous normal/maximized state.
                ShowWindow(host, SW_MINIMIZE);
            }
        } else if (action == L"read" || action == L"manual") {
            CHECK(!IsIconic(host));
            CHECK(static_cast<bool>(fullscreen) == full);
            CHECK(SUCCEEDED(window->Activate())); // Re-activation preserves the restored state.
            if (full) {
                RECT rect{};
                GetWindowRect(host, &rect);
                MONITORINFO monitor{sizeof(MONITORINFO)};
                CHECK(GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor));
                CHECK(EqualRect(&rect, &monitor.rcMonitor));
                // F11 exits and returns to the pre-fullscreen placement.
                SendMessageW(host, WM_KEYDOWN, VK_F11, 0);
                CHECK(SUCCEEDED(view3->get_IsFullScreenMode(&fullscreen)) && !fullscreen);
            }
            // The single saved mode does not retain a pre-fullscreen
            // maximized mode across restarts; fullscreen exits to windowed.
            CHECK((IsZoomed(host) != FALSE) == (maximized && !full));
            WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
            CHECK(GetWindowPlacement(host, &placement));
            const bool wasMaximized = IsZoomed(host) != FALSE;
            ShowWindow(host, SW_RESTORE);
            RECT client{};
            CHECK(GetClientRect(host, &client));
            CHECK(client.right == 640 && client.bottom == 480);
            if (wasMaximized) ShowWindow(host, SW_MAXIMIZE);
            CHECK(placement.rcNormalPosition.left == 80 && placement.rcNormalPosition.top == 90);
            // Alt+Enter toggles both ways; autorepeat does not toggle again.
            SendMessageW(host, WM_SYSKEYDOWN, VK_RETURN, 1L << 29);
            CHECK(SUCCEEDED(view3->get_IsFullScreenMode(&fullscreen)) && fullscreen);
            SendMessageW(host, WM_SYSKEYDOWN, VK_RETURN, (1L << 29) | (1L << 30));
            CHECK(SUCCEEDED(view3->get_IsFullScreenMode(&fullscreen)) && fullscreen);
            SendMessageW(host, WM_SYSKEYDOWN, VK_RETURN, 1L << 29);
            CHECK(SUCCEEDED(view3->get_IsFullScreenMode(&fullscreen)) && !fullscreen);
            CHECK((IsZoomed(host) != FALSE) == (maximized && !full));
        } else {
            CHECK(!fullscreen && !IsIconic(host) && !IsZoomed(host));
            if (clamped) {
                RECT rect{}, client{};
                GetWindowRect(host, &rect);
                MONITORINFO monitor{sizeof(MONITORINFO)};
                CHECK(GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor));
                CHECK(rect.left >= monitor.rcWork.left && rect.top >= monitor.rcWork.top);
                CHECK(rect.right <= monitor.rcWork.right && rect.bottom <= monitor.rcWork.bottom);
                CHECK(GetClientRect(host, &client));
                WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
                CHECK(GetWindowPlacement(host, &placement));
                // Repairs must reach the INI on activation, without waiting for close.
                CHECK(static_cast<LONG>(GetPrivateProfileIntW(L"window", L"X", -1, file.c_str())) ==
                      placement.rcNormalPosition.left);
                CHECK(static_cast<LONG>(GetPrivateProfileIntW(L"window", L"Y", -1, file.c_str())) ==
                      placement.rcNormalPosition.top);
                CHECK(GetPrivateProfileIntW(L"window", L"Width", 0, file.c_str()) == static_cast<UINT>(client.right));
                CHECK(GetPrivateProfileIntW(L"window", L"Height", 0, file.c_str()) == static_cast<UINT>(client.bottom));
            } else {
                RECT client{};
                CHECK(GetClientRect(host, &client));
                SIZE expected{};
                CHECK(DefaultClientSize(host, expected));
                CHECK(client.right == expected.cx && client.bottom == expected.cy);
                CHECK(GetPrivateProfileIntW(L"window", L"Width", 0, file.c_str()) == static_cast<UINT>(client.right));
                CHECK(GetPrivateProfileIntW(L"window", L"Height", 0, file.c_str()) == static_cast<UINT>(client.bottom));
            }
        }
        // Every launch rewrites known settings to valid, canonical values,
        // including corrupt input, before the application closes.
        wchar_t savedState[64]{};
        GetPrivateProfileStringW(L"window", L"State", L"", savedState, ARRAYSIZE(savedState), file.c_str());
        CHECK(wcscmp(savedState, L"windowed") == 0 || wcscmp(savedState, L"maximized") == 0 ||
              wcscmp(savedState, L"fullscreen") == 0);
        if (action == L"invalid") CHECK(wcscmp(savedState, L"windowed") == 0);
        CHECK(GetPrivateProfileIntW(L"window", L"Width", 0, file.c_str()) > 0);
        CHECK(GetPrivateProfileIntW(L"window", L"Height", 0, file.c_str()) > 0);
        PostQuitMessage(0);
        return S_OK;
    });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(std::filesystem::exists(file));
    const auto read = [&](const wchar_t* key) {
        return static_cast<LONG>(GetPrivateProfileIntW(L"window", key, -1, file.c_str()));
    };
    const LONG x = read(L"X"), y = read(L"Y"), width = read(L"Width"), height = read(L"Height");
    wchar_t state[64]{};
    GetPrivateProfileStringW(L"window", L"State", L"", state, ARRAYSIZE(state), file.c_str());
    CHECK(width > 0 && height > 0);
    CHECK(wcscmp(state, L"windowed") == 0 || wcscmp(state, L"maximized") == 0 || wcscmp(state, L"fullscreen") == 0);
    if (action == L"write") {
        CHECK(x == 80 && y == 90 && width == 640 && height == 480);
        CHECK(wcscmp(state, full ? L"fullscreen" : maximized ? L"maximized" : L"windowed") == 0);
    }
    printf("windowtest: %ls %ls; %d failure(s)\n", action.c_str(), mode.c_str(), failures);
    return failures ? 1 : 0;
}
