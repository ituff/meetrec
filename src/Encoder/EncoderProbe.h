// Virtual Screen Recorder - H.264 encoder capability probe (FR-050, FR-051,
// FR-062, FR-063; T030; ADR-004).
//
// Selection policy (deterministic): prefer hardware MFTs that are D3D11-aware
// and accept NV12, then hardware MFTs, then the Microsoft software encoder.
// When nothing compliant exists, recording fails before any output file is
// created with VSR-ENCODER-UNAVAILABLE.
#pragma once

#include <string>
#include <vector>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::encoder {

// User-selectable encoder class. Auto keeps the deterministic hardware-first
// policy; Hardware/Software restrict the candidate pool so users can escape a
// misbehaving hardware MFT (e.g. known vendor stall defects) or deliberately
// trade speed for compatibility.
enum class EncoderPreference { Auto, Hardware, Software };

// "auto" / "hardware" / "software"; anything else falls back to Auto.
EncoderPreference EncoderPreferenceFromString(const std::string& s) noexcept;
const char* ToString(EncoderPreference preference) noexcept;

struct EncoderCapability {
    CLSID clsid{};
    std::wstring name;
    bool hardware = false;
    bool d3d11Aware = false;
    bool acceptsNv12 = true;
    bool producesH264 = true;   // output subtype is H.264 (video encoders only)
};

struct EncoderProbeResult {
    bool available = false;
    EncoderCapability selected;
    std::vector<EncoderCapability> candidates;
    // Names of all discovered encoder transforms (diagnostics only).
    std::vector<std::wstring> discovered;
};

// Pure selection policy, unit-testable without Media Foundation. With
// Hardware/Software preference the candidate pool is filtered first; an empty
// matching pool yields VSR-ENCODER-UNAVAILABLE. `excludeName` skips one
// candidate (encoder-fallback retry after a failed writer activation).
Result<EncoderCapability> SelectEncoder(
    const std::vector<EncoderCapability>& candidates,
    EncoderPreference preference = EncoderPreference::Auto,
    const std::wstring& excludeName = std::wstring());

// Enumerates the system's H.264 encoders via MFTEnumEx and applies the
// selection policy. See SelectEncoder for `excludeName`.
Result<EncoderProbeResult> ProbeEncoders(
    EncoderPreference preference = EncoderPreference::Auto,
    const std::wstring& excludeName = std::wstring());

} // namespace vsr::encoder
