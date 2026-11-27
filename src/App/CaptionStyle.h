// MeetRec - live caption appearance model (P2).
//
// Kept separate from the overlay window so both the settings UI and the
// renderer share the same keys, defaults and fallbacks.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>

namespace vsr::app {

enum class CaptionAnchor { BottomCenter, TopCenter, BottomLeft, BottomRight };

struct CaptionStyle {
    std::wstring fontFamily = L"Microsoft YaHei UI";
    int fontSize = 22; // logical pixels at 96 DPI
    bool autoColor = true;
    Gdiplus::Color textColor = Gdiplus::Color(255, 176, 208, 255);
    CaptionAnchor anchor = CaptionAnchor::BottomCenter;
};

inline int CaptionFontSizeClamp(int size) {
    if (size < 14) return 14;
    if (size > 48) return 48;
    return size;
}

inline std::wstring CaptionFontFamilyFromKey(const std::string& key) {
    if (key == "segoe") return L"Segoe UI";
    if (key == "simsun") return L"SimSun";
    if (key == "arial") return L"Arial";
    return L"Microsoft YaHei UI"; // "yahei" and unknown keys
}

inline bool CaptionColorIsAuto(const std::string& key) {
    return key.empty() || key == "auto";
}

inline Gdiplus::Color CaptionTextColorFromKey(const std::string& key) {
    if (key == "white") return Gdiplus::Color(255, 255, 255, 255);
    if (key == "yellow") return Gdiplus::Color(255, 245, 222, 140);
    if (key == "green") return Gdiplus::Color(255, 176, 236, 192);
    if (key == "orange") return Gdiplus::Color(255, 255, 190, 140);
    // "auto" and "blue" both use the participant color; auto is resolved per
    // cue by the renderer.
    return Gdiplus::Color(255, 176, 208, 255);
}

inline CaptionAnchor CaptionAnchorFromKey(const std::string& key) {
    if (key == "top-center") return CaptionAnchor::TopCenter;
    if (key == "bottom-left") return CaptionAnchor::BottomLeft;
    if (key == "bottom-right") return CaptionAnchor::BottomRight;
    return CaptionAnchor::BottomCenter;
}

inline CaptionStyle CaptionStyleFromKeys(const std::string& fontKey,
                                         int fontSize,
                                         const std::string& colorKey,
                                         const std::string& positionKey) {
    CaptionStyle style;
    style.fontFamily = CaptionFontFamilyFromKey(fontKey);
    style.fontSize = CaptionFontSizeClamp(fontSize);
    style.autoColor = CaptionColorIsAuto(colorKey);
    style.textColor = CaptionTextColorFromKey(colorKey);
    style.anchor = CaptionAnchorFromKey(positionKey);
    return style;
}

} // namespace vsr::app
