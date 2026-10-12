#include "options.h"
#include <windows.h>
#include <cwchar>
#include <string>
namespace ctr2 {
namespace {
// First use can be inside DllMain, so this splits the command line itself
// rather than loading shell32 under the loader lock. Switches never contain
// spaces or quotes; a quoted program path is skipped as one word.
template <typename Visit> void ForEachWord(const wchar_t* text, Visit visit) {
    while (*text) {
        while (*text == L' ' || *text == L'\t') ++text;
        if (!*text) break;
        std::wstring word;
        bool quoted = false;
        for (; *text && (quoted || (*text != L' ' && *text != L'\t')); ++text) {
            if (*text == L'"')
                quoted = !quoted;
            else
                word += *text;
        }
        visit(word);
    }
}
bool Environment(const wchar_t* name, const wchar_t* expected) {
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(name, value, 16);
    return length > 0 && length < 16 && wcscmp(value, expected) == 0;
}
Options Parse() {
    Options options;
    options.log = Environment(L"CTR2SHIM_LOG", L"1");
    options.trace = Environment(L"CTR2SHIM_TRACE", L"1");
    options.capture = Environment(L"CTR2SHIM_CAPTURE", L"1");
    options.diagnostics = Environment(L"CTR2SHIM_DIAGNOSTICS", L"1");
    options.passthrough = Environment(L"CTR2SHIM_MODE", L"trace");
    bool program = true;
    ForEachWord(GetCommandLineW(), [&](const std::wstring& word) {
        if (program) {
            program = false;
            return;
        }
        if (_wcsicmp(word.c_str(), L"--log") == 0) options.log = true;
        if (_wcsicmp(word.c_str(), L"--trace") == 0) options.trace = true;
        if (_wcsicmp(word.c_str(), L"--capture") == 0) options.capture = true;
        if (_wcsicmp(word.c_str(), L"--diagnostics") == 0) options.diagnostics = true;
        if (_wcsicmp(word.c_str(), L"--passthrough") == 0) options.passthrough = true;
    });
    // Tracing and passthrough exist to produce a log.
    if (options.trace || options.passthrough || options.diagnostics) options.log = true;
    if (options.passthrough) options.trace = true;
    return options;
}
}
const Options& GetOptions() noexcept {
    static const Options options = [] {
        try {
            return Parse();
        } catch (...) {
            return Options{}; // Out of memory at startup: everything stays off.
        }
    }();
    return options;
}
}
