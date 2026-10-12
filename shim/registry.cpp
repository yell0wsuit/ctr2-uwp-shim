#include "registry.h"
#include "log.h"
#include "version.h"
#include <winstring.h>
#include <new>

extern "C" HRESULT WINAPI ShimGetFactory(HSTRING name, IActivationFactory** factory) noexcept {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    const wchar_t* text = WindowsGetStringRawBuffer(name, nullptr);
    try {
        if (wcscmp(text, L"Windows.Globalization.Calendar") == 0) return ctr2::MakeCalendarFactory(factory);
        if (wcscmp(text, L"Windows.Foundation.Diagnostics.AsyncCausalityTracer") == 0)
            return ctr2::MakeCausalityFactory(factory);
        if (wcscmp(text, L"Windows.UI.Notifications.ToastNotificationManager") == 0)
            return ctr2::MakeToastFactory(factory);
        if (wcscmp(text, L"Windows.UI.Notifications.ScheduledToastNotification") == 0)
            return ctr2::MakeScheduledToastFactory(factory);
        if (wcscmp(text, L"Windows.System.Display.DisplayRequest") == 0) return ctr2::MakeDeviceFactory(true, factory);
        if (wcscmp(text, L"Windows.Security.ExchangeActiveSyncProvisioning.EasClientDeviceInformation") == 0)
            return ctr2::MakeDeviceFactory(false, factory);
        if (wcscmp(text, L"Windows.ApplicationModel.Store.CurrentApp") == 0) return ctr2::MakeStoreFactory(factory);
        if (wcscmp(text, L"Windows.Storage.Streams.DataReader") == 0) return ctr2::MakeDataReaderFactory(factory);
        if (wcscmp(text, L"Windows.UI.ViewManagement.ApplicationView") == 0)
            return ctr2::MakeApplicationViewFactory(factory);
        if (wcscmp(text, L"Windows.System.Profile.HardwareIdentification") == 0)
            return ctr2::MakeHardwareFactory(factory);
        if (wcscmp(text, L"Windows.Networking.Connectivity.NetworkInformation") == 0)
            return ctr2::MakeNetworkFactory(factory);
        if (wcscmp(text, L"Windows.Web.Http.Filters.HttpBaseProtocolFilter") == 0)
            return ctr2::MakeHttpFactory(true, factory);
        if (wcscmp(text, L"Windows.Web.Http.HttpClient") == 0) return ctr2::MakeHttpFactory(false, factory);
        if (wcscmp(text, L"VungleSDK.AdFactory") == 0) return ctr2::MakeAdFactory(factory);
        if (wcscmp(text, L"Windows.Storage.ApplicationData") == 0) return ctr2::MakeStorageFactory(factory);
        if (wcscmp(text, L"Windows.UI.Input.PointerVisualizationSettings") == 0)
            return ctr2::MakeVisualizationFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Window") == 0) return ctr2::MakeWindowFactory(factory);
        if (wcscmp(text, L"Windows.Graphics.Display.DisplayInformation") == 0 ||
            wcscmp(text, L"Windows.Graphics.Display.DisplayProperties") == 0)
            return ctr2::MakeDisplayFactory(factory);
        if (wcscmp(text, L"Windows.ApplicationModel.Core.CoreApplication") == 0)
            return ctr2::MakeCoreApplicationFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Controls.Frame") == 0 ||
            wcscmp(text, L"Windows.UI.Xaml.Controls.Page") == 0 ||
            wcscmp(text, L"Windows.UI.Xaml.Controls.UserControl") == 0)
            return ctr2::MakeControlFactory(text, factory);
        if (wcscmp(text, L"Windows.UI.Xaml.Application") == 0) return ctr2::MakeApplicationFactory(factory);
        if (wcscmp(text, L"Windows.UI.Xaml.DispatcherTimer") == 0) return ctr2::MakeDispatcherTimerFactory(factory);
        if (wcscmp(text, L"Windows.UI.Core.CoreCursor") == 0) return ctr2::MakeCursorFactory(factory);
        if (wcscmp(text, L"Windows.System.Threading.ThreadPoolTimer") == 0)
            return ctr2::MakeThreadPoolFactory(true, factory);
        if (wcscmp(text, L"Windows.System.Threading.ThreadPool") == 0)
            return ctr2::MakeThreadPoolFactory(false, factory);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllGetActivationFactory(HSTRING name, IActivationFactory** value) noexcept {
    return ShimGetFactory(name, value);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
extern "C" void __cdecl ShimBootstrap() noexcept {}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Opens each run's log and names the build, so a log shows which DLL ran.
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        ctr2::Log("INFO", "==== ctr2shim attached version=%s commit=%s pid=%lu exe=%ls ====", CTR2SHIM_VERSION,
                  CTR2SHIM_COMMIT, GetCurrentProcessId(), path);
        ctr2::PatchActivationImports();
    }
    return TRUE;
}
