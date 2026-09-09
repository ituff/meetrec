# ADR-002: IDD driver lifecycle — SwDeviceCreate software device

- **Status:** Accepted (draft finalized by T012; hardware evidence pending on reference machine)
- **Date:** 2026-09-09
- **Deciders:** Virtual Screen Recorder project
- **Related requirements:** FR-010, FR-013..FR-017, NFR-021; design.md §4.3 (driver control)

## Context

The UMDF/IddCx driver must expose exactly one virtual monitor. Windows requires a *device node*
for the driver to load on. Two lifecycles are possible:

1. **Persistent installed device:** the installer creates the root device once; it survives
   reboots and is enabled/disabled via SetupAPI property changes.
2. **Software device created at runtime:** the application (or installer workflow) creates the
   device with `SwDeviceCreate` when needed; closing the handle removes the device and disconnects
   the monitor.

The product wants the virtual display to exist *only* while the user wants it, without leaving
an extra display permanently attached to a user's desktop, while still supporting
enable/disable (FR-016) and rejecting disable during recording (FR-017).

## Decision

We will use the **software-device lifecycle** (option 2), the model proven by the Microsoft
IddSampleApp:

- `SoftwareDisplayDevice::EnsureDisplayAvailable` calls `SwDeviceCreate` with the product
  instance/hardware IDs (`VirtualScreenRecorder`), holding the `HSWDEVICE` handle for the app's
  lifetime of the display need.
- Closing the handle (`ReleaseDisplay`) removes the devnode and disconnects the monitor — used
  by the tray "disable display" command and app shutdown, and rejected by the coordinator while
  Preparing/Recording/Paused/Stopping (FR-017).
- `SetAdapterEnabled` (SetupAPI DICS_ENABLE/DICS_DISABLE) covers enable/disable of a device that
  was created outside the app (installer path).
- Driver staging (`pnputil /add-driver`, `SWDeviceCapabilitiesSilentInstall`) is a separate,
  elevated step performed by the installer or `scripts/install-driver.ps1`.

Stable identity: the INF matches hardware IDs `Root\VirtualScreenRecorder` /
`VirtualScreenRecorder`; the device instance path is rooted under
`ROOT\VirtualScreenRecorder...`, and the monitor EDID name ("Virtual Recording Display") plus a
fixed container ID (`Edid.h kContainerId`) give the application a stable, ordinal-independent
identity (FR-013).

## Alternatives considered

- **Persistent devnode created by installer** — rejected for MVP: a permanent extra display
  confuses users and re-anchors the desktop topology; enable/disable semantics then carry the
  whole UX. It remains the documented fallback if `SwDeviceCreate` proves unreliable on the
  reference machine (see stop conditions in tasks.md §6).
- **Devcon-style runtime install each session** — rejected: requires elevation every run,
  violating FR-003.

## Consequences

- (+) No elevation at application runtime (FR-003); display appears only when needed.
- (+) Remove is trivial and reliable (`SwDeviceClose`).
- (-) The driver package must be staged in the driver store ahead of time (installer does this).
- (-) Display arrival is asynchronous; the app must poll the topology for up to 5 s (bounded by
  NFR-005) — implemented in `EnsureDisplayAvailable`.

## Validation evidence

- Driver + INF identity contract is unit-tested (`Identity` tests) and shared as a single
  constant set (`VirtualDisplayIdentity.h` ↔ `Edid.h` ↔ INF).
- T012's ten create/destroy cycles, reboot retention, and duplicate-monitor checks are executed
  per `docs/driver-manual-test-log.md`; completion is pending a WDK/test machine and tracked as
  the remaining evidence for this ADR.
