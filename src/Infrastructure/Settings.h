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
    bool showPreview = true;
    bool captureCursor = false;
    bool minimizeToTray = true;
    bool restoreTargetWindow = true;
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
