#include "placement.h"
#include "registry.h"
#include "log.h"
#include <windows.storage.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <filesystem>
#include <algorithm>
#include <cstdio>
using namespace Microsoft::WRL;
namespace ctr2 {
namespace {
constexpr UINT_PTR SaveTimer = 0x43545232;
std::wstring SettingsPath() {
    wchar_t disabled[8]{};
    if (GetEnvironmentVariableW(L"CTR2SHIM_WINDOW_STATE", disabled, ARRAYSIZE(disabled)) &&
        _wcsicmp(disabled, L"off") == 0)
        return {};
    ComPtr<IActivationFactory> factory;
    ComPtr<ABI::Windows::Storage::IApplicationDataStatics> statics;
    ComPtr<ABI::Windows::Storage::IApplicationData> data;
    ComPtr<ABI::Windows::Storage::IStorageFolder> folder;
    ComPtr<ABI::Windows::Storage::IStorageItem> item;
    Wrappers::HString path;
    if (FAILED(MakeStorageFactory(&factory)) || FAILED(factory.As(&statics)) || FAILED(statics->get_Current(&data)) ||
        FAILED(data->get_LocalFolder(&folder)) || FAILED(folder.As(&item)) ||
        FAILED(item->get_Path(path.GetAddressOf())))
        return {};
    return (std::filesystem::path(path.GetRawBuffer(nullptr)) / L"window.ini").wstring();
}
void Normalize(WINDOWPLACEMENT& placement) {
    const bool maximized = placement.showCmd == SW_SHOWMAXIMIZED ||
                           ((placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE) &&
                            (placement.flags & WPF_RESTORETOMAXIMIZED));
    placement.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    placement.flags = 0;
}
}
void WindowPlacement::Initialize(HWND window) {
    window_ = window;
    path_.clear();
    fullscreen_ = false;
    changing_ = true;
    activated_ = false;
    style_ = GetWindowLongPtrW(window_, GWL_STYLE);
    normal_ = {sizeof(WINDOWPLACEMENT)};
    GetWindowPlacement(window_, &normal_);
    Normalize(normal_);
    try {
        path_ = SettingsPath();
    } catch (...) {
        Log("FAIL", "cannot resolve window settings path");
    }
    bool restoreFullscreen = false;
    if (!path_.empty()) {
        wchar_t value[256]{};
        GetPrivateProfileStringW(L"window", L"Placement", L"", value, ARRAYSIZE(value), path_.c_str());
        RECT rect{};
        int version = 0, maximized = 0, full = 0;
        wchar_t extra{};
        if (swscanf_s(value, L"%d,%ld,%ld,%ld,%ld,%d,%d %lc", &version, &rect.left, &rect.top, &rect.right,
                      &rect.bottom, &maximized, &full, &extra, 1u) == 7 &&
            version == 1 && (maximized == 0 || maximized == 1) && (full == 0 || full == 1) && rect.left >= -100000 &&
            rect.top >= -100000 && rect.right <= 100000 && rect.bottom <= 100000 &&
            static_cast<LONGLONG>(rect.right) - rect.left > 0 && static_cast<LONGLONG>(rect.bottom) - rect.top > 0) {
            normal_.rcNormalPosition = rect;
            normal_.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            restoreFullscreen = full != 0;
        }
    }
    FitMonitor();
    auto hidden = normal_;
    hidden.showCmd = SW_HIDE;
    SetWindowPlacement(window_, &hidden);
    changing_ = false;
    if (restoreFullscreen) SetFullscreen(true);
}
void WindowPlacement::FitMonitor() {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromRect(&normal_.rcNormalPosition, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    auto& rect = normal_.rcNormalPosition;
    rect.right = rect.left + std::min(rect.right - rect.left, monitor.rcWork.right - monitor.rcWork.left);
    rect.bottom = rect.top + std::min(rect.bottom - rect.top, monitor.rcWork.bottom - monitor.rcWork.top);
    // SetWindowPlacement takes workspace coordinates and also moves windows
    // back onto the desktop when their saved monitor has been disconnected.
}
void WindowPlacement::Activate() {
    ShowWindow(window_, activated_    ? (IsIconic(window_) ? SW_RESTORE : SW_SHOW)
                        : fullscreen_ ? SW_SHOW
                                      : static_cast<int>(normal_.showCmd));
    activated_ = true;
}
HRESULT WindowPlacement::SetFullscreen(bool value) {
    if (!IsWindow(window_)) return RO_E_CLOSED;
    if (value == fullscreen_) return S_OK;
    changing_ = true;
    if (value) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) {
            changing_ = false;
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (activated_) {
            GetWindowPlacement(window_, &normal_);
            Normalize(normal_);
        }
        style_ = GetWindowLongPtrW(window_, GWL_STYLE) & ~(WS_MINIMIZE | WS_MAXIMIZE);
        if (IsZoomed(window_) || IsIconic(window_)) ShowWindow(window_, SW_RESTORE);
        fullscreen_ = true;
        SetWindowLongPtrW(window_, GWL_STYLE,
                          static_cast<LONG_PTR>((static_cast<DWORD>(style_) & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        fullscreen_ = false;
        SetWindowLongPtrW(window_, GWL_STYLE, style_);
        FitMonitor();
        auto restore = normal_;
        if (!activated_) restore.showCmd = SW_HIDE;
        SetWindowPlacement(window_, &restore);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    changing_ = false;
    if (activated_) Save();
    return S_OK;
}
void WindowPlacement::Save() {
    if (changing_ || !activated_ || path_.empty()) return;
    if (!fullscreen_) {
        if (!GetWindowPlacement(window_, &normal_)) return;
        Normalize(normal_);
    }
    wchar_t value[256]{};
    const auto& rect = normal_.rcNormalPosition;
    swprintf_s(value, L"1,%ld,%ld,%ld,%ld,%d,%d", rect.left, rect.top, rect.right, rect.bottom,
               normal_.showCmd == SW_SHOWMAXIMIZED ? 1 : 0, fullscreen_ ? 1 : 0);
    if (!WritePrivateProfileStringW(L"window", L"Placement", value, path_.c_str()))
        Log("FAIL", "saving window placement error=%lu", GetLastError());
}
void WindowPlacement::OnMessage(UINT message, WPARAM wparam) {
    if (!window_) return;
    if (message == WM_TIMER && wparam == SaveTimer) {
        KillTimer(window_, SaveTimer);
        Save();
    }
    if (message == WM_DESTROY) {
        KillTimer(window_, SaveTimer);
        Save();
        window_ = nullptr;
        fullscreen_ = false;
        activated_ = false;
    }
    if (!changing_ && activated_ && (message == WM_MOVE || message == WM_SIZE))
        SetTimer(window_, SaveTimer, 300, nullptr);
    if (message == WM_EXITSIZEMOVE) Save();
    if (message == WM_DISPLAYCHANGE && fullscreen_) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor))
            SetWindowPos(window_, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
}
