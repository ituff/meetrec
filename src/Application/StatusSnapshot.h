// Virtual Screen Recorder - immutable status snapshot published by the
// coordinator to the UI (FR-076, FR-077; design.md §4.2).
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include <Infrastructure/Error.h>
#include "AppState.h"
#include <Encoder/VideoPreset.h>

namespace vsr::app {

using encoder::QualityPreset;

struct StatusSnapshot {
    AppState state = AppState::Initializing;
    std::wstring correlationId;

    // Target window (titles are never included — FR-094).
    HWND targetHwnd = nullptr;
    DWORD targetPid = 0;
    std::wstring targetExeName;

    // Virtual display.
    bool virtualMonitorActive = false;
    LONG monitorX = 0, monitorY = 0, monitorW = 0, monitorH = 0;

    // Output.
    std::wstring outputDirectory;
    std::wstring partialPath;
    std::wstring finalPath;
    QualityPreset preset = QualityPreset::Standard;
    bool captureCursor = false;

    // Progress.
    std::int64_t activeElapsedMs = 0;
    double approxFps = 0.0;
    std::uint64_t capturedFrames = 0;
    std::uint64_t encodedFrames = 0;
    std::uint64_t droppedFrames = 0;
    std::uint32_t captureWidth = 0;
    std::uint32_t captureHeight = 0;

    // Latest failure (empty when healthy).
    bool hasError = false;
    ErrorCode errorCode = ErrorCode::None;
    std::wstring errorUserMessage;
};

} // namespace vsr::app
