// MeetRec - ffmpeg argument builders for file-to-file transcoding (ADR-007).
//
// The recording pipeline's builders in Encoder/FfmpegWriter target rawvideo
// pipes and are deliberately not reused; these take a normal input file and
// keep the preset semantics (capped CRF, still-image tuning, long GOP).
#pragma once

#include <cstdint>
#include <string>

#include "TranscodePreset.h"

namespace vsr::transcode {

// Quoting for CreateProcessW command lines. Public for tests.
std::wstring QuoteArgument(const std::wstring& value);

std::wstring BuildTranscodeArguments(const std::wstring& input,
                                     const std::wstring& partialOutput,
                                     const EncodeOptions& options, Resolution resolution,
                                     std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                     double sourceFrameRate);

} // namespace vsr::transcode
