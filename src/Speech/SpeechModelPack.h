// MeetRec - local speech model pack discovery (P1 transcription).
//
// The installer stays model-free. A pack directory contains the sherpa-onnx
// SenseVoice int8 export and Silero VAD:
//
//   models/sensevoice/model.int8.onnx
//   models/sensevoice/tokens.txt
//   models/sensevoice/silero_vad.onnx
//
// Search order: explicit override (settings/tests), VSR_SPEECH_MODEL_DIR,
// <exe>/models/sensevoice, <exe>/models/sensevoice-int8.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vsr::speech {

struct SpeechModelPaths {
    std::filesystem::path directory;
    std::filesystem::path model;
    std::filesystem::path tokens;
    std::filesystem::path vad;
};

struct SpeechModelStatus {
    bool ready = false;
    SpeechModelPaths paths;
    std::wstring detail; // technical, for logs and diagnostics
};

// `overrideDirectory` is empty when the caller has no preference.
SpeechModelStatus LocateSpeechModel(const std::filesystem::path& overrideDirectory = {});

// <exe>/models/sensevoice; the path shown to the user when nothing is found.
std::filesystem::path DefaultSpeechModelDirectory();

// True when the sherpa-onnx runtime DLLs are deployable next to the exe.
// Used by the session before it calls into the C API.
bool SpeechRuntimeAvailable(std::wstring& detail);

} // namespace vsr::speech
