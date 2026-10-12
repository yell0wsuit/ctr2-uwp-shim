#include "generated/store.h"
#include "registry.h"
#include "com/factory.h"
#include "com/error_operation.h"
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::ApplicationModel::Store;
namespace ctr2 {
namespace {
using ProductMap = __FIMapView_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using ProductPair = __FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using ProductIterable = __FIIterable_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using ProductIterator = __FIIterator_1___FIKeyValuePair_2_HSTRING_Windows__CApplicationModel__CStore__CProductLicense;
using UnfulfilledView = __FIVectorView_1_Windows__CApplicationModel__CStore__CUnfulfilledConsumable;
class EmptyProductIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, ProductIterator, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE get_Current(ProductPair** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override { return get_HasCurrent(value); }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, ProductPair** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        return S_OK;
    }
};
class EmptyProductMap final : public RuntimeClass<RuntimeClassFlags<WinRt>, ProductMap, ProductIterable, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IMapView`2", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING, IProductLicense** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING, boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Split(ProductMap** first, ProductMap** second) override {
        if (first) *first = nullptr;
        if (second) *second = nullptr;
        return first && second ? S_OK : E_POINTER;
    }
    HRESULT STDMETHODCALLTYPE First(ProductIterator** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto iterator = Make<EmptyProductIterator>();
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class OfflineLicense final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultILicenseInformation, FtmBase> {
    InspectableClass(L"Windows.ApplicationModel.Store.LicenseInformation", BaseTrust);
    EventSource<ILicenseChangedEventHandler> changed_;

public:
    HRESULT STDMETHODCALLTYPE get_ProductLicenses(ProductMap** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto products = Make<EmptyProductMap>();
        return products ? products.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsActive(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsTrial(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExpirationDate(DateTime* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_LicenseChanged(ILicenseChangedEventHandler* handler,
                                                 EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_LicenseChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
};
template <class RuntimeType, class AbiType = RuntimeType> HRESULT OfflineError(IAsyncOperation<RuntimeType>** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    auto operation = Make<ErrorOperation<RuntimeType, AbiType>>(E_ACCESSDENIED);
    return operation ? operation.CopyTo(value) : E_OUTOFMEMORY;
}
class StoreFactory final : public Factory<DefaultICurrentApp, DefaultICurrentAppWithConsumables> {
public:
    StoreFactory() : Factory(L"Windows.ApplicationModel.Store.CurrentApp") {}
    HRESULT STDMETHODCALLTYPE get_LicenseInformation(ILicenseInformation** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        // No entitlement provenance has been established. Keep the base app
        // inactive and all products absent; failed purchases cannot change it.
        static auto license = Make<OfflineLicense>();
        return license ? license.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE RequestAppPurchaseAsync(boolean, IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE RequestProductPurchaseAsync(HSTRING, boolean, IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE GetAppReceiptAsync(IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE GetProductReceiptAsync(HSTRING, IAsyncOperation<HSTRING>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE LoadListingInformationAsync(IAsyncOperation<ListingInformation*>** value) override {
        return OfflineError<ListingInformation*, IListingInformation*>(value);
    }
    HRESULT STDMETHODCALLTYPE ReportConsumableFulfillmentAsync(HSTRING, GUID,
                                                               IAsyncOperation<FulfillmentResult>** value) override {
        return OfflineError(value);
    }
    HRESULT STDMETHODCALLTYPE
    RequestProductPurchaseWithResultsAsync(HSTRING, IAsyncOperation<PurchaseResults*>** value) override {
        return OfflineError<PurchaseResults*, IPurchaseResults*>(value);
    }
    HRESULT STDMETHODCALLTYPE RequestProductPurchaseWithDisplayPropertiesAsync(
        HSTRING, HSTRING, IProductPurchaseDisplayProperties*, IAsyncOperation<PurchaseResults*>** value) override {
        return OfflineError<PurchaseResults*, IPurchaseResults*>(value);
    }
    HRESULT STDMETHODCALLTYPE GetUnfulfilledConsumablesAsync(IAsyncOperation<UnfulfilledView*>** value) override {
        return OfflineError(value);
    }
};
}
HRESULT MakeStoreFactory(IActivationFactory** value) {
    auto factory = Make<StoreFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
