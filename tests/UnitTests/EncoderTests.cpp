// Encoder policy and preset tests (T030, T031): deterministic encoder
// selection (FR-062) and spec preset table (spec.md §7.3, FR-051).
#include "TestFramework.h"
#include <Encoder/VideoWriterFactory.h>
#include <Encoder/FfmpegWriter.h>
#include <Encoder/EncoderProbe.h>
#include <Encoder/VideoPreset.h>

using namespace vsr;
using namespace vsr::encoder;

VSR_TEST(Preset, TableMatchesSpec) {
    VSR_CHECK(ValidatePresetTable().IsOk());

    const auto low = GetPresetParams(QualityPreset::Low);
    VSR_CHECK_EQ(low.width, 1280u);
    VSR_CHECK_EQ(low.height, 720u);
    VSR_CHECK_EQ(low.fps, 30u);
    VSR_CHECK_EQ(low.crf, 28);
    VSR_CHECK_EQ(low.bitrateBps, 1'000'000u);
    VSR_CHECK_EQ(low.gopSeconds, 5u);
    VSR_CHECK_EQ(low.audioBitrateBps, 48'000u);
    VSR_CHECK_EQ(low.audioChannels, 1u);

    const auto standard = GetPresetParams(QualityPreset::Standard);
    VSR_CHECK_EQ(standard.width, 1920u);
    VSR_CHECK_EQ(standard.height, 1080u);
    VSR_CHECK_EQ(standard.fps, 30u);
    VSR_CHECK_EQ(standard.crf, 26);
    VSR_CHECK_EQ(standard.bitrateBps, 2'000'000u);
    VSR_CHECK_EQ(standard.gopSeconds, 5u);
    VSR_CHECK_EQ(standard.audioBitrateBps, 64'000u);
    VSR_CHECK_EQ(standard.audioChannels, 1u);

    const auto high = GetPresetParams(QualityPreset::High);
    VSR_CHECK_EQ(high.width, 1920u);
    VSR_CHECK_EQ(high.height, 1080u);
    VSR_CHECK_EQ(high.fps, 60u);
    VSR_CHECK_EQ(high.crf, 22);
    VSR_CHECK_EQ(high.bitrateBps, 6'000'000u);
    VSR_CHECK_EQ(high.gopSeconds, 5u);
    VSR_CHECK_EQ(high.audioBitrateBps, 128'000u);
    VSR_CHECK_EQ(high.audioChannels, 2u);

    // Screen-content tuning is for the static tiers; the 60 FPS tier records
    // motion and keeps default deblocking.
    VSR_CHECK(low.screenContentTune);
    VSR_CHECK(standard.screenContentTune);
    VSR_CHECK(!high.screenContentTune);
}

VSR_TEST(Preset, FrameDurationFromFps) {
    VSR_CHECK_EQ(FrameDuration100Ns(30), 333'333); // 10M/30
    VSR_CHECK_EQ(FrameDuration100Ns(60), 166'666); // 10M/60
    VSR_CHECK_EQ(FrameDuration100Ns(0), 333'333);  // guarded default
}

VSR_TEST(EncoderPolicy, PrefersHardwareD3D11Aware) {
    EncoderCapability sw{};
    sw.name = L"sw";
    sw.acceptsNv12 = true;

    EncoderCapability hw{};
    hw.name = L"hw-nvenc";
    hw.hardware = true;
    hw.d3d11Aware = true;
    hw.acceptsNv12 = true;

    auto pick = SelectEncoder({sw, hw});
    VSR_CHECK(pick.IsOk());
    VSR_CHECK(pick.Value().name == L"hw-nvenc");
}

VSR_TEST(EncoderPolicy, HardwareWithoutD3D11BeatsSoftware) {
    EncoderCapability sw{};
    sw.name = L"sw-ms";
    sw.acceptsNv12 = true;

    EncoderCapability hw{};
    hw.name = L"hw-legacy";
    hw.hardware = true;
    hw.d3d11Aware = false;
    hw.acceptsNv12 = true;

    auto pick = SelectEncoder({sw, hw});
    VSR_CHECK(pick.IsOk());
    VSR_CHECK(pick.Value().name == L"hw-legacy");
}

VSR_TEST(EncoderPolicy, RejectsNoNv12Candidates) {
    EncoderCapability a{};
    a.name = L"no-nv12";
    a.hardware = true;
    a.acceptsNv12 = false;

    auto pick = SelectEncoder({a});
    VSR_CHECK(!pick.IsOk());
    VSR_CHECK(pick.GetError().code == ErrorCode::EncoderUnavailable); // FR-063
}

VSR_TEST(EncoderPolicy, EmptyCandidateListFails) {
    auto pick = SelectEncoder({});
    VSR_CHECK(!pick.IsOk());
    VSR_CHECK(pick.GetError().code == ErrorCode::EncoderUnavailable);
}


// --- Encoder preference (user-selectable hardware/software) -----------------

VSR_TEST(EncoderPolicy, SoftwarePreferencePicksSoftwareEvenWhenHardwareExists) {
    EncoderCapability sw{};
    sw.name = L"sw-ms";
    sw.acceptsNv12 = true;

    EncoderCapability hw{};
    hw.name = L"hw-amd";
    hw.hardware = true;
    hw.d3d11Aware = true;
    hw.acceptsNv12 = true;

    auto pick = SelectEncoder({sw, hw}, EncoderPreference::Software);
    VSR_CHECK(pick.IsOk());
    VSR_CHECK(pick.Value().name == L"sw-ms");
}

VSR_TEST(EncoderPolicy, HardwarePreferencePicksHardwareEvenWhenSwRanksFirst) {
    EncoderCapability sw{};
    sw.name = L"sw-ms";
    sw.acceptsNv12 = true;

    EncoderCapability hw{};
    hw.name = L"hw-amd";
    hw.hardware = true;
    hw.acceptsNv12 = true;

    auto pick = SelectEncoder({sw, hw}, EncoderPreference::Hardware);
    VSR_CHECK(pick.IsOk());
    VSR_CHECK(pick.Value().name == L"hw-amd");
}

VSR_TEST(EncoderPolicy, SoftwarePreferenceFailsWhenOnlyHardwareExists) {
    EncoderCapability hw{};
    hw.name = L"hw-amd";
    hw.hardware = true;
    hw.acceptsNv12 = true;

    auto pick = SelectEncoder({hw}, EncoderPreference::Software);
    VSR_CHECK(!pick.IsOk());
    VSR_CHECK(pick.GetError().code == ErrorCode::EncoderUnavailable);
}

VSR_TEST(EncoderPolicy, HardwarePreferenceIgnoresNonNv12Hardware) {
    EncoderCapability hw{};
    hw.name = L"hw-nonv12";
    hw.hardware = true;
    hw.acceptsNv12 = false;

    auto pick = SelectEncoder({hw}, EncoderPreference::Hardware);
    VSR_CHECK(!pick.IsOk());
}

VSR_TEST(EncoderPolicy, AutoPreferenceKeepsHardwareFirstOrder) {
    EncoderCapability sw{};
    sw.name = L"sw-ms";
    sw.acceptsNv12 = true;

    EncoderCapability hw{};
    hw.name = L"hw-amd";
    hw.hardware = true;
    hw.acceptsNv12 = true;

    auto pick = SelectEncoder({sw, hw}, EncoderPreference::Auto);
    VSR_CHECK(pick.IsOk());
    VSR_CHECK(pick.Value().name == L"hw-amd");

    VSR_CHECK(EncoderPreferenceFromString("software") == EncoderPreference::Software);
    VSR_CHECK(EncoderPreferenceFromString("garbage") == EncoderPreference::Auto);
    VSR_CHECK(std::string(ToString(EncoderPreference::Hardware)) == "hardware");
}
VSR_TEST(EncoderPolicy, ExcludeNameSkipsTheFailedCandidate) {
    EncoderCapability sw{};
    sw.name = L"sw-ms";
    sw.acceptsNv12 = true;

    EncoderCapability hw{};
    hw.name = L"hw-amd";
    hw.hardware = true;
    hw.acceptsNv12 = true;

    // Auto would normally pick hw-amd; excluding it falls through to sw-ms.
    auto pick = SelectEncoder({sw, hw}, EncoderPreference::Auto, L"hw-amd");
    VSR_CHECK(pick.IsOk());
    VSR_CHECK(pick.Value().name == L"sw-ms");

    // Excluding the software encoder under a Software preference has no
    // candidate left: typed failure.
    auto none = SelectEncoder({sw, hw}, EncoderPreference::Software, L"sw-ms");
    VSR_CHECK(!none.IsOk());
    VSR_CHECK(none.GetError().code == ErrorCode::EncoderUnavailable);

    // Excluding a name that matches nothing changes the normal choice.
    auto unaffected = SelectEncoder({sw, hw}, EncoderPreference::Auto, L"not-in-list");
    VSR_CHECK(unaffected.IsOk());
    VSR_CHECK(unaffected.Value().name == L"hw-amd");
}

VSR_TEST(FfmpegBackend, BackendStringRoundTrip) {
    VSR_CHECK(vsr::encoder::WriterBackendFromString("ffmpeg") ==
              vsr::encoder::WriterBackend::Ffmpeg);
    VSR_CHECK(vsr::encoder::WriterBackendFromString("mf") ==
              vsr::encoder::WriterBackend::MediaFoundation);
    VSR_CHECK(vsr::encoder::WriterBackendFromString("auto") ==
              vsr::encoder::WriterBackend::Auto);
    VSR_CHECK(vsr::encoder::WriterBackendFromString("garbage") ==
              vsr::encoder::WriterBackend::Auto);
    VSR_CHECK(std::string(vsr::encoder::ToString(vsr::encoder::WriterBackend::Ffmpeg)) ==
              "ffmpeg");
}

VSR_TEST(FfmpegBackend, CommandLineCarriesPipelineParameters) {
    vsr::encoder::PresetParams params{};
    params.width = 1920;
    params.height = 1080;
    params.fps = 30;
    params.crf = 26;
    params.bitrateBps = 2'000'000;
    params.gopSeconds = 5;
    params.screenContentTune = true;
    params.audioBitrateBps = 64'000;
    params.audioChannels = 1;
    vsr::encoder::AudioFormat audio{};
    audio.sampleRate = 44100;
    audio.channels = 2;

    // Video child: exactly ONE input (the rawvideo pipe) - a second raw pipe
    // into the same ffmpeg stalls its interleaved demuxer.
    const std::wstring videoArgs = vsr::encoder::FfmpegWriter::BuildVideoArguments(
        params, L"' + pipe_video + '", L"' + out_vid + '");
    VSR_CHECK(videoArgs.find(L"-f rawvideo") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-pix_fmt nv12") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-video_size 1920x1080") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-framerate 30") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"' + pipe_video + '") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-c:v libx264") != std::wstring::npos);
    // Meeting-oriented capped CRF: quality target + preset ceiling.
    VSR_CHECK(videoArgs.find(L"-crf 26") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-maxrate 2000000") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-bufsize 4000000") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-b:v") == std::wstring::npos); // no ABR on x264
    VSR_CHECK(videoArgs.find(L"-tune stillimage") != std::wstring::npos);
    // Keyframe interval is seconds * fps: 5 s at 30 FPS.
    VSR_CHECK(videoArgs.find(L"-g 150") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"-f mp4") != std::wstring::npos);
    VSR_CHECK(videoArgs.find(L"pipe") == videoArgs.rfind(L"pipe")); // one pipe input only

    // The motion tier opts out of the still-image tuning, and the keyframe
    // interval follows the preset rate.
    vsr::encoder::PresetParams motion = params;
    motion.fps = 60;
    motion.gopSeconds = 2;
    motion.screenContentTune = false;
    const std::wstring motionArgs = vsr::encoder::FfmpegWriter::BuildVideoArguments(
        motion, L"' + pipe_video + '", L"' + out_vid + '");
    VSR_CHECK(motionArgs.find(L"-tune") == std::wstring::npos);
    VSR_CHECK(motionArgs.find(L"-g 120") != std::wstring::npos);

    // Audio child: one s16le pipe input -> AAC in an m4a container. The preset
    // bitrate and a downmix to the preset channel count apply on top of the
    // endpoint's captured layout.
    const std::wstring audioArgs = vsr::encoder::FfmpegWriter::BuildAudioArguments(
        audio, params, L"' + pipe_audio + '", L"' + out_m4a + '");
    VSR_CHECK(audioArgs.find(L"-f s16le") != std::wstring::npos);
    VSR_CHECK(audioArgs.find(L"-ar 44100") != std::wstring::npos);
    VSR_CHECK(audioArgs.find(L"-ac 2") != std::wstring::npos);
    VSR_CHECK(audioArgs.find(L"' + pipe_audio + '") != std::wstring::npos);
    VSR_CHECK(audioArgs.find(L"-c:a aac -b:a 64k") != std::wstring::npos);
    VSR_CHECK(audioArgs.find(L"-ac 1") != std::wstring::npos); // 2 -> 1 downmix
    VSR_CHECK(audioArgs.find(L"-f ipod") != std::wstring::npos);

    // A stereo preset leaves the captured layout untouched.
    vsr::encoder::PresetParams stereo = params;
    stereo.audioBitrateBps = 128'000;
    stereo.audioChannels = 2;
    const std::wstring stereoArgs = vsr::encoder::FfmpegWriter::BuildAudioArguments(
        audio, stereo, L"' + pipe_audio + '", L"' + out_m4a + '");
    VSR_CHECK(stereoArgs.find(L"-b:a 128k") != std::wstring::npos);
    VSR_CHECK(stereoArgs.find(L"-ac 1") == std::wstring::npos);

    // Finalize remux: stream copy of both intermediates into the MP4.
    const std::wstring muxArgs = vsr::encoder::FfmpegWriter::BuildMuxArguments(
        L"' + out_vid + '", L"' + out_m4a + '", L"' + out_rec + '");
    VSR_CHECK(muxArgs.find(L"-c copy") != std::wstring::npos);
    VSR_CHECK(muxArgs.find(L"' + out_vid + '") != std::wstring::npos);
    VSR_CHECK(muxArgs.find(L"' + out_m4a + '") != std::wstring::npos);
    VSR_CHECK(muxArgs.rfind(L"-f mp4") != std::wstring::npos);
    VSR_CHECK(muxArgs.find(L"' + out_rec + '") != std::wstring::npos);
}
