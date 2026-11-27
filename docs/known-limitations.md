# Known limitations (MVP 1.0)

User-facing limitations follow spec.md §1 and §11. This file is the source for the README and
release notes sections with the same purpose.

## Rendering of the recorded application

- The recorded application must stay in a **normal rendering state on the virtual display**.
  Minimizing the target application may freeze or stop its rendering in the video — Windows may
  stop redrawing minimized applications (FR-030; the controller shows this note in the UI).
- Applications protected by DRM or that deliberately suppress capture will record as a black or
  frozen image. The product never attempts to bypass capture restrictions (NFR-025).
- Secure desktop surfaces (UAC consent, lock screen, sign-in) are never captured.

## Virtual display

- Exactly **one** virtual monitor ("Virtual Recording Display") is provided. Multiple virtual
  displays are out of MVP scope.
- The virtual display is SDR, 8-bit only, with modes 1280x720@30, 1920x1080@30 (preferred), and
  1920x1080@60. HDR, H.265/AV1, and 4K outputs are not supported.
- On builds that draw a yellow border around captured content, the border may be visible in the
  recording. Suppressing it requires OS consent APIs not used by the MVP.
- If the virtual display is disconnected mid-recording, the recorder stops **safely**,
  finalizes the file, and reports `VSR-DISPLAY-DISCONNECTED` — the video up to that point is
  preserved (FR-015).

## Driver signing (internal builds)

- Internal builds ship a **test-signed** driver. Installing it requires Windows Test Mode
  (`bcdedit /set testsigning on`) and trusting the exported certificate. Production-signed
  distribution is a separate release gate (spec.md §13).
- Third-party display mirroring software, GPU driver rollbacks mid-session, and virtualization
  (Remote Desktop, Sandbox, nested GPU) can break virtual display creation or capture and are
  outside the support boundary (spec.md §11).

## Recording behavior

- **Audio recording is opt-in** (the "Record system audio" checkbox, or `--audio` on the dev
  CLI; default off). Two sources can be recorded and are mixed into one AAC track: WASAPI
  loopback of a render endpoint (participants/system sound) and, when "Record microphone" is
  also ticked (the default when audio is enabled), a capture endpoint (your own voice). Both
  endpoints are chosen in the two device lists below the checkboxes; the first entry follows
  the **Windows default** (render: console role; capture: communications role).
- Meeting apps keep their **own** audio device selection. The recorder cannot read it, so the
  lists must be matched to the app: if Teams (or Zoom / Tencent Meeting) uses the built-in
  microphone array while Windows' default mic is a headset, select the array in the
  Microphone list - otherwise your voice is not in the recording and that source stays
  silent. The log records the actual endpoints (`AUDIO_LOOPBACK_MIX_FORMAT`,
  `AUDIO_MIC_MIX_FORMAT`).
- Endpoint changes mid-recording are not followed. Plugging in a headset or letting a
  Bluetooth headset connect while recording invalidates the stream; the source thread logs
  `AUDIO_..._SOURCE_EXITED` and that source stays silent for the rest of the recording while
  the other one continues. Select the device you will use before starting.
- No acoustic echo cancellation. With speakers, the microphone picks up the participants
  along with your voice, so the recording contains a delayed copy of the remote side. Use
  headphones for clean two-sided meeting audio.
- The loopback track captures **everything** the selected output renders (meeting audio,
  notifications, music, other apps). Content on protected audio paths records as silence;
  the recorder never attempts to bypass that (NFR-025).
- Microphone level is fixed (no auto gain); `VSR_MIC_GAIN=<multiplier>` adjusts it
  (0.25-8.0) when your voice is much quieter than the participants. A stretch with no
  audible sound records as silence. AAC finalization has been observed to hang on Windows
  Insider builds (see the encoder section below), which is why audio stays opt-in until
  verified on retail builds.
- **Microphone mute during a recording**: the footer microphone button, the tray menu item, or
  `Ctrl+Alt+M` removes the microphone from the recorded mix only - the meeting app still hears
  you. Muting the microphone in Windows Sound settings (or a headset switch that the driver
  reports as endpoint mute) is followed automatically and also removes it from the recording.
  A meeting app's own **Mute** button is internal to that app and is not visible to other
  processes, so it does **not** stop the microphone track. The recorder only reads the
  endpoint mute flag and never calls `SetMute`, so it never changes the system or meeting mute
  state. Mute detection follows the endpoint selected in MeetRec; if the meeting app uses a
  different microphone, its mute is not observed (select the same device in both apps).
- Recordings pause correctly (paused time is excluded from the output, AC-005), but frames are
  not buffered during pause: what happens on the virtual display during a pause is not recorded
  and cannot be recovered.
- If free disk space hits the documented stop threshold — max(256 MB, 30 seconds at the active
  bitrate) — recording stops and finalizes automatically (`VSR-OUTPUT-DISK-LOW`, AC-007).
- A hard crash or power loss can leave an unfinalized `.partial.mp4`; the file may not be
  playable. Controlled stops always finalize (design.md risk R-007).
- If no hardware H.264 encoder is available, the recorder falls back to the Microsoft software
  encoder (higher CPU use); with no software encoder either, it fails before recording with
  `VSR-ENCODER-UNAVAILABLE` (FR-063).
- With the bundled ffmpeg backend (the default), the **Automatic** encoder mode uses CPU x264.
  **Hardware (GPU)** uses a detected `h264_qsv` / `h264_nvenc` / `h264_amf` encoder with the
  same capped constant-quality recipe as the compressor, so CPU usage during recording drops
  markedly and file sizes stay close to a compressed recording; stability still depends on the
  graphics driver. If no GPU encoder passes the startup test, recording stops before capture
  with `VSR-ENCODER-UNAVAILABLE`; switch back to Automatic or Software. For long, critical
  recordings, run a short hardware-mode test first.
- On some GPU drivers the hardware encoder rejects GPU-resident samples; the recorder detects
  this at start (before any content is recorded) and automatically switches to a CPU staging
  path (slightly higher CPU, identical output). The first frame of every recording is a brief
  encoder-priming black frame (<= 1 frame duration).
- Media Foundation applies no backpressure on encoder input; the recorder therefore feeds
  frames at wall-clock rate only. A hard-stalled encoder for the whole session would
  accumulate memory in the media pipeline — monitored via the AC-008 two-hour soak on
  reference machines.

## Meeting transcript (P1)

The local transcript is opt-in and separate from the recording pipeline:

- It needs the SenseVoiceSmall int8 model pack (~230 MB on disk) under
  `models/sensevoice/`, plus the sherpa-onnx runtime DLLs that ship with the app. The first
  time the transcript is enabled, the app offers to download and install it (with progress and
  cancel); an offline model zip is also published. If the pack or the DLLs are missing,
  recording still works and the transcript is skipped (logged).
- Recognition is **local only**; audio is never uploaded. The app only downloads the model
  after the user confirms the prompt; it never fetches it silently.
- Supported languages are the model's: Mandarin, Cantonese, English, Japanese and Korean
  (automatic language detection per utterance). Other languages are not supported.
- SenseVoice has **no hotword/phrase boosting**. Names, product names and acronyms are often
  wrong; treat the transcript as a first draft that needs terminology review.
- Timestamps come from VAD segment boundaries, not native word alignment. Cues are
  sentence-level (typically 2-15 s); long continuous speech is force-split at 15 s.
- A very short noise/music segment can be misrecognized (observed: a 0.9 s segment produced
  Japanese text in an otherwise Chinese meeting). Review short odd cues.
- The transcript requires **system audio and/or the microphone**. With both sources off, or
  with transcription disabled in Settings, no `.srt`/`.txt` is produced. The two sources are
  recognized separately and labelled "对方/Them" (loopback) and "我/Me" (microphone); with
  speakers, microphone echo produces duplicate lines (use headphones).
- The live caption overlay is **sentence-level**: a cue appears after the VAD endpoint and the
  segment decode, typically 0.5-1.5 s after the phrase (continuous speech is force-split after
  at most 10 s). There is no word-by-word interim text.
- The overlay is a topmost, click-through window that follows the recorded window; it hides
  while paused or when the target is minimized. It is **not part of the MP4** (MeetRec records
  only the target window), but it is visible to screen-sharing tools if you share the whole
  desktop.
- Cues are written during recording to a crash journal (`<recording>.partial.srt`); the final
  `.srt`/`.txt` appear next to the MP4 after stop.
- **Live translation is opt-in and online.** When enabled, the recognized text is sent to the
  free Bing/Edge public endpoint (`edge.microsoft.com/translate/translatetext`) and the
  translation is shown under the original in the overlay; audio is never uploaded. The endpoint
  is unofficial, requires network access, and may be rate-limited, change or disappear (the
  older token endpoint was retired in 2026). Failures leave the original caption visible.
- Translation applies to the **live overlay only**; the `.srt`/`.txt` files contain the original
  transcript. Target languages: English, Simplified Chinese, Japanese, Korean, French, German,
  Spanish, Russian.
- Recognition runs on a below-normal-priority worker with a bounded queue. On a very slow
  machine the transcript/captions may lag behind the recording; the recording itself is
  unaffected.
- The model weights are licensed under the FunASR Model Open Source License Agreement v1.1;
  the pack includes the license and attribution. See
  `docs/adr/ADR-009-local-meeting-transcription.md`.

## Encoder media stack (Windows Insider build 26200 findings)

Recording verification on Insider build 26200 machines exposed encoder-MFT failure modes the
recorder now detects and contains (see `docs/task-completion-notes.md`, session 12):

- **Per-session encoder stalls**: a hardware encoder MFT session may accept samples without
  ever encoding them, wedge in `WriteSample`, or hang in `Finalize` — with the odds changing
  per process. The recorder gates every session start on the encoder actually consuming the
  priming frame, fails over to the next encoder candidate, stops mid-recording when the
  encoded-count freezes while frames keep arriving, and runs an independent watchdog thread
  for the wedged-`WriteSample` case. A stalled session therefore ends in a bounded,
  process-surviving stop with the partial file kept — not a hang.
- **The Microsoft software H.264 encoder was observed deterministically dead** on one 26200
  machine (zero encoded samples ever; `MF_E_SINK_NO_SAMPLES_PROCESSED` at finalize). The
  "software" encoder preference is not a guaranteed escape hatch on Insider builds.
- These failures survived a display-adapter restart; only a reboot restored healthy encoder
  sessions on the affected machine. Sustained-recording evidence (soaks) should be collected
  shortly after a reboot on machines showing this family of behavior.

## Environment

- Windows 10 2004+ x64 or Windows 11 x64; interactive sessions only. Remote Desktop,
  Windows Sandbox, and unattended/services contexts are unsupported (NFR-030, spec.md §11).
- 32-bit x86 and ARM64 are not supported.
- Mixed-DPI environments are supported (Per-Monitor V2), but applications that misbehave when
  moved between DPI boundaries may render oddly after placement — the recorder restores their
  original placement on stop.
