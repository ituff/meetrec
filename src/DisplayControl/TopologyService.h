// Virtual Screen Recorder - display topology service (FR-013, FR-014, FR-040).
//
// Enumerates active display paths through the Display Configuration APIs and
// maps the stable virtual display identity to an HMONITOR and desktop
// geometry. No fixed display ordinals are used anywhere.
#pragma once

#include <windows.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::display {

struct MonitorDescriptor {
    std::wstring stableId;        // identity-derived stable key
    std::wstring friendlyName;    // monitor EDID name
    std::wstring gdiDeviceName;   // e.g. \\.\DISPLAY3 (diagnostics only)
    HMONITOR handle = nullptr;
    RECT monitorRect{};           // desktop coordinates
    RECT workRect{};              // work area excluding taskbar
    UINT dpiX = 96;
    UINT dpiY = 96;
    bool primary = false;
    bool virtualRecordingDisplay = false;
};

class TopologyService {
public:
    // Enumerates every active monitor in the current desktop.
    static std::vector<MonitorDescriptor> EnumerateMonitors(Error* error = nullptr);

    // Locates the virtual recording monitor by stable identity (FR-013).
    static Result<MonitorDescriptor> FindVirtualMonitor();

    // True when the virtual monitor is active and part of the extended
    // desktop (FR-014). The primary flag is irrelevant for this check.
    static Result<void> VerifyVirtualMonitorActive();

    // Diagnostics helper: sanitized topology snapshot (no user content).
    struct TopologySummary {
        struct Entry {
            std::wstring friendlyName;
            std::wstring gdiDeviceName;
            long x = 0, y = 0, w = 0, h = 0;
            UINT dpiX = 96;
            bool isVirtual = false;
            bool primary = false;
        };
        std::vector<Entry> monitors;
    };
    static TopologySummary Summarize();
};

// Watches display topology changes and invokes the callback after a short
// debounce, on a dedicated hidden-window thread (design.md §4.4).
class TopologyWatcher {
public:
    using Callback = std::function<void()>;

    TopologyWatcher();
    ~TopologyWatcher();
    TopologyWatcher(const TopologyWatcher&) = delete;
    TopologyWatcher& operator=(const TopologyWatcher&) = delete;

    Result<void> Start(Callback callback);
    void Stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vsr::display
