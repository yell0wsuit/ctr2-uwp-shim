#pragma once
#include <windows.h>
#include <filesystem>

namespace ctr2 {
// Repair the two OPC-encoded map names left by raw package extraction.
// Existing decoded files are preserved. Missing assets need no repair.
HRESULT RepairAssetNames(const std::filesystem::path& gameDirectory) noexcept;
}
