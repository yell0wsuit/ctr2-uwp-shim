#include "log.h"
#include "options.h"
#include <winstring.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>

namespace ctr2 {
bool TraceMode() noexcept { return GetOptions().passthrough; }
// ctr2shim.log beside the game executable.
static const std::wstring& LogPath() noexcept {
    static const std::wstring path = []() noexcept -> std::wstring {
        try {
            std::wstring module(32768, L'\0');
            auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
            module.resize(size < module.size() ? size : 0);
            auto slash = module.rfind(L'\\');
            return slash == std::wstring::npos ? std::wstring() : module.substr(0, slash + 1) + L"ctr2shim.log";
        } catch (...) {
            return {}; // No log rather than a crash.
        }
    }();
    return path;
}
// Each logging run starts a new ctr2shim.log. The two before it are kept as
// ctr2shim.1.log and ctr2shim.2.log; older ones are replaced. Runs without
// logging leave them alone.
static void RotateLogs(const std::wstring& path) noexcept {
    static constexpr int kept = 2;
    std::wstring older[kept + 1];
    try {
        const auto stem = path.substr(0, path.size() - 4); // Without ".log".
        for (int n = 1; n <= kept; ++n) older[n] = stem + L"." + std::to_wstring(n) + L".log";
    } catch (...) {
        return; // Out of memory: keep appending to the current log.
    }
    for (int n = kept; n > 1; --n) MoveFileExW(older[n - 1].c_str(), older[n].c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(path.c_str(), older[1].c_str(), MOVEFILE_REPLACE_EXISTING);
}
void Log(const char* level, const char* format, ...) noexcept {
    // Off unless --log or --trace (or their environment variables) is given.
    const auto& options = GetOptions();
    if (!options.log || (!options.trace && strcmp(level, "TRACE") == 0)) return;
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    char message[1600]{};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    char line[1800]{};
    snprintf(line, sizeof(line), "[%llu][%lu] %s %s\r\n", GetTickCount64(), GetCurrentThreadId(), level, message);
    OutputDebugStringA(line);
    const auto& path = LogPath();
    if (path.empty()) return;
    static bool rotated = false;
    if (!rotated) {
        rotated = true;
        RotateLogs(path);
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
}
void LogMissingInterface(const char* type, REFIID iid) noexcept {
    Log("FAIL", "%s QI missing %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", type, iid.Data1, iid.Data2,
        iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6],
        iid.Data4[7]);
}
void LogActivation(HSTRING name, REFIID iid, HRESULT result) noexcept {
    LogThreadStorage();
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    Log(FAILED(result) ? "FAIL" : "TRACE",
        "activation %ls iid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x hr=%08lx", text, iid.Data1, iid.Data2,
        iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6],
        iid.Data4[7], result);
}
}
