#pragma once
#include <windows.h>
#include <winstring.h>
namespace ctr2 {
void Log(const char* level, const char* format, ...) noexcept;
void LogActivation(HSTRING name, REFIID iid, HRESULT result) noexcept;
bool TraceMode() noexcept;
void LogThreadStorage() noexcept;
void LogMissingInterface(const char* type, REFIID iid) noexcept;
}
// For WRL RuntimeClasses using InspectableClass, which calls this hook before
// its own QueryInterface: logs interfaces callers asked for but we lack.
#define CTR2_LOG_MISSING_INTERFACES(type)                                                                              \
public:                                                                                                                \
    HRESULT CustomQueryInterface(REFIID iid, void** value, bool* handled) {                                            \
        *handled = true;                                                                                               \
        auto hr = RuntimeClassT::QueryInterface(iid, value);                                                           \
        if (FAILED(hr)) ::ctr2::LogMissingInterface(type, iid);                                                        \
        return hr;                                                                                                     \
    }                                                                                                                  \
                                                                                                                       \
private:
