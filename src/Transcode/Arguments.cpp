#include "Arguments.h"

namespace vsr::transcode {

const char* ToString(VideoEncoder encoder) noexcept {
    switch (encoder) {
        case VideoEncoder::Qsv: return "qsv";
        case VideoEncoder::Nvenc: return "nvenc";
        case VideoEncoder::Amf: return "amf";
        case VideoEncoder::X264: break;
    }
    return "x264";
}

std::wstring QuoteArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring BuildTranscodeArguments(const std::wstring& input,
                                     const std::wstring& partialOutput,
                                     const EncodeOptions& options, Resolution resolution,
                                     std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                                     double sourceFrameRate, VideoEncoder encoder) {
    std::wstring command;
    command += L"-hide_banner -loglevel error -nostdin -nostats -y ";

    // Keep the source timestamps and metadata; only the codec changes.
    command += L"-i " + QuoteArgument(input) + L" ";
    // First video stream, first audio stream if present. Subtitles and data
    // streams are dropped: MP4 cannot carry most of them.
    command += L"-map 0:v:0 -map 0:a:0? -sn -dn ";

    switch (encoder) {
        case VideoEncoder::Qsv:
            // QSV has no CRF; ICQ (global_quality) is its constant-quality
            // mode. The offset keeps output sizes close to the x264 presets.
            command += L"-c:v h264_qsv -preset veryfast ";
            command += L"-global_quality " + std::to_wstring(options.crf + 5) + L" ";
            break;
        case VideoEncoder::Nvenc:
            command += L"-c:v h264_nvenc -preset p5 -tune hq ";
            command += L"-rc vbr -cq " + std::to_wstring(options.crf) + L" -b:v 0 ";
            command += L"-maxrate " + std::to_wstring(options.maxrateBps) + L" ";
            command += L"-bufsize " + std::to_wstring(options.maxrateBps * 2) + L" ";
            command += L"-pix_fmt yuv420p ";
            break;
        case VideoEncoder::Amf:
            command += L"-c:v h264_amf -quality balanced ";
            command += L"-rc cqp -qp_i " + std::to_wstring(options.crf) + L" -qp_p " +
                       std::to_wstring(options.crf) + L" -qp_b " +
                       std::to_wstring(options.crf) + L" ";
            command += L"-pix_fmt yuv420p ";
            break;
        case VideoEncoder::X264:
        default:
            command += L"-c:v libx264 -preset veryfast ";
            if (options.screenContentTune) command += L"-tune stillimage ";
            command += L"-crf " + std::to_wstring(options.crf) + L" ";
            command += L"-maxrate " + std::to_wstring(options.maxrateBps) + L" ";
            command += L"-bufsize " + std::to_wstring(options.maxrateBps * 2) + L" ";
            command += L"-pix_fmt yuv420p ";
            break;
    }

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
