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
static HWND TestWindow() {
    HWND result = nullptr;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM context) -> BOOL {
            wchar_t name[80]{};
            GetClassNameW(window, name, ARRAYSIZE(name));
            if (wcscmp(name, L"CTR2ShimWindow") != 0) return TRUE;
            *reinterpret_cast<HWND*>(context) = window;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 1;
    const std::wstring action(argv[1]), mode(argv[2]);
    const bool full = mode == L"full" || mode == L"maxfull";
    const bool maximized = mode == L"max" || mode == L"maxfull";
    wchar_t root[32768]{};
    if (!GetEnvironmentVariableW(L"CTR2SHIM_DATA", root, ARRAYSIZE(root))) return 1;
    auto local = std::filesystem::path(root) / L"Local";
    std::filesystem::create_directories(local);
    auto file = local / L"window.ini";
    if (action == L"invalid") {
        const wchar_t* record = mode == L"offscreen"      ? L"50000,50000,640,480,0"
                                : mode == L"overflow"     ? L"2147483647,0,2147483647,480,1"
                                : mode == L"legacy"       ? L"1,80,90,720,570,0,0"
                                : mode == L"invalidstate" ? L"80,90,640,480,3"
                                : mode == L"invalidsize"  ? L"80,90,0,480,0"
                                                          : L"80,90,640,480,0,trailing";
        CHECK(WritePrivateProfileStringW(L"window", L"Placement", record, file.c_str()));
    }
    if (action == L"manual") {
        wchar_t record[80]{};
        swprintf_s(record, L"80,90,640,480,%d", full ? 2 : maximized ? 1 : 0);
        CHECK(WritePrivateProfileStringW(L"window", L"Placement", record, file.c_str()));
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
    auto callback =
        Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) -> HRESULT {
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
                placement.rcNormalPosition = {80, 90, 720, 570};
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
                CHECK(placement.rcNormalPosition.right - placement.rcNormalPosition.left == 640);
                CHECK(placement.rcNormalPosition.bottom - placement.rcNormalPosition.top == 480);
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
                if (mode == L"offscreen") {
                    RECT rect{}, intersection{};
                    GetWindowRect(host, &rect);
                    MONITORINFO monitor{sizeof(MONITORINFO)};
                    CHECK(GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor));
                    CHECK(IntersectRect(&intersection, &rect, &monitor.rcWork));
                } else {
                    RECT client{};
                    GetClientRect(host, &client);
                    CHECK(client.right == 1024 && client.bottom == 768);
                }
            }
            PostQuitMessage(0);
            return S_OK;
        });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(std::filesystem::exists(file));
    wchar_t record[256]{};
    GetPrivateProfileStringW(L"window", L"Placement", L"", record, ARRAYSIZE(record), file.c_str());
    LONG x = 0, y = 0, width = 0, height = 0;
    int state = 0;
    wchar_t extra{};
    CHECK(swscanf_s(record, L"%ld,%ld,%ld,%ld,%d %lc", &x, &y, &width, &height, &state, &extra, 1u) == 5);
    CHECK(width > 0 && height > 0 && state >= 0 && state <= 2);
    if (action == L"write") {
        CHECK(x == 80 && y == 90 && width == 640 && height == 480);
        CHECK(state == (full ? 2 : maximized ? 1 : 0));
    }
    printf("windowtest: %ls %ls; %d failure(s)\n", action.c_str(), mode.c_str(), failures);
    return failures ? 1 : 0;
}
