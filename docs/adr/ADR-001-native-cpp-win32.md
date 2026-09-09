# ADR-001: Native C++20 / Win32 architecture

- **Status:** Accepted
- **Date:** 2026-09-09
- **Deciders:** Virtual Screen Recorder project
- **Related requirements:** NFR-030..NFR-034, NFR-010..NFR-014, spec.md §1 (architecture baseline)

## Context

The recorder must create a virtual display through a kernel-cooperative driver, capture that
monitor at 30-60 FPS with GPU-resident frames, hardware-encode H.264, present a tray-resident
Win32 UX, and stay inside aggressive resource budgets (idle CPU < 1%, 1080p30 CPU < 15%,
recording working set < 250 MB). Managed runtimes add a GC and a runtime footprint; generic
multimedia stacks (FFmpeg, libobs) add tens of megabytes and a foreign licensing/audit surface.

## Decision

We will implement the product as native C++20 on Win32: one desktop executable statically
linking first-party static libraries (Infrastructure, DisplayControl, WindowManagement, Capture,
Encoder, Application), Windows.Graphics.Capture for monitor capture, Direct3D 11 for GPU frame
transport and conversion, and Media Foundation for H.264/MP4 encoding. A separate UMDF 2/IddCx
driver project provides the virtual display (ADR-002).

## Alternatives considered

- **Electron/browser UI + node backend** — rejected: memory and CPU footprint, packaging size,
  and no native access to capture/IDDCX without additional native layers.
- **.NET Desktop Runtime (WinUI/WPF) + P/Invoke** — rejected: requires shipping a runtime,
  GC pauses under frame pressure, and most of the pipeline is native anyway.
- **FFmpeg or OBS libraries for encode/mux** — rejected by spec (non-goals): binary size,
  licensing surface, and duplicated OS capability; Media Foundation provides hardware H.264 and
  MP4 muxing in-box.

## Consequences

- (+) Smallest realistic footprint; direct access to every required API with no shim layers.
- (+) Static linking keeps the payload within the 20 MB quality target.
- (-) Higher implementation cost for UI, JSON, and plumbing that managed stacks provide.
- (-) Requires disciplined RAII and callback hygiene (NFR-010, NFR-011).
- The compact JSON reader/writer (`src/Infrastructure/Json.*`) and the unit-test framework are
  in-house; both are small and audited, per design.md §4.10.

## Validation evidence

- The full application builds with `/W4 /WX` C++20 on MSVC 14.44 / SDK 10.0.26100.0 and the
  unit suite (56 tests) passes on the development machine (see `artifacts/build-manifest.json`).
- Memory/CPU budgets are verified in task T091 on the reference machine.
