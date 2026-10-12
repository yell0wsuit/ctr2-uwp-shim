#include "log.h"
#include <winstring.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

namespace ctr2 {
bool TraceMode() noexcept {
    wchar_t value[16]{};
    GetEnvironmentVariableW(L"CTR2SHIM_MODE", value, 16);
    return wcscmp(value, L"trace") == 0;
}
void Log(const char* level, const char* format, ...) noexcept {
    if (strcmp(level, "TRACE") == 0) {
        wchar_t flag[4]{}; GetEnvironmentVariableW(L"CTR2SHIM_TRACE", flag, 4);
        if (flag[0] != L'1' && !TraceMode()) return;
    }
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    char message[1600]{};
    va_list args; va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    char line[1800]{};
    snprintf(line, sizeof(line), "[%llu][%lu] %s %s\r\n", GetTickCount64(), GetCurrentThreadId(), level, message);
    OutputDebugStringA(line);
    wchar_t path[32768]{};
    auto size = GetModuleFileNameW(nullptr, path, 32768);
    if (!size || size >= 32768) return;
    auto slash = wcsrchr(path, L'\\'); if (!slash) return;
    wcscpy_s(slash + 1, 32768 - (slash + 1 - path), L"ctr2shim.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written; WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr); CloseHandle(file);
}
void LogMissingInterface(const char* type, REFIID iid) noexcept {
    Log("FAIL", "%s QI missing %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", type,
        iid.Data1, iid.Data2, iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);
}
void LogActivation(HSTRING name, REFIID iid, HRESULT result) noexcept {
    LogThreadStorage();
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    Log(FAILED(result) ? "FAIL" : "TRACE", "activation %ls iid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x hr=%08lx", text,
        iid.Data1, iid.Data2, iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7], result);
}
}
