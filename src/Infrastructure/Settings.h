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
    // The user's microphone mixed into the same audio track as the system
    // loopback (meeting recording needs both ends of the call). On by
    // default whenever audio recording is enabled.
    bool recordMicrophone = true;
    // Audio endpoint IDs to record (empty = the Windows default). Meeting
    // apps keep their own device selection, which can differ from the
    // Windows default - recording the wrong endpoint is silent for that
    // source, so the UI lets the user match the app's devices.
    std::wstring audioOutputDeviceId;
    std::wstring audioInputDeviceId;
    // UI language: "system" follows the Windows display language; "zh-CN" and
    // "en-US" are explicit overrides. Unknown values fall back to "system".
    std::string uiLanguage = "system";
    // Post-recording behavior (Settings > General).
    bool showCompletionNotification = true;
    bool autoOpenOutputFolder = true;
    // Preview column share of the main window's content width (0.20..0.80).
    // The splitter between settings and preview writes this value; the left
    // column keeps a fixed minimum so every control stays usable.
    double previewPaneRatio = 0.40;
    // T103: local crash dumps are strictly opt-in and never uploaded.
    bool enableCrashDumps = false;
    // Automatic GitHub release checks (at most once per day). The timestamp is
    // Unix seconds; the notified version prevents repeat toasts.
    bool autoCheckUpdates = true;
    long long lastUpdateCheckUnix = 0;
    std::string lastNotifiedUpdateVersion;
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
