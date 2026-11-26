// P1 speech pipeline tests: resampling, subtitle formatting, transcript files
// and model-pack discovery. SenseVoice inference itself needs the 230 MB model
// pack and is covered by `MeetRec --speech-test`.
#include "TestFramework.h"

#include <App/CaptionStyle.h>
#include <Infrastructure/Strings.h>
#include <Speech/CaptionFeed.h>
#include <Speech/SpeechModelPack.h>
#include <Speech/SpeechResampler.h>
#include <Speech/SubtitleFormat.h>
#include <Speech/TranscriptStore.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path TempDir(const wchar_t* name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        (std::wstring(name) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(stamp));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string ReadAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

double Rms(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (float value : samples) sum += static_cast<double>(value) * value;
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

std::vector<std::int16_t> Sine(std::uint32_t rate, double hz, double seconds,
                               double amplitude = 0.5) {
    const std::size_t frames = static_cast<std::size_t>(rate * seconds);
    std::vector<std::int16_t> samples(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double value =
            amplitude * std::sin(2.0 * 3.14159265358979323846 * hz *
                                 static_cast<double>(i) / static_cast<double>(rate));
        samples[i] = static_cast<std::int16_t>(std::lround(value * 32767.0));
    }
    return samples;
}

int DisplayWidth(const std::wstring& text) {
    int width = 0;
    for (wchar_t c : text) {
        width += (c >= 0x2E80) ? 2 : 1;
    }
    return width;
}

} // namespace

VSR_TEST(SpeechResampler, Downsamples48kToOneSecondAt16k) {
    vsr::speech::SpeechResampler resampler(48000);
    const std::vector<std::int16_t> input = Sine(48000, 1000.0, 1.0);
    std::vector<float> output;
    resampler.Process(input.data(), input.size(), output);

    VSR_CHECK(output.size() >= 15998 && output.size() <= 16002);
    const double rms = Rms(output);
    // 0.5 amplitude sine has RMS 0.3535; the 7.2 kHz low-pass keeps 1 kHz flat.
    VSR_CHECK(rms > 0.30 && rms < 0.40);
}

VSR_TEST(SpeechResampler, ChunkedMatchesSingleCall) {
    const std::vector<std::int16_t> input = Sine(48000, 440.0, 0.5);
    vsr::speech::SpeechResampler single(48000);
    vsr::speech::SpeechResampler chunked(48000);
    std::vector<float> whole;
    std::vector<float> parts;
    single.Process(input.data(), input.size(), whole);
    const std::size_t chunk = 480;
    for (std::size_t offset = 0; offset < input.size(); offset += chunk) {
        const std::size_t count = std::min(chunk, input.size() - offset);
        chunked.Process(input.data() + offset, count, parts);
    }
    VSR_CHECK_EQ(whole.size(), parts.size());
    for (std::size_t i = 0; i < std::min(whole.size(), parts.size()); ++i) {
        VSR_CHECK(std::fabs(whole[i] - parts[i]) < 1e-6f);
    }
}

VSR_TEST(SpeechResampler, Handles44100AndPassthrough) {
    vsr::speech::SpeechResampler arbitrary(44100);
    const std::vector<std::int16_t> input = Sine(44100, 1000.0, 1.0);
    std::vector<float> output;
    arbitrary.Process(input.data(), input.size(), output);
    VSR_CHECK(output.size() >= 15996 && output.size() <= 16004);

    vsr::speech::SpeechResampler passthrough(16000);
    std::vector<float> direct;
    passthrough.Process(input.data(), 100, direct);
    VSR_CHECK_EQ(direct.size(), static_cast<std::size_t>(100));
    VSR_CHECK(std::fabs(direct[1] - static_cast<float>(input[1]) / 32768.0f) < 1e-7f);
}

VSR_TEST(Subtitle, TimestampsAndLabels) {
    using namespace vsr::speech;
    VSR_CHECK_EQ(FormatSrtTimestamp(0), std::wstring(L"00:00:00,000"));
    VSR_CHECK_EQ(FormatSrtTimestamp(3'661'234'000LL), std::wstring(L"01:01:01,234"));
    VSR_CHECK_EQ(FormatSrtTimestamp(-5), std::wstring(L"00:00:00,000"));
    VSR_CHECK_EQ(FormatTranscriptClock(3'661'000'000LL), std::wstring(L"[01:01:01]"));
    VSR_CHECK_EQ(SpeakerPrefix(true, true), std::wstring(L"我："));
    VSR_CHECK_EQ(SpeakerPrefix(false, false), std::wstring(L"Them: "));
}

VSR_TEST(Subtitle, SanitizesRichTranscriptionTags) {
    using namespace vsr::speech;
    VSR_CHECK_EQ(SanitizeTranscriptText(L"<|zh|><|HAPPY|>你好\r\n世界"),
                 std::wstring(L"你好 世界"));
    VSR_CHECK_EQ(SanitizeTranscriptText(L"<|zh|><|Speech|>"), std::wstring());
    VSR_CHECK_EQ(SanitizeTranscriptText(L"  a   b  "), std::wstring(L"a b"));
}

VSR_TEST(Subtitle, WrapsCjkAndLatinWithoutOverflow) {
    using namespace vsr::speech;
    const std::wstring cjk(30, L'测');
    const std::vector<std::wstring> lines = WrapSubtitleText(cjk, 42);
    VSR_CHECK_EQ(lines.size(), static_cast<std::size_t>(2));
    VSR_CHECK_EQ(lines[0].size(), static_cast<std::size_t>(21));
    for (const std::wstring& line : lines) {
        VSR_CHECK(DisplayWidth(line) <= 42);
    }

    const std::vector<std::wstring> latin = WrapSubtitleText(
        L"hello world this is a test of the emergency broadcast system", 42);
    VSR_CHECK(latin.size() >= 2);
    for (const std::wstring& line : latin) {
        VSR_CHECK(line.size() <= 42);
        VSR_CHECK(!line.empty());
        VSR_CHECK(line.front() != L' ');
    }
}

VSR_TEST(TranscriptStore, WritesSrtTxtAndRemovesPartial) {
    using namespace vsr::speech;
    const std::filesystem::path dir = TempDir(L"meetrec-transcript");
    const std::filesystem::path base = dir / L"Recording_20261126_120000";

    TranscriptStore store(true);
    VSR_CHECK(store.OpenPartial(base));
    TranscriptSegment first;
    first.startUs = 1'000'000;
    first.endUs = 2'500'000;
    first.microphone = false;
    first.language = "<|zh|>";
    first.text = L"大家好，会议开始。";
    store.Add(first);

    TranscriptSegment second;
    second.startUs = 3'000'000;
    second.endUs = 4'000'000;
    second.microphone = true;
    second.text = L"好的，我先共享屏幕。";
    store.Add(second);

    const TranscriptStore::Written written = store.Finalize(base);
    VSR_CHECK(!written.srt.empty());
    VSR_CHECK(!written.txt.empty());
    VSR_CHECK(std::filesystem::exists(written.srt));
    VSR_CHECK(std::filesystem::exists(written.txt));
    VSR_CHECK(!std::filesystem::exists(base.wstring() + L".partial.srt"));

    const std::string srt = ReadAllBytes(written.srt);
    const std::string txt = ReadAllBytes(written.txt);
    VSR_CHECK(srt.rfind("\xEF\xBB\xBF", 0) == 0);
    VSR_CHECK(srt.find("00:00:01,000 --> 00:00:02,500") != std::string::npos);
    VSR_CHECK(srt.find(vsr::WideToUtf8(L"对方：")) != std::string::npos);
    VSR_CHECK(srt.find(vsr::WideToUtf8(L"我：")) != std::string::npos);
    VSR_CHECK(txt.rfind("\xEF\xBB\xBF", 0) == 0);
    VSR_CHECK(txt.find("[00:00:03]") != std::string::npos);

    // A second finalize with the same base must not overwrite the first pair.
    TranscriptStore secondStore(true);
    VSR_CHECK(secondStore.OpenPartial(base));
    secondStore.Add(first);
    const TranscriptStore::Written collision = secondStore.Finalize(base);
    VSR_CHECK(!collision.srt.empty());
    VSR_CHECK(collision.srt.wstring().find(L"-1.srt") != std::wstring::npos);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

VSR_TEST(TranscriptStore, EmptyRecordingLeavesNoFiles) {
    using namespace vsr::speech;
    const std::filesystem::path dir = TempDir(L"meetrec-transcript-empty");
    const std::filesystem::path base = dir / L"Recording_empty";
    TranscriptStore store(false);
    VSR_CHECK(store.OpenPartial(base));
    const TranscriptStore::Written written = store.Finalize(base);
    VSR_CHECK(written.srt.empty());
    VSR_CHECK(written.txt.empty());
    VSR_CHECK(!std::filesystem::exists(base.wstring() + L".partial.srt"));
    VSR_CHECK(!std::filesystem::exists(base.wstring() + L".srt"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

VSR_TEST(CaptionFeed, PublishesNewestCuesAndClears) {
    using namespace vsr::speech;
    CaptionFeed feed;
    VSR_CHECK(feed.Empty());
    feed.Push(L"first line", false, 0, 1'000'000);
    feed.Push(L"second line", true, 1'000'000, 2'000'000);
    VSR_CHECK(!feed.Empty());
    const std::vector<CaptionCue> cues = feed.Snapshot(2);
    VSR_CHECK_EQ(cues.size(), static_cast<std::size_t>(2));
    VSR_CHECK_EQ(cues[0].text, std::wstring(L"first line"));
    VSR_CHECK_EQ(cues[1].text, std::wstring(L"second line"));
    VSR_CHECK(cues[1].microphone);
    VSR_CHECK(feed.Sequence() >= 2);

    feed.Clear();
    VSR_CHECK(feed.Empty());
    VSR_CHECK(feed.Snapshot(2).empty());
}

VSR_TEST(CaptionFeed, KeepsOnlyTheMostRecentCues) {
    using namespace vsr::speech;
    CaptionFeed feed;
    for (int i = 0; i < 10; ++i) {
        feed.Push(L"cue " + std::to_wstring(i), false, i * 1'000'000,
                  (i + 1) * 1'000'000);
    }
    const std::vector<CaptionCue> cues = feed.Snapshot(2);
    VSR_CHECK_EQ(cues.size(), static_cast<std::size_t>(2));
    VSR_CHECK_EQ(cues[0].text, std::wstring(L"cue 8"));
    VSR_CHECK_EQ(cues[1].text, std::wstring(L"cue 9"));
    VSR_CHECK_EQ(feed.Snapshot(100).size(), static_cast<std::size_t>(8));
}

VSR_TEST(CaptionStyle, MapsKeysAndClampsValues) {
    using namespace vsr::app;
    VSR_CHECK_EQ(CaptionFontFamilyFromKey("segoe"), std::wstring(L"Segoe UI"));
    VSR_CHECK_EQ(CaptionFontFamilyFromKey("simsun"), std::wstring(L"SimSun"));
    VSR_CHECK_EQ(CaptionFontFamilyFromKey("bogus"),
                 std::wstring(L"Microsoft YaHei UI"));
    VSR_CHECK_EQ(CaptionFontSizeClamp(8), 14);
    VSR_CHECK_EQ(CaptionFontSizeClamp(99), 48);
    VSR_CHECK(CaptionColorIsAuto("auto"));
    VSR_CHECK(CaptionColorIsAuto(""));
    VSR_CHECK(!CaptionColorIsAuto("white"));
    VSR_CHECK_EQ(CaptionTextColorFromKey("yellow").GetValue(),
                 Gdiplus::Color(255, 245, 222, 140).GetValue());
    VSR_CHECK(CaptionAnchorFromKey("top-center") == CaptionAnchor::TopCenter);
    VSR_CHECK(CaptionAnchorFromKey("bottom-right") == CaptionAnchor::BottomRight);
    VSR_CHECK(CaptionAnchorFromKey("bogus") == CaptionAnchor::BottomCenter);

    const CaptionStyle style =
        CaptionStyleFromKeys("arial", 20, "green", "bottom-left");
    VSR_CHECK_EQ(style.fontFamily, std::wstring(L"Arial"));
    VSR_CHECK_EQ(style.fontSize, 20);
    VSR_CHECK(!style.autoColor);
    VSR_CHECK(style.anchor == CaptionAnchor::BottomLeft);
}

VSR_TEST(SpeechModelPack, ReportsMissingPackWithPath) {
    using namespace vsr::speech;
    const std::filesystem::path dir = TempDir(L"meetrec-model-missing") / L"absent";
    const SpeechModelStatus status = LocateSpeechModel(dir);
    VSR_CHECK(!status.ready);
    VSR_CHECK(!status.detail.empty());
    VSR_CHECK(status.paths.directory == dir);
    VSR_CHECK_EQ(DefaultSpeechModelDirectory().filename(), std::filesystem::path(L"sensevoice"));
    std::error_code ec;
    std::filesystem::remove_all(dir.parent_path(), ec);
}
