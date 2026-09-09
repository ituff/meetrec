// Virtual Screen Recorder - GUID helper.
#pragma once

#include <string>

namespace vsr {

// Returns a new GUID formatted as "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
// (lowercase). Used for per-session correlation IDs.
std::wstring NewGuidString();

} // namespace vsr
