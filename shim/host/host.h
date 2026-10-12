#pragma once
#include <windows.h>
#include <functional>
namespace ctr2 {
HWND HostWindow() noexcept;
// A child window covering the host's client area, on top of earlier ones,
// for one redirected swap chain. Closing it (WM_CLOSE) destroys it.
HWND CreateSurfaceWindow() noexcept;
DWORD HostThread() noexcept;
void ActivateHostWindow();
bool IsHostFullscreen() noexcept;
HRESULT SetHostFullscreen(bool value);
HRESULT PostHostWork(int priority, std::function<void()> work);
HRESULT RunHost(const std::function<HRESULT()>& initialize);
}
