#include <windows.h>
#include <roapi.h>
#include <windows.ui.notifications.h>
#include <windows.data.xml.dom.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Notifications;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"ctr2shim.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    auto hr = get(HStringReference(L"Windows.UI.Notifications.ToastNotificationManager").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL Toast factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<IToastNotificationManagerStatics> manager;
    if (FAILED(factory.As(&manager))) return 3;
    ComPtr<IToastNotifier> notifier;
    if (FAILED(manager->CreateToastNotifier(&notifier)) || !notifier) return 4;
    NotificationSetting setting{};
    if (FAILED(notifier->get_Setting(&setting)) || setting == NotificationSetting_Enabled) return 5;
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<ScheduledToastNotification*>> list;
    if (FAILED(notifier->GetScheduledToastNotifications(&list)) || !list) return 6;
    UINT32 count = 42;
    if (FAILED(list->get_Size(&count)) || count) return 7;
    ComPtr<IScheduledToastNotification> item;
    if (list->GetAt(0, &item) != E_BOUNDS || item) return 8;
    ComPtr<ABI::Windows::Foundation::Collections::IIterable<ScheduledToastNotification*>> iterable;
    ComPtr<ABI::Windows::Foundation::Collections::IIterator<ScheduledToastNotification*>> iterator;
    if (FAILED(list.As(&iterable)) || FAILED(iterable->First(&iterator))) return 9;
    boolean current = true;
    if (FAILED(iterator->get_HasCurrent(&current)) || current) return 10;
    if (notifier->Show(nullptr) != E_INVALIDARG || notifier->AddToSchedule(nullptr) != E_INVALIDARG) return 11;
    // Disabled notifications fail silently, as on Windows: a C++/CX caller
    // turns any failing HRESULT into an exception that ends its work item.
    using namespace ABI::Windows::Data::Xml::Dom;
    ComPtr<IXmlDocument> document;
    if (FAILED(manager->GetTemplateContent(ToastTemplateType_ToastText02, &document)) || !document) return 12;
    ComPtr<IXmlNodeList> texts;
    UINT32 length = 0;
    if (FAILED(document->GetElementsByTagName(HStringReference(L"text").Get(), &texts)) ||
        FAILED(texts->get_Length(&length)) || length != 2)
        return 13;
    ComPtr<IXmlNode> first;
    if (FAILED(texts->Item(0, &first)) || !first) return 14;
    // XmlNodeList is also IVectorView/IIterable<IXmlNode>; C++/CX GetAt and range-for use those.
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<IXmlNode*>> view;
    UINT32 size = 0;
    if (FAILED(texts.As(&view)) || FAILED(view->get_Size(&size)) || size != 2) return 27;
    ComPtr<IXmlNode> second;
    if (FAILED(view->GetAt(1, &second)) || !second || view->GetAt(2, &second) != E_BOUNDS) return 28;
    ComPtr<ABI::Windows::Foundation::Collections::IIterable<IXmlNode*>> nodes;
    ComPtr<ABI::Windows::Foundation::Collections::IIterator<IXmlNode*>> cursor;
    boolean has = false;
    UINT32 seen = 0;
    if (FAILED(texts.As(&nodes)) || FAILED(nodes->First(&cursor))) return 29;
    while (SUCCEEDED(cursor->get_HasCurrent(&has)) && has) {
        ++seen;
        cursor->MoveNext(&has);
    }
    if (seen != 2) return 30;
    ComPtr<IXmlText> text;
    if (FAILED(document->CreateTextNode(HStringReference(L"Om Nom misses you").Get(), &text))) return 15;
    ComPtr<IXmlNode> textNode, appended;
    if (FAILED(text.As(&textNode)) || FAILED(first->AppendChild(textNode.Get(), &appended))) return 16;
    ComPtr<IXmlNodeSerializer> serializer;
    HString xml;
    if (FAILED(document.As(&serializer)) || FAILED(serializer->GetXml(xml.GetAddressOf()))) return 17;
    if (!wcsstr(xml.GetRawBuffer(nullptr), L"<text id=\"1\">Om Nom misses you</text>")) {
        printf("xml=%ls\n", xml.GetRawBuffer(nullptr));
        return 18;
    }
    if (FAILED(get(HStringReference(L"Windows.UI.Notifications.ScheduledToastNotification").Get(), &factory)))
        return 19;
    ComPtr<IScheduledToastNotificationFactory> scheduledFactory;
    if (FAILED(factory.As(&scheduledFactory))) return 20;
    ABI::Windows::Foundation::DateTime when{132000000000000000};
    ComPtr<IScheduledToastNotification> scheduled;
    if (FAILED(scheduledFactory->CreateScheduledToastNotification(document.Get(), when, &scheduled)) || !scheduled)
        return 21;
    ABI::Windows::Foundation::DateTime delivery{};
    if (FAILED(scheduled->get_DeliveryTime(&delivery)) || delivery.UniversalTime != when.UniversalTime) return 22;
    if (FAILED(notifier->AddToSchedule(scheduled.Get()))) return 23;
    list.Reset();
    if (FAILED(notifier->GetScheduledToastNotifications(&list)) || FAILED(list->get_Size(&count)) || count != 1)
        return 24;
    if (FAILED(notifier->RemoveFromSchedule(scheduled.Get()))) return 25;
    list.Reset();
    if (FAILED(notifier->GetScheduledToastNotifications(&list)) || FAILED(list->get_Size(&count)) || count) return 26;
    puts("toasttest: notifications disabled; schedule accepted silently");
    return 0;
}
