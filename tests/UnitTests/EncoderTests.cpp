// Encoder policy and preset tests (T030, T031): deterministic encoder
// selection (FR-062) and spec preset table (spec.md §7.3, FR-051).
#include "TestFramework.h"
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
    VSR_CHECK_EQ(low.bitrateBps, 3'000'000u);

    const auto standard = GetPresetParams(QualityPreset::Standard);
    VSR_CHECK_EQ(standard.width, 1920u);
    VSR_CHECK_EQ(standard.height, 1080u);
    VSR_CHECK_EQ(standard.fps, 30u);
    VSR_CHECK_EQ(standard.bitrateBps, 6'000'000u);

    const auto high = GetPresetParams(QualityPreset::High);
    VSR_CHECK_EQ(high.width, 1920u);
    VSR_CHECK_EQ(high.height, 1080u);
    VSR_CHECK_EQ(high.fps, 60u);
    VSR_CHECK_EQ(high.bitrateBps, 12'000'000u);
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