#include <windows.h>
#include "log.h"
#include "options.h"
#include <mutex>
#include <intrin.h>
namespace ctr2 {
static void LogAngleError(EXCEPTION_POINTERS* fault) noexcept {
    if (!GetOptions().diagnostics || fault->ExceptionRecord->NumberParameters < 2) return;
    wchar_t message[64]{};
    SIZE_T read{};
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]),
                      message, sizeof(message) - sizeof(wchar_t), &read);
    if (wcsncmp(message, L"eglMakeCurrent failed", 21) != 0 && wcsncmp(message, L"eglSwapBuffers failed", 21) != 0)
        return;
    auto* image = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    auto* name = wcsrchr(path, L'\\');
    if (!name || _wcsicmp(name + 1, L"Game.exe") != 0) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    // Diagnostic only, for the supported 1.0.0.46 executable's ANGLE TLS key.
    if (nt->OptionalHeader.SizeOfImage <= 0x5adde4) return;
    DWORD index = TLS_OUT_OF_INDEXES;
    if (!ReadProcessMemory(GetCurrentProcess(), image + 0x5adde0, &index, sizeof(index), &read)) return;
    DWORD previous = GetLastError();
    auto* storage = TlsGetValue(index);
    SetLastError(previous);
    DWORD state[6]{};
    if (ReadProcessMemory(GetCurrentProcess(), storage, state, sizeof(state), &read))
        Log("TRACE", "ANGLE error message=%ls key=%lu state=%p words=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx", message,
            index, storage, state[0], state[1], state[2], state[3], state[4], state[5]);
}
void LogThreadStorage() noexcept {
    if (!GetOptions().diagnostics) return;
    auto* image = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!directory.VirtualAddress) return;
    auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY32*>(image + directory.VirtualAddress);
    DWORD index = 0, blocks[8]{};
    SIZE_T read{};
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(tls->AddressOfIndex), &index, sizeof(index),
                           &read))
        return;
    auto vector = __readfsdword(0x2c);
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(vector), blocks, sizeof(blocks), &read)) return;
    DWORD values[6]{};
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(blocks[0]), values, sizeof(values), &read);
    Log("TRACE",
        "thread storage gameIndex=%lu vector=%08lx slot0=%08lx gameBlock=%08lx "
        "slot0Words=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx",
        index, vector, blocks[0], index < 8 ? blocks[index] : 0, values[0], values[1], values[2], values[3], values[4],
        values[5]);
}
// Names the module holding an address and the offset into it; the offset
// matches a disassembly at any load address. "?" outside every module.
static void DescribeAddress(DWORD address, wchar_t (&module)[64], DWORD& rva) noexcept {
    wcscpy_s(module, L"?");
    rva = address;
    HMODULE handle{};
    wchar_t path[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &handle) ||
        !GetModuleFileNameW(handle, path, MAX_PATH))
        return;
    const auto* name = wcsrchr(path, L'\\');
    wcsncpy_s(module, name ? name + 1 : path, _TRUNCATE);
    rva = address - reinterpret_cast<DWORD>(handle);
}
static LONG CALLBACK LogFault(EXCEPTION_POINTERS* fault) {
    if (fault->ExceptionRecord->ExceptionCode == 0x4001000a) {
        LogAngleError(fault);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (fault->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    auto* context = fault->ContextRecord;
    LogThreadStorage();
    HMODULE module{};
    wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(context->Eip), &module)) {
        GetModuleFileNameW(module, path, MAX_PATH);
        Log("FAIL", "fault module=%ls rva=%08lx", path, context->Eip - reinterpret_cast<DWORD>(module));
    }
    Log("FAIL", "access violation ip=%08lx address=%08lx eax=%08lx ecx=%08lx edx=%08lx esp=%08lx ebp=%08lx",
        context->Eip, static_cast<DWORD>(fault->ExceptionRecord->ExceptionInformation[1]), context->Eax, context->Ecx,
        context->Edx, context->Esp, context->Ebp);
    // Frame-pointer-omitting game functions hide their callers from the ebp
    // walk below, so also scan the stack for words inside Game.exe's image.
    // Windows relocates the image (ASLR); RVAs match a disassembly at any base.
    auto* image = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    const DWORD base = reinterpret_cast<DWORD>(image), size = nt->OptionalHeader.SizeOfImage;
    DWORD stack[512]{};
    SIZE_T read{};
    // The handler runs on the faulting thread; fs:[4] is that thread's stack base.
    const DWORD stackBase = __readfsdword(4);
    SIZE_T length = sizeof(stack);
    if (stackBase > context->Esp && stackBase - context->Esp < length) length = stackBase - context->Esp;
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(context->Esp), stack, length, &read);
    for (unsigned i = 0; i < read / sizeof(DWORD); ++i)
        if (stack[i] - base < size) Log("FAIL", "stack[%u]=%08lx rva=%08lx", i, stack[i], stack[i] - base);
    // Short std::strings live inline on the stack; their text names the data
    // (asset keys, resource names) the faulting code was handling.
    const auto* text = reinterpret_cast<const char*>(stack);
    for (SIZE_T start = 0, i = 0; i <= read; ++i) {
        if (i < read && text[i] >= 0x20 && text[i] < 0x7f) continue;
        if (i - start >= 4) Log("FAIL", "stack text +%03zx \"%.*s\"", start, static_cast<int>(i - start), text + start);
        start = i + 1;
    }
    DWORD frame = context->Ebp;
    for (unsigned i = 0; i < 16 && frame; ++i) {
        DWORD words[6]{};
        SIZE_T bytes{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(frame), words, sizeof(words), &bytes) ||
            bytes != sizeof(words))
            break;
        wchar_t caller[64];
        DWORD rva;
        DescribeAddress(words[1], caller, rva);
        Log("FAIL", "frame[%u] ebp=%08lx return=%08lx (%ls+%08lx) args=%08lx,%08lx,%08lx,%08lx", i, frame, words[1],
            caller, rva, words[2], words[3], words[4], words[5]);
        if (words[0] <= frame || words[0] - frame > 0x100000) break;
        frame = words[0];
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void InitializeDiagnostics() {
    if (!GetOptions().diagnostics) return;
    static std::once_flag once;
    std::call_once(once, [] { AddVectoredExceptionHandler(1, LogFault); });
}
}
