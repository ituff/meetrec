// MeetRec - "Compress videos" window (ADR-007).
//
// A separate top-level window in the same executable: the recorder's main
// window and state machine stay untouched. The window owns a
// transcode::TranscodeService, polls its snapshot on a timer, and suspends the
// queue while the recorder is capturing so the two x264 workloads never
// compete on one machine.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Transcode/TranscodeService.h>

#include "UiDraw.h"

namespace vsr::app {

class CompressWindow {
public:
    // `recorderBusy` returns true while the recorder is preparing, recording,
    // paused or stopping.
    CompressWindow(HWND owner, std::function<bool()> recorderBusy);
    ~CompressWindow();

    CompressWindow(const CompressWindow&) = delete;
    CompressWindow& operator=(const CompressWindow&) = delete;

    void Show();
    void Invalidate();
    void OnLanguageChanged();
    // Development/QA: queue files as if they had been dropped.
    void QueueFiles(const std::vector<std::wstring>& files);
    bool IsOpen() const noexcept { return hwnd_ != nullptr; }
    HWND Hwnd() const noexcept { return hwnd_; }

private:
    enum class Hit {
        None,
        Close,
        Minimize,
        Add,
        Clear,
        Quality,
        Resolution,
        OutputChoose,
        OutputReset,
        Start,
        Cancel,
    };

    struct HitTarget {
        Hit id = Hit::None;
        Gdiplus::RectF rect;
    };

    struct RowLayout {
        std::uint64_t id = 0;
        Gdiplus::RectF rect;
        Gdiplus::RectF action;
        bool canOpen = false;
        bool canRemove = false;
    };

    struct Layout {
        Gdiplus::RectF titleBar;
        Gdiplus::RectF title;
        Gdiplus::RectF minimize;
        Gdiplus::RectF close;
        Gdiplus::RectF add;
        Gdiplus::RectF clear;
        Gdiplus::RectF list;
        Gdiplus::RectF qualityLabel;
        Gdiplus::RectF qualityField;
        Gdiplus::RectF resolutionLabel;
        Gdiplus::RectF resolutionField;
        Gdiplus::RectF outputLabel;
        Gdiplus::RectF outputField;
        Gdiplus::RectF outputChoose;
        Gdiplus::RectF outputReset;
        Gdiplus::RectF estimate;
        Gdiplus::RectF footer;
        Gdiplus::RectF status;
        Gdiplus::RectF cancelButton;
        Gdiplus::RectF startButton;
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool CreateWindowIfNeeded();
    void OnDestroy();
    void OnDpiChanged(UINT dpi);
    void UpdateLayout();
    void RebuildRows();
    void Paint(HDC targetDc);
    void Draw(Gdiplus::Graphics& graphics, int width, int height);
    void DrawTitleBar(Gdiplus::Graphics& graphics);
    void DrawHeader(Gdiplus::Graphics& graphics);
    void DrawList(Gdiplus::Graphics& graphics);
    void DrawEmptyState(Gdiplus::Graphics& graphics);
    void DrawRow(Gdiplus::Graphics& graphics, const transcode::JobSnapshot& job,
                 const RowLayout& row, bool hovered);
    void DrawSettings(Gdiplus::Graphics& graphics);
    void DrawFooter(Gdiplus::Graphics& graphics);
    void DrawField(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                   const std::wstring& text, bool hover, bool focused,
                   ui::Icon icon = ui::Icon::None);
    void DrawFocusRing(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect);

    Hit HitTest(POINT point) const;
    void Activate(Hit hit);
    bool IsHitEnabled(Hit hit) const;
    void CycleFocus(bool backwards);
    void FocusControl(Hit hit, bool keyboard);

    void Poll();
    void RefreshSnapshot();
    void OnWheel(int delta);
    void AddFromDialog();
    void AddFiles(const std::vector<std::wstring>& files);
    void ChooseOutputFolder();
    void ShowQualityMenu();
    void ShowResolutionMenu();
    void OpenOutput(const transcode::JobSnapshot& job, bool selectFile);
    void RowAction(const transcode::JobSnapshot& job);
    void RequestClose();

    const transcode::JobSnapshot* FindJob(std::uint64_t id) const;
    std::wstring StatusText() const;
    std::wstring RemainingText() const;
    std::wstring RowSubtitle(const transcode::JobSnapshot& job) const;
    std::wstring RowStatus(const transcode::JobSnapshot& job) const;
    std::wstring QualityLabel() const;
    std::wstring ResolutionLabel() const;
    bool RecordingBusy() const;
    bool StartEnabled() const;
    bool AnyActiveJob() const;

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    std::function<bool()> recorderBusy_;
    transcode::TranscodeService service_;
    std::vector<transcode::JobSnapshot> jobs_;
    std::vector<RowLayout> rows_;
    std::vector<HitTarget> hits_;

    UINT dpi_ = 96;
    ui::Fonts fonts_;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
    UINT timerId_ = 1;

    Layout layout_;
    float scrollOffset_ = 0.0f;
    Hit hover_ = Hit::None;
    Hit pressed_ = Hit::None;
    Hit focus_ = Hit::None;
    bool keyboardFocus_ = false;
    bool trackingMouse_ = false;

    transcode::Quality quality_ = transcode::Quality::Standard;
    transcode::Resolution resolution_ = transcode::Resolution::Keep;
    std::wstring outputDir_;
    std::wstring lastSignature_;
    bool active_ = false;
    bool suspended_ = false;
    bool running_ = false;
    std::wstring toolError_;
};

} // namespace vsr::app
