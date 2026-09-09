// Virtual Screen Recorder - stable virtual display identity (FR-010, FR-013).
//
// These values are the single source of truth shared by the UMDF driver
// (src/Driver), its INF, and this application. The application locates the
// virtual monitor by these identities, never by display ordinal or GDI name
// such as "\\.\DISPLAY2" (design.md §4.3).
#pragma once

#include <string>

namespace vsr::display {

struct VirtualDisplayIdentity {
    // Monitor friendly name as exposed to Windows via the EDID descriptor.
    // Two EDID string descriptors concatenate to this name; it is what
    // Settings > System > Display shows.
    static constexpr wchar_t kMonitorFriendlyName[] = L"Virtual Recording Display";

    // Adapter endpoint strings reported to IddCx for diagnostics.
    static constexpr wchar_t kEndpointFriendlyName[] = L"VSR Virtual Display";
    static constexpr wchar_t kEndpointManufacturer[] = L"VirtualScreenRecorder Project";
    static constexpr wchar_t kEndpointModel[] = L"Virtual Recording Display Adapter";

    // Root device identities. The INF matches these hardware IDs; the
    // application creates the software device with the same values.
    static constexpr wchar_t kEnumeratorName[] = L"VirtualScreenRecorder";
    static constexpr wchar_t kInstanceId[] = L"VirtualScreenRecorder";
    static constexpr wchar_t kHardwareId[] = L"VirtualScreenRecorder";
    static constexpr wchar_t kDeviceDescription[] = L"Virtual Screen Recorder Virtual Display";
    static constexpr wchar_t kDeviceGroup[] = L"VirtualScreenRecorderGroup";

    // Device instance path prefix created under the ROOT enumerator. Stable
    // across reboots and independent of the GDI display ordinal.
    static constexpr wchar_t kInstancePathPrefix[] = L"ROOT\\VIRTUALSCREENRECORD";

    // The EDID manufacturer id block ("VSR") and serial descriptor
    // (src/Driver/Edid.h). Kept here for documentation; matching relies on
    // the friendly name and instance path.
    static constexpr wchar_t kEdidSerial[] = L"VSR-0001";
};

// Case-insensitive comparison of a Windows-reported monitor friendly name
// against the identity. Unit-testable without a display stack.
bool MatchesVirtualMonitorName(const std::wstring& reportedName);

// True when a device instance ID (CM_Get_Device_ID form) belongs to the
// virtual display adapter.
bool MatchesVirtualInstancePath(const std::wstring& instanceId);

} // namespace vsr::display
