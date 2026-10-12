#pragma once
#include <windows.h>

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
    // The default is 1024x768 at 100% scale, scaled by the system DPI, then shrunk
    // keeping its shape to fit inside the desktop together with its borders.
    using SystemDpi = UINT(WINAPI*)();
    auto systemDpi =
        reinterpret_cast<SystemDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    const int dpi = systemDpi ? static_cast<int>(systemDpi()) : 96;
    LONG width = MulDiv(1024, dpi, 96), height = MulDiv(768, dpi, 96);
    const LONG roomX = monitor.rcWork.right - monitor.rcWork.left - (frame.right - frame.left);
    const LONG roomY = monitor.rcWork.bottom - monitor.rcWork.top - (frame.bottom - frame.top);
    if (roomX > 0 && roomY > 0 && (width > roomX || height > roomY)) {
        const double fit = width * roomY > height * roomX ? double(roomX) / width : double(roomY) / height;
        width = static_cast<LONG>(width * fit);
        height = static_cast<LONG>(height * fit);
    }
    size.cx = width;
    size.cy = height;
    return size.cx > 0 && size.cy > 0;
}
