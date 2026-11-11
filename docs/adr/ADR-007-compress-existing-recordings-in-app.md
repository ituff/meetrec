# ADR-007: Compress existing recordings as a module of the existing app

- **Status:** Accepted (implemented; release-corpus validation pending)
- **Date:** 2026-11-10
- **Deciders:** MeetRec (Virtual Screen Recorder) project
- **Related requirements:** FR-050..FR-059 (output format, presets, no silent overwrite); customer
  request 2026-11: re-encode recordings produced by other applications

## Context

Customers are satisfied with the size and quality of MeetRec output (capped-CRF H.264 with
`-tune stillimage` and a 5 s GOP; see `VideoPreset.h`). They have now asked to apply the same
treatment to older screen recordings produced by other apps — arbitrary MP4/MKV/MOV/FLV files,
H.264 or H.265, variable frame rate, multiple audio tracks, odd resolutions.

This is a different workflow from recording: file-to-file transcoding with a queue, progress
and cancellation, not a live capture pipeline. Two delivery shapes were on the table:

1. **A standalone compressor app** — a second executable, installer/update channel, docs and
   support path.
2. **A module inside MeetRec** — same executable and release, a separate window for the
   compression workflow.

Forces:

- The dominant cost is not the encode recipe but the shell: ~12k lines of custom-drawn
  Win32/GDI+ UI, bilingual i18n, `VSR-*` error codes, settings, ffmpeg child-process hosting,
  portable packaging and CI. A second app either duplicates or forces a shared-library
  refactor before the first customer can use it.
- MeetRec is already installed at the customer site; a second app adds download, trust,
  desktop clutter and a second support/update path for a workflow described as "顺便压一下".
- Size cost is near zero: `ffmpeg.exe` is already bundled by `package-release.ps1`, and the
  encoder recipe is already screen-content-tuned. `ffprobe.exe` is the only new binary.
- Product positioning stays coherent: "small files" is already a selling point; compressing old
  recordings is the same story, not a pivot.
- The workflows must not interfere: offline x264 transcoding competes for CPU with the real-time
  x264 encoder and can cause dropped frames in an active recording.

## Decision

We will **ship compression as a module of the existing MeetRec app**, not as a standalone
product, with a hard internal boundary:

- **One executable, one release.** The feature is reached from the main menu and the tray menu
  as **"Compress videos…"** and opens its own top-level window. It is not a tab inside the
  recorder screen and does not touch the recorder state machine.
- **Own code module.** New `src/Transcode/` containing input probing (ffprobe), job/queue model,
  and an ffmpeg child-process runner that parses `-progress pipe:1`. It depends only on
  `Infrastructure` (paths, logging, errors, JSON) — never on `Capture`, `Encoder` pipes, or
  `App` UI. The recorder's `FfmpegWriter` argument builders are **not** reused: they are
  rawvideo-pipe specific. `PresetParams` semantics (CRF, `-tune stillimage`, GOP, yuv420p)
  are reused for file input.
- **Same presets, plus "keep source resolution".** The three existing quality presets apply;
  resolution defaults to the source, with optional downscale. Output is `<name>-min.mp4` in a
  user-chosen folder and never overwrites the source (FR-054).
- **No concurrency with recording.** While `AppState` is Preparing/Recording/Paused/Stopping the
  compressor is disabled (running jobs may finish or be paused); transcode children are created
  with `BELOW_NORMAL_PRIORITY_CLASS`.
- **Expectation management.** ffprobe supplies duration/resolution/frame rate/bitrate; the window
  shows an estimated output size and warns when the source bitrate is already low enough that
  re-encoding may not be worthwhile.
- **Packaging.** Add `ffprobe.exe` to the portable package and to the GPL notice in
  `package-release.ps1`; verify that the bundled build can decode the common source codecs
  (H.265, VP9, AV1) before advertising format support.

The module boundary is deliberate: if compression later becomes a separate product (batch
converter, CLI, service), `src/Transcode/` can be extracted into a library behind a second
front end without rewriting the recorder.

## Alternatives considered

- **Standalone app** — rejected for now: duplicates or refactors the UI/i18n/settings/packaging
  investment before delivering value, and adds a second install, update channel, support path
  and signing surface for a customer request that is explicitly an add-on. Revisit if
  compression becomes its own product line with independent pricing or a batch/CLI audience.
- **A tab/view inside the main recorder window** — rejected: the recorder screen is organized
  around one workflow (select window → check audio → record) and a preview column; a file queue
  does not fit it, and sharing the view would couple the compressor to recorder state. A
  separate window keeps the recorder UI untouched and the module independently testable.
- **Menu command that shells out to a batch script** — rejected: no probing, no estimate, no
  progress, no cancel, no actionable errors; support cost falls back on the customer.
- **Hardware encoder for offline transcoding** — not chosen as the default: hardware encoders
  only offer ABR in this codebase and would not reproduce the size/quality that customers
  praised. May be offered as a speed option later.

## Consequences

- (+) Reuses the UI kit, i18n, settings, error taxonomy, ffmpeg hosting, packaging and CI;
  one download, one update channel, one support path.
- (+) The recorder pipeline and state machine stay untouched; the feature is opt-in and
  isolated in its own window.
- (+) Decision is reversible: a clean `Transcode` module can become a standalone front end later.
- (-) MeetRec is no longer strictly single-purpose; the UI must keep the recorder primary and
  the compressor clearly secondary (menu entry, separate window, no recorder-screen clutter).
- (-) Adds a CPU-heavy background workload; blocked during recording to protect frame rate, and
  run at below-normal priority otherwise.
- (-) Re-encoding already-compressed sources has diminishing returns; source-dependent estimates
  and warnings are required, not optional.
- (-) The portable package grows by `ffprobe.exe`; arbitrary-source decoding expands the
  codec/licensing surface that must be validated.

## Validation evidence

Implemented in `src/Transcode/` (probe, queue, ffmpeg runner, argument builders) and the
`CompressWindow` UI. Verified on 2026-11-11:

- `TranscodeTests` (14 tests) pass alongside the existing suite: 102 tests, 0 failing. They
  cover ffprobe JSON parsing (including attached cover art), `-progress` parsing, argument
  construction and scaling, output naming/collision handling, and a regression test for child
  stdout capture.
- End-to-end (development entry `--compress`): a 12.9 MB H.264/AAC 1280x720 test recording was
  re-encoded to 2.6 MB at Standard and 1.7 MB at Low; `ffprobe` confirms 12.01 s, H.264,
  1280x720, AAC.
- The Compress window was exercised in its empty, queued (probe info + estimate), running
  (progress, cancel) and completed (open file) states at 125% DPI.

Still required before release:

- A corpus of recordings from other apps (Teams/Zoom/OBS MP4, MKV, VFR, odd resolutions,
  multiple audio tracks), each output checked with `MediaValidator`.
- Size/bitrate comparison for all three presets, including an already-low-bitrate source to
  calibrate the "not worthwhile" warning.
- A `--dev-compete` run records a baseline and then a run with a live transcode job. The job
  was suspended 200 ms after recording started and requeued; drops were 13 vs. a 14-frame
  baseline (tolerance 3). The local GDI PrintWindow capture path drops a small number of
  frames even without a transcode, so the claim is "no additional drops", not "zero drops".
- Cancel and failure paths: killing ffmpeg mid-job leaves no partial output and surfaces a
  localized `VSR-*` error.
