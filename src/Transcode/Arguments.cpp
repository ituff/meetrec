#include "Arguments.h"

namespace vsr::transcode {

std::wstring QuoteArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring BuildTranscodeArguments(const std::wstring& input,
                                     const std::wstring& partialOutput,
                                     const EncodeOptions& options, Resolution resolution,
                                     std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                     double sourceFrameRate) {
    std::wstring command;
    command += L"-hide_banner -loglevel error -nostdin -nostats -y ";

    // Keep the source timestamps and metadata; only the codec changes.
    command += L"-i " + QuoteArgument(input) + L" ";
    // First video stream, first audio stream if present. Subtitles and data
    // streams are dropped: MP4 cannot carry most of them.
    command += L"-map 0:v:0 -map 0:a:0? -sn -dn ";

    command += L"-c:v libx264 -preset veryfast ";
    if (options.screenContentTune) command += L"-tune stillimage ";
    command += L"-crf " + std::to_wstring(options.crf) + L" ";
    command += L"-maxrate " + std::to_wstring(options.maxrateBps) + L" ";
    command += L"-bufsize " + std::to_wstring(options.maxrateBps * 2) + L" ";
    command += L"-pix_fmt yuv420p ";

    std::uint32_t targetWidth = 0;
    std::uint32_t targetHeight = 0;
    TargetDimensions(resolution, sourceWidth, sourceHeight, targetWidth, targetHeight);
    if (targetWidth > 0 && targetHeight > 0 &&
        (targetWidth != sourceWidth || targetHeight != sourceHeight)) {
        command += L"-vf scale=" + std::to_wstring(targetWidth) + L":" +
                   std::to_wstring(targetHeight) + L" ";
    }

    command += L"-g " + std::to_wstring(GopFrames(options, sourceFrameRate)) + L" ";
    command += L"-c:a aac -b:a " + std::to_wstring(options.audioBitrateBps) + L" -ac " +
               std::to_wstring(options.audioChannels) + L" ";
    command += L"-movflags +faststart -max_muxing_queue_size 1024 ";
    command += L"-f mp4 ";

    // Progress goes to stdout as key=value blocks; stderr keeps errors only.
    command += L"-progress pipe:1 ";
    command += QuoteArgument(partialOutput);
    return command;
}

} // namespace vsr::transcode
