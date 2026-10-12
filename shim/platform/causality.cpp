#include "com/factory.h"
#include "registry.h"
#include <windows.foundation.diagnostics.h>
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation::Diagnostics;
namespace ctr2 {
// Store-build ppltasks.h reports every task to these statics and throws
// ClassNotRegisteredException from task creation when the class is absent,
// which killed the game's render work item. Tracing is never enabled here.
class CausalityFactory final : public Factory<IAsyncCausalityTracerStatics> {
    EventSource<__FIEventHandler_1_Windows__CFoundation__CDiagnostics__CTracingStatusChangedEventArgs> changed_;

public:
    CausalityFactory() : Factory(L"Windows.Foundation.Diagnostics.AsyncCausalityTracer") {}
    HRESULT STDMETHODCALLTYPE TraceOperationCreation(CausalityTraceLevel, CausalitySource, GUID, UINT64, HSTRING,
                                                     UINT64) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TraceOperationCompletion(CausalityTraceLevel, CausalitySource, GUID, UINT64,
                                                       ABI::Windows::Foundation::AsyncStatus) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TraceOperationRelation(CausalityTraceLevel, CausalitySource, GUID, UINT64,
                                                     CausalityRelation) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TraceSynchronousWorkStart(CausalityTraceLevel, CausalitySource, GUID, UINT64,
                                                        CausalitySynchronousWork) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TraceSynchronousWorkCompletion(CausalityTraceLevel, CausalitySource,
                                                             CausalitySynchronousWork) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_TracingStatusChanged(
        __FIEventHandler_1_Windows__CFoundation__CDiagnostics__CTracingStatusChangedEventArgs* handler,
        EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_TracingStatusChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
};
HRESULT MakeCausalityFactory(IActivationFactory** value) {
    static auto factory = Make<CausalityFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
