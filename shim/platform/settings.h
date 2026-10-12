#pragma once
#include <windows.foundation.h>
#include <wrl.h>
#include <memory>
#include <string>
namespace ctr2 {
class SettingsPersistence;
HRESULT CreatePersistentSettings(const std::wstring& path,
                                 Microsoft::WRL::ComPtr<ABI::Windows::Foundation::Collections::IPropertySet>& values,
                                 std::shared_ptr<SettingsPersistence>& persistence);
}
