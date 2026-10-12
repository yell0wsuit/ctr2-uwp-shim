// Frame navigation, MediaElement, and DispatcherTimer as the intro-movie page
// uses them: Navigate(MovieMgrPage) calls the page's OnNavigatedTo, a 3-second
// DispatcherTimer ticks on the UI thread, and the page returns with GoBack.
#include <windows.h>
#include <roapi.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.navigation.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Interop;
using namespace ABI::Windows::UI::Xaml::Markup;
using namespace ABI::Windows::UI::Xaml::Navigation;
static int failures = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
using GetFactory = HRESULT(WINAPI*)(HSTRING, IActivationFactory**);
static GetFactory get;
// Page type names here are ASCII.
static std::string Narrow(HSTRING text) {
    std::string result;
    for (const wchar_t* c = WindowsGetStringRawBuffer(text, nullptr); *c; ++c) result += static_cast<char>(*c);
    return result;
}
static std::vector<std::string> events;
static ComPtr<IInspectable> lastParameter;
// Aggregates a shim Page, as a C++/CX page class does, and records overrides.
class PageOuter final : public IPageOverrides, public IComponentConnector {
    std::atomic<ULONG> refs_{1};
    std::string name_;

public:
    ComPtr<IInspectable> inner;
    int connections = 0;
    explicit PageOuter(std::string name) : name_(std::move(name)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(IPageOverrides)) {
            *out = static_cast<IPageOverrides*>(this);
            AddRef();
            return S_OK;
        }
        if (iid == __uuidof(IComponentConnector)) {
            *out = static_cast<IComponentConnector*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(iid, out) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** ids) override {
        *count = 0;
        *ids = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT Record(const char* what, INavigationEventArgs* args) {
        if (!args) return E_POINTER;
        NavigationMode mode{};
        args->get_NavigationMode(&mode);
        TypeName type{};
        args->get_SourcePageType(&type);
        auto source = Narrow(type.Name);
        WindowsDeleteString(type.Name);
        events.push_back(name_ + ":" + what + (mode == NavigationMode_Back ? ":back:" : ":new:") + source);
        lastParameter.Reset();
        args->get_Parameter(&lastParameter);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigatedFrom(INavigationEventArgs* args) override { return Record("from", args); }
    HRESULT STDMETHODCALLTYPE OnNavigatedTo(INavigationEventArgs* args) override { return Record("to", args); }
    HRESULT STDMETHODCALLTYPE OnNavigatingFrom(INavigatingCancelEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Connect(INT32 id, IInspectable* target) override {
        CHECK(id == 1 && target);
        ComPtr<IMediaElement> media;
        CHECK(SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&media))));
        ComPtr<IFrameworkElement> element;
        CHECK(SUCCEEDED(inner.As(&element)));
        ComPtr<IInspectable> named;
        CHECK(SUCCEEDED(element->FindName(HStringReference(L"mediaElement").Get(), &named)));
        CHECK(named.Get() == target);
        ++connections;
        return S_OK;
    }
};
static std::vector<PageOuter*> pages;
class XamlType final : public RuntimeClass<RuntimeClassFlags<WinRt>, IXamlType> {
    std::string name_;

public:
    explicit XamlType(std::string name) : name_(std::move(name)) {}
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** result) override {
        ComPtr<IActivationFactory> factory;
        auto hr = get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &factory);
        ComPtr<IPageFactory> pageFactory;
        if (SUCCEEDED(hr)) hr = factory.As(&pageFactory);
        if (FAILED(hr)) return hr;
        auto* outer = new PageOuter(name_);
        ComPtr<IPage> page;
        hr = pageFactory->CreateInstance(static_cast<IPageOverrides*>(outer), &outer->inner, &page);
        if (FAILED(hr)) {
            outer->Release();
            return hr;
        }
        pages.push_back(outer);
        *result = static_cast<IPageOverrides*>(outer); // Transfers the creation reference.
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BaseType(IXamlType**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_ContentProperty(IXamlMember**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_FullName(HSTRING*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IsArray(boolean*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IsCollection(boolean*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IsConstructible(boolean*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IsDictionary(boolean*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IsMarkupExtension(boolean*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IsBindable(boolean*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_ItemType(IXamlType**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_KeyType(IXamlType**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_UnderlyingType(TypeName*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateFromString(HSTRING, IInspectable**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMember(HSTRING, IXamlMember**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddToVector(IInspectable*, IInspectable*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AddToMap(IInspectable*, IInspectable*, IInspectable*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RunInitializer() override { return E_NOTIMPL; }
};
// The game's App aggregates Application and provides XAML type metadata.
class AppOuter final : public IXamlMetadataProvider {
    std::atomic<ULONG> refs_{1};

public:
    ComPtr<IInspectable> inner;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IInspectable) || iid == __uuidof(IXamlMetadataProvider)) {
            *out = static_cast<IXamlMetadataProvider*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(iid, out) : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** ids) override {
        *count = 0;
        *ids = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) override {
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) override {
        *value = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetXamlType(TypeName type, IXamlType** result) override {
        auto xamlType = Make<XamlType>(Narrow(type.Name));
        return xamlType.CopyTo(result);
    }
    HRESULT STDMETHODCALLTYPE GetXamlTypeByFullName(HSTRING, IXamlType**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetXmlnsDefinitions(UINT32*, XmlnsDefinition**) override { return E_NOTIMPL; }
};
static ComPtr<IFrame> FrameOf(IInspectable* page) {
    ComPtr<IPage> asPage;
    ComPtr<IFrame> frame;
    if (page && SUCCEEDED(page->QueryInterface(IID_PPV_ARGS(&asPage)))) asPage->get_Frame(&frame);
    return frame;
}
static void TestNavigation() {
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Frame").Get(), &factory)));
    ComPtr<IInspectable> object;
    CHECK(factory && SUCCEEDED(factory->ActivateInstance(&object)));
    ComPtr<IFrame> frame;
    ComPtr<INavigate> navigate;
    ComPtr<IContentControl> content;
    if (!object || FAILED(object.As(&frame)) || FAILED(object.As(&navigate)) || FAILED(object.As(&content))) {
        CHECK(false);
        return;
    }
    boolean ok = false, canGoBack = true;
    CHECK(SUCCEEDED(navigate->Navigate(TypeName{HStringReference(L"MainPage").Get(), TypeKind_Custom}, &ok)) && ok);
    CHECK((events == std::vector<std::string>{"MainPage:to:new:MainPage"}));
    ComPtr<IInspectable> mainPage;
    CHECK(SUCCEEDED(content->get_Content(&mainPage)) && mainPage);
    CHECK(FrameOf(mainPage.Get()).Get() == frame.Get());
    CHECK(SUCCEEDED(frame->get_CanGoBack(&canGoBack)) && !canGoBack);
    CHECK(frame->GoBack() == E_ILLEGAL_METHOD_CALL);

    events.clear();
    ComPtr<ABI::Windows::Foundation::IPropertyValueStatics> values;
    CHECK(SUCCEEDED(
        RoGetActivationFactory(HStringReference(L"Windows.Foundation.PropertyValue").Get(), IID_PPV_ARGS(&values))));
    ComPtr<IInspectable> parameter;
    if (values) values->CreateString(HStringReference(L"intro").Get(), &parameter);
    CHECK(parameter);
    ok = false;
    CHECK(SUCCEEDED(frame->Navigate(TypeName{HStringReference(L"MovieMgrPage").Get(), TypeKind_Custom}, parameter.Get(),
                                    &ok)) &&
          ok);
    CHECK((events == std::vector<std::string>{"MainPage:from:new:MovieMgrPage", "MovieMgrPage:to:new:MovieMgrPage"}));
    CHECK(lastParameter.Get() == parameter.Get());
    ComPtr<IInspectable> moviePage;
    CHECK(SUCCEEDED(content->get_Content(&moviePage)) && moviePage && moviePage.Get() != mainPage.Get());
    CHECK(FrameOf(moviePage.Get()).Get() == frame.Get());
    INT32 depth = 0;
    CHECK(SUCCEEDED(frame->get_CanGoBack(&canGoBack)) && canGoBack);
    CHECK(SUCCEEDED(frame->get_BackStackDepth(&depth)) && depth == 1);

    events.clear();
    CHECK(SUCCEEDED(frame->GoBack()));
    CHECK((events == std::vector<std::string>{"MovieMgrPage:from:back:MainPage", "MainPage:to:back:MainPage"}));
    ComPtr<IInspectable> restored;
    CHECK(SUCCEEDED(content->get_Content(&restored)) && restored.Get() == mainPage.Get());
    CHECK(!FrameOf(moviePage.Get()));
    CHECK(FrameOf(mainPage.Get()).Get() == frame.Get());
    CHECK(SUCCEEDED(frame->get_CanGoBack(&canGoBack)) && !canGoBack);
    CHECK(SUCCEEDED(frame->get_BackStackDepth(&depth)) && depth == 0);
    restored.Reset();
    moviePage.Reset();
    mainPage.Reset();
}
static void TestMediaElement() {
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &factory)));
    ComPtr<IPageFactory> pageFactory;
    if (!factory || FAILED(factory.As(&pageFactory))) {
        CHECK(false);
        return;
    }
    ComPtr<IPage> page;
    CHECK(SUCCEEDED(pageFactory->CreateInstance(nullptr, nullptr, &page)));
    ComPtr<IFrameworkElement> element;
    if (!page || FAILED(page.As(&element))) {
        CHECK(false);
        return;
    }
    // MovieMgrPage.InitializeComponent safe_casts this name to MediaElement^.
    ComPtr<IInspectable> object, same;
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"mediaElement").Get(), &object)) && object);
    CHECK(SUCCEEDED(element->FindName(HStringReference(L"mediaElement").Get(), &same)) && same.Get() == object.Get());
    // LoadComponent must run the compiled XAML connector; binding the named
    // field alone leaves the game's startup watchdog armed during playback.
    auto* outer = new PageOuter("MovieMgrPage");
    ComPtr<IPage> movie;
    CHECK(SUCCEEDED(pageFactory->CreateInstance(static_cast<IPageOverrides*>(outer), &outer->inner, &movie)));
    ComPtr<IActivationFactory> appFactory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &appFactory)));
    ComPtr<IApplicationStatics> appStatics;
    CHECK(SUCCEEDED(appFactory.As(&appStatics)));
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> componentUris;
    CHECK(SUCCEEDED(
        RoGetActivationFactory(HStringReference(L"Windows.Foundation.Uri").Get(), IID_PPV_ARGS(&componentUris))));
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> componentUri;
    CHECK(SUCCEEDED(componentUris->CreateUri(HStringReference(L"ms-appx:///MovieMgrPage.xaml").Get(), &componentUri)));
    CHECK(SUCCEEDED(appStatics->LoadComponent(movie.Get(), componentUri.Get())));
    CHECK(outer->connections == 1);
    movie.Reset();
    outer->Release();
    ComPtr<IMediaElement> media;
    ComPtr<IMediaElement2> media2;
    if (!object || FAILED(object.As(&media)) || FAILED(object.As(&media2))) {
        CHECK(false);
        return;
    }
    ComPtr<IFrameworkElement> mediaElement;
    CHECK(SUCCEEDED(object.As(&mediaElement)));
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> uris;
    CHECK(SUCCEEDED(RoGetActivationFactory(HStringReference(L"Windows.Foundation.Uri").Get(), IID_PPV_ARGS(&uris))));
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri, source;
    if (uris) uris->CreateUri(HStringReference(L"ms-appx:///Assets/assets/intro.mp4").Get(), &uri);
    CHECK(uri);
    CHECK(SUCCEEDED(media->put_AutoPlay(true)));
    CHECK(SUCCEEDED(media2->put_Stretch(ABI::Windows::UI::Xaml::Media::Stretch_UniformToFill)));
    CHECK(SUCCEEDED(media->put_Source(uri.Get())));
    CHECK(SUCCEEDED(media->get_Source(&source)) && source.Get() == uri.Get());
    ABI::Windows::UI::Xaml::Media::Stretch stretch{};
    CHECK(SUCCEEDED(media2->get_Stretch(&stretch)) && stretch == ABI::Windows::UI::Xaml::Media::Stretch_UniformToFill);
    boolean autoPlay = false;
    CHECK(SUCCEEDED(media->get_AutoPlay(&autoPlay)) && autoPlay);
    CHECK(SUCCEEDED(media->Play()));
    CHECK(SUCCEEDED(media->Stop()));
    CHECK(SUCCEEDED(media->put_AutoPlay(false)));
}
static int ticks = 0;
static DWORD uiThread = 0;
static void StartTimer() {
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.DispatcherTimer").Get(), &factory)));
    ComPtr<IDispatcherTimerFactory> timers;
    if (!factory || FAILED(factory.As(&timers))) {
        CHECK(false);
        PostQuitMessage(0);
        return;
    }
    ComPtr<IInspectable> activated;
    CHECK(SUCCEEDED(factory->ActivateInstance(&activated)) && activated);
    // MovieMgrPage creates its timer through the composable factory with no outer.
    ComPtr<IDispatcherTimer> timer;
    CHECK(SUCCEEDED(timers->CreateInstance(nullptr, nullptr, &timer)) && timer);
    if (!timer) {
        PostQuitMessage(0);
        return;
    }
    ABI::Windows::Foundation::TimeSpan interval{100000}; // 10 ms
    CHECK(SUCCEEDED(timer->put_Interval(interval)));
    CHECK(timer->put_Interval(ABI::Windows::Foundation::TimeSpan{-1}) == E_INVALIDARG);
    ABI::Windows::Foundation::TimeSpan read{};
    CHECK(SUCCEEDED(timer->get_Interval(&read)) && read.Duration == interval.Duration);
    auto tick = Callback<ABI::Windows::Foundation::IEventHandler<IInspectable*>>(
        [](IInspectable* sender, IInspectable*) -> HRESULT {
            CHECK(GetCurrentThreadId() == uiThread);
            ComPtr<IDispatcherTimer> self;
            CHECK(sender && SUCCEEDED(sender->QueryInterface(IID_PPV_ARGS(&self))));
            if (++ticks == 2 && self) {
                // The page's Tick handler stops the timer and navigates back.
                CHECK(SUCCEEDED(self->Stop()));
                boolean enabled = true;
                CHECK(SUCCEEDED(self->get_IsEnabled(&enabled)) && !enabled);
                PostQuitMessage(0);
            }
            return S_OK;
        });
    EventRegistrationToken token{};
    CHECK(SUCCEEDED(timer->add_Tick(tick.Get(), &token)));
    boolean enabled = true;
    CHECK(SUCCEEDED(timer->get_IsEnabled(&enabled)) && !enabled);
    HRESULT otherThread = S_OK;
    std::thread([&] { otherThread = timer->Start(); }).join();
    CHECK(otherThread == RPC_E_WRONG_THREAD);
    CHECK(SUCCEEDED(timer->Start()));
    CHECK(SUCCEEDED(timer->get_IsEnabled(&enabled)) && enabled);
    // A started timer stays alive without outside references until it stops.
    timer.Reset();
}
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    CHECK(dll);
    if (!dll) return 1;
    get = reinterpret_cast<GetFactory>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory)));
    ComPtr<IApplicationFactory> compose;
    ComPtr<IApplicationStatics> statics;
    if (!factory || FAILED(factory.As(&compose)) || FAILED(factory.As(&statics))) return 1;
    auto* appOuter = new AppOuter;
    ComPtr<IApplication> app;
    auto callback = Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) {
        uiThread = GetCurrentThreadId();
        CHECK(SUCCEEDED(compose->CreateInstance(appOuter, &appOuter->inner, &app)));
        TestNavigation();
        TestMediaElement();
        StartTimer();
        return S_OK;
    });
    CHECK(SUCCEEDED(statics->Start(callback.Get())));
    CHECK(ticks == 2);
    printf("navtest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
