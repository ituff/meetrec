// MeetRec - unit tests for the offline compressor (ADR-007).
//
// Covers the pure parts: ffprobe JSON parsing, -progress parsing, argument
// construction, output naming, and the mirrored quality table. The ffmpeg
// child-process path is exercised by the release checklist on a real machine.
#include "TestFramework.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <Encoder/FfmpegWriter.h>
#include <Encoder/VideoPreset.h>
#include <Transcode/Arguments.h>
#include <Transcode/ChildProcess.h>
#include <Transcode/OutputNaming.h>
#include <Transcode/Probe.h>
#include <Transcode/ProgressParser.h>
#include <Transcode/TranscodePreset.h>
#include <Transcode/TranscodeService.h>

#include <windows.h>

using namespace vsr;
using namespace vsr::transcode;

namespace {

std::wstring TempDirectory() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / L"MeetRecTranscodeTests";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.wstring();
}

} // namespace

VSR_TEST(Transcode, ChildCaptureReadsStdoutAndExitCode) {
    wchar_t systemRoot[MAX_PATH] = {};
    GetEnvironmentVariableW(L"SystemRoot", systemRoot, MAX_PATH);
    const std::wstring cmd = std::wstring(systemRoot) + L"\\System32\\cmd.exe";
    auto result = RunChildCapture(cmd, L"/c echo hello", 5000);
    VSR_CHECK(result.IsOk());
    if (result.IsOk()) {
        VSR_CHECK_EQ(result.Value().exitCode, static_cast<DWORD>(0));
        VSR_CHECK(!result.Value().timedOut);
        VSR_CHECK(result.Value().stdoutText.find("hello") != std::string::npos);
    }
}

VSR_TEST(Transcode, ProbeParsesFormatAndStreams) {
    const std::string json = R"({
        "streams": [
            {"codec_type": "video", "codec_name": "h264", "width": 1920,
             "height": 1080, "avg_frame_rate": "30000/1001",
             "duration": "754.300000"},
            {"codec_type": "audio", "codec_name": "aac", "channels": 2}
        ],
        "format": {"duration": "754.321000", "size": "123456789",
                   "bit_rate": "1309000"}
    })";
    auto info = ParseProbeJson(json);
    VSR_CHECK(info.IsOk());
    const MediaInfo& media = info.Value();
    VSR_CHECK_EQ(media.durationMs, static_cast<std::int64_t>(754321));
    VSR_CHECK_EQ(media.width, 1920u);
    VSR_CHECK_EQ(media.height, 1080u);
    VSR_CHECK(media.frameRate > 29.96 && media.frameRate < 29.98);
    VSR_CHECK_EQ(media.bitrateBps, static_cast<std::int64_t>(1309000));
    VSR_CHECK_EQ(media.fileBytes, static_cast<std::uint64_t>(123456789));
    VSR_CHECK(media.hasVideo);
    VSR_CHECK(media.hasAudio);
    VSR_CHECK_EQ(media.audioChannels, 2u);
    VSR_CHECK_EQ(media.videoCodec, std::string("h264"));
}

VSR_TEST(Transcode, ProbeSkipsAttachedCoverArt) {
    const std::string json = R"({
        "streams": [
            {"codec_type": "video", "codec_name": "mjpeg", "width": 600,
             "height": 600, "disposition": {"attached_pic": 1}},
            {"codec_type": "video", "codec_name": "hevc", "width": 1280,
             "height": 720, "avg_frame_rate": "25/1"}
        ],
        "format": {"duration": "120.0", "size": "1000", "bit_rate": "500000"}
    })";
    auto info = ParseProbeJson(json);
    VSR_CHECK(info.IsOk());
    VSR_CHECK_EQ(info.Value().videoCodec, std::string("hevc"));
    VSR_CHECK_EQ(info.Value().width, 1280u);
}

VSR_TEST(Transcode, ProbeRejectsAudioOnly) {
    const std::string json = R"({
        "streams": [{"codec_type": "audio", "codec_name": "aac", "channels": 2}],
        "format": {"duration": "10.0", "size": "1000", "bit_rate": "64000"}
    })";
    auto info = ParseProbeJson(json);
    VSR_CHECK(!info.IsOk());
}

VSR_TEST(Transcode, ProbeRejectsMalformedJson) {
    auto info = ParseProbeJson("{ not json");
    VSR_CHECK(!info.IsOk());
}

VSR_TEST(Transcode, ProgressParsesMicrosecondsAndClock) {
    ProgressSample sample = ParseProgressLine("out_time_us=1234567");
    VSR_CHECK(sample.valid);
    VSR_CHECK_EQ(sample.outTimeUs, static_cast<std::int64_t>(1234567));

    sample = ParseProgressLine("out_time_ms=7654321");
    VSR_CHECK(sample.valid);
    VSR_CHECK_EQ(sample.outTimeUs, static_cast<std::int64_t>(7654321));

    sample = ParseProgressLine("out_time=00:01:02.500000");
    VSR_CHECK(sample.valid);
    VSR_CHECK_EQ(sample.outTimeUs, static_cast<std::int64_t>(62500000));

    sample = ParseProgressLine("speed=1.25x");
    VSR_CHECK(sample.valid);
    VSR_CHECK(sample.speed > 1.24 && sample.speed < 1.26);

    sample = ParseProgressLine("speed=N/A");
    VSR_CHECK(sample.valid);
    VSR_CHECK_EQ(sample.speed, 0.0);

    sample = ParseProgressLine("total_size=4096");
    VSR_CHECK_EQ(sample.totalBytes, static_cast<std::uint64_t>(4096));

    sample = ParseProgressLine("progress=end");
    VSR_CHECK(sample.valid);
    VSR_CHECK(sample.finished);

    sample = ParseProgressLine("bitrate=1234.5kbits/s");
    VSR_CHECK(!sample.valid);
}

VSR_TEST(Transcode, ArgumentsUseScreenRecipeAndSourceResolution) {
    const EncodeOptions options = OptionsForQuality(Quality::Standard);
    const std::wstring args = BuildTranscodeArguments(
        L"C:\\videos\\meeting.mp4", L"C:\\videos\\meeting-min.partial.mp4", options,
        Resolution::Keep, 1920, 1080, 30.0);
    VSR_CHECK(args.find(L"-c:v libx264") != std::wstring::npos);
    VSR_CHECK(args.find(L"-preset veryfast") != std::wstring::npos);
    VSR_CHECK(args.find(L"-tune stillimage") != std::wstring::npos);
    VSR_CHECK(args.find(L"-crf 26") != std::wstring::npos);
    VSR_CHECK(args.find(L"-maxrate 2000000") != std::wstring::npos);
    VSR_CHECK(args.find(L"-bufsize 4000000") != std::wstring::npos);
    VSR_CHECK(args.find(L"-g 150") != std::wstring::npos);
    VSR_CHECK(args.find(L"-vf scale=") == std::wstring::npos);
    VSR_CHECK(args.find(L"-map 0:a:0?") != std::wstring::npos);
    VSR_CHECK(args.find(L"-progress pipe:1") != std::wstring::npos);
    VSR_CHECK(args.find(L"\"C:\\videos\\meeting.mp4\"") != std::wstring::npos);
    VSR_CHECK(args.find(L"\"C:\\videos\\meeting-min.partial.mp4\"") !=
              std::wstring::npos);
}

VSR_TEST(Transcode, ArgumentsScaleDownButNeverUp) {
    const EncodeOptions options = OptionsForQuality(Quality::Standard);
    const std::wstring down = BuildTranscodeArguments(
        L"in.mp4", L"out.mp4", options, Resolution::P720, 1920, 1080, 30.0);
    VSR_CHECK(down.find(L"-vf scale=1280:720") != std::wstring::npos);

    const std::wstring smallSource = BuildTranscodeArguments(
        L"in.mp4", L"out.mp4", options, Resolution::P720, 640, 360, 30.0);
    VSR_CHECK(smallSource.find(L"-vf scale=") == std::wstring::npos);

    const std::wstring portrait = BuildTranscodeArguments(
        L"in.mp4", L"out.mp4", options, Resolution::P720, 1080, 1920, 30.0);
    VSR_CHECK(portrait.find(L"-vf scale=404:720") != std::wstring::npos);
}

VSR_TEST(Transcode, HardwareArgumentsUseDetectedVendorEncoder) {
    const EncodeOptions options = OptionsForQuality(Quality::Standard);
    const std::wstring qsv = BuildTranscodeArguments(
        L"in.mp4", L"out.mp4", options, Resolution::Keep, 1920, 1080, 30.0,
        VideoEncoder::Qsv);
    VSR_CHECK(qsv.find(L"-c:v h264_qsv") != std::wstring::npos);
    VSR_CHECK(qsv.find(L"-global_quality 31") != std::wstring::npos);
    VSR_CHECK(qsv.find(L"-crf") == std::wstring::npos);
    VSR_CHECK(qsv.find(L"-tune stillimage") == std::wstring::npos);

    const std::wstring nvenc = BuildTranscodeArguments(
        L"in.mp4", L"out.mp4", options, Resolution::Keep, 1920, 1080, 30.0,
        VideoEncoder::Nvenc);
    VSR_CHECK(nvenc.find(L"-c:v h264_nvenc") != std::wstring::npos);
    VSR_CHECK(nvenc.find(L"-rc vbr") != std::wstring::npos);
    VSR_CHECK(nvenc.find(L"-cq 26") != std::wstring::npos);
    VSR_CHECK(nvenc.find(L"-b:v 0") != std::wstring::npos);

    const std::wstring amf = BuildTranscodeArguments(
        L"in.mp4", L"out.mp4", options, Resolution::Keep, 1920, 1080, 30.0,
        VideoEncoder::Amf);
    VSR_CHECK(amf.find(L"-c:v h264_amf") != std::wstring::npos);
    VSR_CHECK(amf.find(L"-rc cqp") != std::wstring::npos);
    VSR_CHECK(amf.find(L"-qp_i 26") != std::wstring::npos);
}

VSR_TEST(Transcode, HardwareRecordingUsesSameRateControlAsCompression) {
    // Recording and compression are compared directly by users, so the GPU
    // rate-control fragments must stay identical. A drift here is what made
    // GPU recordings larger than the same clip compressed with the same GPU.
    const encoder::PresetParams preset =
        encoder::GetPresetParams(encoder::QualityPreset::Standard);
    const EncodeOptions options = OptionsForQuality(Quality::Standard);
    struct Row {
        const wchar_t* encoderName;
        VideoEncoder encoder;
        const wchar_t* rateControl;
    };
    const Row rows[] = {
        {L"h264_qsv", VideoEncoder::Qsv, L"-preset veryfast -global_quality 31"},
        {L"h264_nvenc", VideoEncoder::Nvenc,
         L"-preset p5 -tune hq -rc vbr -cq 26 -b:v 0 -maxrate 2000000 -bufsize "
         L"4000000"},
        {L"h264_amf", VideoEncoder::Amf,
         L"-quality balanced -rc cqp -qp_i 26 -qp_p 26 -qp_b 26"},
    };
    for (const Row& row : rows) {
        const std::wstring recording = encoder::FfmpegWriter::BuildVideoArguments(
            preset, L"\\\\.\\pipe\\video", L"out.mp4", row.encoderName);
        const std::wstring compression = BuildTranscodeArguments(
            L"in.mp4", L"out.mp4", options, Resolution::Keep, 1920, 1080, 30.0,
            row.encoder);
        VSR_CHECK(recording.find(row.rateControl) != std::wstring::npos);
        VSR_CHECK(compression.find(row.rateControl) != std::wstring::npos);
    }
}

VSR_TEST(Transcode, HighPresetDropsStillImageTune) {
    const EncodeOptions options = OptionsForQuality(Quality::High);
    const std::wstring args = BuildTranscodeArguments(L"in.mp4", L"out.mp4", options,
                                                      Resolution::Keep, 1920, 1080, 30.0);
    VSR_CHECK(args.find(L"-crf 22") != std::wstring::npos);
    VSR_CHECK(args.find(L"-tune stillimage") == std::wstring::npos);
    VSR_CHECK(args.find(L"-ac 2") != std::wstring::npos);
}

VSR_TEST(Transcode, PresetTableMirrorsEncoderPresets) {
    struct Row {
        Quality quality;
        encoder::QualityPreset encoderPreset;
    };
    const Row rows[] = {
        {Quality::Low, encoder::QualityPreset::Low},
        {Quality::Standard, encoder::QualityPreset::Standard},
        {Quality::High, encoder::QualityPreset::High},
    };
    for (const Row& row : rows) {
        const EncodeOptions transcodeOptions = OptionsForQuality(row.quality);
        const encoder::PresetParams preset = encoder::GetPresetParams(row.encoderPreset);
        VSR_CHECK_EQ(transcodeOptions.crf, preset.crf);
        VSR_CHECK_EQ(transcodeOptions.maxrateBps, preset.bitrateBps);
        VSR_CHECK_EQ(transcodeOptions.gopSeconds, preset.gopSeconds);
        VSR_CHECK_EQ(transcodeOptions.screenContentTune, preset.screenContentTune);
        VSR_CHECK_EQ(transcodeOptions.audioBitrateBps, preset.audioBitrateBps);
        VSR_CHECK_EQ(transcodeOptions.audioChannels, preset.audioChannels);
    }
}

VSR_TEST(Transcode, EstimatedOutputIsBoundedByRateCeiling) {
    const EncodeOptions options = OptionsForQuality(Quality::Standard);
    const std::uint64_t bytes = EstimatedOutputBytes(options, 60000);
    // 60 s * (2.0 Mbps video + 64 kbps audio) / 8 * 1.05.
    VSR_CHECK(bytes > 16000000ull);
    VSR_CHECK(bytes < 17000000ull);
    VSR_CHECK_EQ(EstimatedOutputBytes(options, 0), static_cast<std::uint64_t>(0));
}

VSR_TEST(Transcode, TargetDimensionsStayEvenAndKeepAspect) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TargetDimensions(Resolution::P720, 1919, 1079, width, height);
    VSR_CHECK_EQ(width, 1280u);
    VSR_CHECK_EQ(height, 720u);

    TargetDimensions(Resolution::Keep, 1919, 1079, width, height);
    VSR_CHECK_EQ(width, 1918u);
    VSR_CHECK_EQ(height, 1078u);

    TargetDimensions(Resolution::P720, 0, 0, width, height);
    VSR_CHECK_EQ(width, 0u);
    VSR_CHECK_EQ(height, 0u);
}

VSR_TEST(Transcode, OutputNamingAddsMinSuffix) {
    const std::filesystem::path input = L"C:\\videos\\meeting.mp4";
    VSR_CHECK(DeriveOutputPath(input, L"") ==
              std::filesystem::path(L"C:\\videos\\meeting-min.mp4"));
    VSR_CHECK(DeriveOutputPath(input, L"D:\\out") ==
              std::filesystem::path(L"D:\\out\\meeting-min.mp4"));
    VSR_CHECK(PartialPathFor(L"C:\\videos\\meeting-min.mp4") ==
              std::filesystem::path(L"C:\\videos\\meeting-min.partial.mp4"));
}

VSR_TEST(Transcode, OutputNamingAvoidsExistingFiles) {
    const std::filesystem::path dir = TempDirectory();
    const std::filesystem::path desired = dir / L"clip-min.mp4";
    std::error_code ec;
    std::filesystem::remove(desired, ec);
    std::filesystem::remove(dir / L"clip-min-2.mp4", ec);
    std::filesystem::remove(dir / L"clip-min-3.mp4", ec);

    VSR_CHECK(EnsureUniquePath(desired) == desired);

    std::ofstream(desired).put('x');
    VSR_CHECK(EnsureUniquePath(desired) == dir / L"clip-min-2.mp4");

    std::ofstream(dir / L"clip-min-2.mp4").put('x');
    VSR_CHECK(EnsureUniquePath(desired) == dir / L"clip-min-3.mp4");

    std::filesystem::remove(desired, ec);
    std::filesystem::remove(dir / L"clip-min-2.mp4", ec);
}

VSR_TEST(Transcode, FileAddedAfterFinishedBatchWaitsForStart) {
    TranscodeService service;
    const std::filesystem::path dir = TempDirectory();
    const std::filesystem::path first = dir / L"missing-input-1.mp4";
    const std::filesystem::path second = dir / L"missing-input-2.mp4";
    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);

    VSR_CHECK(service.Add(first, {}) != 0);
    service.Start(Quality::Standard, Resolution::Keep);

    bool finished = false;
    for (int i = 0; i < 400 && !finished; ++i) {
        const ServiceSnapshot snapshot = service.Snapshot();
        finished = snapshot.jobs.size() == 1 &&
                   (snapshot.jobs[0].state == JobState::Failed ||
                    snapshot.jobs[0].state == JobState::Completed);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    VSR_CHECK(finished);

    // The queue has drained; a newly added file must stay queued until the
    // caller starts the next batch explicitly.
    VSR_CHECK(service.Add(second, {}) != 0);
    bool probed = false;
    for (int i = 0; i < 400 && !probed; ++i) {
        const ServiceSnapshot snapshot = service.Snapshot();
        probed = snapshot.jobs.size() == 2 && snapshot.jobs[1].probed;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    VSR_CHECK(probed);
    const ServiceSnapshot after = service.Snapshot();
    VSR_CHECK(!after.active);
    VSR_CHECK(after.jobs.size() == 2);
    if (after.jobs.size() == 2) {
        VSR_CHECK(after.jobs[1].state == JobState::Queued);
    }
}
