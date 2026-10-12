#include "registry.h"
#include "log.h"
#include <roapi.h>
#include <wrl/client.h>
#include <cstring>

namespace ctr2 {
using FactoryCall = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
using ActivateCall = HRESULT(WINAPI*)(HSTRING, IInspectable**);
static FactoryCall originalFactory = &RoGetActivationFactory;
static ActivateCall originalActivate = &RoActivateInstance;
static HRESULT WINAPI GetFactory(HSTRING name, REFIID iid, void** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    HRESULT hr;
    if (TraceMode()) {
        hr = originalFactory(name, iid, out);
    } else {
        // Prefer the OS Streams implementation when available. The partial
        // buffer reader fills the missing Wine class, without replacing the
        // complete Windows DataReader implementation.
        if (wcscmp(WindowsGetStringRawBuffer(name, nullptr), L"Windows.Storage.Streams.DataReader") == 0) {
            hr = originalFactory(name, iid, out);
            if (SUCCEEDED(hr)) {
                LogActivation(name, iid, hr);
                return hr;
            }
            *out = nullptr;
        }
        Microsoft::WRL::ComPtr<IActivationFactory> factory;
        hr = ShimGetFactory(name, &factory);
        if (hr == CLASS_E_CLASSNOTAVAILABLE)
            hr = originalFactory(name, iid, out);
        else if (SUCCEEDED(hr))
            hr = factory->QueryInterface(iid, out);
    }
    LogActivation(name, iid, hr);
    return hr;
}
static HRESULT WINAPI Activate(HSTRING name, IInspectable** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    HRESULT hr;
    if (TraceMode()) {
        hr = originalActivate(name, out);
    } else {
        Microsoft::WRL::ComPtr<IActivationFactory> factory;
        hr = ShimGetFactory(name, &factory);
        if (hr == CLASS_E_CLASSNOTAVAILABLE)
            hr = originalActivate(name, out);
        else if (SUCCEEDED(hr))
            hr = factory->ActivateInstance(out);
    }
    LogActivation(name, __uuidof(IInspectable), hr);
    return hr;
}
static void PatchModule(HMODULE module) noexcept {
    if (!module) return;
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return;
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + imports->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            auto* symbol = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            void* hook = nullptr;
            if (strcmp(reinterpret_cast<char*>(symbol->Name), "RoGetActivationFactory") == 0)
                hook = reinterpret_cast<void*>(&GetFactory);
            if (strcmp(reinterpret_cast<char*>(symbol->Name), "RoActivateInstance") == 0)
                hook = reinterpret_cast<void*>(&Activate);
            if (!hook) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old)) continue;
            InterlockedExchange(reinterpret_cast<LONG*>(&slots->u1.Function), reinterpret_cast<LONG>(hook));
            DWORD ignored;
            VirtualProtect(&slots->u1.Function, sizeof(DWORD), old, &ignored);
        }
    }
}
void PatchActivationImports() noexcept {
    PatchModule(GetModuleHandleW(nullptr));
    PatchModule(GetModuleHandleW(L"vccorlib120_app.dll"));
    PatchModule(GetModuleHandleW(L"msvcr120_app.dll"));
    PatchModule(GetModuleHandleW(L"msvcp120_app.dll"));
}
}
