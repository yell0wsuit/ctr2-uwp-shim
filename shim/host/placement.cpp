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
void CreateSettingsFile(const std::wstring& path) {
    // Seed new files only. Updating Placement through the INI API preserves
    // these comments and any comments the player adds to an existing file.
    HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    constexpr char guide[] = "; ctr2shim window placement. Saved automatically between runs.\r\n"
                             "; Close the game before editing; later saves update Placement.\r\n"
                             "; Placement=X,Y,Width,Height,State\r\n"
                             "; State: 0=windowed, 1=maximized, 2=fullscreen.\r\n"
                             "; Width/Height are the outer window size, including title bar and borders.\r\n"
                             "; Examples at (80,90), windowed:\r\n"
                             ";   1280x720: Placement=80,90,1280,720,0\r\n"
                             ";   1366x768: Placement=80,90,1366,768,0\r\n"
                             ";   1600x900: Placement=80,90,1600,900,0\r\n"
                             "; Maximized/fullscreen use the monitor size; Width/Height are kept for exit.\r\n"
                             "; After restarting in fullscreen, exiting returns to windowed mode.\r\n"
                             "; Toggle fullscreen with F11 or Alt+Enter.\r\n"
                             "; Delete this file with the game closed to reset placement.\r\n\r\n";
    DWORD written{};
    if (!WriteFile(file, guide, sizeof(guide) - 1, &written, nullptr) || written != sizeof(guide) - 1)
        Log("FAIL", "writing window settings guide error=%lu", GetLastError());
    CloseHandle(file);
}
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
        LONGLONG x = 0, y = 0, width = 0, height = 0;
        LONGLONG state = 0;
        wchar_t extra{};
        if (swscanf_s(value, L"%lld,%lld,%lld,%lld,%lld %lc", &x, &y, &width, &height, &state, &extra, 1u) == 5 &&
            x >= -100000 && x <= 100000 && y >= -100000 && y <= 100000 && width > 0 && width <= 100000 && height > 0 &&
            height <= 100000 && x + width <= 100000 && y + height <= 100000 && state >= 0 && state <= 2) {
            normal_.rcNormalPosition = {
                static_cast<LONG>(x),
                static_cast<LONG>(y),
                static_cast<LONG>(x + width),
                static_cast<LONG>(y + height),
            };
            normal_.showCmd = state == 1 ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            restoreFullscreen = state == 2;
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
    swprintf_s(value, L"%ld,%ld,%ld,%ld,%d", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               fullscreen_                           ? 2
               : normal_.showCmd == SW_SHOWMAXIMIZED ? 1
                                                     : 0);
    CreateSettingsFile(path_);
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
