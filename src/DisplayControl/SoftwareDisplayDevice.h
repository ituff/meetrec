// Virtual Screen Recorder - software device lifecycle for the virtual display
// adapter (FR-010, FR-013..FR-017; ADR-002).
//
// The MVP lifecycle creates the root-enumerated display device on demand via
// SwDeviceCreate (the model proven by the Microsoft IddSampleApp). The device
// handle is held by this object; closing it removes the device and disconnects
// the virtual monitor. A staged driver package (pnputil /add-driver, see
// scripts/install-driver.ps1) is required before creation succeeds.
#pragma once

#include <chrono>
#include <memory>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>
#include "TopologyService.h"

namespace vsr::display {

class SoftwareDisplayDevice {
public:
    SoftwareDisplayDevice();
    ~SoftwareDisplayDevice();
    SoftwareDisplayDevice(const SoftwareDisplayDevice&) = delete;
    SoftwareDisplayDevice& operator=(const SoftwareDisplayDevice&) = delete;

    // True when a present display-class device instance matches the virtual
    // display identity (ROOT\VirtualScreenRecorder...).
    static bool IsAdapterDevicePresent();

    // True when the virtual monitor is currently attached to the desktop.
    static bool IsMonitorActive();

    // Creates the software device if needed and waits for the virtual monitor
    // to join the extended desktop. Idempotent: an already-active monitor
    // returns immediately.
    Result<MonitorDescriptor> EnsureDisplayAvailable(std::chrono::milliseconds timeout);

    // Disconnects the virtual monitor created by this instance. Rejected while
    // the coordinator holds the recording lock (FR-017 enforced by caller).
    void ReleaseDisplay();

    // Enable/disable the adapter device through SetupAPI (FR-016). Only valid
    // while no recording is active; the coordinator enforces that.
    static Result<void> SetAdapterEnabled(bool enabled);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vsr::display
