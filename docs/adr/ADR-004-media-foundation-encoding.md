# ADR-004: Media Foundation H.264 encoding with explicit MFT selection

- **Status:** Accepted
- **Date:** 2026-09-09
- **Deciders:** Virtual Screen Recorder project
- **Related requirements:** FR-050..FR-063, NFR-005, NFR-006; design.md §4.8

## Context

Output must be H.264 in MP4 (FR-050) at fixed presets (Low/Standard/High, FR-051), prefer
hardware encoders (FR-062), and fail with `VSR-ENCODER-UNAVAILABLE` before any file is created
when no compliant encoder exists (FR-063). Capture delivers BGRA8; H.264 encoders universally
accept NV12. Stop must drain and finalize within bounded time (FR-056, NFR-006), and a partial
file must never masquerade as a valid recording (FR-057/FR-058).

## Decision

We will encode with **Media Foundation using an explicit `IMFSinkWriter`** pipeline:

1. **Capability probe (T030):** `MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_ALL |
   MFT_ENUM_FLAG_SORTANDFILTER)` enumerates all encoder MFTs; for each we read
   `MFT_FRIENDLY_NAME_Attribute`, `MFT_ENUM_HARDWARE_URL_Attribute` (hardware flag),
   `MF_SA_D3D11_AWARE`, and the advertised NV12 input types. The deterministic selection policy
   (`SelectEncoder`, unit-tested) prefers hardware + D3D11-aware > hardware > software; no
   candidate ⇒ `VSR-ENCODER-UNAVAILABLE` **before** capture or file creation.
2. **Hardware path:** D3D11-aware MFT + `MF_SINK_WRITER_D3D_MANAGER`
   (`MFCreateDXGIDeviceManager` + `ResetDevice`); the NV12 texture from the D3D11 VideoProcessor
   is wrapped with `MFCreateDXGISurfaceBuffer` and submitted — zero CPU copies in the normal
   path.
3. **Documented software fallback:** when only a non-D3D11-aware (software) MFT exists, frames
   are staged to system memory through a staging texture (CPU copy) — an explicit, documented
   degradation (spec.md §7.3 "documented fallback"), still producing valid output.
4. **Container:** the sink writer targets the `.partial.mp4` path directly (MP4 sink by
   extension); timestamps come from the monotonic recording timeline (paused time excluded,
   FR-055); non-monotonic inputs are clamped to `last + duration` (FR-043).
5. **Finalization:** `Finalize()` is idempotent (FR-073), drains, finalizes, and closes; the
   coordinator then renames `.partial.mp4` → `.mp4` (FR-058). On failure the partial file is
   preserved and surfaced in diagnostics (FR-057).

## Alternatives considered

- **Direct MFT graph (`IMFMediaSession`/custom topology)** — rejected: more moving parts than a
  sink writer for a single-video-stream MP4, no benefit at MVP scope.
- **Always insert our own scaler/encoder and mux MP4 boxes manually** — rejected: duplicate OS
  capability, high maintenance, MP4 correctness risk (moov atom finalization).
- **BGRA input to sink writer, letting it convert** — rejected: sink-writer insertion of
  converters is not guaranteed to stay on GPU across vendors; explicit VideoProcessor
  conversion keeps the normal path CPU-free and predictable (ADR pairs with design.md §4.7).

## Consequences

- (+) Deterministic, testable selection policy; vendor probes recorded by T030.
- (+) Hardware path satisfies the CPU budget (NFR-004) with zero CPU copies.
- (-) Software fallback incurs a staged CPU copy; visible in diagnostics counters when used.
- (-) Sink writer behavior for exotic drivers can vary; the probe records the exact MFT chosen
  per machine in the log for support.

## Validation evidence

- Encoder policy and preset-table unit tests (EncoderTests).
- T031/T040 validation (10-second encode per preset, 5-minute end-to-end, monotonic timestamp
  and duration checks via `MediaValidator.exe`) on Intel/NVIDIA/AMD reference machines is
  recorded during release qualification (T091/T092); probe output is captured in logs
  (`ENCODER_SELECTED`).
