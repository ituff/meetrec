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
                                        EncoderPreference preference,
                                        const std::wstring& excludeName) {
    const EncoderCapability* best = nullptr;
    for (const auto& c : candidates) {
        if (!excludeName.empty() && c.name == excludeName) continue;
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

// Stream test: configures NV12 -> H264 on an (async) transform, feeds ONE
// black frame, drains, and requires the transform to emit at least one output
// sample. Type negotiation is not enough codec identity on this OS build:
// async hardware MFTs enumerate every codec's format list before an input
// type is set AND accept foreign output subtypes at configuration time
// (observed: "Intel Hardware VP9 Encoder MFT" accepting H264 negotiation and
// then silently encoding nothing). Bounded: 3 s without output fails.
bool LiveEncodesNv12ToH264(IMFTransform* transform) {
    Microsoft::WRL::ComPtr<IMFAttributes> attrs;
    if (SUCCEEDED(transform->GetAttributes(&attrs))) {
        UINT32 isAsync = 0;
        if (SUCCEEDED(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &isAsync)) && isAsync) {
            attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        }
    }
    Microsoft::WRL::ComPtr<IMFMediaType> outType;
    Microsoft::WRL::ComPtr<IMFMediaType> inType;
    if (FAILED(MFCreateMediaType(&outType)) || FAILED(MFCreateMediaType(&inType))) return false;
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    outType->SetUINT32(MF_MT_AVG_BITRATE, 2'000'000);
    MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, 320, 240);
    MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, 320, 240);
    MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, 30, 1);
    MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(transform->SetOutputType(0, outType.Get(), 0))) return false;
    if (FAILED(transform->SetInputType(0, inType.Get(), 0))) return false;

    Microsoft::WRL::ComPtr<IMFMediaEventGenerator> events;
    if (FAILED(transform->QueryInterface(IID_PPV_ARGS(&events)))) return false;
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    bool fed = false;
    bool draining = false;
    UINT32 produced = 0;
    const ULONGLONG deadline = GetTickCount64() + 3'000;
    for (;;) {
        if (GetTickCount64() >= deadline) break;
        Microsoft::WRL::ComPtr<IMFMediaEvent> ev;
        if (events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &ev) != S_OK) {
            Sleep(10);
            continue;
        }
        MediaEventType type = MEUnknown;
        ev->GetType(&type);
        if (type == METransformNeedInput && !fed) {
            const DWORD luma = 320 * 240;
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buf;
            if (FAILED(MFCreateMemoryBuffer(luma + luma / 2, &buf))) break;
            BYTE* dst = nullptr;
            if (SUCCEEDED(buf->Lock(&dst, nullptr, nullptr))) {
                memset(dst, 16, luma);
                memset(dst + luma, 128, luma / 2);
                buf->Unlock();
                buf->SetCurrentLength(luma + luma / 2);
            }
            Microsoft::WRL::ComPtr<IMFSample> sample;
            if (FAILED(MFCreateSample(&sample))) break;
            sample->AddBuffer(buf.Get());
            sample->SetSampleTime(0);
            sample->SetSampleDuration(10'000'000 / 30);
            if (FAILED(transform->ProcessInput(0, sample.Get(), 0))) break;
            fed = true;
        } else if (type == METransformNeedInput && fed && !draining) {
            transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            draining = true;
        } else if (type == METransformHaveOutput) {
            MFT_OUTPUT_DATA_BUFFER out = {};
            out.dwStreamID = 0;
            DWORD status = 0;
            if (SUCCEEDED(transform->ProcessOutput(0, 1, &out, &status))) ++produced;
            if (out.pEvents != nullptr) out.pEvents->Release();
        } else if (type == METransformDrainComplete) {
            break;
        }
    }
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    return produced >= 1;
}

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
        if (sawH264) {
            // Ambiguous static match: prove codec identity by a live stream
            // test (see LiveEncodesNv12ToH264).
            cap.producesH264 = LiveEncodesNv12ToH264(transform.Get());
        } else if (!sawOutAny && !cap.hardware) {
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

Result<EncoderProbeResult> ProbeEncoders(EncoderPreference preference,
                                         const std::wstring& excludeName) {
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

    auto selected = SelectEncoder(result.candidates, preference, excludeName);
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
