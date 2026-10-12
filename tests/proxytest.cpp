#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
int main() {
    auto* module = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!module) {
        printf("FAIL load %lu\n", GetLastError());
        return 1;
    }
    auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(module, "D3DCompile"));
    if (!compile) return 2;
    const char* shader = "float4 main() : SV_Target { return float4(1,0,0,1); }";
    ID3DBlob* code = nullptr;
    ID3DBlob* error = nullptr;
    auto hr = compile(shader, strlen(shader), "proxytest", nullptr, nullptr, "main", "ps_4_0", 0, 0, &code, &error);
    if (error) {
        printf("compiler: %s\n", static_cast<const char*>(error->GetBufferPointer()));
        error->Release();
    }
    if (FAILED(hr) || !code || !code->GetBufferSize()) {
        printf("FAIL compile %08lx\n", hr);
        return 3;
    }
    auto disassemble = reinterpret_cast<pD3DDisassemble>(GetProcAddress(module, "D3DDisassemble"));
    if (!disassemble) return 4;
    ID3DBlob* text = nullptr;
    hr = disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text);
    code->Release();
    if (FAILED(hr) || !text) return 5;
    text->Release();
    puts("proxytest: compile and disassemble passed");
    return 0;
}
