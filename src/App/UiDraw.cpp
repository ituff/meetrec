#include "UiDraw.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace vsr::app::ui {

int Scale(int logical, UINT dpi) {
    return MulDiv(logical, static_cast<int>(dpi), 96);
}

float ScaleF(float logical, UINT dpi) {
    return logical * static_cast<float>(dpi) / 96.0f;
}

bool HasCjk(const std::wstring& text) {
    for (const wchar_t ch : text) {
        const wchar_t c = static_cast<wchar_t>(ch);
        if ((c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
            (c >= 0xFF00 && c <= 0xFFEF)) {
            return true;
        }
    }
    return false;
}

namespace {

constexpr int kRoleCount = 11;

float RoleSize(FontRole role) {
    switch (role) {
        case FontRole::Body: return 13.0f;
        case FontRole::BodyStrong: return 13.0f;
        case FontRole::Small: return 11.5f;
        case FontRole::SmallStrong: return 11.5f;
        case FontRole::Section: return 13.0f;
        case FontRole::Title: return 15.5f;
        case FontRole::Button: return 13.5f;
        case FontRole::Big: return 19.0f;
        case FontRole::Timer: return 18.0f;
        case FontRole::Badge: return 10.5f;
        case FontRole::Mono: return 12.0f;
    }
    return 13.0f;
}

bool RoleBold(FontRole role) {
    switch (role) {
        case FontRole::BodyStrong:
        case FontRole::SmallStrong:
        case FontRole::Section:
        case FontRole::Title:
        case FontRole::Button:
        case FontRole::Big:
        case FontRole::Timer:
        case FontRole::Badge:
            return true;
        default:
            return false;
    }
}

Gdiplus::StringAlignment ToAlignment(Align align) {
    switch (align) {
        case Align::Center: return Gdiplus::StringAlignmentCenter;
        case Align::Far: return Gdiplus::StringAlignmentFar;
        default: return Gdiplus::StringAlignmentNear;
    }
}

Gdiplus::StringAlignment ToAlignment(VAlign align) {
    switch (align) {
        case VAlign::Center: return Gdiplus::StringAlignmentCenter;
        case VAlign::Far: return Gdiplus::StringAlignmentFar;
        default: return Gdiplus::StringAlignmentNear;
    }
}

void BuildRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float r = std::min({radius, rect.Width / 2.0f, rect.Height / 2.0f});
    if (r <= 0.01f) {
        path.AddRectangle(rect);
        return;
    }
    const float d = r * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

} // namespace

void Fonts::CreateSet(const wchar_t* family, Gdiplus::Font** out) {
    Gdiplus::FontFamily fontFamily(family);
    const Gdiplus::FontFamily* chosen = &fontFamily;
    if (fontFamily.GetLastStatus() != Gdiplus::Ok) {
        chosen = Gdiplus::FontFamily::GenericSansSerif();
    }
    for (int i = 0; i < kRoleCount; ++i) {
        const FontRole role = static_cast<FontRole>(i);
        const Gdiplus::FontStyle style = RoleBold(role) ? Gdiplus::FontStyleBold
                                                       : Gdiplus::FontStyleRegular;
        out[i] = new Gdiplus::Font(chosen, ScaleF(RoleSize(role), dpi_), style,
                                   Gdiplus::UnitPixel);
        if (out[i]->GetLastStatus() != Gdiplus::Ok) {
            delete out[i];
            out[i] = new Gdiplus::Font(Gdiplus::FontFamily::GenericSansSerif(),
                                       ScaleF(RoleSize(role), dpi_), style,
                                       Gdiplus::UnitPixel);
        }
    }
}

void Fonts::DestroySet(Gdiplus::Font** set) {
    for (int i = 0; i < kRoleCount; ++i) {
        delete set[i];
        set[i] = nullptr;
    }
}

void Fonts::Create(UINT dpi, i18n::Language language) {
    Destroy();
    dpi_ = dpi;
    language_ = language;
    CreateSet(L"Segoe UI", latin_);
    CreateSet(L"Microsoft YaHei UI", cjk_);
}

void Fonts::Destroy() {
    DestroySet(latin_);
    DestroySet(cjk_);
}

Gdiplus::Font* Fonts::For(FontRole role, const std::wstring& text) const {
    const int index = static_cast<int>(role);
    if (language_ == i18n::Language::ZhCn || HasCjk(text)) return cjk_[index];
    return latin_[index];
}

float Fonts::Size(FontRole role) const {
    return ScaleF(RoleSize(role), dpi_);
}

void FillRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
              const Gdiplus::Color& color) {
    Gdiplus::SolidBrush brush(color);
    graphics.FillRectangle(&brush, rect);
}

void FillRoundRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                   const Gdiplus::Color& color) {
    Gdiplus::GraphicsPath path;
    BuildRoundRect(path, rect, radius);
    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

void StrokeRoundRect(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                     const Gdiplus::Color& color, float width) {
    Gdiplus::GraphicsPath path;
    BuildRoundRect(path, rect, radius);
    Gdiplus::Pen pen(color, width);
    graphics.DrawPath(&pen, &path);
}

void FillRoundRectWithBorder(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                             float radius, const Gdiplus::Color& fill,
                             const Gdiplus::Color& border, float borderWidth) {
    FillRoundRect(graphics, rect, radius, fill);
    if (borderWidth > 0.0f) StrokeRoundRect(graphics, rect, radius, border, borderWidth);
}

void DrawLine(Gdiplus::Graphics& graphics, float x1, float y1, float x2, float y2,
              const Gdiplus::Color& color, float width) {
    Gdiplus::Pen pen(color, width);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    graphics.DrawLine(&pen, x1, y1, x2, y2);
}

void DrawCircle(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                const Gdiplus::Color& fill, const Gdiplus::Color& border,
                float borderWidth) {
    if (fill.GetA() != 0) {
        Gdiplus::SolidBrush brush(fill);
        graphics.FillEllipse(&brush, rect);
    }
    if (borderWidth > 0.0f && border.GetA() != 0) {
        Gdiplus::Pen pen(border, borderWidth);
        graphics.DrawEllipse(&pen, rect);
    }
}

void DrawText(Gdiplus::Graphics& graphics, const Fonts& fonts, const std::wstring& text,
              const Gdiplus::RectF& rect, FontRole role, const Gdiplus::Color& color,
              Align align, VAlign valign, bool ellipsis) {
    if (text.empty()) return;
    Gdiplus::Font* font = fonts.For(role, text);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(ToAlignment(align));
    format.SetLineAlignment(ToAlignment(valign));
    format.SetTrimming(ellipsis ? Gdiplus::StringTrimmingEllipsisCharacter
                                : Gdiplus::StringTrimmingNone);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetHotkeyPrefix(Gdiplus::HotkeyPrefixNone);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), font, rect, &format,
                        &brush);
}

void DrawTextWrapped(Gdiplus::Graphics& graphics, const Fonts& fonts, const std::wstring& text,
                     const Gdiplus::RectF& rect, FontRole role, const Gdiplus::Color& color,
                     Align align) {
    if (text.empty()) return;
    Gdiplus::Font* font = fonts.For(role, text);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(ToAlignment(align));
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetTrimming(Gdiplus::StringTrimmingNone);
    format.SetHotkeyPrefix(Gdiplus::HotkeyPrefixNone);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), font, rect, &format,
                        &brush);
}

Gdiplus::SizeF MeasureText(Gdiplus::Graphics& graphics, const Fonts& fonts,
                           const std::wstring& text, FontRole role) {
    if (text.empty()) return Gdiplus::SizeF(0.0f, fonts.Size(role));
    Gdiplus::Font* font = fonts.For(role, text);
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetHotkeyPrefix(Gdiplus::HotkeyPrefixNone);
    Gdiplus::RectF bounds;
    graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), font,
                           Gdiplus::PointF(0.0f, 0.0f), &format, &bounds);
    return Gdiplus::SizeF(bounds.Width, bounds.Height);
}

void DrawLogo(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius) {
    FillRoundRect(graphics, rect, radius, colors::Primary());
    // White camcorder: rounded body plus a triangular lens on the right.
    const float x = rect.X + rect.Width * 0.24f;
    const float y = rect.Y + rect.Height * 0.32f;
    const float w = rect.Width * 0.34f;
    const float h = rect.Height * 0.36f;
    FillRoundRect(graphics, Gdiplus::RectF(x, y, w, h), h * 0.28f, colors::White());
    Gdiplus::PointF lens[3] = {
        Gdiplus::PointF(x + w + rect.Width * 0.03f, y + h * 0.28f),
        Gdiplus::PointF(rect.X + rect.Width * 0.80f, y + h * 0.50f),
        Gdiplus::PointF(x + w + rect.Width * 0.03f, y + h * 0.72f)};
    Gdiplus::SolidBrush lensBrush(colors::White());
    graphics.FillPolygon(&lensBrush, lens, 3);
}

void DrawIcon(Gdiplus::Graphics& graphics, Icon icon, const Gdiplus::RectF& rect,
              const Gdiplus::Color& color, float strokeWidth) {
    const float x = rect.X;
    const float y = rect.Y;
    const float w = rect.Width;
    const float h = rect.Height;

    Gdiplus::Pen pen(color, strokeWidth);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    Gdiplus::SolidBrush brush(color);

    switch (icon) {
        case Icon::None:
            return;
        case Icon::Logo: {
            DrawLogo(graphics, rect, w * 0.24f);
            return;
        }
        case Icon::Gear: {
            const Gdiplus::PointF center(x + w / 2.0f, y + h / 2.0f);
            const float outer = w * 0.27f;
            const float inner = w * 0.10f;
            graphics.DrawEllipse(&pen, center.X - outer, center.Y - outer, outer * 2.0f,
                                 outer * 2.0f);
            graphics.DrawEllipse(&pen, center.X - inner, center.Y - inner, inner * 2.0f,
                                 inner * 2.0f);
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 3.14159265f / 4.0f;
                const float r1 = outer + w * 0.02f;
                const float r2 = w * 0.40f;
                graphics.DrawLine(&pen, center.X + std::cos(a) * r1,
                                  center.Y + std::sin(a) * r1,
                                  center.X + std::cos(a) * r2,
                                  center.Y + std::sin(a) * r2);
            }
            return;
        }
        case Icon::More: {
            const float r = w * 0.065f;
            for (int i = 0; i < 3; ++i) {
                const float cx = x + w * (0.30f + 0.20f * static_cast<float>(i));
                graphics.FillEllipse(&brush, cx - r, y + h / 2.0f - r, r * 2.0f, r * 2.0f);
            }
            return;
        }
        case Icon::Minimize:
            graphics.DrawLine(&pen, x + w * 0.25f, y + h * 0.55f, x + w * 0.75f,
                              y + h * 0.55f);
            return;
        case Icon::Maximize:
            graphics.DrawRectangle(&pen, x + w * 0.27f, y + h * 0.27f, w * 0.46f, h * 0.46f);
            return;
        case Icon::Restore:
            graphics.DrawRectangle(&pen, x + w * 0.22f, y + h * 0.34f, w * 0.42f, h * 0.42f);
            graphics.DrawLine(&pen, x + w * 0.34f, y + h * 0.26f, x + w * 0.76f,
                              y + h * 0.26f);
            graphics.DrawLine(&pen, x + w * 0.76f, y + h * 0.26f, x + w * 0.76f,
                              y + h * 0.64f);
            return;
        case Icon::Close:
            graphics.DrawLine(&pen, x + w * 0.28f, y + h * 0.28f, x + w * 0.72f,
                              y + h * 0.72f);
            graphics.DrawLine(&pen, x + w * 0.72f, y + h * 0.28f, x + w * 0.28f,
                              y + h * 0.72f);
            return;
        case Icon::ChevronDown:
            graphics.DrawLine(&pen, x + w * 0.28f, y + h * 0.40f, x + w * 0.50f,
                              y + h * 0.62f);
            graphics.DrawLine(&pen, x + w * 0.50f, y + h * 0.62f, x + w * 0.72f,
                              y + h * 0.40f);
            return;
        case Icon::Folder: {
            Gdiplus::GraphicsPath path;
            path.AddLine(x + w * 0.16f, y + h * 0.33f, x + w * 0.42f, y + h * 0.33f);
            path.AddLine(x + w * 0.42f, y + h * 0.33f, x + w * 0.48f, y + h * 0.42f);
            path.AddLine(x + w * 0.48f, y + h * 0.42f, x + w * 0.84f, y + h * 0.42f);
            path.AddLine(x + w * 0.84f, y + h * 0.42f, x + w * 0.84f, y + h * 0.72f);
            path.AddLine(x + w * 0.84f, y + h * 0.72f, x + w * 0.16f, y + h * 0.72f);
            path.CloseFigure();
            graphics.DrawPath(&pen, &path);
            return;
        }
        case Icon::Video: {
            FillRoundRect(graphics, Gdiplus::RectF(x + w * 0.16f, y + h * 0.30f, w * 0.44f,
                                                   h * 0.40f),
                          h * 0.10f, color);
            Gdiplus::PointF lens[3] = {
                Gdiplus::PointF(x + w * 0.63f, y + h * 0.40f),
                Gdiplus::PointF(x + w * 0.84f, y + h * 0.50f),
                Gdiplus::PointF(x + w * 0.63f, y + h * 0.60f)};
            graphics.FillPolygon(&brush, lens, 3);
            return;
        }
        case Icon::Speaker: {
            Gdiplus::PointF poly[6] = {
                Gdiplus::PointF(x + w * 0.18f, y + h * 0.42f),
                Gdiplus::PointF(x + w * 0.34f, y + h * 0.42f),
                Gdiplus::PointF(x + w * 0.50f, y + h * 0.26f),
                Gdiplus::PointF(x + w * 0.50f, y + h * 0.74f),
                Gdiplus::PointF(x + w * 0.34f, y + h * 0.58f),
                Gdiplus::PointF(x + w * 0.18f, y + h * 0.58f)};
            graphics.FillPolygon(&brush, poly, 6);
            graphics.DrawArc(&pen, x + w * 0.44f, y + h * 0.34f, w * 0.24f, h * 0.32f, -60.0f,
                             120.0f);
            graphics.DrawArc(&pen, x + w * 0.50f, y + h * 0.24f, w * 0.34f, h * 0.52f, -60.0f,
                             120.0f);
            return;
        }
        case Icon::Microphone: {
            const Gdiplus::RectF capsule(x + w * 0.36f, y + h * 0.16f, w * 0.28f, h * 0.42f);
            FillRoundRect(graphics, capsule, w * 0.14f, color);
            graphics.DrawArc(&pen, x + w * 0.26f, y + h * 0.34f, w * 0.48f, h * 0.40f, 0.0f,
                             180.0f);
            graphics.DrawLine(&pen, x + w * 0.50f, y + h * 0.74f, x + w * 0.50f,
                              y + h * 0.86f);
            graphics.DrawLine(&pen, x + w * 0.36f, y + h * 0.86f, x + w * 0.64f,
                              y + h * 0.86f);
            return;
        }
        case Icon::Check:
            graphics.DrawLine(&pen, x + w * 0.20f, y + h * 0.52f, x + w * 0.42f,
                              y + h * 0.72f);
            graphics.DrawLine(&pen, x + w * 0.42f, y + h * 0.72f, x + w * 0.82f,
                              y + h * 0.28f);
            return;
        case Icon::Alert: {
            const Gdiplus::RectF circle(x + w * 0.10f, y + h * 0.10f, w * 0.80f, h * 0.80f);
            graphics.FillEllipse(&brush, circle);
            Gdiplus::Pen white(Gdiplus::Color(255, 255, 255, 255), strokeWidth * 1.15f);
            white.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound,
                             Gdiplus::DashCapRound);
            graphics.DrawLine(&white, x + w * 0.50f, y + h * 0.28f, x + w * 0.50f,
                              y + h * 0.56f);
            Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
            graphics.FillEllipse(&whiteBrush, x + w * 0.465f, y + h * 0.66f, w * 0.07f,
                                 h * 0.07f);
            return;
        }
        case Icon::Play: {
            Gdiplus::PointF tri[3] = {
                Gdiplus::PointF(x + w * 0.30f, y + h * 0.20f),
                Gdiplus::PointF(x + w * 0.80f, y + h * 0.50f),
                Gdiplus::PointF(x + w * 0.30f, y + h * 0.80f)};
            graphics.FillPolygon(&brush, tri, 3);
            return;
        }
        case Icon::Pause: {
            FillRoundRect(graphics, Gdiplus::RectF(x + w * 0.28f, y + h * 0.20f, w * 0.14f,
                                                   h * 0.60f),
                          w * 0.05f, color);
            FillRoundRect(graphics, Gdiplus::RectF(x + w * 0.58f, y + h * 0.20f, w * 0.14f,
                                                   h * 0.60f),
                          w * 0.05f, color);
            return;
        }
        case Icon::Square:
            FillRoundRect(graphics, Gdiplus::RectF(x + w * 0.27f, y + h * 0.27f, w * 0.46f,
                                                   h * 0.46f),
                          w * 0.08f, color);
            return;
        case Icon::Record:
            graphics.FillEllipse(&brush, x + w * 0.22f, y + h * 0.22f, w * 0.56f, h * 0.56f);
            return;
        case Icon::File: {
            Gdiplus::GraphicsPath path;
            path.AddLine(x + w * 0.28f, y + h * 0.14f, x + w * 0.60f, y + h * 0.14f);
            path.AddLine(x + w * 0.60f, y + h * 0.14f, x + w * 0.76f, y + h * 0.30f);
            path.AddLine(x + w * 0.76f, y + h * 0.30f, x + w * 0.76f, y + h * 0.86f);
            path.AddLine(x + w * 0.76f, y + h * 0.86f, x + w * 0.28f, y + h * 0.86f);
            path.CloseFigure();
            graphics.DrawPath(&pen, &path);
            graphics.DrawLine(&pen, x + w * 0.60f, y + h * 0.14f, x + w * 0.60f,
                              y + h * 0.30f);
            graphics.DrawLine(&pen, x + w * 0.60f, y + h * 0.30f, x + w * 0.76f,
                              y + h * 0.30f);
            return;
        }
        case Icon::Clock: {
            const Gdiplus::RectF circle(x + w * 0.14f, y + h * 0.14f, w * 0.72f, h * 0.72f);
            graphics.DrawEllipse(&pen, circle);
            graphics.DrawLine(&pen, x + w * 0.50f, y + h * 0.30f, x + w * 0.50f,
                              y + h * 0.52f);
            graphics.DrawLine(&pen, x + w * 0.50f, y + h * 0.52f, x + w * 0.66f,
                              y + h * 0.62f);
            return;
        }
        case Icon::Info: {
            const Gdiplus::RectF circle(x + w * 0.08f, y + h * 0.08f, w * 0.84f, h * 0.84f);
            graphics.FillEllipse(&brush, circle);
            Gdiplus::Pen white(Gdiplus::Color(255, 255, 255, 255), strokeWidth);
            white.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound,
                             Gdiplus::DashCapRound);
            Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
            graphics.FillEllipse(&whiteBrush, x + w * 0.465f, y + h * 0.27f, w * 0.07f,
                                 h * 0.07f);
            graphics.DrawLine(&white, x + w * 0.50f, y + h * 0.44f, x + w * 0.50f,
                              y + h * 0.70f);
            return;
        }
        case Icon::Search: {
            const Gdiplus::RectF circle(x + w * 0.16f, y + h * 0.16f, w * 0.50f, h * 0.50f);
            graphics.DrawEllipse(&pen, circle);
            graphics.DrawLine(&pen, x + w * 0.62f, y + h * 0.62f, x + w * 0.82f,
                              y + h * 0.82f);
            return;
        }
        case Icon::Refresh: {
            const Gdiplus::RectF arcRect(x + w * 0.18f, y + h * 0.18f, w * 0.64f, h * 0.64f);
            graphics.DrawArc(&pen, arcRect, 40.0f, 280.0f);
            graphics.FillEllipse(&brush, x + w * 0.70f, y + h * 0.14f, w * 0.16f, h * 0.16f);
            return;
        }
        case Icon::Help: {
            const Gdiplus::RectF circle(x + w * 0.12f, y + h * 0.12f, w * 0.76f, h * 0.76f);
            graphics.DrawEllipse(&pen, circle);
            graphics.DrawArc(&pen, x + w * 0.40f, y + h * 0.32f, w * 0.22f, h * 0.18f, 190.0f,
                             230.0f);
            graphics.DrawLine(&pen, x + w * 0.52f, y + h * 0.50f, x + w * 0.50f,
                              y + h * 0.60f);
            graphics.FillEllipse(&brush, x + w * 0.475f, y + h * 0.68f, w * 0.06f, h * 0.06f);
            return;
        }
        case Icon::Exit: {
            graphics.DrawRectangle(&pen, x + w * 0.18f, y + h * 0.20f, w * 0.38f, h * 0.60f);
            graphics.DrawLine(&pen, x + w * 0.46f, y + h * 0.50f, x + w * 0.82f,
                              y + h * 0.50f);
            graphics.DrawLine(&pen, x + w * 0.70f, y + h * 0.38f, x + w * 0.82f,
                              y + h * 0.50f);
            graphics.DrawLine(&pen, x + w * 0.82f, y + h * 0.50f, x + w * 0.70f,
                              y + h * 0.62f);
            return;
        }
        case Icon::Window:
            graphics.DrawRectangle(&pen, x + w * 0.16f, y + h * 0.20f, w * 0.68f, h * 0.60f);
            graphics.DrawLine(&pen, x + w * 0.16f, y + h * 0.38f, x + w * 0.84f,
                              y + h * 0.38f);
            return;
        case Icon::Sliders: {
            for (int i = 0; i < 3; ++i) {
                const float yy = y + h * (0.28f + 0.22f * static_cast<float>(i));
                graphics.DrawLine(&pen, x + w * 0.16f, yy, x + w * 0.84f, yy);
                const float knobX =
                    x + w * (i == 0 ? 0.40f : (i == 1 ? 0.66f : 0.30f));
                graphics.FillEllipse(&brush, knobX - w * 0.07f, yy - h * 0.07f, w * 0.14f,
                                     h * 0.14f);
            }
            return;
        }
        case Icon::Globe: {
            const Gdiplus::RectF circle(x + w * 0.14f, y + h * 0.14f, w * 0.72f, h * 0.72f);
            graphics.DrawEllipse(&pen, circle);
            graphics.DrawEllipse(&pen, x + w * 0.34f, y + h * 0.14f, w * 0.32f, h * 0.72f);
            graphics.DrawLine(&pen, x + w * 0.14f, y + h * 0.50f, x + w * 0.86f,
                              y + h * 0.50f);
            return;
        }
        case Icon::External: {
            graphics.DrawRectangle(&pen, x + w * 0.20f, y + h * 0.32f, w * 0.42f, h * 0.44f);
            graphics.DrawLine(&pen, x + w * 0.46f, y + h * 0.54f, x + w * 0.82f,
                              y + h * 0.18f);
            graphics.DrawLine(&pen, x + w * 0.62f, y + h * 0.18f, x + w * 0.82f,
                              y + h * 0.18f);
            graphics.DrawLine(&pen, x + w * 0.82f, y + h * 0.18f, x + w * 0.82f,
                              y + h * 0.38f);
            return;
        }
        case Icon::Diagnostics: {
            // Document with a small magnifier: support can tell what it is.
            Gdiplus::GraphicsPath path;
            path.AddLine(x + w * 0.24f, y + h * 0.14f, x + w * 0.56f, y + h * 0.14f);
            path.AddLine(x + w * 0.56f, y + h * 0.14f, x + w * 0.72f, y + h * 0.30f);
            path.AddLine(x + w * 0.72f, y + h * 0.30f, x + w * 0.72f, y + h * 0.86f);
            path.AddLine(x + w * 0.72f, y + h * 0.86f, x + w * 0.24f, y + h * 0.86f);
            path.CloseFigure();
            graphics.DrawPath(&pen, &path);
            graphics.DrawEllipse(&pen, x + w * 0.36f, y + h * 0.44f, w * 0.24f, h * 0.24f);
            graphics.DrawLine(&pen, x + w * 0.58f, y + h * 0.66f, x + w * 0.74f,
                              y + h * 0.82f);
            return;
        }
    }
}

void DrawButton(Gdiplus::Graphics& graphics, const Fonts& fonts, const Gdiplus::RectF& rect,
                const std::wstring& label, ButtonStyle style, bool hover, bool pressed,
                bool enabled, Icon icon) {
    Gdiplus::Color fill;
    Gdiplus::Color border(0, 0, 0, 0);
    Gdiplus::Color text;
    const float radius = std::min(ScaleF(8.0f, fonts.Dpi()), rect.Height / 2.0f);

    switch (style) {
        case ButtonStyle::Primary:
            fill = !enabled ? Gdiplus::Color(255, 180, 203, 240)
                            : (pressed ? colors::PrimaryPressed()
                                       : (hover ? colors::PrimaryHover() : colors::Primary()));
            text = enabled ? colors::White() : Gdiplus::Color(255, 245, 249, 255);
            break;
        case ButtonStyle::Danger:
            fill = !enabled ? Gdiplus::Color(255, 238, 190, 187)
                            : (pressed ? Gdiplus::Color(255, 176, 32, 27)
                                       : (hover ? colors::DangerHover() : colors::Danger()));
            text = enabled ? colors::White() : Gdiplus::Color(255, 255, 245, 244);
            break;
        case ButtonStyle::Secondary:
            fill = !enabled ? colors::SurfaceMuted()
                            : (pressed ? colors::PressedTint()
                                       : (hover ? colors::ControlHover()
                                                : colors::Surface()));
            border = enabled ? colors::BorderStrong() : colors::Divider();
            text = enabled ? colors::Text() : colors::TextTertiary();
            break;
        case ButtonStyle::Ghost:
            fill = pressed ? colors::PressedTint()
                           : (hover ? colors::HoverTint() : Gdiplus::Color(0, 0, 0, 0));
            text = enabled ? colors::Text() : colors::TextTertiary();
            break;
    }

    if (fill.GetA() != 0) FillRoundRect(graphics, rect, radius, fill);
    if (border.GetA() != 0) {
        StrokeRoundRect(graphics, rect, radius, border, ScaleF(1.0f, fonts.Dpi()));
    }

    const float iconSize = ScaleF(16.0f, fonts.Dpi());
    const float gap = icon == Icon::None ? 0.0f : ScaleF(7.0f, fonts.Dpi());
    const Gdiplus::SizeF textSize = MeasureText(graphics, fonts, label, FontRole::Button);
    const float totalWidth = (icon == Icon::None ? 0.0f : iconSize + gap) + textSize.Width;
    float x = rect.X + (rect.Width - totalWidth) / 2.0f;
    if (icon != Icon::None) {
        DrawIcon(graphics, icon,
                 Gdiplus::RectF(x, rect.Y + (rect.Height - iconSize) / 2.0f, iconSize,
                                iconSize),
                 text, ScaleF(1.6f, fonts.Dpi()));
        x += iconSize + gap;
    }
    DrawText(graphics, fonts, label,
             Gdiplus::RectF(x, rect.Y, rect.GetRight() - x, rect.Height), FontRole::Button, text,
             Align::Near, VAlign::Center, false);
}

void DrawToggleSwitch(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, bool on,
                      bool enabled, bool hover, UINT dpi) {
    const float radius = rect.Height / 2.0f;
    Gdiplus::Color track = on ? colors::Primary() : colors::ToggleOff();
    if (on && hover && enabled) track = colors::PrimaryHover();
    if (!enabled) {
        track = on ? Gdiplus::Color(255, 170, 194, 232) : Gdiplus::Color(255, 232, 235, 240);
    }
    FillRoundRect(graphics, rect, radius, track);
    const float knob = rect.Height - ScaleF(4.0f, dpi);
    const float knobX = on ? rect.GetRight() - knob - ScaleF(2.0f, dpi)
                           : rect.X + ScaleF(2.0f, dpi);
    const float knobY = rect.Y + (rect.Height - knob) / 2.0f;
    DrawCircle(graphics, Gdiplus::RectF(knobX, knobY, knob, knob),
               enabled ? colors::White() : Gdiplus::Color(255, 248, 249, 251),
               Gdiplus::Color(0, 0, 0, 0), 0.0f);
}

void DrawCheckBox(Gdiplus::Graphics& graphics, const Gdiplus::RectF& box, bool checked,
                  bool enabled, bool hover, UINT dpi) {
    const float radius = ScaleF(4.0f, dpi);
    if (checked) {
        Gdiplus::Color fill = enabled ? colors::Primary() : Gdiplus::Color(255, 170, 194, 232);
        if (enabled && hover) fill = colors::PrimaryHover();
        FillRoundRect(graphics, box, radius, fill);
        DrawIcon(graphics, Icon::Check, box, colors::White(), ScaleF(1.7f, dpi));
    } else {
        Gdiplus::Color fill = enabled ? colors::Surface() : colors::SurfaceMuted();
        Gdiplus::Color border =
            enabled ? (hover ? colors::Primary() : colors::BorderStrong()) : colors::Divider();
        FillRoundRectWithBorder(graphics, box, radius, fill, border, ScaleF(1.0f, dpi));
    }
}

void DrawSectionHeader(Gdiplus::Graphics& graphics, const Fonts& fonts,
                       const Gdiplus::RectF& badge, int number, const std::wstring& title,
                       const Gdiplus::Color& badgeColor, const Gdiplus::Color& titleColor) {
    FillRoundRect(graphics, badge, ScaleF(5.0f, fonts.Dpi()), badgeColor);
    const std::wstring numberText = std::to_wstring(number);
    DrawText(graphics, fonts, numberText, badge, FontRole::Badge, colors::White(),
             Align::Center, VAlign::Center, false);
    DrawText(graphics, fonts, title,
             Gdiplus::RectF(badge.GetRight() + ScaleF(9.0f, fonts.Dpi()), badge.Y,
                            badge.Width + ScaleF(460.0f, fonts.Dpi()), badge.Height),
             FontRole::Section, titleColor, Align::Near, VAlign::Center, false);
}

void DrawLevelMeter(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float level,
                    int segments, UINT dpi) {
    if (segments <= 0 || rect.Width <= 0.0f) return;
    const float gap = ScaleF(2.0f, dpi);
    const float width =
        std::min(ScaleF(6.0f, dpi), (rect.Width - gap * (segments - 1)) / segments);
    const float height = std::min(rect.Height, ScaleF(5.0f, dpi));
    const float display = std::sqrt(std::max(0.0f, std::min(1.0f, level)));
    for (int i = 0; i < segments; ++i) {
        const bool on = (static_cast<float>(i + 1) / static_cast<float>(segments)) <= display;
        const Gdiplus::RectF segment(rect.X + i * (width + gap), rect.Y, width, height);
        FillRoundRect(graphics, segment, height / 2.0f,
                      on ? colors::LevelOn() : colors::LevelOff());
    }
}

std::wstring CompactPath(const std::wstring& path, size_t maxChars) {
    if (path.size() <= maxChars || maxChars < 12) return path;
    const std::wstring prefix = path.size() > 3 ? path.substr(0, 3) : std::wstring();
    const size_t tailBudget = maxChars - prefix.size() - 3;
    std::wstring tail = path.substr(path.size() - tailBudget);
    const size_t slash = tail.find(L'\\');
    if (slash != std::wstring::npos && slash + 1 < tail.size()) {
        tail = tail.substr(slash);
    }
    return prefix + L"..." + tail;
}

std::wstring InitialFor(const std::wstring& name) {
    for (const wchar_t ch : name) {
        if (!std::iswspace(ch)) {
            std::wstring result(1, static_cast<wchar_t>(std::towupper(ch)));
            return result;
        }
    }
    return L"?";
}

} // namespace vsr::app::ui
