// The game uses the Windows 8.1 Store purchase ABI.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#pragma warning(disable : 4996)
#endif
#include <windows.h>
#include <roapi.h>
#include <windows.applicationmodel.store.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <atomic>
#include <thread>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::ApplicationModel::Store;
using namespace ABI::Windows::Foundation;
using Map = __FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using Pair = __FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using Iterable = __FIIterable_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using Iterator = __FIIterator_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
class SelfClosingCompletion final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAsyncOperationCompletedHandler<HSTRING>> {
    ComPtr<SelfClosingCompletion>* owner_;
    std::atomic<int>* released_;
    bool* releasedDuringInvoke_;

public:
    SelfClosingCompletion(ComPtr<SelfClosingCompletion>* owner, std::atomic<int>* released, bool* during)
        : owner_(owner), released_(released), releasedDuringInvoke_(during) {}
    ~SelfClosingCompletion() override { ++*released_; }
    HRESULT STDMETHODCALLTYPE Invoke(IAsyncOperation<HSTRING>* operation, AsyncStatus) override {
        // Keep observation pointers on the stack, so the failing baseline can
        // report early destruction without dereferencing this after Close.
        auto* owner = owner_;
        auto* released = released_;
        auto* during = releasedDuringInvoke_;
        ComPtr<IAsyncInfo> info;
        operation->QueryInterface(IID_PPV_ARGS(&info));
        owner->Reset();
        info->Close();
        *during = released->load() != 0;
        return S_OK;
    }
};
class ReentrantCompletion final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IAsyncOperationCompletedHandler<HSTRING>> {
    ComPtr<IAsyncInfo> operation_;
    std::atomic<int>& released_;

public:
    ReentrantCompletion(IAsyncInfo* operation, std::atomic<int>& released)
        : operation_(operation), released_(released) {}
    ~ReentrantCompletion() override {
        AsyncStatus status{};
        if (operation_->get_Status(&status) == RO_E_CLOSED && SUCCEEDED(operation_->Close())) ++released_;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IAsyncOperation<HSTRING>*, AsyncStatus) override { return S_OK; }
};
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    auto hr = get(HStringReference(L"Windows.ApplicationModel.Store.CurrentApp").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL CurrentApp factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<ICurrentApp> app;
    ComPtr<ICurrentAppWithConsumables> consumables;
    if (FAILED(factory.As(&app)) || FAILED(factory.As(&consumables))) return 3;
    ComPtr<ILicenseInformation> license;
    if (app->get_LicenseInformation(nullptr) != E_POINTER || FAILED(app->get_LicenseInformation(&license)) || !license)
        return 4;
    boolean active = true, trial = true;
    if (FAILED(license->get_IsActive(&active)) || active || FAILED(license->get_IsTrial(&trial)) || trial) return 5;
    DateTime expiration{1};
    if (FAILED(license->get_ExpirationDate(&expiration)) || expiration.UniversalTime != 0) return 6;
    EventRegistrationToken event{};
    auto changed = Callback<ILicenseChangedEventHandler>([]() -> HRESULT { return E_FAIL; });
    if (FAILED(license->add_LicenseChanged(changed.Get(), &event)) || !event.value ||
        FAILED(license->remove_LicenseChanged(event)))
        return 7;
    ComPtr<Map> products;
    if (FAILED(license->get_ProductLicenses(&products)) || !products) return 8;
    UINT32 size = 99;
    boolean found = true;
    ComPtr<IProductLicense> product;
    auto sizeHr = products->get_Size(&size);
    auto keyHr = products->HasKey(HStringReference(L"anything").Get(), &found);
    auto lookupHr = products->Lookup(HStringReference(L"anything").Get(), &product);
    if (FAILED(sizeHr) || size || FAILED(keyHr) || found || lookupHr != E_BOUNDS || product) {
        printf("FAIL empty map sizehr=%08lx size=%u keyhr=%08lx found=%u lookup=%08lx expected=%08lx product=%p\n",
               sizeHr, size, keyHr, found, lookupHr, (E_BOUNDS), product.Get());
        return 9;
    }
    ComPtr<Map> left, right;
    if (FAILED(products->Split(&left, &right)) || left || right) return 10;
    ComPtr<Iterable> iterable;
    ComPtr<Iterator> iterator;
    if (FAILED(products.As(&iterable)) || FAILED(iterable->First(&iterator)) || !iterator) return 11;
    ComPtr<Pair> current;
    found = true;
    if (FAILED(iterator->get_HasCurrent(&found)) || found || iterator->get_Current(&current) != E_BOUNDS || current ||
        FAILED(iterator->MoveNext(&found)) || found)
        return 12;
    Pair* items[2] = {};
    UINT32 actual = 99;
    if (FAILED(iterator->GetMany(2, items, &actual)) || actual) return 13;
    ComPtr<IAsyncOperation<HSTRING>> purchase;
    if (app->RequestAppPurchaseAsync(false, nullptr) != E_POINTER ||
        FAILED(app->RequestAppPurchaseAsync(false, &purchase)) || !purchase)
        return 14;
    ComPtr<IAsyncInfo> info;
    if (FAILED(purchase.As(&info))) return 15;
    AsyncStatus status{};
    HRESULT error = S_OK;
    if (FAILED(info->get_Status(&status)) || status != AsyncStatus::Error || FAILED(info->get_ErrorCode(&error)) ||
        SUCCEEDED(error))
        return 16;
    HString receipt;
    if (SUCCEEDED(purchase->GetResults(receipt.GetAddressOf())) || receipt.Length()) return 17;
    bool notified = false;
    auto completed =
        Callback<IAsyncOperationCompletedHandler<HSTRING>>([&](IAsyncOperation<HSTRING>*, AsyncStatus s) -> HRESULT {
            notified = s == AsyncStatus::Error;
            return S_OK;
        });
    if (FAILED(purchase->put_Completed(completed.Get())) || !notified) return 18;
    if (FAILED(app->RequestProductPurchaseAsync(HStringReference(L"anything").Get(), false, &purchase)) ||
        SUCCEEDED(purchase->GetResults(receipt.GetAddressOf())))
        return 19;
    ComPtr<IAsyncOperation<PurchaseResults*>> results;
    if (FAILED(consumables->RequestProductPurchaseWithResultsAsync(HStringReference(L"anything").Get(), &results)) ||
        !results)
        return 20;
    ComPtr<IPurchaseResults> result;
    if (SUCCEEDED(results->GetResults(&result)) || result) return 21;
    GUID transaction{};
    ComPtr<IAsyncOperation<FulfillmentResult>> fulfillment;
    if (FAILED(consumables->ReportConsumableFulfillmentAsync(HStringReference(L"anything").Get(), transaction,
                                                             &fulfillment)) ||
        !fulfillment)
        return 22;
    FulfillmentResult fulfilled{};
    if (SUCCEEDED(fulfillment->GetResults(&fulfilled))) return 23;
    if (FAILED(license->get_IsActive(&active)) || active || FAILED(products->get_Size(&size)) || size) return 24;
    ComPtr<IAsyncOperation<HSTRING>> selfClosing;
    if (FAILED(app->RequestAppPurchaseAsync(false, &selfClosing))) return 30;
    ComPtr<SelfClosingCompletion> owner;
    std::atomic<int> selfReleased{0};
    bool duringInvoke = false;
    owner = Make<SelfClosingCompletion>(&owner, &selfReleased, &duringInvoke);
    if (FAILED(selfClosing->put_Completed(owner.Get())) || duringInvoke || selfReleased != 1) {
        puts("FAIL completion delegate destroyed during Invoke");
        return 31;
    }
    // Close must release delegates outside its mutex: delegate destruction
    // can query and close the same operation again.
    ComPtr<IAsyncOperation<HSTRING>> reentrant;
    if (FAILED(app->RequestAppPurchaseAsync(false, &reentrant))) return 25;
    ComPtr<IAsyncInfo> reentrantInfo;
    if (FAILED(reentrant.As(&reentrantInfo))) return 26;
    std::atomic<int> released{0};
    auto reentrantHandler = Make<ReentrantCompletion>(reentrantInfo.Get(), released);
    if (FAILED(reentrant->put_Completed(reentrantHandler.Get()))) return 27;
    reentrantHandler.Reset();
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread closeThread([reentrantInfo, closed] {
        reentrantInfo->Close();
        SetEvent(closed);
    });
    if (WaitForSingleObject(closed, 2000) != WAIT_OBJECT_0) {
        puts("FAIL Close deadlocked during reentrant handler destruction");
        fflush(stdout);
        ExitProcess(28);
    }
    closeThread.join();
    CloseHandle(closed);
    if (released != 1) return 29;
    puts("storetest: 0 failure(s)");
    return 0;
}
