#include <windows.h>
#include <cstdio>
extern "C" __declspec(dllimport) void __cdecl ShimBootstrap();
extern "C" void* ctr2_targets[];
extern "C" const char* ctr2_export_names[];
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static HMODULE self;
static BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[32768]{};
    UINT length = GetSystemDirectoryW(path, 32768);
    if (!length || length + 20 >= 32768) return FALSE;
    wcscat_s(path, L"\\d3dcompiler_47.dll");
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module || module == self) return FALSE;
    for (unsigned i = 0; i != 29; ++i)
        ctr2_targets[i] = reinterpret_cast<void*>(GetProcAddress(module, ctr2_export_names[i]));
    // Keep the module loaded: tail thunks hold addresses for process lifetime.
    return TRUE;
}
extern "C" void __cdecl ctr2_resolve(unsigned index) {
    if (!InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr) || !ctr2_targets[index]) {
        char message[256];
        snprintf(message, sizeof(message), "ctr2 proxy: missing real compiler export %s (error %lu)\n",
                 ctr2_export_names[index], GetLastError());
        OutputDebugStringA(message);
        // Never return through an ABI-unknown signature with a fabricated result.
        TerminateProcess(GetCurrentProcess(), ERROR_PROC_NOT_FOUND);
    }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        ShimBootstrap();
    }
    return TRUE;
}
