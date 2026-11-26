// MeetRec - `--speech-test` development/QA entry point (P1).
//
// Runs the real SpeechSession (Silero VAD + SenseVoice int8) over a 16-bit
// PCM WAV without capture or encoding, then writes the same .srt/.txt pair a
// recording would produce. Used to validate the model pack and timestamp
// mapping on real meeting audio.
#pragma once

namespace vsr::app {

// argv: --speech-test <wav> [--model <dir>] [--out <base>]
int RunSpeechSelfTest(int argc, wchar_t* argv[]);

} // namespace vsr::app
