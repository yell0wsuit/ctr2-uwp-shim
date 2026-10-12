// Logging is off unless Game.exe's command line asks for it. Loading the shim
// writes an "attached" line, so the log grows exactly when logging is on.
//   optionstest              expects no new log lines
//   optionstest --log        expects the attach line, no TRACE lines
//   optionstest --trace      expects the attach line
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
static std::string ReadLog() {
    HANDLE file = CreateFileW(L"ctr2shim.log", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text(GetFileSize(file, nullptr), '\0');
    DWORD read{};
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
    CloseHandle(file);
    text.resize(read);
    return text;
}
int main(int argc, char** argv) {
    // The log sits beside the executable; run from there.
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    *wcsrchr(path, L'\\') = 0;
    SetCurrentDirectoryW(path);
    const bool log = argc > 1 && (!strcmp(argv[1], "--log") || !strcmp(argv[1], "--trace"));
    const bool trace = argc > 1 && !strcmp(argv[1], "--trace");
    const auto before = ReadLog().size();
    if (!LoadLibraryW(L"ctr2shim.dll")) return 1;
    const auto added = ReadLog().substr(before);
    const bool attached = added.find("ctr2shim attached") != std::string::npos;
    if (attached != log) {
        printf("optionstest: attach line %s, expected %s\n", attached ? "logged" : "missing", log ? "logged" : "none");
        return 2;
    }
    const bool traced = added.find(" TRACE ") != std::string::npos;
    if (traced && !trace) {
        printf("optionstest: TRACE lines without --trace\n");
        return 3;
    }
    puts("optionstest: ok");
    return 0;
}
