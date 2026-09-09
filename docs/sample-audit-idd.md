# Sample audit — Microsoft IndirectDisplay import (T010)

Source: <https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay>
(IddSampleDriver), Microsoft Corporation, **MIT License**. Retrieved 2026-09-09.

Per design.md §11 rule 6, imported code is marked and every sample TODO is reviewed. Files
below list the exact provenance of each driver source file in `src/Driver`.

## Imported files

| Our file | Sample origin | Import kind | Modifications |
|---|---|---|---|
| `src/Driver/Driver.h` | `IddSampleDriver/Driver.h` | Adapted | Namespace `vsr::driver`; single-monitor mode table (`VirtualMonitorDefinition`: 1080p30/1080p60/720p30, preferred index 0); removed `IndirectSampleMonitor` EDID array declarations; added `Edid.h` include; product comments. |
| `src/Driver/Driver.cpp` | `IddSampleDriver/Driver.cpp` | Adapted | See "Behavioral changes" below; all sample `TODO` blocks replaced with reviewed, documented behavior; WPP `TraceEvents` added on every lifecycle callback and swap-chain loop exit; callbacks renamed `Vsr*`; endpoint identity strings are product values. |
| `src/Driver/Trace.h` | `IddSampleDriver/Trace.h` | Adapted | New tracing control GUID (`7d2a1f4e-…`), product tracing id `VirtualScreenRecorder\UMDF2\VirtualDisplayDriver v1.0`. |
| `src/Driver/VirtualDisplayDriver.inf` | `IddSampleDriver/IddSampleDriver.inf` | Adapted | Hardware IDs `Root\VirtualScreenRecorder` + `VirtualScreenRecorder`; unique `DeviceGroupId=VirtualScreenRecorderGroup` (per sample README guidance); product strings; catalog name. **`UpperFilters=IndirectKmd` retained** — the in-box IddCx kernel stub required by UMDF indirect display drivers. |
| `src/Driver/VirtualDisplayDriver.vcxproj` | `IddSampleDriver/IddSampleDriver.vcxproj` | Adapted | x64-only configurations; product names/GUID; `OutDir` to `artifacts\<config>\driver`; keeps WPP enabled, IddCx 1.4+ defines, Spectre mitigation. |
| `src/Driver/VirtualDisplayDriver.rc` | (new, conventional) | New | Product version resource. |
| `src/Driver/Edid.h` | (new; generated) | New | Product EDID (name "Virtual Recording Display", manufacturer "VSR", DTD 1080p30 + 1080p60, checksum 0x33) and the stable container-ID GUID. Regenerate with `scripts/gen-edid.py`. |

The application-side reference for `SwDeviceCreate` usage
(`IddSampleApp/main.cpp`, same sample) informed `SoftwareDisplayDevice`'s creation pattern
(callback + capability flags); that file is referenced as documentation, not copied.

## Behavioral changes vs. sample (all reviewed)

1. **One adapter, one monitor (FR-010).** `MaxMonitorsSupported = 1`; `FinishInit(0)` only; the
   sample's three-monitor static table and edid-less monitor paths are removed.
2. **Product modes only (FR-011, FR-012).** Monitor modes, default modes, and target modes are
   exactly 1280x720@30, 1920x1080@30 (preferred), 1920x1080@60; the OS offers their
   intersection. The sample's 4K/144 Hz target modes are removed because the endpoint cannot
   process them.
3. **Product EDID with stable identity (FR-013).** Sample EDIDs (Dell/Lenovo blocks) removed;
   the generated product EDID declares the "Virtual Recording Display" name; the sample's
   per-arrival `CoCreateGuid` container ID is replaced by a fixed GUID so identity is stable
   across reboot and device cycles.
4. **Frame loop keeps sample semantics.** The driver performs no pixel processing (comment
   documents the intent); acquire → release reference → `IddCxSwapChainFinishedProcessingFrame`.
   The sample's two "TODO: process frame / report statistics" blocks are resolved as
   "intentionally no-op for this product" — statistics would only matter if the driver encoded,
   which it must not (design.md §4.3).
5. **WPP tracing added (NFR-034).** The sample defines tracing infrastructure but never traces;
   our driver emits lifecycle events (DriverEntry, DeviceAdd, D0Entry, adapter init,
   monitor create/arrival, swap-chain assign/unassign, loop exits) on the product tracing GUID.
6. **WPP init/cleanup added.** `WPP_INIT_TRACING` in `DriverEntry` with cleanup in the driver
   object's `EvtCleanupCallback`; the sample never initializes WPP (it also never traces).
7. **Remaining sample TODO review.** Every `TODO` block present in the sample sources was either
   implemented (identity, modes, EDID), removed with its dead code path (multi-monitor), or
   documented as an intentional no-op with rationale (frame processing, frame statistics,
   commit-modes reconfiguration). **No undocumented sample TODO remains** (T010 done-criteria).

## Planned removals / follow-ups

- None of the sample's auxiliary samples (IddSampleApp) are shipped; the product application is
  the sole consumer of the driver.
- If ADR-002's fallback lifecycle (persistent device) is ever activated, revisit the INF's
  silent-install capability flags.

## License preservation

Original files carry Microsoft's copyright header; our adaptations state the derivation in each
file header ("Derived from the Microsoft Windows-driver-samples IndirectDisplay sample … MIT
License") and this audit lists the per-file provenance. The project license file must include
the MIT license text covering the derived files.
