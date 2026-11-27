// MeetRec - click-through live caption overlay (P2).
//
// A topmost, non-activating, layered popup that follows the recorded window
// and shows the newest finalized speech cue. It is a separate top-level
// window, so Windows.Graphics.Capture of the target window never includes it;
// the overlay also never takes focus or intercepts clicks.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Speech/CaptionFeed.h>

#include "CaptionStyle.h"
#include "UiDraw.h"

namespace vsr::app {

class CaptionOverlay {
public:
    CaptionOverlay() = default;
    ~CaptionOverlay();

    CaptionOverlay(const CaptionOverlay&) = delete;
    CaptionOverlay& operator=(const CaptionOverlay&) = delete;

    // Starts following `target` and polling `feed`. Safe to call again for a
    // new recording; the overlay is reused.
    void Show(HWND target, std::shared_ptr<speech::CaptionFeed> feed, bool chinese,
              const CaptionStyle& style = CaptionStyle{});
    void Hide();
    // Applies a new font/size/color/anchor while the overlay is visible.
    void SetStyle(const CaptionStyle& style);
    // Sets the drag offset from the selected anchor (logical 96-DPI pixels).
    void SetOffset(int logicalX, int logicalY);
    // Called after a drag or a double-click reset with the new logical offset.
    void SetMovedCallback(std::function<void(int, int)> callback);
    // Pause keeps the target/feed but hides the text; Resume shows it again
    // when the next cue arrives.
    void SetPaused(bool paused);

    bool IsVisible() const;

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void EnsureWindow();
    void DestroyWindowNow();
    void UpdateContent();
    bool Position();
    void ComputeBasePosition(int width, int height, int& x, int& y) const;
    bool IsGripPoint(POINT client) const;
    void FinishDrag(bool persist);
    void Paint();
    void ReleaseBackBuffer();
    void RecreateFonts(UINT dpi);
    void ApplyRegion();
    void SetShown(bool shown);
    float LatestLineHeight() const;
    float PreviousLineHeight() const;
    int WrapUnits() const;
    int PreferredWidth() const;
    int ContentWidth() const;

    HWND hwnd_ = nullptr;
    HWND target_ = nullptr;
    std::shared_ptr<speech::CaptionFeed> feed_;
    bool chinese_ = true;
    CaptionStyle style_;
    bool paused_ = false;
    bool shown_ = false;
    UINT dpi_ = 96;
    Gdiplus::Font* captionFont_ = nullptr;
    Gdiplus::Font* previousFont_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
    std::uint64_t lastSequence_ = 0;
    std::vector<speech::CaptionCue> cues_;
    std::vector<std::wstring> latestLines_;
    std::wstring previousLine_;
    bool latestMicrophone_ = false;
    bool hasPrevious_ = false;
    int lastX_ = -1;
    int lastY_ = -1;
    int lastWidth_ = -1;
    int lastHeight_ = -1;
    int baseX_ = 0;
    int baseY_ = 0;
    int offsetLogicalX_ = 0;
    int offsetLogicalY_ = 0;
    bool dragging_ = false;
    bool gripHover_ = false;
    bool trackingLeave_ = false;
    POINT dragStartCursor_ = {};
    RECT dragStartRect_ = {};
    std::function<void(int, int)> onMoved_;
};

} // namespace vsr::app
