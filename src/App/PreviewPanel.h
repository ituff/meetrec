#pragma once

// Virtual Screen Recorder - observability-only preview panel (FR-047, FR-048,
// T072). Rendering consumes sampled frames at a low rate; closing or hiding
// the panel releases its resources and never affects capture or encoding.

#include <windows.h>
#include <dwmapi.h>

#include <vector>

namespace vsr::app {

class Coordinator;

class PreviewPanel {
public:
    explicit PreviewPanel(Coordinator& coordinator) : coordinator_(coordinator) {}
    ~PreviewPanel();

    PreviewPanel(const PreviewPanel&) = delete;
    PreviewPanel& operator=(const PreviewPanel&) = delete;

    void Create(HWND parent, HINSTANCE instance);
    void Destroy();

    // Samples the latest frame (bounded to ~10 FPS) and repaints.
    void Tick();
    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool IsEnabled() const noexcept { return enabled_; }
    // Drops the cached frame and repaints an empty panel.
    void Clear();
    HWND Hwnd() const noexcept { return hwnd_; }

    // Idle preview: a live DWM thumbnail of the selected window (works even
    // for GPU-composited apps where PrintWindow returns black), with a
    // one-shot PrintWindow bitmap as the fallback. Tick() replaces it with
    // live capture frames once recording starts.
    bool ShowThumbnail(HWND source);
    void UpdateThumbnailRect();
    void HideThumbnail();
    bool HasThumbnail() const noexcept { return thumbnail_ != nullptr; }
    void SetStaticFrame(std::vector<std::uint8_t> bgra, UINT width, UINT height);
    bool HasFrame() const noexcept {
        return thumbnail_ != nullptr || (frameW_ > 0 && frameH_ > 0);
    }
    bool CopyLastFrame(std::vector<std::uint8_t>& outBgra, UINT& width, UINT& height) const;

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    // Double-buffered: compose black letterbox + scaled frame in an offscreen
    // bitmap, then blit once. Prevents the black-then-image flicker at 10 FPS.
    void Render(HDC targetDc = nullptr);
    void ReleaseBackBuffer();

    Coordinator& coordinator_;
    HWND hwnd_ = nullptr;
    bool enabled_ = true;
    std::vector<std::uint8_t> frameBgra_;
    UINT frameW_ = 0;
    UINT frameH_ = 0;
    HBITMAP bitmap_ = nullptr;
    void* dibBits_ = nullptr;
    UINT dibW_ = 0;
    UINT dibH_ = 0;
    DWORD lastRenderMs_ = 0;
    HDC backDc_ = nullptr;
    HBITMAP backBmp_ = nullptr;
    int backW_ = 0;
    int backH_ = 0;
    HTHUMBNAIL thumbnail_ = nullptr;
    HWND thumbnailSource_ = nullptr;
};

} // namespace vsr::app
