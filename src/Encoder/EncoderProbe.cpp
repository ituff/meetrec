#include "EncoderProbe.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfobjects.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace vsr::encoder {

EncoderPreference EncoderPreferenceFromString(const std::string& s) noexcept {
    if (s == "hardware") return EncoderPreference::Hardware;
    if (s == "software") return EncoderPreference::Software;
    return EncoderPreference::Auto;
}

const char* ToString(EncoderPreference preference) noexcept {
    switch (preference) {
        case EncoderPreference::Hardware: return "hardware";
        case EncoderPreference::Software: return "software";
        case EncoderPreference::Auto: return "auto";
    }
    return "auto";
}

Result<EncoderCapability> SelectEncoder(const std::vector<EncoderCapability>& candidates,
                                        EncoderPreference preference) {
    const EncoderCapability* best = nullptr;
    for (const auto& c : candidates) {
        if (!c.acceptsNv12 || !c.producesH264) continue;
        if (preference == EncoderPreference::Hardware && !c.hardware) continue;
        if (preference == EncoderPreference::Software && c.hardware) continue;
        if (best == nullptr ||
            std::make_pair(c.hardware, c.d3d11Aware) >
                std::make_pair(best->hardware, best->d3d11Aware)) {
            best = &c;
        }
    }
    if (best == nullptr) {
        if (preference == EncoderPreference::Hardware) {
            return Error(ErrorCode::EncoderUnavailable,
                         L"No hardware H.264 encoder is available, but the encoder preference "
                         L"is set to Hardware. Switch the encoder preference to Auto or "
                         L"Software, then try again.",
                         L"Hardware encoder preference matched no candidate MFT");
        }
        if (preference == EncoderPreference::Software) {
            return Error(ErrorCode::EncoderUnavailable,
                         L"No software H.264 encoder is available, but the encoder preference "
                         L"is set to Software. Switch the encoder preference to Auto or "
                         L"Hardware, then try again.",
                         L"Software encoder preference matched no candidate MFT");
        }
        return Error(ErrorCode::EncoderUnavailable,
                     L"No compatible H.264 encoder is available on this machine. Install a GPU "
                     L"driver with hardware video encoding, or enable the Microsoft software "
                     L"H.264 encoder, then try again.",
                     L"No H.264 encoder MFT accepted NV12 input");
    }
    return *best;
}

namespace {

// Reads capability flags from an MFT activation object.
EncoderCapability InspectCandidate(IMFActivate* activate) {
    EncoderCapability cap;
    cap.name = L"<unknown>";

    LPWSTR name = nullptr;
    UINT32 nameLen = 0;
    if (SUCCEEDED(activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &nameLen)) &&
        name != nullptr) {
        cap.name.assign(name);
        CoTaskMemFree(name);
    }

    // Hardware transforms expose an hardware URL attribute.
    LPWSTR hwUrl = nullptr;
    UINT32 hwLen = 0;
    if (SUCCEEDED(activate->GetAllocatedString(MFT_ENUM_HARDWARE_URL_Attribute, &hwUrl, &hwLen))) {
        cap.hardware = true;
        CoTaskMemFree(hwUrl);
    }

    UINT32 d3dAware = 0;
    if (SUCCEEDED(activate->GetUINT32(MF_SA_D3D11_AWARE, &d3dAware))) {
        cap.d3d11Aware = d3dAware != 0;
    }

    // Check the advertised input types for NV12 acceptance. When a transform
    // advertises no static input types, assume NV12 and fail later at
    // SetInputType with a clear error.
    Microsoft::WRL::ComPtr<IMFTransform> transform;
    if (SUCCEEDED(activate->ActivateObject(IID_PPV_ARGS(&transform)))) {
        bool sawAny = false;
        bool sawNv12 = false;
        for (DWORD index = 0; index < 64; ++index) {
            IMFMediaType* type = nullptr;
            const HRESULT hr = transform->GetInputAvailableType(0, index, &type);
            if (hr == MF_E_NO_MORE_TYPES) break;
            if (FAILED(hr)) break;
            sawAny = true;
            GUID subtype{};
            if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                subtype == MFVideoFormat_NV12) {
                sawNv12 = true;
            }
            type->Release();
        }
        cap.acceptsNv12 = sawNv12 || !sawAny;
    }

    // Only transforms that produce an H.264 elementary stream qualify as
    // video encoders for this pipeline; other registered "encoder" MFTs
    // (e.g. image/HEIF extensions) would otherwise be selected.
    if (transform) {
        bool sawOutAny = false;
        bool sawH264 = false;
        for (DWORD index = 0; index < 64; ++index) {
            IMFMediaType* type = nullptr;
            const HRESULT hr = transform->GetOutputAvailableType(0, index, &type);
            if (hr == MF_E_NO_MORE_TYPES) break;
            if (FAILED(hr)) break;
            sawOutAny = true;
            GUID subtype{};
            if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                subtype == MFVideoFormat_H264) {
                sawH264 = true;
            }
            type->Release();
        }
        if (sawOutAny) {
            cap.producesH264 = sawH264;
        } else if (!cap.hardware) {
            // No static output types and not a known hardware MFT: verify
            // H.264 by live negotiation, otherwise Store-hosted non-video
            // "encoders" (HEIF image extensions and friends) pass.
            IMFMediaType* outType = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&outType))) {
                outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                cap.producesH264 =
                    SUCCEEDED(transform->SetOutputType(0, outType, 0));
                outType->Release();
            }
        }
    }
    return cap;
}

} // namespace

Result<EncoderProbeResult> ProbeEncoders(EncoderPreference preference) {
    EncoderProbeResult result;

    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    // Enumerate every encoder (hardware + software, sync and async), sorted.
    const UINT32 flags = MFT_ENUM_FLAG_ALL | MFT_ENUM_FLAG_SORTANDFILTER;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, nullptr, &activates,
                           &count);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"Windows could not enumerate video encoders.",
                                L"MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER) failed", true);
    }

    for (UINT32 i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IMFActivate> activate;
        activate.Attach(activates[i]);
        EncoderCapability cap = InspectCandidate(activate.Get());
        result.discovered.push_back(cap.name);
        result.candidates.push_back(std::move(cap));
    }
    CoTaskMemFree(activates);

    auto selected = SelectEncoder(result.candidates, preference);
    if (!selected) return selected.TakeError();
    result.selected = selected.Value();
    result.available = true;
    log::Info("Encoder", "ENCODER_SELECTED", L"", L"Encoder transform selected.",
              {{"name", WideToUtf8(result.selected.name)},
               {"hardware", result.selected.hardware ? "1" : "0"},
               {"d3d11Aware", result.selected.d3d11Aware ? "1" : "0"},
               {"acceptsNv12", result.selected.acceptsNv12 ? "1" : "0"},
               {"preference", ToString(preference)},
               {"discovered", std::to_string(result.discovered.size())}});
    return result;
}

} // namespace vsr::encoder
