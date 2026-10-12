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
    // Seed new files only; INI updates preserve these comments.
    HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    constexpr char guide[] = "; ctr2shim window settings. Saved automatically between runs.\r\n"
                             "; Close the game before editing.\r\n"
                             "; Width/Height specify the game area; borders are added automatically.\r\n"
                             "; Examples: Width=1280 Height=720; Width=1366 Height=768; Width=1600 Height=900.\r\n"
                             "; X/Y specify window position in workspace coordinates.\r\n"
                             "; Unsafe positions/sizes are clamped to the monitor work area and saved back.\r\n"
                             "; State: windowed, maximized, or fullscreen.\r\n"
                             "; Maximized/fullscreen retain Width/Height for returning to a normal window.\r\n"
                             "; After restarting in fullscreen, exiting returns to windowed mode.\r\n"
                             "; Invalid settings reset to safe defaults and are overwritten on opening.\r\n"
                             "; Toggle fullscreen with F11 or Alt+Enter.\r\n"
                             "; Delete this file with the game closed to reset settings.\r\n\r\n";
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
        const auto read = [&](const wchar_t* key, LONGLONG fallback, LONGLONG& result) {
            wchar_t value[64]{}, defaultValue[64]{};
            swprintf_s(defaultValue, L"%lld", fallback);
            GetPrivateProfileStringW(L"window", key, defaultValue, value, ARRAYSIZE(value), path_.c_str());
            wchar_t extra{};
            return swscanf_s(value, L"%lld %lc", &result, &extra, 1u) == 1;
        };
        LONGLONG x = 0, y = 0, width = 0, height = 0;
        wchar_t state[64]{};
        GetPrivateProfileStringW(L"window", L"State", L"windowed", state, ARRAYSIZE(state), path_.c_str());
        const bool maximized = _wcsicmp(state, L"maximized") == 0;
        const bool fullscreen = _wcsicmp(state, L"fullscreen") == 0;
        const bool validState = maximized || fullscreen || _wcsicmp(state, L"windowed") == 0;
        const auto frame = FrameSize();
        const auto& rect = normal_.rcNormalPosition;
        if (read(L"X", rect.left, x) && read(L"Y", rect.top, y) &&
            read(L"Width", rect.right - rect.left - frame.cx, width) &&
            read(L"Height", rect.bottom - rect.top - frame.cy, height) && validState && x >= -100000 && x <= 100000 &&
            y >= -100000 && y <= 100000 && width > 0 && width <= 100000 && height > 0 && height <= 100000 &&
            x + width + frame.cx <= 100000 && y + height + frame.cy <= 100000) {
            normal_.rcNormalPosition = {
                static_cast<LONG>(x),
                static_cast<LONG>(y),
                static_cast<LONG>(x + width + frame.cx),
                static_cast<LONG>(y + height + frame.cy),
            };
            normal_.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            restoreFullscreen = fullscreen;
        }
    }
    FitMonitor();
    auto hidden = normal_;
    hidden.showCmd = SW_HIDE;
    SetWindowPlacement(window_, &hidden);
    changing_ = false;
    if (restoreFullscreen) SetFullscreen(true);
}
SIZE WindowPlacement::FrameSize() const {
    // Use the decorated, restored style even while maximized or fullscreen.
    RECT frame{};
    AdjustWindowRectEx(&frame, static_cast<DWORD>(style_) & ~(WS_MINIMIZE | WS_MAXIMIZE), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)));
    return {frame.right - frame.left, frame.bottom - frame.top};
}
void WindowPlacement::FitMonitor() {
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromRect(&normal_.rcNormalPosition, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    // WINDOWPLACEMENT uses workspace coordinates, while rcWork is in screen
    // coordinates. Account for taskbars on the top or left of this monitor.
    RECT work = monitor.rcWork;
    OffsetRect(&work, monitor.rcMonitor.left - monitor.rcWork.left, monitor.rcMonitor.top - monitor.rcWork.top);
    auto& rect = normal_.rcNormalPosition;
    const LONG width = std::min(rect.right - rect.left, work.right - work.left);
    const LONG height = std::min(rect.bottom - rect.top, work.bottom - work.top);
    rect.left = std::clamp(rect.left, work.left, work.right - width);
    rect.top = std::clamp(rect.top, work.top, work.bottom - height);
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
}
void WindowPlacement::Activate() {
    ShowWindow(window_, activated_    ? (IsIconic(window_) ? SW_RESTORE : SW_SHOW)
                        : fullscreen_ ? SW_SHOW
                                      : static_cast<int>(normal_.showCmd));
    activated_ = true;
    Save(); // Persist corrected settings immediately, even before the first move.
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
    const RECT previous = normal_.rcNormalPosition;
    FitMonitor();
    if (!fullscreen_ && !IsIconic(window_) && !EqualRect(&previous, &normal_.rcNormalPosition)) {
        changing_ = true;
        SetWindowPlacement(window_, &normal_);
        changing_ = false;
    }
    const auto& rect = normal_.rcNormalPosition;
    const auto frame = FrameSize();
    const wchar_t* state = fullscreen_                           ? L"fullscreen"
                           : normal_.showCmd == SW_SHOWMAXIMIZED ? L"maximized"
                                                                 : L"windowed";
    const LONG values[] = {
        rect.left,
        rect.top,
        rect.right - rect.left - frame.cx,
        rect.bottom - rect.top - frame.cy,
    };
    const wchar_t* keys[] = {L"X", L"Y", L"Width", L"Height"};
    CreateSettingsFile(path_);
    for (size_t i = 0; i < ARRAYSIZE(values); ++i) {
        wchar_t value[64]{};
        swprintf_s(value, L"%ld", values[i]);
        if (!WritePrivateProfileStringW(L"window", keys[i], value, path_.c_str()))
            Log("FAIL", "saving window settings error=%lu", GetLastError());
    }
    if (!WritePrivateProfileStringW(L"window", L"State", state, path_.c_str()))
        Log("FAIL", "saving window state error=%lu", GetLastError());
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
    if (message == WM_DISPLAYCHANGE && !fullscreen_) Save();
    if (message == WM_DISPLAYCHANGE && fullscreen_) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor))
            SetWindowPos(window_, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
}
