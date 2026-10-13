#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
#define CHECK(call)                                                                                                    \
    do {                                                                                                               \
        HRESULT result = (call);                                                                                       \
        if (FAILED(result)) {                                                                                          \
            printf("shadertest: %s hr=%08lx\n", #call, result);                                                        \
            return result;                                                                                             \
        }                                                                                                              \
    } while (0)
static HRESULT Draw() {
    auto compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compiler) return HRESULT_FROM_WIN32(GetLastError());
    auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile) return E_NOINTERFACE;
    auto shader = [&](const char* source, const char* profile, ID3DBlob** code) {
        ComPtr<ID3DBlob> error;
        auto hr = compile(source, strlen(source), "shadertest", nullptr, nullptr, "main", profile, 0, 0, code, &error);
        if (error) printf("%s\n", static_cast<const char*>(error->GetBufferPointer()));
        return hr;
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_9_3};
    CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device,
                            nullptr, &context));
    ComPtr<ID3DBlob> vsCode, psCode;
    CHECK(shader("float4 main(float2 p : POSITION) : SV_Position { return float4(p,0,1); }", "vs_4_0_level_9_3",
                 &vsCode));
    CHECK(shader("cbuffer Color : register(b0) { float4 color; };"
                 "float4 main() : SV_Target { return color; }",
                 "ps_4_0_level_9_3", &psCode));
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    CHECK(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs));
    CHECK(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps));
    D3D11_INPUT_ELEMENT_DESC element{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    ComPtr<ID3D11InputLayout> layout;
    CHECK(device->CreateInputLayout(&element, 1, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout));
    const float vertices[] = {-1, -1, -1, 3, 3, -1};
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(vertices);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{vertices, 0, 0};
    ComPtr<ID3D11Buffer> vertexBuffer;
    CHECK(device->CreateBuffer(&bd, &initial, &vertexBuffer));
    const float red[] = {1, 0, 0, 1};
    bd.ByteWidth = sizeof(red);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    initial.pSysMem = red;
    ComPtr<ID3D11Buffer> constants;
    CHECK(device->CreateBuffer(&bd, &initial, &constants));
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 64;
    td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> image;
    CHECK(device->CreateTexture2D(&td, nullptr, &image));
    ComPtr<ID3D11RenderTargetView> target;
    CHECK(device->CreateRenderTargetView(image.Get(), nullptr, &target));
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    CHECK(device->CreateRasterizerState(&rd, &raster));
    context->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0, 0, 64, 64, 0, 1};
    context->RSSetViewports(1, &viewport);
    UINT stride = 2 * sizeof(float), offset = 0;
    context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetInputLayout(layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    const float black[] = {0, 0, 0, 1};
    context->ClearRenderTargetView(target.Get(), black);
    context->Draw(3, 0);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    CHECK(device->CreateTexture2D(&td, nullptr, &staging));
    context->CopyResource(staging.Get(), image.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    const auto* pixel = static_cast<const BYTE*>(mapped.pData) + 32 * mapped.RowPitch + 32 * 4;
    const bool valid = pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
    printf("shadertest: FL9 constant-buffer pixel=%u,%u,%u,%u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    context->Unmap(staging.Get(), 0);
    return valid ? S_OK : E_FAIL;
}
int main() { return FAILED(Draw()) ? 1 : 0; }
