#include "VirtualDisplayIdentity.h"

#include <Infrastructure/Strings.h>

namespace vsr::display {

bool MatchesVirtualMonitorName(const std::wstring& reportedName) {
    return EqualsNoCase(reportedName, VirtualDisplayIdentity::kMonitorFriendlyName);
}

bool MatchesVirtualInstancePath(const std::wstring& instanceId) {
    const std::wstring lower = ToLower(instanceId);
    const std::wstring prefix = ToLower(VirtualDisplayIdentity::kInstancePathPrefix);
    return lower.compare(0, prefix.size(), prefix) == 0;
}

} // namespace vsr::display
