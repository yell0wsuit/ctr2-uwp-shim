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
static std::map<int, std::deque<std::function<void()>>, std::greater<int>> queue;
static constexpr UINT WorkMessage = WM_APP + 1;
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
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    PointerHostMessage(message, wparam, lparam);
    if (message == WM_ACTIVATE) {
        DWORD process = 0;
        auto foreground = GetForegroundWindow();
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
            CoreHostMessage(message, wparam, lparam);
            XamlHostMessage(message, wparam, lparam);
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
    RECT rect{0, 0, 1024, 768};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowExW(0, cls.lpszClassName, L"Cut the Rope 2", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, cls.hInstance, nullptr);
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
