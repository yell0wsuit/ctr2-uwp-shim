#pragma once
#include <windows.h>
#include <functional>
namespace ctr2 {
HWND HostWindow() noexcept;
DWORD HostThread() noexcept;
HRESULT PostHostWork(int priority, std::function<void()> work);
HRESULT RunHost(const std::function<HRESULT()>& initialize);
}
