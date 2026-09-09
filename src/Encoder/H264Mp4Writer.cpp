#include "H264Mp4Writer.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <codecapi.h>
#include <cstring>
#include <mferror.h>
#include <mfobjects.h>
#include <windows.graphics.directx.direct3d11.interop.h>

namespace vsr::encoder {

namespace {

Error EncodeError(const char* what, HRESULT hr, bool recoverable = false) {
    return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                            L"Recording could not write video and was stopped safely. Free disk "
                            L"space and check the GPU driver, then try again.",
                            std::wstring(what, what + std::char_traits<char>::length(what)) +
                                L" failed: " + FormatHresult(hr),
                            recoverable);
}

Error FinalizeError(const char* what, HRESULT hr) {
    return ErrorFromHresult(hr, ErrorCode::FinalizeFailed,
                            L"The recording file could not be finalized. The partial file was "
                            L"kept for diagnostics.",
                            std::wstring(what, what + std::char_traits<char>::length(what)) +
                                L" failed: " + FormatHresult(hr),
                            true);
}

Microsoft::WRL::ComPtr<IMFMediaType> MakeVideoType(GUID subtype, const PresetParams& p,
                                                   bool output) {
    Microsoft::WRL::ComPtr<IMFMediaType> type;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, p.width, p.height);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, p.fps, 1);
    MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    type->SetUINT32(MF_MT_YUV_MATRIX, 2 /* MFVideoTransferMatrix_BT.709 */);
    if (output) {
        type->SetUINT32(MF_MT_AVG_BITRATE, p.bitrateBps);
        type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    }
    return type;
}

} // namespace

Result<void> MFLifetime::EnsureStarted() {
    // Reference-counted per process; MFStartup returns S_OK/S_FALSE (already
    // started) and both are success.
    const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"Windows Media Foundation could not start.",
                                L"MFStartup failed");
    }
    return Result<void>{};
}

void MFLifetime::NotifyShutdown() {
    MFShutdown();
}

Result<std::unique_ptr<H264Mp4Writer>> H264Mp4Writer::Create(const capture::GraphicsDevice& device,
                                                             const EncoderCapability& encoder,
                                                             const PresetParams& preset,
                                                             const std::filesystem::path&
                                                                 partialPath) {
    auto ensureMf = MFLifetime::EnsureStarted();
    if (!ensureMf) return ensureMf.TakeError();

    if (encoder.clsid == GUID{} || partialPath.empty()) {
        return Error(ErrorCode::EncoderUnavailable, L"Internal encoder configuration error.",
                     L"H264Mp4Writer::Create invalid arguments");
    }

    auto writer = std::unique_ptr<H264Mp4Writer>(new H264Mp4Writer());
    writer->encoder_ = encoder;
    writer->preset_ = preset;
    writer->partialPath_ = partialPath;

    // Output media type (H.264 in MP4).
    Microsoft::WRL::ComPtr<IMFMediaType> outType = MakeVideoType(MFVideoFormat_H264, preset, true);

    // Input media type: NV12 (converter output), same dimensions and rate.
    Microsoft::WRL::ComPtr<IMFMediaType> inType = MakeVideoType(MFVideoFormat_NV12, preset, false);

    Microsoft::WRL::ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 4);
    if (encoder.hardware && encoder.d3d11Aware) {
        // D3D11-aware hardware path: hand the device manager to the pipeline.
        UINT resetToken = 0;
        HRESULT hr = MFCreateDXGIDeviceManager(&resetToken, &writer->deviceManager_);
        if (FAILED(hr)) {
            return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                    L"Windows could not initialize GPU video encoding.",
                                    L"MFCreateDXGIDeviceManager failed");
        }
        hr = writer->deviceManager_->ResetDevice(device.Device(), resetToken);
        if (FAILED(hr)) {
            return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                    L"Windows could not attach the GPU to the encoder.",
                                    L"IMFDXGIDeviceManager::ResetDevice failed");
        }
        attrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, writer->deviceManager_.Get());
    }
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, encoder.hardware ? 1 : 0);
    attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);

    HRESULT hr = MFCreateSinkWriterFromURL(partialPath.c_str(), nullptr, attrs.Get(),
                                           &writer->writer_);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::OutputNotWritable,
                                L"The recording file could not be created at the chosen "
                                L"location. Pick another folder and try again.",
                                L"MFCreateSinkWriterFromURL failed: " + FormatHresult(hr) +
                                    L" path=" + partialPath.wstring(),
                                true);
    }

    hr = writer->writer_->AddStream(outType.Get(), &writer->streamIndex_);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"Windows could not configure the H.264 video stream.",
                                L"AddStream failed");
    }
    hr = writer->writer_->SetInputMediaType(writer->streamIndex_, inType.Get(), nullptr);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"The selected encoder rejected the recording configuration.",
                                L"SetInputMediaType(NV12) failed");
    }
    hr = writer->writer_->BeginWriting();
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"Windows could not start the encoder.",
                                L"BeginWriting failed");
    }

    log::Info("Encoder", "ENCODER_WRITER_CREATED", L"", L"Sink writer ready.",
              {{"path", WideToUtf8(partialPath.wstring())},
               {"hardware", encoder.hardware ? "1" : "0"},
               {"d3d11Aware", encoder.d3d11Aware ? "1" : "0"},
               {"width", std::to_string(preset.width)},
               {"height", std::to_string(preset.height)},
               {"fps", std::to_string(preset.fps)},
               {"bitrateBps", std::to_string(preset.bitrateBps)}});
    return writer;
}

H264Mp4Writer::~H264Mp4Writer() {
    CloseWriter();
}

void H264Mp4Writer::CloseWriter() noexcept {
    writer_.Reset();
    deviceManager_.Reset();
    staging_.Reset();
    cpuBuffer_.Reset();
}

Result<void> H264Mp4Writer::EnsureStaging(ID3D11Texture2D* nv12Frame) {
    if (staging_ != nullptr) return Result<void>{};
    D3D11_TEXTURE2D_DESC desc = {};
    nv12Frame->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    // Staging texture is created through the same D3D device the frame lives on.
    Microsoft::WRL::ComPtr<ID3D11Device> frameDevice;
    nv12Frame->GetDevice(&frameDevice);
    HRESULT hr = frameDevice->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf());
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                L"Recording ran out of GPU resources and stopped safely.",
                                L"CreateTexture2D(staging NV12) failed", true);
    }
    return Result<void>{};
}

Result<void> H264Mp4Writer::SubmitFrameCpu(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                           std::int64_t duration100Ns) {
    auto stagingResult = EnsureStaging(nv12Frame);
    if (!stagingResult) return stagingResult.TakeError();

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    {
        Microsoft::WRL::ComPtr<ID3D11Device> frameDevice;
        nv12Frame->GetDevice(&frameDevice);
        frameDevice->GetImmediateContext(&context);
    }
    context->CopyResource(staging_.Get(), nv12Frame);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return EncodeError("ID3D11DeviceContext::Map", hr, true);
    }

    D3D11_TEXTURE2D_DESC frameDesc = {};
    staging_->GetDesc(&frameDesc);
    const UINT lumaSize = frameDesc.Width * frameDesc.Height;
    const UINT chromaSize = lumaSize / 2;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(lumaSize + chromaSize, &buffer);
    if (SUCCEEDED(hr)) {
        BYTE* dst = nullptr;
        if (SUCCEEDED(buffer->Lock(&dst, nullptr, nullptr))) {
            const BYTE* srcY = static_cast<const BYTE*>(mapped.pData);
            for (UINT row = 0; row < frameDesc.Height; ++row) {
                memcpy(dst + row * frameDesc.Width, srcY + row * mapped.RowPitch, frameDesc.Width);
            }
            const BYTE* srcUv =
                static_cast<const BYTE*>(mapped.pData) + mapped.RowPitch * frameDesc.Height;
            BYTE* dstUv = dst + lumaSize;
            for (UINT row = 0; row < frameDesc.Height / 2; ++row) {
                memcpy(dstUv + row * frameDesc.Width, srcUv + row * mapped.RowPitch,
                       frameDesc.Width);
            }
            buffer->Unlock();
            buffer->SetCurrentLength(lumaSize + chromaSize);
        } else {
            hr = E_FAIL;
        }
    }
    context->Unmap(staging_.Get(), 0);
    if (FAILED(hr)) {
        return EncodeError("MFCreateMemoryBuffer/Lock", hr, true);
    }

    Microsoft::WRL::ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return EncodeError("MFCreateSample", hr, true);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(time100Ns);
    sample->SetSampleDuration(duration100Ns);
    hr = writer_->WriteSample(streamIndex_, sample.Get());
    if (FAILED(hr)) return EncodeError("WriteSample", hr, true);
    writtenFrames_.fetch_add(1, std::memory_order_relaxed);
    return Result<void>{};
}

Result<void> H264Mp4Writer::SubmitFrameGpu(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                           std::int64_t duration100Ns) {
    Microsoft::WRL::ComPtr<IMFSample> sample;
    HRESULT hr = MFCreateSample(&sample);
    if (FAILED(hr)) return EncodeError("MFCreateSample", hr, true);

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, nv12Frame, 0, FALSE, &buffer);
    if (FAILED(hr)) return EncodeError("MFCreateDXGISurfaceBuffer", hr, true);

    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(time100Ns);
    sample->SetSampleDuration(duration100Ns);
    hr = writer_->WriteSample(streamIndex_, sample.Get());
    if (FAILED(hr)) return EncodeError("WriteSample", hr, true);
    writtenFrames_.fetch_add(1, std::memory_order_relaxed);
    return Result<void>{};
}

Result<void> H264Mp4Writer::SubmitFrame(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                                        std::int64_t duration100Ns) {
    if (writer_ == nullptr) {
        return Error(ErrorCode::InternalState, L"Internal recording error.",
                     L"SubmitFrame after finalize");
    }
    if (nv12Frame == nullptr) {
        return Error(ErrorCode::InternalState, L"Internal recording error.",
                     L"SubmitFrame null frame");
    }
    // FR-043: never emit non-monotonic timestamps; clamp to last + duration.
    if (time100Ns <= lastTimestamp_) {
        time100Ns = lastTimestamp_ + duration100Ns;
    }

    if (encoder_.hardware && encoder_.d3d11Aware) {
        auto r = SubmitFrameGpu(nv12Frame, time100Ns, duration100Ns);
        if (r.IsOk()) lastTimestamp_ = time100Ns;
        return r;
    }
    auto r = SubmitFrameCpu(nv12Frame, time100Ns, duration100Ns);
    if (r.IsOk()) lastTimestamp_ = time100Ns;
    return r;
}

Result<void> H264Mp4Writer::Finalize() {
    bool expected = false;
    if (!finalized_.compare_exchange_strong(expected, true)) {
        // Idempotent (FR-073 / NFR-014).
        return Result<void>{};
    }
    if (writer_ == nullptr) {
        return Result<void>{};
    }
    HRESULT hr = writer_->Finalize();
    if (FAILED(hr)) {
        CloseWriter();
        return FinalizeError("IMFSinkWriter::Finalize", hr);
    }
    CloseWriter();
    log::Info("Encoder", "ENCODER_FINALIZED", L"", L"Output finalized.",
              {{"writtenFrames", std::to_string(writtenFrames_.load())},
               {"path", WideToUtf8(partialPath_.wstring())}});
    return Result<void>{};
}

} // namespace vsr::encoder
