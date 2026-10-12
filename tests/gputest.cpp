#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
int main() {
    DeleteFileW(L"ctr2-frame.bmp");
    SetEnvironmentVariableW(L"CTR2SHIM_CAPTURE", L"1");
    RoInitialize(RO_INIT_SINGLETHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory))) return 2;
    ComPtr<IApplicationStatics> statics;
    factory.As(&statics);
    ComPtr<IApplicationFactory> compose;
    factory.As(&compose);
    HRESULT gpu = E_FAIL;
    auto callback =
        Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) -> HRESULT {
            ComPtr<IApplication> app;
            compose->CreateInstance(nullptr, nullptr, &app);
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            gpu = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                    &device, nullptr, &context);
            if (SUCCEEDED(gpu)) {
                ComPtr<IDXGIDevice> dxgi;
                device.As(&dxgi);
                ComPtr<IDXGIAdapter> adapter;
                dxgi->GetAdapter(&adapter);
                ComPtr<IDXGIFactory2> dxgiFactory;
                adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = 64;
                desc.Height = 64;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.Scaling = DXGI_SCALING_STRETCH;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
                ComPtr<IDXGISwapChain1> swap;
                gpu = dxgiFactory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &swap);
                // The result must target our host HWND, not an unattached composition surface.
                HWND hwnd{};
                if (SUCCEEDED(gpu)) gpu = swap->GetHwnd(&hwnd);
                if (SUCCEEDED(gpu) && !IsWindow(hwnd)) gpu = E_UNEXPECTED;
                // ANGLE's SwapChainPanel path scales via the composition matrix and
                // fails surface creation when this call fails.
                if (SUCCEEDED(gpu)) {
                    ComPtr<IDXGISwapChain2> swap2;
                    gpu = swap.As(&swap2);
                    DXGI_MATRIX_3X2_F scale{0.5f, 0, 0, 0.5f, 0, 0}, stored{};
                    if (SUCCEEDED(gpu)) gpu = swap2->SetMatrixTransform(&scale);
                    if (SUCCEEDED(gpu)) gpu = swap2->GetMatrixTransform(&stored);
                    if (SUCCEEDED(gpu) && (stored._11 != 0.5f || stored._22 != 0.5f)) gpu = E_UNEXPECTED;
                    if (FAILED(gpu)) printf("gputest: matrix transform hr=%08lx\n", gpu);
                }
                ComPtr<ID3D11Texture2D> back;
                if (SUCCEEDED(gpu)) gpu = swap->GetBuffer(0, IID_PPV_ARGS(&back));
                ComPtr<ID3D11RenderTargetView> target;
                if (SUCCEEDED(gpu)) gpu = device->CreateRenderTargetView(back.Get(), nullptr, &target);
                if (SUCCEEDED(gpu)) {
                    const float red[] = {1, 0, 0, 1};
                    context->ClearRenderTargetView(target.Get(), red);
                    D3D11_TEXTURE2D_DESC td{};
                    back->GetDesc(&td);
                    td.Usage = D3D11_USAGE_STAGING;
                    td.BindFlags = 0;
                    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    td.MiscFlags = 0;
                    ComPtr<ID3D11Texture2D> readback;
                    gpu = device->CreateTexture2D(&td, nullptr, &readback);
                    if (SUCCEEDED(gpu)) {
                        context->CopyResource(readback.Get(), back.Get());
                        D3D11_MAPPED_SUBRESOURCE mapped{};
                        gpu = context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                        if (SUCCEEDED(gpu)) {
                            const auto* px = static_cast<const BYTE*>(mapped.pData);
                            if (px[0] != 255 || px[1] != 0 || px[2] != 0 || px[3] != 255) gpu = E_FAIL;
                            context->Unmap(readback.Get(), 0);
                        }
                    }
                    if (SUCCEEDED(gpu)) gpu = swap->Present(0, 0);
                    if (SUCCEEDED(gpu)) {
                        FILE* frame{};
                        fopen_s(&frame, "ctr2-frame.bmp", "rb");
                        if (!frame) {
                            gpu = E_FAIL;
                        } else {
                            BYTE pixel[4]{};
                            fseek(frame, 54, SEEK_SET);
                            fread(pixel, 1, 4, frame);
                            fclose(frame);
                            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 255) gpu = E_FAIL;
                        }
                        DeleteFileW(L"ctr2-frame.bmp");
                    }
                }
                // On resize the game creates its next swap chain before releasing the
                // old one. Each must stay flip-model in its own window, newest on top;
                // a second chain on one window fell back to a blt chain hidden under the
                // old flip surface, so the screen froze on a stale frame.
                ComPtr<IDXGISwapChain1> next;
                HWND first{}, second{}, host = GetAncestor(hwnd, GA_ROOT);
                desc.Width = 96;
                if (SUCCEEDED(gpu))
                    gpu = dxgiFactory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &next);
                if (SUCCEEDED(gpu)) gpu = swap->GetHwnd(&first);
                if (SUCCEEDED(gpu)) gpu = next->GetHwnd(&second);
                DXGI_SWAP_CHAIN_DESC1 nextDesc{};
                if (SUCCEEDED(gpu)) gpu = next->GetDesc1(&nextDesc);
                RECT client{}, surface{};
                GetClientRect(host, &client);
                GetClientRect(second, &surface);
                if (SUCCEEDED(gpu) &&
                    (first == second || GetParent(second) != host ||
                     nextDesc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || GetWindow(host, GW_CHILD) != second ||
                     surface.right != client.right || surface.bottom != client.bottom)) {
                    printf("gputest: second swap chain shares or hides behind the first window\n");
                    gpu = E_FAIL;
                }
                // Surfaces follow the host's client area.
                SetWindowPos(host, nullptr, 0, 0, 900, 700, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                GetClientRect(host, &client);
                GetClientRect(second, &surface);
                if (SUCCEEDED(gpu) && (surface.right != client.right || surface.bottom != client.bottom)) {
                    printf("gputest: surface window did not follow the host size\n");
                    gpu = E_FAIL;
                }
                // Releasing a swap chain closes its window once the queue runs.
                back.Reset();
                target.Reset();
                swap.Reset();
                context->ClearState();
                context->Flush();
                MSG msg{};
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
                if (SUCCEEDED(gpu) && IsWindow(first)) {
                    printf("gputest: released swap chain kept its window\n");
                    gpu = E_FAIL;
                }
            }
            PostQuitMessage(0);
            return S_OK;
        });
    auto hr = statics->Start(callback.Get());
    printf("gputest: host=%08lx draw/readback/present=%08lx\n", hr, gpu);
    return FAILED(hr) || FAILED(gpu) ? 1 : 0;
}
