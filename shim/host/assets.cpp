#include "assets.h"
#include <new>

namespace ctr2 {
HRESULT RepairAssetNames(const std::filesystem::path& gameDirectory) noexcept {
    try {
        const auto maps = gameDirectory / L"Assets" / L"assets" / L"maps";
        for (const auto* name : {L"_MapSelectController.xml", L"_MasterGameDesignDocument.xml"}) {
            const auto encoded = maps / (std::wstring(L"%21") + name);
            const auto decoded = maps / (std::wstring(L"!") + name);
            const auto attributes = GetFileAttributesW(encoded.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
                return HRESULT_FROM_WIN32(error);
            }
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
            // MoveFile never overwrites a user's existing decoded asset.
            if (!MoveFileW(encoded.c_str(), decoded.c_str())) {
                const DWORD error = GetLastError();
                if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) continue;
                return HRESULT_FROM_WIN32(error);
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
}
