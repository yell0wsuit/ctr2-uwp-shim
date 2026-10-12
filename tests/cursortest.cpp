// CoreWindow.PointerCursor shows the cursor the game asks for: a standard one,
// a custom one from the executable's resources, or none. The game sets custom
// cursors 101 and 102 from Game.exe.
#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.core.h>
#include <windows.ui.xaml.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
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
static HWND HostWindow() {
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
// The cursor Windows would show with the pointer over the game.
static HCURSOR Shown(HWND host) {
    SetCursor(LoadCursorW(nullptr, IDC_WAIT)); // Something none of the checks expect.
    SendMessageW(host, WM_SETCURSOR, reinterpret_cast<WPARAM>(host), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    return GetCursor();
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
    bool ran = false;
    auto callback = Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) {
        ran = true;
        CHECK(SUCCEEDED(compose->CreateInstance(nullptr, nullptr, &app)));
        ComPtr<IActivationFactory> windowFactory, cursorFactory;
        ComPtr<IWindowStatics> windows;
        ComPtr<IWindow> window;
        ComPtr<ICoreWindow> core;
        ComPtr<ICoreCursorFactory> cursors;
        HWND host = HostWindow();
        if (!host || FAILED(get(HStringReference(L"Windows.UI.Xaml.Window").Get(), &windowFactory)) ||
            FAILED(windowFactory.As(&windows)) || FAILED(windows->get_Current(&window)) ||
            FAILED(window->get_CoreWindow(&core)) ||
            FAILED(get(HStringReference(L"Windows.UI.Core.CoreCursor").Get(), &cursorFactory)) ||
            FAILED(cursorFactory.As(&cursors))) {
            CHECK(false);
            PostQuitMessage(0);
            return S_OK;
        }
        auto use = [&](CoreCursorType type, UINT32 id) {
            ComPtr<ICoreCursor> cursor;
            CHECK(SUCCEEDED(cursors->CreateCursor(type, id, &cursor)));
            CHECK(SUCCEEDED(core->put_PointerCursor(cursor.Get())));
            ComPtr<ICoreCursor> current;
            CHECK(SUCCEEDED(core->get_PointerCursor(&current)) && current == cursor);
            return Shown(host);
        };
        const HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);
        const HCURSOR custom = LoadCursorW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
        CHECK(custom != nullptr && custom != arrow);
        CHECK(Shown(host) == arrow); // Before the game picks one.
        CHECK(use(CoreCursorType_Hand, 0) == LoadCursorW(nullptr, IDC_HAND));
        CHECK(use(CoreCursorType_Custom, 101) == custom);
        CHECK(use(CoreCursorType_SizeWestEast, 0) == LoadCursorW(nullptr, IDC_SIZEWE));
        CHECK(use(CoreCursorType_Custom, 999) == arrow); // Not in the executable.
        CHECK(use(CoreCursorType_Arrow, 0) == arrow);
        // No cursor hides the pointer, as on the Store version.
        CHECK(SUCCEEDED(core->put_PointerCursor(nullptr)));
        CHECK(Shown(host) == nullptr);
        PostQuitMessage(0);
        return S_OK;
    });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(ran);
    printf("cursortest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
