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

Result<EncoderCapability> SelectEncoder(const std::vector<EncoderCapability>& candidates) {
    const EncoderCapability* best = nullptr;
    for (const auto& c : candidates) {
        if (!c.acceptsNv12) continue;
        if (best == nullptr ||
            std::make_pair(c.hardware, c.d3d11Aware) >
                std::make_pair(best->hardware, best->d3d11Aware)) {
            best = &c;
        }
    }
    if (best == nullptr) {
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
    return cap;
}

} // namespace

Result<EncoderProbeResult> ProbeEncoders() {
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

    auto selected = SelectEncoder(result.candidates);
    if (!selected) return selected.TakeError();
    result.selected = selected.Value();
    result.available = true;
    log::Info("Encoder", "ENCODER_SELECTED", L"", L"Encoder transform selected.",
              {{"name", WideToUtf8(result.selected.name)},
               {"hardware", result.selected.hardware ? "1" : "0"},
               {"d3d11Aware", result.selected.d3d11Aware ? "1" : "0"},
               {"acceptsNv12", result.selected.acceptsNv12 ? "1" : "0"},
               {"discovered", std::to_string(result.discovered.size())}});
    return result;
}

} // namespace vsr::encoder
