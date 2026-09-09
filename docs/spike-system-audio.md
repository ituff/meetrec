# Spike report: optional system audio (P1 / T102)

- **Status:** Evaluation only — no audio code was implemented, per the task's stop rule
  ("Do not implement audio until the spike and spec update are approved").
- **Date:** 2026-09-10

## Scope of the evaluation

Assess what MVP+1 would need to record system audio alongside the virtual display video:
capture API, synchronization, encoding, device changes, and privacy UX.

## 1. Capture path — WASAPI loopback

- The in-box approach is WASAPI loopback (`IAudioClient::Initialize` with
  `AUDCLNT_STREAMFLAGS_LOOPBACK` on a *render* endpoint) — no kernel filter or virtual audio
  device required.
- Estimated footprint: one dedicated thread, 10 ms period packets, `IAudioCaptureClient` reads.
  Complexity is modest and mirrors the existing capture-worker pattern; it would live in a new
  `Audio` static library next to `Capture`/`Encoder`.
- Render endpoints only: loopback does not capture microphone input. A separate capture-client
  on the communications device would be needed for narration (P2, larger privacy surface).
- Process-loopback (`ActivateAudioInterfaceAsync` with `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`,
  Windows 10 2004+) exists but is per-process — wrong model for "record the virtual display";
  exclude.

## 2. Synchronization

- WASAPI packets carry `GetBuffer`-time QPC stamps in the same time domain as
  `QueryPerformanceCounter`; our recording timeline is already QPC-based, so mapping to the
  sink writer's 100 ns units is direct.
- Video frames currently use *capture-callback* arrival times, not composition times. For A/V
  sync the muxer needs a shared anchor: anchor both streams at Recording-state entry
  (`timeline_.StartAt`), then rely on each stream's own pacing. Expected skew budget ±40 ms —
  acceptable for demos; lip-sync perfection is NOT promised.
- Drift: WASAPI shared mode mixes at the endpoint's mix rate; video frame intervals wobble.
  Sink writer handles per-stream pacing; duration tolerance would need updating (spec.md §9
  says video duration within 2 s — audio track length should match the video within ±0.5 s).

## 3. Encoding and container

- AAC-LC via the in-box Microsoft AAC encoder MFT (`MFT_CATEGORY_AUDIO_ENCODER`) at 192 kbps
  stereo, 48 kHz — hardware audio encoders are not a thing on typical machines; CPU cost is
  negligible vs. video.
- MP4 sink writer supports adding an audio stream (MFAudioFormat_AAC → MP4) before
  `BeginWriting`. A/V interleaving is handled by the sink writer; no container change needed.
- Files stay H.264 video + AAC audio — still within the "MP4, in-box codecs" product frame.

## 4. Device changes and failure modes

- Default-render-device change mid-recording: loopback sessions attach to one endpoint;
  handling requires re-activation at the boundary — MVP+1 should **stop audio and continue
  video** on device loss (video continuity is the product's promise), with a UI notice.
- No audio device at all: record video-only, log an event, no user-visible error (matches
  FR-061's spirit that audio is optional).

## 5. Privacy UX

- System loopback captures *everything* audible (notifications, other apps, DRM-protected
  streams output through the endpoint). Spec FR-094/NFR-025 analogues apply: a persistent
  recording indicator must state that system audio is captured; a checkbox (settings +
  start-flow) gates it; default OFF.
- Some content (protected audio paths) yields silence by design; document as a limitation —
  never attempt bypass (NFR-025).

## Recommendation

Feasible within the native architecture (WASAPI + AAC MFT + existing sink writer) with roughly
one new library, a settings field, one UI toggle, and media-validator extensions. Effort
estimate: ~1.5–2 weeks implementation plus soak. **Decision requested:** approve scope as
MVP+1; update spec (non-goals §4, FR-061) and tasks before implementation begins.
