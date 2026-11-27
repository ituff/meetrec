# ADR-009: Local meeting transcription with SenseVoiceSmall int8 (P1)

- **Status:** Accepted (P1 transcript and P2 live caption overlay implemented)
- **Date:** 2026-11-26
- **Deciders:** MeetRec (Virtual Screen Recorder) project
- **Related requirements:** meeting transcript (new, 2026-11), FR-090..FR-092 (settings),
  FR-094 (no content in logs), NFR-014 (bounded teardown), NFR-033 (static first-party modules)

## Context

Users record meetings with MeetRec and then need a text record of what was said. The
2026-11-26 A/B evaluation on a real 20-minute meeting (`Recording_20261126_110113.mp4`) compared
faster-whisper large-v3 with SenseVoiceSmall int8 ONNX on the same 5m21s sample:

| | faster-whisper large-v3 | SenseVoiceSmall int8 |
|---|---:|---:|
| Inference (321.5s audio) | 762 s (0.42× real time) | 34 s (9.5× real time) |
| Model size | 2,944 MB | 237 MB |
| Failure mode | repeated "点赞/订阅" hallucination over ~4 min | complete coverage, punctuated |

SenseVoiceSmall is a non-autoregressive CTC model, so it cannot stream token by token; a VAD
must cut the audio into utterances. The product already captures the two audio sources
separately (WASAPI loopback = participants, microphone = local speaker) before mixing them into
one AAC track, which is exactly the split a two-party transcript needs.

Constraints:

- Portable, no-install app; adding a 230 MB model to the main zip is unacceptable.
- Recording must never depend on the model, the runtime DLLs or the network.
- Everything stays local; the product's privacy posture forbids uploading audio.
- Model weights are under the FunASR Model Open Source License Agreement v1.1, not a
  permissive OSI license; redistribution needs the license text and attribution.

## Decision

We will ship **post-recording meeting transcription as an optional local module**:

- **Runtime**: sherpa-onnx 2.0.3, Windows x64 shared `/MT` build, pinned and SHA-256 verified
  by `scripts/fetch-sherpa-onnx.ps1`. The C API DLL is **delay-loaded**, so a deployment
  without `sherpa-onnx-c-api.dll`/`onnxruntime.dll` still starts and records; the transcript
  feature reports why it is unavailable.
- **Model pack**: the sherpa-onnx export of SenseVoiceSmall int8 (2024-07-17,
  `model.int8.onnx` 239 MB + `tokens.txt`) plus Silero VAD, distributed **separately** as
  `MeetRec-<version>-win64-speech-model.zip`. It unpacks to `models/sensevoice/` next to
  `MeetRec.exe`. The FunASR export used by `funasr_onnx` is **not** compatible (no
  sherpa-onnx metadata, different `tokens.txt`); the fetch script documents this.
- **Audio path**: `AudioCapture` gets a per-source mono tap in its mixer loop, before the two
  sources are summed. `SpeechSession` runs on a dedicated below-normal-priority thread:
  per-source bounded queue -> 48 kHz/44.1 kHz to 16 kHz resampler -> Silero VAD ->
  SenseVoiceSmall -> finalized cue. Source separation gives a free "对方/Them" and "我/Me"
  label without diarization.
- **Timeline**: cue timestamps use the same `RecordingTimeline` mapping as the video and AAC
  track, so paused intervals are excluded. Pause flushes the in-flight utterance; a dropped
  tap chunk resets the VAD epoch instead of corrupting later times.
- **Output**: each finalized cue is appended immediately to
  `<recording>.partial.srt` (crash-safe journal); on stop the partial is closed and
  `<recording>.srt` + `<recording>.txt` (UTF-8 BOM, speaker-labelled) are written next to the
  MP4. Existing files are never overwritten; collisions get `-1`, `-2`.
- **Settings**: opt-in **Settings -> Recording -> "Save a meeting transcript"**
  (`generateTranscript`, `speechModelDirectory`). Enabling requires a ready model pack and at
  least one audio source. A missing pack/runtime/audio source degrades to a normal recording
  and is logged; it never fails `Start`.
- **Logging**: cue text is never logged (FR-094); only counts, paths, timings and error codes.

P1 delivers the transcript files. P2 adds a **sentence-level live caption overlay** driven by
the same finalized cues (see the P2 addendum below); it does not add word-by-word interim
decoding, which would multiply CPU use on the recording machine.

## Alternatives considered

- **faster-whisper large-v3** — 22× slower on the evaluation machine and produced four minutes
  of hallucinated text that replaced real speech. Rejected.
- **funasr_onnx (Python)** — would add a Python runtime to a portable C++ app and cannot be
  embedded as a static module. Rejected; its model export is also unreadable by sherpa-onnx.
- **Q4 GGUF SenseVoice** — smaller (~129 MB) but only third-party quantizations exist and it
  requires a separate ggml runtime. int8 ONNX via sherpa-onnx has the best-supported C API and
  VAD and was chosen for P1; Q4 remains a possible later size optimization behind the
  `SenseVoiceRecognizer` wrapper.
- **Transcribe the mixed AAC track after recording** — loses the loopback/mic split (and thus
  speaker labels), re-decodes a lossy track, and makes the transcript wait for the MP4. The
  live tap is cheaper and uses the original PCM.

## Consequences

- The main package grows by the sherpa runtime (~23 MB of DLLs); the optional model pack adds
  ~155 MB compressed (~230 MB on disk). Neither is installed or downloaded silently.
- Recognition is utterance-level: cues appear after the VAD endpoint, timestamps are
  segment-level, and a very short noise/music segment can be misrecognized (one Japanese
  hallucination was observed at 00:36 in the 60 s smoke sample). Live caption polish must
  address this in P2.
- No hotword/phrase boosting in SenseVoice: names and product terms need manual correction or
  a later terminology-replacement step.
- The model license is not OSI-standard; the pack ships the FunASR license and attribution,
  and each release must keep those notices. Commercial redistribution should be reviewed by
  legal.
- Speech failures never block recording, but they also fail quietly from the user's point of
  view in P1 (log + missing transcript). The P2 overlay/status surface should show model state.

## Validation evidence

- `artifacts/Debug/tests/UnitTests.exe`: 119 tests pass, including resampler continuity,
  SRT/TXT formatting, collision handling, crash-journal cleanup and model-pack discovery.
- `MeetRec.exe --speech-test slice_190_250.wav`: 60 s -> 9 cues; model load 2.5 s; SRT/TXT
  written with `00:00:00,166` as the first cue.
- A 20 s window recording with a WAV played through the default render endpoint and
  `--audio --transcript`: `SPEECH_SESSION_STARTED` (4 threads), model ready in 4.5 s, 2 cues,
  `droppedChunks=0`, and `<recording>.srt`/`.txt` next to the MP4.
- Deleting `sherpa-onnx-c-api.dll` still starts the app and `--version`; `--speech-test`
  reports the missing runtime instead of crashing.
- A model directory with a Chinese path (junction) produced the same 9 cues, confirming UTF-8
  path handling on Windows.

## P2 addendum: live caption overlay (2026-11-26)

The transcript worker now publishes each finalized cue to a `CaptionFeed` (mutex-protected
ring of the newest cues, no UI dependency). A `CaptionOverlay` window owned by the main window
polls the feed:

- `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST`,
  so it is click-through, never takes focus, and follows the recorded window's bottom edge
  (100 ms timer). It hides while paused/minimized and is destroyed with the main window.
- It draws the newest cue plus the previous one in a dark rounded panel; the source color and
  accent bar distinguish "对方/Them" (blue) from "我/Me" (green). Text is wrapped by the shared
  subtitle formatter; a speaker prefix is prepended to the first line so it never sits alone.
- Enabled by **Settings -> Recording -> Show live captions while recording**
  (`showLiveCaptions`, default on), the footer **CC icon** button, the tray **Live captions**
  item, or `Ctrl+Alt+S`.
- Appearance lives in `CaptionStyle` (`captionFont`, `captionFontSize`, `captionColor`,
  `captionPosition`): YaHei/Segoe UI/SimSun/Arial, 16-32 px, auto/white/yellow/blue/green/
  orange text, and bottom-center/top-center/bottom-left/bottom-right anchors. The overlay
  shrinks to the text width and re-lays out when the style changes during a recording.
- VAD `max_speech_duration` was reduced from 15 s to 10 s so a continuous utterance cannot
  delay its caption by 15 s. The final SRT uses the same (slightly shorter) cues.

Why no interim decoding: SenseVoice is non-autoregressive, so partial text requires
re-decoding the growing utterance every few hundred milliseconds. At the measured RTF
(~0.14 at 4 threads) that costs more than a core for two sources; the sentence-level overlay
keeps the recording path safe and is what the evaluation corpus supports.

Validation:

- `artifacts/Debug/tests/UnitTests.exe`: 124 tests pass (CaptionFeed ordering/trim added).
- A 22 s window recording with the evaluation WAV played through the default render endpoint
  showed the overlay over the recorded window: previous cue dim, latest cue in blue with the
  speaker prefix inline, positioned above the taskbar; the MP4 and SRT/TXT were produced.
- The dev CLI path (`--transcript`) starts GDI+ only for the overlay lifetime; removing the
  overlay leaves recording unchanged.

## Model installation addendum

The model pack is no longer a manual-only step. When the user enables the transcript and
`LocateSpeechModel()` reports no pack, the settings window asks for confirmation and starts an
in-app installer:

- Pinned URLs/hashes from `SpeechModelDownload()` (sherpa-onnx export + Silero VAD). The
  downloader tries the same GitHub asset proxy as the updater and falls back to github.com;
  both files are SHA-256 verified before extraction.
- Windows `tar.exe -xjf` extracts the archive; `model.int8.onnx`, `tokens.txt`,
  `silero_vad.onnx`, the license pointer and `version.txt` are installed into
  `PreferredSpeechModelDirectory()`: `<exe>/models/sensevoice` when writable, otherwise
  `%LOCALAPPDATA%\VirtualScreenRecorder\models\sensevoice`. `LocateSpeechModel()` searches
  both.
- A custom modal `ModelDownloadWindow` shows download/verify/extract/install phases, a
  progress bar, bytes and a Cancel button. Cancelling aborts the HTTP stream or terminates
  the tar child; a temp-cleanup guard removes the archive and extraction directory.
- No silent download: the confirmation dialog is required. Offline/enterprise users can still
  pre-seed the directory or use the release model zip.

Validation: the headless `--download-speech-model` path was exercised against a local HTTP
mirror of the pinned archive; it downloaded, verified, extracted and installed 230 MB under
`models/sensevoice`, and `--speech-test` then recognized the 60 s sample without
`VSR_SPEECH_MODEL_DIR`.

## Live translation addendum

Live captions can optionally be translated. This is a deliberate exception to the
"everything local" rule and is therefore **opt-in and clearly disclosed**:

- The old `edge.microsoft.com/translate/auth` token flow was retired in 2026 (404 on every
  request), so the implementation uses the current unauthenticated endpoint
  `POST https://edge.microsoft.com/translate/translatetext?from=&to=<lang>&isEnterpriseClient=false`.
  The service rejects raw UTF-8 CJK bodies with HTTP 500, so the request body is serialized as
  ASCII `\uXXXX` escapes. One request per cue, no token, no API key.
- `CaptionTranslator` polls the `CaptionFeed` from the main UI timer, translates the newest cues
  on a worker thread through `BingTranslator` (WinHTTP), and writes the result back into the
  feed. Only one request is in flight; a bounded queue (32) and a failed marker prevent retry
  storms. The overlay shows the original in small dim text and the translation in the large
  speaker-colored text.
- Target languages: en, zh-Hans, ja, ko, fr, de, es, ru. Enabled only when the transcript and
  live captions are enabled; the setting is off by default. Failures keep the original caption
  and only log a warning.
- Privacy: only the recognized text is sent to Microsoft; audio never leaves the machine. The
  settings page states this explicitly. Translation applies to the live overlay only; SRT/TXT
  remain the local original transcript.
- The endpoint is unofficial and may break or rate-limit; the fallback is simply the original
  caption. Users who cannot send text online leave the option off.

Validation: `--translate-test "你好，世界..." --to en` returned
"Hello, world. Today, we are meeting to discuss the project progress."; the feed-level
`--caption-translate-test` produced the same through `CaptionTranslator`; a real 22 s dev
recording displayed the Chinese original and English translation in the overlay, captured with
`PrintWindow` while the desktop was locked.
