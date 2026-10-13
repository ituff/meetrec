#include "Settings.h"

#include <windows.h>

#include "Paths.h"
#include "Strings.h"

#include <fstream>
#include <streambuf>

namespace vsr {

QualityPresetName PresetNameFromString(const std::string& s) noexcept {
    if (s == "low") return QualityPresetName::Low;
    if (s == "high") return QualityPresetName::High;
    return QualityPresetName::Standard;
}

const char* ToString(QualityPresetName preset) noexcept {
    switch (preset) {
        case QualityPresetName::Low: return "low";
        case QualityPresetName::High: return "high";
        case QualityPresetName::Standard: return "standard";
    }
    return "standard";
}

SettingsStore::SettingsStore() : path_(SettingsPath()) {}

Settings SettingsStore::Load(bool* usedDefaults) {
    if (usedDefaults) *usedDefaults = false;
    unknown_ = json::Value{};

    std::ifstream file(path_, std::ios::binary);
    if (!file.good()) {
        if (usedDefaults) *usedDefaults = true;
        return Settings{};
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    bool parsed = false;
    const json::Value root = json::Parse(text, &parsed);
    if (!parsed || root.GetType() != json::Type::Object) {
        if (usedDefaults) *usedDefaults = true;
        return Settings{};
    }

    Settings settings;
    bool anyFallback = false;

    // Known fields; invalid types fall back to defaults (FR-092).
    {
        const json::Value& v = root.Find("schemaVersion");
        if (v.GetType() == json::Type::Number) {
            settings.schemaVersion = static_cast<int>(v.AsInt(1));
        } else if (!v.IsNull()) {
            anyFallback = true;
        }
    }
    {
        const json::Value& v = root.Find("outputDirectory");
        if (v.GetType() == json::Type::String) {
            settings.outputDirectory = Utf8ToWide(v.AsString());
        } else if (!v.IsNull()) {
            anyFallback = true;
        }
    }
    {
        const json::Value& v = root.Find("qualityPreset");
        if (v.GetType() == json::Type::String) {
            settings.preset = PresetNameFromString(v.AsString());
        } else if (!v.IsNull()) {
            anyFallback = true;
        }
    }
    {
        const json::Value& v = root.Find("encoderPreference");
        if (v.GetType() == json::Type::String) {
            const std::string value = v.AsString();
            if (value == "auto" || value == "hardware" || value == "software") {
                settings.encoderPreference = value;
            } else {
                anyFallback = true;
            }
        } else if (!v.IsNull()) {
            anyFallback = true;
        }
    }
    {
        const json::Value& v = root.Find("recordSystemAudio");
        if (v.GetType() == json::Type::Boolean) settings.recordSystemAudio = v.AsBool();
        else if (!v.IsNull()) anyFallback = true;
    }
    {
        const json::Value& v = root.Find("showPreview");
        if (v.GetType() == json::Type::Boolean) settings.showPreview = v.AsBool();
        else if (!v.IsNull()) anyFallback = true;
    }
    {
        const json::Value& v = root.Find("captureCursor");
        if (v.GetType() == json::Type::Boolean) settings.captureCursor = v.AsBool();
        else if (!v.IsNull()) anyFallback = true;
    }
    {
        const json::Value& v = root.Find("minimizeToTray");
        if (v.GetType() == json::Type::Boolean) settings.minimizeToTray = v.AsBool();
        else if (!v.IsNull()) anyFallback = true;
    }
    {
        const json::Value& v = root.Find("restoreTargetWindow");
        if (v.GetType() == json::Type::Boolean) settings.restoreTargetWindow = v.AsBool();
        else if (!v.IsNull()) anyFallback = true;
    }
    {
        const json::Value& v = root.Find("enableCrashDumps");
        if (v.GetType() == json::Type::Boolean) settings.enableCrashDumps = v.AsBool();
        else if (!v.IsNull()) anyFallback = true;
    }

    // Preserve unknown fields verbatim (FR-091).
    for (const auto& kv : root.AsObject()) {
        const bool known = kv.first == "schemaVersion" || kv.first == "outputDirectory" ||
                           kv.first == "encoderPreference" ||
                           kv.first == "qualityPreset" || kv.first == "showPreview" ||
                           kv.first == "captureCursor" || kv.first == "minimizeToTray" ||
                           kv.first == "restoreTargetWindow" ||
                           kv.first == "recordSystemAudio" ||
                           kv.first == "enableCrashDumps";
        if (!known) {
            unknown_.Set(kv.first, kv.second);
        }
    }

    if (anyFallback) {
        if (usedDefaults) *usedDefaults = true;
    }
    return settings;
}

bool SettingsStore::Save(const Settings& settings) {
    json::Value root{json::Object{}};
    root.Set("schemaVersion", json::Value(settings.schemaVersion));
    root.Set("outputDirectory", json::Value(WideToUtf8(settings.outputDirectory)));
    root.Set("qualityPreset", json::Value(ToString(settings.preset)));
    root.Set("encoderPreference", json::Value(settings.encoderPreference));
    root.Set("showPreview", json::Value(settings.showPreview));
    root.Set("captureCursor", json::Value(settings.captureCursor));
    root.Set("minimizeToTray", json::Value(settings.minimizeToTray));
    root.Set("restoreTargetWindow", json::Value(settings.restoreTargetWindow));
    root.Set("recordSystemAudio", json::Value(settings.recordSystemAudio));
    root.Set("enableCrashDumps", json::Value(settings.enableCrashDumps));

    // Re-attach preserved unknown keys verbatim (FR-091).
    if (unknown_.GetType() == json::Type::Object) {
        for (const auto& kv : unknown_.AsObject()) {
            root.Set(kv.first, kv.second);
        }
    }

    // Atomic replacement: write temp -> flush -> replace (design.md §4.10).
    const std::filesystem::path temp = path_.wstring() + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.good()) return false;
        const std::string text = root.Serialize();
        out.write(text.c_str(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            DeleteFileW(temp.c_str());
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

} // namespace vsr
