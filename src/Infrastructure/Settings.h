// Virtual Screen Recorder - user settings store (FR-090..FR-092, design.md §4.10).
#pragma once

#include <filesystem>
#include <string>

#include "Json.h"

namespace vsr {

// Quality preset names as stored in settings (spec.md §7.3).
enum class QualityPresetName { Low, Standard, High };

QualityPresetName PresetNameFromString(const std::string& s) noexcept;
const char* ToString(QualityPresetName preset) noexcept;

struct Settings {
    int schemaVersion = 1;
    std::wstring outputDirectory;      // empty = default Videos folder
    QualityPresetName preset = QualityPresetName::Standard;
    // Encoder class preference: "auto" / "hardware" / "software". Default is
    // SOFTWARE: the OS-shipped encoder avoids vendor MFT reliability issues
    // (observed AMD MFT stalls + driver degradation under repeated sessions).
    // (validated on
    // load; anything else falls back to "auto"). Interpreted by the Encoder lib.
    std::string encoderPreference = "software";
    // Recording backend: "auto" (ffmpeg when available, else Media
    // Foundation) / "ffmpeg" / "mf". The ffmpeg backend bypasses the OS
    // media stack whose encoder MFTs are unreliable on Insider builds.
    std::string encoderBackend = "auto";
    bool showPreview = true;
    bool captureCursor = false;
    bool minimizeToTray = true;
    bool restoreTargetWindow = true;
    // System audio (WASAPI loopback) mixed into the recording. Opt-in: the
    // AAC encoder finalize hangs on some Insider builds (26200 observed).
    bool recordSystemAudio = false;
    // T103: local crash dumps are strictly opt-in and never uploaded.
    bool enableCrashDumps = false;
};

class SettingsStore {
public:
    SettingsStore();

    // Loads settings; unknown fields are returned via UnknownFields() so a
    // later Save preserves them (FR-091). Missing or invalid fields fall back
    // to defaults; `usedDefaults` reports whether any fallback happened so the
    // caller can log a warning (FR-092).
    Settings Load(bool* usedDefaults = nullptr);

    // Atomically persists settings (write temp, flush, replace).
    bool Save(const Settings& settings);

    const json::Value& UnknownFields() const noexcept { return unknown_; }

private:
    std::filesystem::path path_;
    json::Value unknown_;
};

} // namespace vsr
