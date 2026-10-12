#include <windows.h>
#include <roapi.h>
#include <windows.graphics.display.h>
#include <windows.applicationmodel.core.h>
#include <windows.ui.input.h>
#include <windows.foundation.diagnostics.h>
#include <windows.globalization.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Graphics::Display;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.Graphics.Display.DisplayInformation").Get(), &factory))) {
        puts("FAIL display factory");
        return 1;
    }
    ComPtr<IDisplayInformationStatics> statics;
    if (FAILED(factory.As(&statics))) return 2;
    ComPtr<IDisplayInformation> info;
    if (FAILED(statics->GetForCurrentView(&info))) return 3;
    FLOAT dpi = 0;
    if (FAILED(info->get_LogicalDpi(&dpi)) || dpi != 96) return 4;
    DisplayOrientations orientation{};
    if (FAILED(info->get_CurrentOrientation(&orientation)) || orientation != DisplayOrientations_Landscape) return 5;
    ResolutionScale scale{};
    if (FAILED(info->get_ResolutionScale(&scale)) || scale != ResolutionScale_Scale100Percent) return 6;
    if (FAILED(get(HStringReference(L"Windows.ApplicationModel.Core.CoreApplication").Get(), &factory))) return 7;
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplication> core;
    if (FAILED(factory.As(&core))) return 8;
    HString id;
    if (FAILED(core->get_Id(id.GetAddressOf())) || id.Length() == 0) return 9;
    if (FAILED(get(HStringReference(L"Windows.UI.Input.PointerVisualizationSettings").Get(), &factory))) return 10;
    ComPtr<ABI::Windows::UI::Input::IPointerVisualizationSettingsStatics> visualStatics;
    if (FAILED(factory.As(&visualStatics))) return 11;
    ComPtr<ABI::Windows::UI::Input::IPointerVisualizationSettings> visual;
    if (FAILED(visualStatics->GetForCurrentView(&visual))) return 12;
    boolean feedback = true;
    if (FAILED(visual->put_IsContactFeedbackEnabled(false)) ||
        FAILED(visual->get_IsContactFeedbackEnabled(&feedback)) || feedback)
        return 13;
    if (FAILED(get(HStringReference(L"Windows.ApplicationModel.Core.CoreApplication").Get(), &factory))) return 14;
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication> immersive;
    if (FAILED(factory.As(&immersive))) return 15;
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplicationView> view;
    if (FAILED(immersive->get_MainView(&view)) || !view) return 16;
    boolean main = false;
    if (FAILED(view->get_IsMain(&main)) || !main) return 17;
    // ppltasks.h in Store builds traces every task through these statics and
    // throws ClassNotRegisteredException from task creation when they are absent.
    if (FAILED(get(HStringReference(L"Windows.Foundation.Diagnostics.AsyncCausalityTracer").Get(), &factory))) {
        puts("FAIL causality factory");
        return 18;
    }
    ComPtr<ABI::Windows::Foundation::Diagnostics::IAsyncCausalityTracerStatics> causality;
    if (FAILED(factory.As(&causality))) return 19;
    using namespace ABI::Windows::Foundation::Diagnostics;
    const GUID platform{0x4b0b2a8f, 0x8a5c, 0x4f32, {0x8c, 0x2e, 0x31, 0x2a, 0x7e, 0x5f, 0x6b, 0x9d}};
    if (FAILED(causality->TraceOperationCreation(CausalityTraceLevel_Required, CausalitySource_Library, platform, 1,
                                                 HStringReference(L"task").Get(), 0)))
        return 20;
    if (FAILED(causality->TraceSynchronousWorkStart(CausalityTraceLevel_Important, CausalitySource_Library, platform, 1,
                                                    CausalitySynchronousWork_Execution)))
        return 21;
    if (FAILED(causality->TraceSynchronousWorkCompletion(CausalityTraceLevel_Important, CausalitySource_Library,
                                                         CausalitySynchronousWork_Execution)))
        return 22;
    if (FAILED(causality->TraceOperationRelation(CausalityTraceLevel_Important, CausalitySource_Library, platform, 1,
                                                 CausalityRelation_AssignDelegate)))
        return 23;
    if (FAILED(causality->TraceOperationCompletion(CausalityTraceLevel_Required, CausalitySource_Library, platform, 1,
                                                   ABI::Windows::Foundation::AsyncStatus::Completed)))
        return 24;
    // Toast scheduling computes delivery times with Windows.Globalization.Calendar.
    if (FAILED(get(HStringReference(L"Windows.Globalization.Calendar").Get(), &factory))) {
        puts("FAIL calendar factory");
        return 25;
    }
    ComPtr<IInspectable> calendarObject;
    if (FAILED(factory->ActivateInstance(&calendarObject))) return 26;
    ComPtr<ABI::Windows::Globalization::ICalendar> calendar;
    if (FAILED(calendarObject.As(&calendar))) return 27;
    if (FAILED(calendar->SetToNow())) return 28;
    ABI::Windows::Foundation::DateTime now{}, later{};
    if (FAILED(calendar->GetDateTime(&now)) || now.UniversalTime <= 0) return 29;
    if (FAILED(calendar->AddDays(1)) || FAILED(calendar->AddHours(2)) || FAILED(calendar->GetDateTime(&later)))
        return 30;
    if (later.UniversalTime - now.UniversalTime != 26LL * 3600 * 10000000) return 31;
    INT32 hour = -1, minute = -1;
    if (FAILED(calendar->put_Hour(9)) || FAILED(calendar->put_Minute(30)) || FAILED(calendar->get_Hour(&hour)) ||
        FAILED(calendar->get_Minute(&minute)) || hour != 9 || minute != 30)
        return 32;
    INT32 day = 0, nextDay = 0;
    if (FAILED(calendar->get_Day(&day)) || FAILED(calendar->AddDays(1)) || FAILED(calendar->get_Day(&nextDay)) ||
        (nextDay != day + 1 && nextDay != 1))
        return 33;
    puts("platformtest: 0 failure(s)");
    return 0;
}
