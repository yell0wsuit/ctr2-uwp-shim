#include "host.h"
#include "pointer.h"
#include "gpu/dxgi_redirect.h"
#include "log.h"
#include <mutex>
#include <map>
#include <deque>
namespace ctr2 {
void InitializeDiagnostics();
void CoreHostMessage(UINT message, WPARAM wparam, LPARAM lparam);
void XamlHostMessage(UINT message, WPARAM wparam, LPARAM lparam);
static HWND window;
static DWORD thread;
static std::mutex queueMutex;
// NOLINTNEXTLINE(bugprone-throwing-static-initialization): allocation failure at DLL load is fatal anyway.
static std::map<int, std::deque<std::function<void()>>, std::greater<int>> queue;
static constexpr UINT WorkMessage = WM_APP + 1;
static constexpr UINT CreateSurfaceMessage = WM_APP + 2;
static constexpr wchar_t SurfaceClass[] = L"CTR2ShimSurface";
// Composition swap chains are independent layers, and the game creates its
// next one (on resize) before releasing the old. A window holds one flip-model
// swap chain at a time, so each redirected swap chain gets its own child
// window; the newest covers the rest.
static LRESULT CALLBACK SurfaceProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT; // Pointer input belongs to the host window.
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
static HWND CreateSurfaceOnHostThread() {
    RECT client{};
    GetClientRect(window, &client);
    HWND surface = CreateWindowExW(0, SurfaceClass, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                   client.right, client.bottom, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (surface) SetWindowPos(surface, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return surface;
}
HWND CreateSurfaceWindow() noexcept {
    if (!window) return nullptr;
    // Windows belong to the thread that creates them; surfaces live on the UI thread.
    if (GetCurrentThreadId() == thread) return CreateSurfaceOnHostThread();
    return reinterpret_cast<HWND>(SendMessageW(window, CreateSurfaceMessage, 0, 0));
}
HRESULT PostHostWork(int priority, std::function<void()> work) {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (!window) return RO_E_CLOSED;
    queue[priority].push_back(std::move(work));
    if (!PostMessageW(window, WorkMessage, 0, 0)) {
        queue[priority].pop_back();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}
HWND HostWindow() noexcept { return window; }
DWORD HostThread() noexcept { return thread; }
// The game tears down its renderer on every Deactivated and rebuilds it, behind
// a "Loading" screen, on the next activation. That suited full-screen Windows
// 8.1, where losing focus meant switching away; a desktop window loses focus
// whenever another window is clicked. So the view counts as active while it
// can be seen, and Activated is raised only when that changes: on minimize,
// hide, restore, or show. Runs on the UI queue, after the message has settled.
static bool reportedActive = false;
static void UpdateActivation(WPARAM activation) {
    const bool active = window && IsWindowVisible(window) && !IsIconic(window);
    if (active == reportedActive) return;
    reportedActive = active;
    const WPARAM state = active ? (activation == WA_CLICKACTIVE ? WA_CLICKACTIVE : WA_ACTIVE) : WA_INACTIVE;
    Log("TRACE", "view activation %s", active ? "active" : "deactivated");
    CoreHostMessage(WM_ACTIVATE, state, 0);
    XamlHostMessage(WM_ACTIVATE, state, 0);
}
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PointerHostMessage(message, wparam, lparam);
    if (message == CreateSurfaceMessage) return reinterpret_cast<LRESULT>(CreateSurfaceOnHostThread());
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED) {
        // Surfaces fill the client area; DXGI stretches each buffer to its window.
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SetWindowPos(child, nullptr, 0, 0, LOWORD(lparam), HIWORD(lparam),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (message == WM_ACTIVATE) {
        DWORD process = 0;
        auto* foreground = GetForegroundWindow();
        GetWindowThreadProcessId(foreground, &process);
        wchar_t name[80]{};
        GetClassNameW(reinterpret_cast<HWND>(lparam), name, 80);
        Log("TRACE", "host activation state=%u other=%p class=%ls foreground=%p process=%lu", LOWORD(wparam),
            reinterpret_cast<HWND>(lparam), name, foreground, process);
    }
    if (message == WM_ACTIVATE || message == WM_SIZE || message == WM_SHOWWINDOW) {
        // XAML notifications belong to the UI queue. Delivering inside ShowWindow
        // reenters OnLaunched before startup has finished.
        PostHostWork(0, [message, wparam, lparam] {
            if (message != WM_ACTIVATE) {
                CoreHostMessage(message, wparam, lparam);
                XamlHostMessage(message, wparam, lparam);
            }
            UpdateActivation(message == WM_ACTIVATE ? LOWORD(wparam) : WA_ACTIVE);
        });
    }
    if (message == WorkMessage) {
        std::function<void()> work;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            auto first = queue.begin();
            while (first != queue.end() && first->second.empty()) first = queue.erase(first);
            if (first != queue.end()) {
                work = std::move(first->second.front());
                first->second.pop_front();
            }
        }
        if (work) work();
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
HRESULT RunHost(const std::function<HRESULT()>& initialize) {
    InitializeDiagnostics();
    if (window) return E_ILLEGAL_METHOD_CALL;
    thread = GetCurrentThreadId();
    WNDCLASSW cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"CTR2ShimWindow";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    WNDCLASSW surface{};
    surface.lpfnWndProc = SurfaceProc;
    surface.hInstance = cls.hInstance;
    surface.lpszClassName = SurfaceClass;
    if (!RegisterClassW(&surface) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32(GetLastError());
    RECT rect{0, 0, 1024, 768};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowExW(0, cls.lpszClassName, L"Cut the Rope 2", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                             nullptr, cls.hInstance, nullptr);
    if (!window) return HRESULT_FROM_WIN32(GetLastError());
    HRESULT hr = InitializeGraphicsRedirect();
    if (FAILED(hr)) Log("FAIL", "DXGI redirect initialization hr=%08lx", hr);
    if (SUCCEEDED(hr)) hr = initialize();
    if (SUCCEEDED(hr)) {
        MSG msg{};
        BOOL result;
        while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (result == -1) hr = HRESULT_FROM_WIN32(GetLastError());
    }
    if (IsWindow(window)) DestroyWindow(window);
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        window = nullptr;
        queue.clear();
    }
    thread = 0;
    // DestroyWindow posts WM_QUIT; don't poison a subsequent host run.
    MSG quit{};
    PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
    return hr;
}
}
