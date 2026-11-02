// MeetRec - small GDI+ drawing toolkit for the custom Win32 UI.
//
// The product UI is drawn by hand (no WebView, no XAML): rounded cards,
// toggles, menus and icons share this theme and these helpers so every window
// looks like one product. All measurements are logical pixels; callers scale
// them with the window DPI.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>

#include "Localization.h"

namespace vsr::app::ui {

namespace colors {
inline Gdiplus::Color Window() { return Gdiplus::Color(255, 247, 248, 251); }
inline Gdiplus::Color Surface() { return Gdiplus::Color(255, 255, 255, 255); }
inline Gdiplus::Color SurfaceMuted() { return Gdiplus::Color(255, 243, 245, 249); }
inline Gdiplus::Color TitleBar() { return Gdiplus::Color(255, 251, 252, 254); }
inline Gdiplus::Color Border() { return Gdiplus::Color(255, 228, 232, 239); }
inline Gdiplus::Color BorderStrong() { return Gdiplus::Color(255, 214, 220, 230); }
inline Gdiplus::Color Divider() { return Gdiplus::Color(255, 236, 239, 244); }
inline Gdiplus::Color Text() { return Gdiplus::Color(255, 31, 35, 41); }
inline Gdiplus::Color TextSecondary() { return Gdiplus::Color(255, 107, 114, 128); }
inline Gdiplus::Color TextTertiary() { return Gdiplus::Color(255, 154, 161, 172); }
inline Gdiplus::Color Primary() { return Gdiplus::Color(255, 0, 100, 234); }
inline Gdiplus::Color PrimaryHover() { return Gdiplus::Color(255, 0, 88, 214); }
inline Gdiplus::Color PrimaryPressed() { return Gdiplus::Color(255, 0, 78, 191); }
inline Gdiplus::Color PrimarySoft() { return Gdiplus::Color(255, 238, 245, 255); }
inline Gdiplus::Color PrimarySoftBorder() { return Gdiplus::Color(255, 214, 230, 255); }
inline Gdiplus::Color InfoSoft() { return Gdiplus::Color(255, 238, 245, 255); }
inline Gdiplus::Color InfoBorder() { return Gdiplus::Color(255, 213, 229, 255); }
inline Gdiplus::Color InfoText() { return Gdiplus::Color(255, 33, 95, 204); }
inline Gdiplus::Color Success() { return Gdiplus::Color(255, 47, 201, 103); }
inline Gdiplus::Color SuccessSoft() { return Gdiplus::Color(255, 234, 250, 241); }
inline Gdiplus::Color Danger() { return Gdiplus::Color(255, 229, 52, 44); }
inline Gdiplus::Color DangerHover() { return Gdiplus::Color(255, 204, 40, 34); }
inline Gdiplus::Color DangerSoft() { return Gdiplus::Color(255, 253, 240, 240); }
inline Gdiplus::Color DangerBorder() { return Gdiplus::Color(255, 248, 215, 215); }
inline Gdiplus::Color DangerText() { return Gdiplus::Color(255, 180, 35, 24); }
inline Gdiplus::Color Warning() { return Gdiplus::Color(255, 233, 162, 27); }
inline Gdiplus::Color WarningSoft() { return Gdiplus::Color(255, 255, 248, 232); }
inline Gdiplus::Color ToggleOff() { return Gdiplus::Color(255, 213, 218, 227); }
inline Gdiplus::Color ControlBg() { return Gdiplus::Color(255, 250, 251, 253); }
inline Gdiplus::Color ControlHover() { return Gdiplus::Color(255, 244, 246, 250); }
inline Gdiplus::Color HoverTint() { return Gdiplus::Color(255, 242, 246, 252); }
inline Gdiplus::Color PressedTint() { return Gdiplus::Color(255, 232, 239, 250); }
inline Gdiplus::Color White() { return Gdiplus::Color(255, 255, 255, 255); }
inline Gdiplus::Color Black() { return Gdiplus::Color(255, 0, 0, 0); }
inline Gdiplus::Color Overlay() { return Gdiplus::Color(200, 20, 24, 31); }
inline Gdiplus::Color LevelOn() { return Gdiplus::Color(255, 47, 201, 103); }
inline Gdiplus::Color LevelOff() { return Gdiplus::Color(255, 226, 232, 238); }
} // namespace colors

int Scale(int logical, UINT dpi);
float ScaleF(float logical, UINT dpi);

bool HasCjk(const std::wstring& text);

enum class FontRole {
    Body,
    BodyStrong,
    Small,
    SmallStrong,
    Section,
    Title,
    Button,
    Big,
    Timer,
    Badge,
    Mono,
};

// Owns the fonts for one DPI/language combination. For() picks the CJK family
// for Chinese text (the product is bilingual even in the English UI, because
// window titles and device names often contain Chinese).
class Fonts {
public:
    Fonts() = default;
    ~Fonts() { Destroy(); }
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    void Create(UINT dpi, i18n::Language language);
    void Destroy();
    Gdiplus::Font* For(FontRole role, const std::wstring& text) const;
    float Size(FontRole role) const;
    UINT Dpi() const noexcept { return dpi_; }

private:
    void CreateSet(const wchar_t* family, Gdiplus::Font** out);
    void DestroySet(Gdiplus::Font** set);

    UINT dpi_ = 96;
    i18n::Language language_ = i18n::Language::ZhCn;
    Gdiplus::Font* latin_[11] = {};
    Gdiplus::Font* cjk_[11] = {};
};

enum class Align { Near, Center, Far };
enum class VAlign { Near, Center, Far };

void FillRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
              const Gdiplus::Color& color);
void FillRoundRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                   const Gdiplus::Color& color);
void StrokeRoundRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& color, float width = 1.0f);
void FillRoundRectWithBorder(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                             float radius, const Gdiplus::Color& fill,
                             const Gdiplus::Color& border, float borderWidth = 1.0f);
void DrawLine(Gdiplus::Graphics& graphics, float x1, float y1, float x2, float y2,
              const Gdiplus::Color& color, float width = 1.0f);
void DrawCircle(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                const Gdiplus::Color& fill, const Gdiplus::Color& border,
                float borderWidth = 1.0f);

void DrawText(Gdiplus::Graphics& graphics, const Fonts& fonts, const std::wstring& text,
              const Gdiplus::RectF& rect, FontRole role, const Gdiplus::Color& color,
              Align align = Align::Near, VAlign valign = VAlign::Center, bool ellipsis = true);
void DrawTextWrapped(Gdiplus::Graphics& graphics, const Fonts& fonts, const std::wstring& text,
                     const Gdiplus::RectF& rect, FontRole role, const Gdiplus::Color& color,
                     Align align = Align::Near);
Gdiplus::SizeF MeasureText(Gdiplus::Graphics& graphics, const Fonts& fonts,
                           const std::wstring& text, FontRole role);

// Draws a clipped path of the given color; used for small vector icons.
enum class Icon {
    None,
    Logo,
    Gear,
    More,
    Minimize,
    Maximize,
    Restore,
    Close,
    ChevronDown,
    Folder,
    Video,
    Speaker,
    Microphone,
    Check,
    Alert,
    Play,
    Pause,
    Square,
    Record,
    File,
    Clock,
    Info,
    Search,
    Refresh,
    Help,
    Exit,
    Window,
    Sliders,
    Globe,
    External,
    Diagnostics,
};

void DrawIcon(Gdiplus::Graphics& graphics, Icon icon, const Gdiplus::RectF& rect,
              const Gdiplus::Color& color, float strokeWidth = 1.6f);

// Draws the product mark: rounded blue tile + white camera glyph.
void DrawLogo(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius);

enum class ButtonStyle { Primary, Secondary, Danger, Ghost };

// Shared controls so the main window, picker and settings stay consistent.
void DrawButton(Gdiplus::Graphics& graphics, const Fonts& fonts, const Gdiplus::RectF& rect,
                const std::wstring& label, ButtonStyle style, bool hover, bool pressed,
                bool enabled, Icon icon = Icon::None);
void DrawToggleSwitch(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, bool on,
                      bool enabled, bool hover, UINT dpi);
void DrawCheckBox(Gdiplus::Graphics& graphics, const Gdiplus::RectF& box, bool checked,
                  bool enabled, bool hover, UINT dpi);
void DrawSectionHeader(Gdiplus::Graphics& graphics, const Fonts& fonts,
                       const Gdiplus::RectF& badge, int number, const std::wstring& title,
                       const Gdiplus::Color& badgeColor, const Gdiplus::Color& titleColor);
void DrawLevelMeter(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float level,
                    int segments, UINT dpi);

// A compact path for narrow controls: "C:\...\Videos\Recordings".
std::wstring CompactPath(const std::wstring& path, size_t maxChars);

// Initials used when a target window has no icon ("Teams" -> "T").
std::wstring InitialFor(const std::wstring& name);

} // namespace vsr::app::ui
