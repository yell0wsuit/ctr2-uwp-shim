#pragma once
#include <windows.h>
#include <algorithm>

inline HWND TestWindow() {
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

inline bool DefaultClientSize(HWND window, SIZE& size) {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!window || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    RECT frame{};
    if (!AdjustWindowRectEx(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                            static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE))))
        return false;
    // The default game area must fit inside the desktop together with its borders.
    size.cx = std::min<LONG>(1024, monitor.rcWork.right - monitor.rcWork.left - (frame.right - frame.left));
    size.cy = std::min<LONG>(768, monitor.rcWork.bottom - monitor.rcWork.top - (frame.bottom - frame.top));
    return size.cx > 0 && size.cy > 0;
}
