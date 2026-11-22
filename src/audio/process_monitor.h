#pragma once
#include <cstdint>
#include <windows.h>

namespace sso {

// Returns the process id of the first running process named `exeName`
// (case-insensitive), or 0 if none is found.
DWORD FindProcessIdByName(const wchar_t* exeName);

} // namespace sso
