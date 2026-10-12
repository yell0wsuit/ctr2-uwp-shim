// IMediaElement's ABI still declares the deprecated PlayToSource property.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include "element.h"
#include "generated/controls.h"
#include "com/factory.h"
#include "registry.h"
#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.ui.xaml.markup.h>
#include <dxgi1_2.h>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Interop;
namespace ctr2 {
class Panel final : public Element<Panel, DefaultISwapChainPanel, ISwapChainPanelNative> {
    ComPtr<IDXGISwapChain> swapChain_;
    EventSource<ABI::Windows::Foundation::ITypedEventHandler<SwapChainPanel*, IInspectable*>> scaleChanged_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.SwapChainPanel";
    explicit Panel(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_CompositionScaleX(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CompositionScaleY(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    add_CompositionScaleChanged(ABI::Windows::Foundation::ITypedEventHandler<SwapChainPanel*, IInspectable*>* handler,
                                EventRegistrationToken* token) override {
        return scaleChanged_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_CompositionScaleChanged(EventRegistrationToken token) override {
        return scaleChanged_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* value) override {
        swapChain_ = value;
        if (value)
            SetPointerTarget(static_cast<IFrameworkElement*>(this));
        else
            ClearPointerTarget(static_cast<IFrameworkElement*>(this));
        Log("TRACE", "ISwapChainPanelNative::SetSwapChain");
        return S_OK;
    }
};
HRESULT MakePanel(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    ISwapChainPanel* panel = nullptr;
    auto hr = Compose<Panel>(nullptr, nullptr, &panel);
    if (FAILED(hr)) return hr;
    hr = panel->QueryInterface(IID_PPV_ARGS(value));
    panel->Release();
    return hr;
}
// MainPage's root Grid. InitializeComponent safe_casts FindName's result to
// Grid^, so a null or non-Grid object breaks the generated field binding.
class Grid final : public Element<Grid, DefaultIGrid, DefaultIPanel> {
public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Grid";
    explicit Grid(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Background(ABI::Windows::UI::Xaml::Media::IBrush** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsItemsHost(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
};
HRESULT MakeGrid(IInspectable** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    IGrid* grid = nullptr;
    auto hr = Compose<Grid>(nullptr, nullptr, &grid);
    if (FAILED(hr)) return hr;
    hr = grid->QueryInterface(IID_PPV_ARGS(value));
    grid->Release();
    return hr;
}
// Lets a Frame tell the shim Page it hosts which frame that is (Page.Frame).
// The Frame clears it before dropping the page; the page never owns the frame.
struct __declspec(uuid("ce92a3c3-5de3-4473-9528-b14b510702c0")) IHostedPage : public IUnknown {
    virtual void STDMETHODCALLTYPE SetHostFrame(IFrame* frame) = 0;
};
static void SetHostFrame(IInspectable* page, IFrame* frame) {
    ComPtr<IHostedPage> hosted;
    if (page && SUCCEEDED(page->QueryInterface(IID_PPV_ARGS(&hosted)))) hosted->SetHostFrame(frame);
}
class NavigationArgs final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, ABI::Windows::UI::Xaml::Navigation::INavigationEventArgs> {
    InspectableClass(L"Windows.UI.Xaml.Navigation.NavigationEventArgs", BaseTrust);
    ComPtr<IInspectable> content_, parameter_;
    Wrappers::HString typeName_;
    TypeKind typeKind_;
    ABI::Windows::UI::Xaml::Navigation::NavigationMode mode_;
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri_;

public:
    NavigationArgs(IInspectable* content, IInspectable* parameter, const TypeName& type,
                   ABI::Windows::UI::Xaml::Navigation::NavigationMode mode)
        : content_(content), parameter_(parameter), typeKind_(type.Kind), mode_(mode) {
        typeName_.Set(type.Name);
    }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_Parameter(IInspectable** value) override {
        if (!value) return E_POINTER;
        return parameter_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_SourcePageType(TypeName* value) override {
        if (!value) return E_POINTER;
        value->Kind = typeKind_;
        return typeName_.CopyTo(&value->Name);
    }
    HRESULT STDMETHODCALLTYPE get_NavigationMode(ABI::Windows::UI::Xaml::Navigation::NavigationMode* value) override {
        if (!value) return E_POINTER;
        *value = mode_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Uri(ABI::Windows::Foundation::IUriRuntimeClass** value) override {
        if (!value) return E_POINTER;
        return uri_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Uri(ABI::Windows::Foundation::IUriRuntimeClass* value) override {
        uri_ = value;
        return S_OK;
    }
};
class Page final
    : public Element<Page, DefaultIPage, DefaultIContentControl, DefaultIControl, DefaultIPageOverrides, IHostedPage> {
    ComPtr<IInspectable> content_;
    ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode cache_ =
        ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode_Disabled;
    IFrame* frame_ = nullptr;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Page";
    explicit Page(IInspectable* outer) : Element(outer) {}
    void STDMETHODCALLTYPE SetHostFrame(IFrame* frame) override { frame_ = frame; }
    HRESULT STDMETHODCALLTYPE get_Frame(IFrame** value) override {
        if (!value) return E_POINTER;
        *value = frame_;
        if (frame_) frame_->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    get_NavigationCacheMode(ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode* value) override {
        if (!value) return E_POINTER;
        *value = cache_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    put_NavigationCacheMode(ABI::Windows::UI::Xaml::Navigation::NavigationCacheMode value) override {
        cache_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        content_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigatedFrom(ABI::Windows::UI::Xaml::Navigation::INavigationEventArgs*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigatedTo(ABI::Windows::UI::Xaml::Navigation::INavigationEventArgs*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    OnNavigatingFrom(ABI::Windows::UI::Xaml::Navigation::INavigatingCancelEventArgs*) override {
        return S_OK;
    }
};
class UserControl final : public Element<UserControl, DefaultIUserControl, DefaultIControl> {
    ComPtr<IUIElement> content_;

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.UserControl";
    explicit UserControl(IInspectable* outer) : Element(outer) {}
    HRESULT STDMETHODCALLTYPE get_Content(IUIElement** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IUIElement* value) override {
        content_ = value;
        return S_OK;
    }
};
class Frame final : public Element<Frame, DefaultIFrame, DefaultIContentControl, DefaultIControl, INavigate> {
    struct Entry {
        ComPtr<IInspectable> page, parameter;
        Wrappers::HString typeName;
        TypeKind typeKind = TypeKind_Custom;
    };
    ComPtr<IInspectable> content_;
    Entry current_;
    // Back entries keep their page instances, as if every page used
    // NavigationCacheMode.Required: GoBack returns to the same MainPage, which
    // owns the game's swap chain panel, instead of constructing a new one.
    std::vector<Entry> back_;

    static HRESULT CallOverride(IInspectable* page, bool to, IInspectable* content, const Entry& entry,
                                ABI::Windows::UI::Xaml::Navigation::NavigationMode mode) {
        ComPtr<IPageOverrides> overrides;
        if (!page || FAILED(page->QueryInterface(IID_PPV_ARGS(&overrides)))) return S_OK;
        TypeName type{entry.typeName.Get(), entry.typeKind};
        auto args = Make<NavigationArgs>(content, entry.parameter.Get(), type, mode);
        if (!args) return E_OUTOFMEMORY;
        auto hr = to ? overrides->OnNavigatedTo(args.Get()) : overrides->OnNavigatedFrom(args.Get());
        if (FAILED(hr)) Log("FAIL", "Page OnNavigated%s hr=%08lx", to ? "To" : "From", hr);
        return hr;
    }
    // XAML order: the old page's OnNavigatedFrom, the content swap, then the new
    // page's OnNavigatedTo; Loaded follows from the UI queue. On a failed
    // OnNavigatedFrom nothing changes and `next` is left intact.
    HRESULT Show(Entry& next, ABI::Windows::UI::Xaml::Navigation::NavigationMode mode) {
        auto hr = CallOverride(content_.Get(), false, next.page.Get(), next, mode);
        if (FAILED(hr)) return hr;
        if (content_) {
            if (mode == ABI::Windows::UI::Xaml::Navigation::NavigationMode_Back)
                SetHostFrame(content_.Get(), nullptr); // Dropped from the frame.
            else
                back_.push_back(std::move(current_));
        }
        content_ = next.page;
        current_ = std::move(next);
        SetHostFrame(content_.Get(), this);
        hr = CallOverride(content_.Get(), true, content_.Get(), current_, mode);
        if (FAILED(hr)) return hr;
        return IsWindowVisible(HostWindow()) ? QueueElementLoaded(content_.Get()) : S_OK;
    }

public:
    static constexpr const wchar_t* Name = L"Windows.UI.Xaml.Controls.Frame";
    explicit Frame(IInspectable* outer) : Element(outer) {}
    ~Frame() {
        SetHostFrame(content_.Get(), nullptr);
        for (auto& entry : back_) SetHostFrame(entry.page.Get(), nullptr);
    }
    HRESULT STDMETHODCALLTYPE get_Content(IInspectable** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Content(IInspectable* value) override {
        content_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanGoBack(boolean* value) override {
        if (!value) return E_POINTER;
        *value = !back_.empty();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BackStackDepth(INT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<INT32>(back_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GoBack() override {
        if (back_.empty()) return E_ILLEGAL_METHOD_CALL;
        Log("TRACE", "Frame::GoBack(%ls)", back_.back().typeName.GetRawBuffer(nullptr));
        auto entry = std::move(back_.back());
        back_.pop_back();
        auto hr = Show(entry, ABI::Windows::UI::Xaml::Navigation::NavigationMode_Back);
        if (entry.page) back_.push_back(std::move(entry)); // OnNavigatedFrom refused; restore.
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_CanGoForward(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Navigate(TypeName type, boolean* result) override {
        return Navigate(type, nullptr, result);
    }
    HRESULT STDMETHODCALLTYPE Navigate(TypeName type, IInspectable* parameter, boolean* result) override {
        if (!result) return E_POINTER;
        *result = false;
        Log("TRACE", "Frame::Navigate(%ls)", WindowsGetStringRawBuffer(type.Name, nullptr));
        ComPtr<IApplication> app;
        auto hr = CurrentApplication(&app);
        if (FAILED(hr) || !app) return E_UNEXPECTED;
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlMetadataProvider> provider;
        hr = app.As(&provider);
        if (FAILED(hr)) return hr;
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlType> xamlType;
        hr = provider->GetXamlType(type, &xamlType);
        if (FAILED(hr)) return hr;
        if (!xamlType) return TYPE_E_ELEMENTNOTFOUND;
        Log("TRACE", "Frame activating page");
        ComPtr<IInspectable> page;
        hr = xamlType->ActivateInstance(&page);
        Log("TRACE", "Frame page activation returned %08lx", hr);
        if (FAILED(hr)) return hr;
        if (!page) return E_UNEXPECTED;
        Entry next;
        next.page = std::move(page);
        next.parameter = parameter;
        next.typeName.Set(type.Name);
        next.typeKind = type.Kind;
        hr = Show(next, ABI::Windows::UI::Xaml::Navigation::NavigationMode_New);
        if (FAILED(hr)) return hr;
        *result = true;
        return S_OK;
    }
};
HRESULT DeliverElementLoaded(IInspectable* object) {
    ComPtr<IFrameworkElement> element;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&element)))) {
        auto found = LoadedCallbacks().find(element.Get());
        if (found != LoadedCallbacks().end()) {
            auto hr = found->second();
            if (FAILED(hr)) return hr;
        }
    }
    ComPtr<IContentControl> content;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&content)))) {
        ComPtr<IInspectable> child;
        auto hr = content->get_Content(&child);
        if (FAILED(hr)) return hr;
        return child ? DeliverElementLoaded(child.Get()) : S_OK;
    }
    ComPtr<IUserControl> user;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&user)))) {
        ComPtr<IUIElement> child;
        auto hr = user->get_Content(&child);
        if (FAILED(hr)) return hr;
        if (child) return DeliverElementLoaded(child.Get());
    }
    return S_OK;
}
HRESULT RaiseElementSizeChanged() {
    // Handlers may create or destroy elements; re-find each key before calling.
    std::vector<IFrameworkElement*> keys;
    for (auto& item : SizeCallbacks()) keys.push_back(item.first);
    for (auto* key : keys) {
        auto found = SizeCallbacks().find(key);
        if (found == SizeCallbacks().end()) continue;
        auto callback = found->second;
        auto hr = callback();
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
HRESULT QueueElementLoaded(IInspectable* value) {
    if (!value) return S_OK;
    ComPtr<IInspectable> retained(value);
    return PostHostWork(0, [retained] {
        auto hr = DeliverElementLoaded(retained.Get());
        if (FAILED(hr)) Log("FAIL", "Loaded handler hr=%08lx", hr);
    });
}
class FrameFactory final : public Factory<DefaultIFrameFactory> {
public:
    FrameFactory() : Factory(L"Windows.UI.Xaml.Controls.Frame") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IFrame** value) override {
        return Compose<Frame>(outer, inner, value);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<IFrame> frame;
        auto hr = Compose<Frame>(nullptr, nullptr, frame.GetAddressOf());
        return FAILED(hr) ? hr : frame.CopyTo(value);
    }
};
class PageFactory final : public Factory<DefaultIPageFactory> {
public:
    PageFactory() : Factory(L"Windows.UI.Xaml.Controls.Page") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IPage** value) override {
        return Compose<Page>(outer, inner, value);
    }
};
class UserControlFactory final : public Factory<DefaultIUserControlFactory> {
public:
    UserControlFactory() : Factory(L"Windows.UI.Xaml.Controls.UserControl") {}
    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner, IUserControl** value) override {
        return Compose<UserControl>(outer, inner, value);
    }
};
HRESULT MakeControlFactory(const wchar_t* name, IActivationFactory** value) {
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.UserControl") == 0) {
        auto factory = Make<UserControlFactory>();
        return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
    }
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Frame") == 0) {
        auto factory = Make<FrameFactory>();
        return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
    }
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Page") == 0) {
        auto factory = Make<PageFactory>();
        return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
}
