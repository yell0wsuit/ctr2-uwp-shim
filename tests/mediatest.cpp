// Real asynchronous playback through the desktop host, including failure,
// cancellation, replay, UI-thread event delivery, and natural completion.
#include <windows.h>
#include <roapi.h>
#include <shlwapi.h>
#undef GetCurrentTime
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <string>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
using namespace ABI::Windows::UI::Xaml::Media;
static int failures;
static HWND TestWindow() {
    HWND result = nullptr;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM context) -> BOOL {
            wchar_t name[80]{};
            GetClassNameW(window, name, ARRAYSIZE(name));
            if (wcscmp(name, L"CTR2ShimWindow") != 0) return TRUE;
            *reinterpret_cast<HWND*>(context) = window;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
static void CALLBACK Timeout(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    CHECK(false);
    PostQuitMessage(0);
}
int wmain(int argc, wchar_t** argv) {
    RoInitialize(RO_INIT_MULTITHREADED);
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    std::wstring folder(executable);
    folder.resize(folder.find_last_of(L"\\/") + 1);
    CreateDirectoryW((folder + L"Assets").c_str(), nullptr);
    // A short silent PCM file needs no external assets or optional codec.
    const std::wstring fixture = folder + L"Assets\\movie test.wav";
    FILE* wav = nullptr;
    _wfopen_s(&wav, fixture.c_str(), L"wb");
    if (!wav) return 1;
    const DWORD samples = 4000, dataBytes = samples * 2, riffBytes = 36 + dataBytes;
    const DWORD fmtBytes = 16, rate = 8000, byteRate = rate * 2;
    const WORD pcm = 1, channels = 1, alignment = 2, bits = 16;
    fwrite("RIFF", 4, 1, wav);
    fwrite(&riffBytes, 4, 1, wav);
    fwrite("WAVEfmt ", 8, 1, wav);
    fwrite(&fmtBytes, 4, 1, wav);
    fwrite(&pcm, 2, 1, wav);
    fwrite(&channels, 2, 1, wav);
    fwrite(&rate, 4, 1, wav);
    fwrite(&byteRate, 4, 1, wav);
    fwrite(&alignment, 2, 1, wav);
    fwrite(&bits, 2, 1, wav);
    fwrite("data", 4, 1, wav);
    fwrite(&dataBytes, 4, 1, wav);
    for (DWORD i = 0; i < samples; ++i) {
        WORD zero = 0;
        fwrite(&zero, 2, 1, wav);
    }
    fclose(wav);
    std::wstring url = L"ms-appx:///Assets/movie%20test.wav";
    if (argc > 1) {
        wchar_t absolute[32768]{}, encoded[32768]{};
        GetFullPathNameW(argv[1], ARRAYSIZE(absolute), absolute, nullptr);
        DWORD length = ARRAYSIZE(encoded);
        if (FAILED(UrlCreateFromPathW(absolute, encoded, &length, 0))) return 1;
        url = encoded;
    }
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Application").Get(), &factory)));
    ComPtr<IApplicationStatics> statics;
    ComPtr<IApplicationFactory> applications;
    CHECK(SUCCEEDED(factory.As(&statics)));
    CHECK(SUCCEEDED(factory.As(&applications)));
    ComPtr<IApplication> app;
    ComPtr<IMediaElement> media;
    ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> good, missing;
    DWORD uiThread = 0;
    int opened = 0, ended = 0, failed = 0, clicks = 0;
    UINT_PTR timeout = 0;
    auto initialize =
        Callback<IApplicationInitializationCallback>([&](IApplicationInitializationCallbackParams*) -> HRESULT {
            uiThread = GetCurrentThreadId();
            CHECK(SUCCEEDED(applications->CreateInstance(nullptr, nullptr, &app)));
            ComPtr<IActivationFactory> pages;
            CHECK(SUCCEEDED(get(HStringReference(L"Windows.UI.Xaml.Controls.Page").Get(), &pages)));
            ComPtr<IPageFactory> pageFactory;
            CHECK(SUCCEEDED(pages.As(&pageFactory)));
            ComPtr<IPage> page;
            CHECK(SUCCEEDED(pageFactory->CreateInstance(nullptr, nullptr, &page)));
            ComPtr<IFrameworkElement> element;
            CHECK(SUCCEEDED(page.As(&element)));
            ComPtr<IInspectable> object;
            CHECK(SUCCEEDED(element->FindName(HStringReference(L"mediaElement").Get(), &object)));
            CHECK(SUCCEEDED(object.As(&media)));
            ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> uris;
            CHECK(SUCCEEDED(
                RoGetActivationFactory(HStringReference(L"Windows.Foundation.Uri").Get(), IID_PPV_ARGS(&uris))));
            CHECK(SUCCEEDED(uris->CreateUri(HStringReference(url.c_str()).Get(), &good)));
            CHECK(
                SUCCEEDED(uris->CreateUri(HStringReference(L"ms-appx:///Assets/does-not-exist.mp4").Get(), &missing)));
            auto onOpened = Callback<IRoutedEventHandler>([&](IInspectable* sender, IRoutedEventArgs* args) -> HRESULT {
                CHECK(GetCurrentThreadId() == uiThread && sender && args);
                ++opened;
                MediaElementState state{};
                CHECK(SUCCEEDED(media->get_CurrentState(&state)));
                if (opened == 1) {
                    CHECK(state == MediaElementState_Playing);
                    CHECK(SUCCEEDED(media->Pause()));
                    CHECK(SUCCEEDED(media->get_CurrentState(&state)) && state == MediaElementState_Paused);
                    CHECK(SUCCEEDED(media->Play()));
                    SendMessageW(TestWindow(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
                    CHECK(clicks == 1);
                    CHECK(SUCCEEDED(media->get_CurrentState(&state)) && state == MediaElementState_Stopped);
                    CHECK(SUCCEEDED(media->put_AutoPlay(false)));
                    CHECK(SUCCEEDED(media->put_Source(good.Get())));
                } else {
                    CHECK(opened == 2 && state == MediaElementState_Stopped);
                    CHECK(SUCCEEDED(media->Play()));
                }
                return S_OK;
            });
            auto onFailed = Callback<IExceptionRoutedEventHandler>(
                [&](IInspectable* sender, IExceptionRoutedEventArgs* args) -> HRESULT {
                    CHECK(GetCurrentThreadId() == uiThread && sender && args);
                    ++failed;
                    CHECK(failed == 1 && opened == 0);
                    HString message;
                    CHECK(SUCCEEDED(args->get_ErrorMessage(message.GetAddressOf())) && message.Length() > 0);
                    if (failed == 1)
                        CHECK(SUCCEEDED(media->put_Source(good.Get())));
                    else
                        PostQuitMessage(0);
                    return S_OK;
                });
            auto onEnded = Callback<IRoutedEventHandler>([&](IInspectable* sender, IRoutedEventArgs* args) -> HRESULT {
                CHECK(GetCurrentThreadId() == uiThread && sender && args);
                ++ended;
                CHECK(opened == 2 && failed == 1);
                CHECK(SUCCEEDED(media->Stop()));
                CHECK(SUCCEEDED(media->put_Source(nullptr)));
                media.Reset(); // Callback delivery must survive release by a handler.
                KillTimer(nullptr, timeout);
                PostQuitMessage(0);
                return S_OK;
            });
            EventRegistrationToken token{};
            ComPtr<IUIElement> input;
            CHECK(SUCCEEDED(media.As(&input)));
            auto onClick = Callback<ABI::Windows::UI::Xaml::Input::IPointerEventHandler>(
                [&](IInspectable*, ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*) -> HRESULT {
                    ++clicks;
                    return media->Stop();
                });
            CHECK(SUCCEEDED(input->add_PointerPressed(onClick.Get(), &token)));
            CHECK(SUCCEEDED(media->add_MediaOpened(onOpened.Get(), &token)));
            CHECK(SUCCEEDED(media->add_MediaEnded(onEnded.Get(), &token)));
            CHECK(SUCCEEDED(media->add_MediaFailed(onFailed.Get(), &token)));
            // Cancel an open before the UI queue runs, then replace the source.
            CHECK(SUCCEEDED(media->put_Source(good.Get())));
            CHECK(SUCCEEDED(media->Stop()));
            CHECK(SUCCEEDED(media->put_Source(missing.Get())));
            timeout = SetTimer(nullptr, 0, argc > 1 ? 120000 : 15000, Timeout);
            ShowWindow(TestWindow(), SW_SHOW);
            return S_OK;
        });
    CHECK(SUCCEEDED(statics->Start(initialize.Get())));
    CHECK(opened == 2 && ended == 1 && failed == 1 && clicks == 1);
    printf("mediatest: opened=%d ended=%d failed=%d; %d failure(s)\n", opened, ended, failed, failures);
    return failures ? 1 : 0;
}
