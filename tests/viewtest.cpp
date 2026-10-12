#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include <windows.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::ViewManagement;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    HRESULT hr = get(HStringReference(L"Windows.UI.ViewManagement.ApplicationView").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL ApplicationView factory: %08lx\n", hr);
        return 2;
    }
    ComPtr<IApplicationViewStatics2> statics;
    if (FAILED(factory.As(&statics))) return 3;
    if (statics->GetForCurrentView(nullptr) != E_POINTER) return 4;
    ComPtr<IApplicationView> view, again;
    if (FAILED(statics->GetForCurrentView(&view)) || FAILED(statics->GetForCurrentView(&again)) ||
        view.Get() != again.Get())
        return 5;
    ApplicationViewOrientation orientation{};
    if (FAILED(view->get_Orientation(&orientation)) || orientation != ApplicationViewOrientation_Landscape) return 6;
    boolean fullscreen = false;
#pragma warning(suppress : 4996)
    if (FAILED(view->get_IsFullScreen(&fullscreen)) || !fullscreen) return 7;
    ComPtr<IApplicationView2> view2;
    if (FAILED(view.As(&view2))) return 8;
    ABI::Windows::Foundation::Rect bounds{};
    if (FAILED(view2->get_VisibleBounds(&bounds)) || bounds.Width != 1024 || bounds.Height != 768) return 9;
    boolean success = false;
    if (FAILED(view2->SetDesiredBoundsMode(ApplicationViewBoundsMode_UseCoreWindow, &success)) || !success) return 10;
    ApplicationViewBoundsMode mode{};
    if (FAILED(view2->get_DesiredBoundsMode(&mode)) || mode != ApplicationViewBoundsMode_UseCoreWindow) return 11;
    ComPtr<IApplicationView3> view3;
    if (FAILED(view.As(&view3))) return 12;
    success = true;
    if (FAILED(view3->TryEnterFullScreenMode(&success)) || success) return 13;
    if (FAILED(view3->get_IsFullScreenMode(&fullscreen)) || fullscreen || FAILED(view3->ExitFullScreenMode()))
        return 14;
    ComPtr<IUnknown> identity1, identity2;
    view.As(&identity1);
    view3.As(&identity2);
    if (identity1.Get() != identity2.Get()) return 15;
    puts("viewtest: 0 failure(s)");
    return 0;
}
