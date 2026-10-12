#include <windows.h>
#undef GetCurrentTime
#include <roapi.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
int main() {
    RoInitialize(RO_INIT_SINGLETHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Frame").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IInspectable> object;
    CHECK(SUCCEEDED(factory->ActivateInstance(&object)));
    if (!object) return 1;
    ComPtr<IUIElement> ui;
    CHECK(SUCCEEDED(object.As(&ui)));
    ABI::Windows::Foundation::Size size{};
    CHECK(SUCCEEDED(ui->get_RenderSize(&size)) && size.Width == 1024 && size.Height == 768);
    ComPtr<INavigate> navigate;
    CHECK(SUCCEEDED(object.As(&navigate)));
    ComPtr<IFrame> frame;
    CHECK(SUCCEEDED(object.As(&frame)));
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IPageFactory> pageFactory;
    CHECK(SUCCEEDED(factory.As(&pageFactory)));
    if (!pageFactory) return 1;
    ComPtr<IPage> page;
    CHECK(SUCCEEDED(pageFactory->CreateInstance(nullptr, nullptr, &page)));
    if (!page) return 1;
    ComPtr<IFrameworkElement> element;
    CHECK(SUCCEEDED(page.As(&element)));
    if (!element) return 1;
    ComPtr<IInspectable> panel, same;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"swapChainPanel").Get(), &panel)));
    if (!panel) return 1;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"swapChainPanel").Get(), &same)));
    CHECK(same.Get() == panel.Get());
    ComPtr<ISwapChainPanelNative> nativePanel;
    CHECK(SUCCEEDED(panel.As(&nativePanel)));
    ComPtr<ISwapChainPanel> scale;
    CHECK(SUCCEEDED(panel.As(&scale)));
    if (!scale) return 1;
    float x = 0;
    CHECK(SUCCEEDED(scale->get_CompositionScaleX(&x)) && x == 1);
    // MainPage.InitializeComponent safe_casts this name to Grid^.
    ComPtr<IInspectable> grid, sameGrid;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"gridForTextBlocks").Get(), &grid)) && grid);
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"gridForTextBlocks").Get(), &sameGrid)) &&
          sameGrid.Get() == grid.Get());
    ComPtr<IGrid> gridType;
    CHECK(grid && SUCCEEDED(grid.As(&gridType)));
    ComPtr<IPanel> gridPanel;
    CHECK(grid && SUCCEEDED(grid.As(&gridPanel)));
    ComPtr<IFrameworkElement> gridElement;
    CHECK(grid && SUCCEEDED(grid.As(&gridElement)));
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"missing").Get(), &same)) && !same);
    ComPtr<IContentControl> content;
    CHECK(SUCCEEDED(object.As(&content)));
    if (!content) return 1;
    CHECK(SUCCEEDED(content->put_Content(panel.Get())));
    CHECK(SUCCEEDED(content->get_Content(&same)) && same.Get() == panel.Get());
    factory.Reset();
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.UserControl").Get(), &factory)));
    if (!factory) return 1;
    ComPtr<IUserControlFactory> userFactory;
    CHECK(SUCCEEDED(factory.As(&userFactory)));
    if (!userFactory) return 1;
    ComPtr<IUserControl> user;
    CHECK(SUCCEEDED(userFactory->CreateInstance(nullptr, nullptr, &user)));
    if (!user) return 1;
    ComPtr<IUIElement> panelElement;
    CHECK(SUCCEEDED(panel.As(&panelElement)));
    CHECK(SUCCEEDED(user->put_Content(panelElement.Get())));
    ComPtr<IUIElement> userContent;
    CHECK(SUCCEEDED(user->get_Content(&userContent)) && userContent.Get() == panelElement.Get());
    ComPtr<IControl> userControl;
    CHECK(SUCCEEDED(user.As(&userControl)));
    ComPtr<IUnknown> userIdentity, controlIdentity;
    CHECK(SUCCEEDED(user.As(&userIdentity)) && SUCCEEDED(userControl.As(&controlIdentity)) &&
          userIdentity.Get() == controlIdentity.Get());
    ComPtr<IFrameworkElement> userElement;
    CHECK(SUCCEEDED(user.As(&userElement)));
    CHECK(SUCCEEDED(user->put_Content(nullptr)));
    userContent.Reset();
    CHECK(SUCCEEDED(user->get_Content(&userContent)) && !userContent);
    printf("controltest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
