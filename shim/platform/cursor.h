#pragma once
#include <windows.h>
#include <windows.ui.core.h>
namespace ctr2 {
// The Win32 cursor for a CoreCursor: a system cursor, or a Custom one loaded by
// ID from the game executable's resources. The arrow when that is missing.
HCURSOR LoadCoreCursor(ABI::Windows::UI::Core::ICoreCursor* cursor) noexcept;
}
