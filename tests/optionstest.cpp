// Logging is off unless Game.exe's command line asks for it. Loading the shim
// writes an "attached" line, so a log exists exactly when logging is on. Each
// logging run starts ctr2shim.log afresh and keeps the two before it.
//   optionstest              expects the seeded logs untouched
//   optionstest --log        expects a new log with the attach line, no TRACE lines
//   optionstest --trace      expects a new log with the attach line
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
static bool Exists(const wchar_t* name) { return GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES; }
static std::string Read(const wchar_t* name) {
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text(GetFileSize(file, nullptr), '\0');
    DWORD read{};
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
    CloseHandle(file);
    text.resize(read);
    return text;
}
static bool Write(const wchar_t* name, const char* text) {
    HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool ok = WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}
int main(int argc, char** argv) {
    // The logs sit beside the executable; run from there.
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    *wcsrchr(path, L'\\') = 0;
    SetCurrentDirectoryW(path);
    const bool log = argc > 1 && (!strcmp(argv[1], "--log") || !strcmp(argv[1], "--trace"));
    const bool trace = argc > 1 && !strcmp(argv[1], "--trace");
    DeleteFileW(L"ctr2shim.3.log");
    if (!Write(L"ctr2shim.log", "current\n") || !Write(L"ctr2shim.1.log", "older\n") ||
        !Write(L"ctr2shim.2.log", "oldest\n"))
        return 1;
    if (!LoadLibraryW(L"ctr2shim.dll")) return 1;
    const auto current = Read(L"ctr2shim.log"), older = Read(L"ctr2shim.1.log"), oldest = Read(L"ctr2shim.2.log");
    if (Exists(L"ctr2shim.3.log")) {
        printf("optionstest: kept more than three logs\n");
        return 2;
    }
    if (!log) {
        if (current != "current\n" || older != "older\n" || oldest != "oldest\n") {
            printf("optionstest: logs changed with logging off\n");
            return 2;
        }
        puts("optionstest: ok");
        return 0;
    }
    if (older != "current\n" || oldest != "older\n") {
        printf("optionstest: previous logs were not shifted (1=\"%s\" 2=\"%s\")\n", older.c_str(), oldest.c_str());
        return 2;
    }
    // The attach line opens the new log and names the build.
    const auto first = current.substr(0, current.find('\n'));
    if (first.find("ctr2shim attached version=") == std::string::npos || first.find(" commit=") == std::string::npos ||
        current.find("current\n") != std::string::npos) {
        printf("optionstest: new log does not start with the attach line\n");
        return 2;
    }
    if (current.find(" TRACE ") != std::string::npos && !trace) {
        printf("optionstest: TRACE lines without --trace\n");
        return 3;
    }
    puts("optionstest: ok");
    return 0;
}
