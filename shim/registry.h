#pragma once
#include <windows.h>
#include <activation.h>
extern "C" HRESULT WINAPI ShimGetFactory(HSTRING name, IActivationFactory** factory) noexcept;
namespace ctr2 {
HRESULT MakeThreadPoolFactory(bool timers, IActivationFactory** factory);
HRESULT MakeApplicationFactory(IActivationFactory** factory);
HRESULT MakeControlFactory(const wchar_t* name, IActivationFactory** factory);
HRESULT MakeDisplayFactory(IActivationFactory** factory);
HRESULT MakeCoreApplicationFactory(IActivationFactory** factory);
HRESULT MakeWindowFactory(IActivationFactory** factory);
HRESULT MakeCursorFactory(IActivationFactory** factory);
HRESULT MakeVisualizationFactory(IActivationFactory** factory);
HRESULT MakeStorageFactory(IActivationFactory** factory);
HRESULT MakeAdFactory(IActivationFactory** factory);
HRESULT MakeHttpFactory(bool filter, IActivationFactory** factory);
HRESULT MakeNetworkFactory(IActivationFactory** factory);
HRESULT MakeApplicationViewFactory(IActivationFactory** factory);
HRESULT MakeHardwareFactory(IActivationFactory** factory);
HRESULT MakeDataReaderFactory(IActivationFactory** factory);
HRESULT MakeStoreFactory(IActivationFactory** factory);
HRESULT MakeToastFactory(IActivationFactory** factory);
HRESULT MakeScheduledToastFactory(IActivationFactory** factory);
HRESULT MakeDeviceFactory(bool display, IActivationFactory** factory);
HRESULT MakeCausalityFactory(IActivationFactory** factory);
HRESULT MakeCalendarFactory(IActivationFactory** factory);
void PatchActivationImports() noexcept;
}
