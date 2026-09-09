#include "Guid.h"

#include <combaseapi.h>
#include <cstdio>

namespace vsr {

std::wstring NewGuidString() {
    GUID g{};
    if (FAILED(CoCreateGuid(&g))) {
        return L"00000000-0000-0000-0000-000000000000";
    }
    wchar_t buf[64];
    swprintf_s(buf, L"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
               static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1],
               g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

} // namespace vsr
