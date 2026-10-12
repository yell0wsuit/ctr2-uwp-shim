#pragma once
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
namespace ctr2 {
// Minimal Windows.Data.Xml.Dom tree (Wine has none) holding a legacy toast
// template, so offline toast code can fill and schedule it without failing.
HRESULT MakeToastTemplate(ABI::Windows::UI::Notifications::ToastTemplateType type,
                          ABI::Windows::Data::Xml::Dom::IXmlDocument** value);
}
