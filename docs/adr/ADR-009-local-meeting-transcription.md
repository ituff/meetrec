# ADR-009: Local meeting transcription with SenseVoiceSmall int8 (P1)

- **Status:** Accepted (P1 implemented; live overlay captions are the next phase)
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

P1 deliberately stops before live overlay captions. The recognition path already produces
finalized cues during recording, which is the foundation for the P2 overlay.

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
