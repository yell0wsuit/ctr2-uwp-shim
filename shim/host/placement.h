#pragma once
#include <windows.h>
#include <atomic>
#include <string>
namespace ctr2 {
// UI-thread window placement; the fullscreen query is also used by WinRT callers.
class WindowPlacement {
    HWND window_ = nullptr;
    std::wstring path_;
    WINDOWPLACEMENT normal_{sizeof(WINDOWPLACEMENT)};
    std::atomic<bool> fullscreen_{false};
    LONG_PTR style_ = 0;
    bool changing_ = false, activated_ = false;
    void Save();
    void FitMonitor();
    SIZE FrameSize() const;

public:
    void Initialize(HWND window);
    void Activate();
    bool IsFullscreen() const noexcept { return fullscreen_; }
    HRESULT SetFullscreen(bool value);
    void OnMessage(UINT message, WPARAM wparam);
};
}
