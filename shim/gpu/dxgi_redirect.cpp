#include "dxgi_redirect.h"
#include "host/host.h"
#include "log.h"
#include "options.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <mutex>
#include <new>
#include <map>
#include <atomic>
#include <vector>
#include <cstdio>
using Microsoft::WRL::ComPtr;
namespace ctr2 {
using CompositionCall = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
                                                    IDXGIOutput*, IDXGISwapChain1**);
static CompositionCall original;
using PresentCall = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Call = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using SetMatrixCall = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain2*, const DXGI_MATRIX_3X2_F*);
using GetMatrixCall = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain2*, DXGI_MATRIX_3X2_F*);
struct PresentHooks {
    PresentCall present;
    Present1Call present1;
    SetMatrixCall setMatrix;
    GetMatrixCall getMatrix;
};
// Private data marking swap chains CreateForHost redirected, and their stored matrix.
static const GUID redirectedTag = {0xa891e43b, 0x7e18, 0x4e38, {0xaa, 0xaa, 0xd7, 0x1b, 0x50, 0x0c, 0x67, 0x5f}};
static const GUID matrixTag = {0xff0dfd5b, 0x2cb8, 0x420c, {0xb1, 0xa0, 0xa4, 0x67, 0x28, 0x6b, 0x3f, 0x86}};
static std::mutex presentMutex;
// NOLINTNEXTLINE(bugprone-throwing-static-initialization): allocation failure at DLL load is fatal anyway.
static std::map<void**, PresentHooks> presentHooks;
static std::atomic<unsigned> frameCount{0};
static void CaptureFrame(IDXGISwapChain* swap) {
    const unsigned frame = ++frameCount;
    // Log the first frames individually to show whether a render loop exists.
    if (frame <= 10) Log("TRACE", "swap-chain frame %u", frame);
    if (frame != 1 && frame % 120 != 0) return;
    if (frame > 10) Log("TRACE", "swap-chain frame %u", frame);
    if (!GetOptions().capture) return;
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr)) return;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 ||
        (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM))
        return;
    ComPtr<ID3D11Device> device;
    back->GetDevice(&device);
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) return;
    context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;
    std::vector<BYTE> pixels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        auto* row = pixels.data() + static_cast<size_t>(y) * desc.Width * 4;
        memcpy(row, static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
               static_cast<size_t>(desc.Width) * 4);
        if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
            for (UINT x = 0; x < desc.Width; ++x) std::swap(row[x * 4], row[x * 4 + 2]);
    }
    context->Unmap(staging.Get(), 0);
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(desc.Width);
    info.biHeight = -static_cast<LONG>(desc.Height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    FILE* file{};
    if (fopen_s(&file, "ctr2-frame.bmp", "wb") != 0) return;
    fwrite(&header, sizeof(header), 1, file);
    fwrite(&info, sizeof(info), 1, file);
    fwrite(pixels.data(), pixels.size(), 1, file);
    fclose(file);
}
static HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* swap, UINT sync, UINT flags) {
    PresentCall call;
    {
        std::lock_guard<std::mutex> lock(presentMutex);
        call = presentHooks.at(*reinterpret_cast<void***>(swap)).present;
    }
    if (!(flags & DXGI_PRESENT_TEST)) CaptureFrame(swap);
    return call(swap, sync, flags);
}
static HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* swap, UINT sync, UINT flags,
                                          const DXGI_PRESENT_PARAMETERS* params) {
    Present1Call call;
    {
        std::lock_guard<std::mutex> lock(presentMutex);
        call = presentHooks.at(*reinterpret_cast<void***>(swap)).present1;
    }
    if (!(flags & DXGI_PRESENT_TEST)) CaptureFrame(swap);
    return call(swap, sync, flags, params);
}
static bool Redirected(IUnknown* swap) {
    ComPtr<IDXGIObject> object;
    BOOL tag = FALSE;
    UINT size = sizeof(tag);
    return SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&object))) &&
           SUCCEEDED(object->GetPrivateData(redirectedTag, &size, &tag)) && size == sizeof(tag) && tag;
}
// ANGLE's SwapChainPanel path scales composition swap chains via
// IDXGISwapChain2::SetMatrixTransform and fails surface creation on error.
// HWND swap chains reject the call (Wine stubs it); the HWND redirect already
// stretches to the client area, so store the matrix without applying it.
static HRESULT STDMETHODCALLTYPE SetMatrix(IDXGISwapChain2* swap, const DXGI_MATRIX_3X2_F* matrix) {
    if (!Redirected(swap)) {
        SetMatrixCall call;
        {
            std::lock_guard<std::mutex> lock(presentMutex);
            call = presentHooks.at(*reinterpret_cast<void***>(swap)).setMatrix;
        }
        return call(swap, matrix);
    }
    if (!matrix) return E_INVALIDARG;
    Log("TRACE", "SetMatrixTransform scale=%gx%g (stretched by HWND redirect)", matrix->_11, matrix->_22);
    return swap->SetPrivateData(matrixTag, sizeof(*matrix), matrix);
}
static HRESULT STDMETHODCALLTYPE GetMatrix(IDXGISwapChain2* swap, DXGI_MATRIX_3X2_F* matrix) {
    if (!Redirected(swap)) {
        GetMatrixCall call;
        {
            std::lock_guard<std::mutex> lock(presentMutex);
            call = presentHooks.at(*reinterpret_cast<void***>(swap)).getMatrix;
        }
        return call(swap, matrix);
    }
    if (!matrix) return E_INVALIDARG;
    UINT size = sizeof(*matrix);
    if (FAILED(swap->GetPrivateData(matrixTag, &size, matrix)) || size != sizeof(*matrix))
        *matrix = DXGI_MATRIX_3X2_F{1, 0, 0, 1, 0, 0};
    return S_OK;
}
static void ObserveSwapChain(IDXGISwapChain1* swap) {
    auto* table = *reinterpret_cast<void***>(swap);
    std::lock_guard<std::mutex> lock(presentMutex);
    if (presentHooks.count(table)) return;
    // Matrix slots exist only in IDXGISwapChain2 and later vtables.
    ComPtr<IDXGISwapChain2> swap2;
    const bool matrix = SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&swap2)));
    const SIZE_T slots = matrix ? 28 : 15;
    DWORD protection;
    if (!VirtualProtect(static_cast<void*>(table + 8), slots * sizeof(void*), PAGE_READWRITE, &protection)) return;
    presentHooks.emplace(table, PresentHooks{
                                    reinterpret_cast<PresentCall>(table[8]),
                                    reinterpret_cast<Present1Call>(table[22]),
                                    matrix ? reinterpret_cast<SetMatrixCall>(table[34]) : nullptr,
                                    matrix ? reinterpret_cast<GetMatrixCall>(table[35]) : nullptr,
                                });
    InterlockedExchangePointer(&table[8], reinterpret_cast<void*>(&Present));
    InterlockedExchangePointer(&table[22], reinterpret_cast<void*>(&Present1));
    if (matrix) {
        InterlockedExchangePointer(&table[34], reinterpret_cast<void*>(&SetMatrix));
        InterlockedExchangePointer(&table[35], reinterpret_cast<void*>(&GetMatrix));
    }
    DWORD ignored;
    VirtualProtect(static_cast<void*>(table + 8), slots * sizeof(void*), protection, &ignored);
}

// Held by a redirected swap chain as private data; DXGI releases it when the
// swap chain is destroyed, which closes the swap chain's surface window.
class SurfaceOwner final : public IUnknown {
    std::atomic<ULONG> refs_{1};
    HWND surface_;

public:
    explicit SurfaceOwner(HWND surface) : surface_(surface) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = iid == __uuidof(IUnknown) ? this : nullptr;
        if (!*value) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto refs = --refs_;
        if (!refs) {
            // Posted: the window belongs to the UI thread, and DXGI wants the
            // swap chain gone before its window.
            PostMessageW(surface_, WM_CLOSE, 0, 0);
            delete this;
        }
        return refs;
    }
};
static const GUID surfaceTag = {0x5d3c1f0e, 0x2b7a, 0x4c55, {0x9e, 0x31, 0x6a, 0x0f, 0x84, 0x2d, 0xc7, 0x19}};
static HRESULT STDMETHODCALLTYPE CreateForHost(IDXGIFactory2* factory, IUnknown* device,
                                               const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* restrictTo,
                                               IDXGISwapChain1** value) {
    if (!HostWindow()) return original(factory, device, desc, restrictTo, value);
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!desc || !device) return E_INVALIDARG;
    auto adjusted = *desc;
    adjusted.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    adjusted.Scaling = DXGI_SCALING_STRETCH;
    HWND surface = CreateSurfaceWindow();
    if (!surface) return HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_INVALID_WINDOW_HANDLE);
    auto hr = factory->CreateSwapChainForHwnd(device, surface, &adjusted, nullptr, restrictTo, value);
    if (FAILED(hr) && (adjusted.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
                       adjusted.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)) {
        Log("TRACE", "DXGI flip swap chain %ux%u failed hr=%08lx; trying DISCARD", adjusted.Width, adjusted.Height, hr);
        adjusted.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory->CreateSwapChainForHwnd(device, surface, &adjusted, nullptr, restrictTo, value);
    }
    if (SUCCEEDED(hr) && *value) {
        const BOOL tag = TRUE;
        hr = (*value)->SetPrivateData(redirectedTag, sizeof(tag), &tag);
        auto* owner = new (std::nothrow) SurfaceOwner(surface);
        if (SUCCEEDED(hr)) hr = owner ? (*value)->SetPrivateDataInterface(surfaceTag, owner) : E_OUTOFMEMORY;
        if (owner) {
            owner->Release(); // The swap chain holds it now, or nothing does and the window closes.
            surface = nullptr;
        }
        if (SUCCEEDED(hr)) {
            ObserveSwapChain(*value);
        } else {
            (*value)->Release();
            *value = nullptr;
        }
    }
    if (surface) PostMessageW(surface, WM_CLOSE, 0, 0);
    Log(FAILED(hr) ? "FAIL" : "TRACE", "DXGI composition -> HWND %ux%u hr=%08lx", adjusted.Width, adjusted.Height, hr);
    return hr;
}
HRESULT InitializeGraphicsRedirect() {
    static std::once_flag once;
    static HRESULT result = E_UNEXPECTED;
    std::call_once(once, [] {
        ComPtr<IDXGIFactory2> factory;
        // First GPU call on the view thread; brackets Wine graphics startup stalls.
        Log("INFO", "CreateDXGIFactory1 begin");
        result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        Log(FAILED(result) ? "FAIL" : "INFO", "CreateDXGIFactory1 hr=%08lx", result);
        if (FAILED(result)) return;
        auto* table = *reinterpret_cast<void***>(factory.Get());
        // IDXGIFactory2 slot 24, copied from dxgi1_2.h's SDK ABI layout.
        DWORD protection;
        if (!VirtualProtect(static_cast<void*>(&table[24]), sizeof(void*), PAGE_READWRITE, &protection)) {
            result = HRESULT_FROM_WIN32(GetLastError());
            return;
        }
        original = reinterpret_cast<CompositionCall>(table[24]);
        InterlockedExchangePointer(&table[24], reinterpret_cast<void*>(&CreateForHost));
        DWORD ignored;
        VirtualProtect(static_cast<void*>(&table[24]), sizeof(void*), protection, &ignored);
        result = S_OK;
    });
    return result;
}
}
