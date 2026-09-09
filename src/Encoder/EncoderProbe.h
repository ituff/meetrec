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

struct EncoderCapability {
    CLSID clsid{};
    std::wstring name;
    bool hardware = false;
    bool d3d11Aware = false;
    bool acceptsNv12 = true;
};

struct EncoderProbeResult {
    bool available = false;
    EncoderCapability selected;
    std::vector<EncoderCapability> candidates;
    // Names of all discovered encoder transforms (diagnostics only).
    std::vector<std::wstring> discovered;
};

// Pure selection policy, unit-testable without Media Foundation.
Result<EncoderCapability> SelectEncoder(const std::vector<EncoderCapability>& candidates);

// Enumerates the system's H.264 encoders via MFTEnumEx and applies the
// selection policy.
Result<EncoderProbeResult> ProbeEncoders();

} // namespace vsr::encoder
