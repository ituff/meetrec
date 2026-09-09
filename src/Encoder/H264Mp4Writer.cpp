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
    auto mfStarted = MFLifetime::EnsureStarted();
    if (!mfStarted) return mfStarted.TakeError();

    if (partialPath.empty()) {
        return Error(ErrorCode::EncoderUnavailable, L"Internal encoder configuration error.",
                     L"H264Mp4Writer::Create invalid arguments");
    }

    // VSR_FORCE_CPU_ENCODER=1 documents an escape hatch for exotic drivers.
    bool allowGpu = encoder.hardware && encoder.d3d11Aware;
    wchar_t forceCpu[4] = {};
    if (GetEnvironmentVariableW(L"VSR_FORCE_CPU_ENCODER", forceCpu, 4) > 0 && forceCpu[0] == L'1') {
        allowGpu = false;
    }

    Error lastError = Error(ErrorCode::EncoderUnavailable,
                            L"No compatible H.264 encoder path could be activated.",
                            L"All writer creation attempts failed");
    // Attempt 0: GPU samples (preferred). Attempt 1: CPU staging fallback.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool gpu = allowGpu && attempt == 0;
        const wchar_t* failStep = L"unknown";
        HRESULT failHr = E_FAIL;

        auto writer = AttemptCreate(device, encoder, preset, partialPath, gpu, &failStep,
                                    &failHr);
        if (writer.IsOk()) {
            if (!gpu) {
                log::Warning("Encoder", "ENCODER_CPU_FALLBACK", L"",
                             L"GPU sample submission rejected by the driver; using the "
                             L"documented CPU staging fallback.",
                             {{"path", WideToUtf8(partialPath.wstring())}});
            }
            return writer;
        }
        lastError = writer.TakeError();
        if (!gpu) break;
        log::Warning("Encoder", "ENCODER_GPU_PATH_REJECTED", L"",
                     L"GPU submission probe failed; retrying with CPU staging.",
                     {{"step", WideToUtf8(failStep)},
                      {"hresult", WideToUtf8(FormatHresult(failHr))}});
        DeleteFileW(partialPath.c_str());
    }
    return lastError;
}

Result<std::unique_ptr<H264Mp4Writer>>
H264Mp4Writer::AttemptCreate(const capture::GraphicsDevice& device, const EncoderCapability& encoder,
                             const PresetParams& preset, const std::filesystem::path& partialPath,
                             bool gpuSamples, const wchar_t** failStep, HRESULT* failHr) {
    auto writer = std::unique_ptr<H264Mp4Writer>(new H264Mp4Writer());
    writer->encoder_ = encoder;
    writer->preset_ = preset;
    writer->partialPath_ = partialPath;
    writer->useGpuSamples_ = gpuSamples;

    // Output media type (H.264 in MP4) and input media type (NV12).
    Microsoft::WRL::ComPtr<IMFMediaType> outType = MakeVideoType(MFVideoFormat_H264, preset, true);
    Microsoft::WRL::ComPtr<IMFMediaType> inType = MakeVideoType(MFVideoFormat_NV12, preset, false);

    Microsoft::WRL::ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 4);
    if (gpuSamples) {
        UINT resetToken = 0;
        HRESULT hr = MFCreateDXGIDeviceManager(&resetToken, &writer->deviceManager_);
        if (FAILED(hr)) {
            *failStep = L"MFCreateDXGIDeviceManager";
            *failHr = hr;
            return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                    L"Windows could not initialize GPU video encoding.",
                                    L"MFCreateDXGIDeviceManager failed");
        }
        hr = writer->deviceManager_->ResetDevice(device.Device(), resetToken);
        if (FAILED(hr)) {
            *failStep = L"IMFDXGIDeviceManager::ResetDevice";
            *failHr = hr;
            return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                    L"Windows could not attach the GPU to the encoder.",
                                    L"IMFDXGIDeviceManager::ResetDevice failed");
        }
        attrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, writer->deviceManager_.Get());
    }
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, encoder.hardware ? 1 : 0);

    // MF's URL/byte-stream resolution is strict about separator style on some
    // installations; hand it a fully native absolute path.
    std::error_code pathEc;
    std::filesystem::path nativePath = std::filesystem::absolute(partialPath, pathEc);
    nativePath.make_preferred();
    writer->nativePath_ = nativePath;

    HRESULT hr = MFCreateSinkWriterFromURL(nativePath.c_str(), nullptr, attrs.Get(),
                                           &writer->writer_);
    if (FAILED(hr)) {
        *failStep = L"MFCreateSinkWriterFromURL";
        *failHr = hr;
        return ErrorFromHresult(hr, ErrorCode::OutputNotWritable,
                                L"The recording file could not be created at the chosen "
                                L"location. Pick another folder and try again.",
                                L"MFCreateSinkWriterFromURL failed: " + FormatHresult(hr) +
                                    L" path=" + partialPath.wstring(),
                                true);
    }

    hr = writer->writer_->AddStream(outType.Get(), &writer->streamIndex_);
    if (FAILED(hr)) {
        *failStep = L"AddStream";
        *failHr = hr;
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"Windows could not configure the H.264 video stream.",
                                L"AddStream failed");
    }
    hr = writer->writer_->SetInputMediaType(writer->streamIndex_, inType.Get(), nullptr);
    if (FAILED(hr)) {
        *failStep = L"SetInputMediaType";
        *failHr = hr;
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"The selected encoder rejected the recording configuration.",
                                L"SetInputMediaType(NV12) failed");
    }
    hr = writer->writer_->BeginWriting();
    if (FAILED(hr)) {
        *failStep = L"BeginWriting";
        *failHr = hr;
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"Windows could not start the encoder.",
                                L"BeginWriting failed");
    }

    // Capability probe: submit one black frame through the selected path and
    // flush. Some driver/MFT combinations accept configuration but reject the
    // first GPU sample with E_INVALIDARG; catching that here keeps recording
    // starts bounded (NFR-005) and enables the CPU fallback (ADR-004).
    auto probeResult = writer->ProbeSubmission(device, preset);
    if (!probeResult) {
        const Error e = probeResult.TakeError();
        *failStep = L"ProbeSubmission";
        *failHr = e.hresult;
        return e;
    }

    log::Info("Encoder", "ENCODER_WRITER_CREATED", L"", L"Sink writer ready.",
              {{"path", WideToUtf8(partialPath.wstring())},
               {"hardware", encoder.hardware ? "1" : "0"},
               {"d3d11Aware", encoder.d3d11Aware ? "1" : "0"},
               {"samplePath", gpuSamples ? "gpu" : "cpu-staging"},
               {"width", std::to_string(preset.width)},
               {"height", std::to_string(preset.height)},
               {"fps", std::to_string(preset.fps)},
               {"bitrateBps", std::to_string(preset.bitrateBps)}});
    return writer;
}

Result<void> H264Mp4Writer::ProbeSubmission(const capture::GraphicsDevice& device,
                                            const PresetParams& preset) {
    // One black NV12 frame at t=0 through the selected submission path.
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = preset.width;
    desc.Height = preset.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    if (useGpuSamples_) {
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> black;
    HRESULT hr = device.Device()->CreateTexture2D(&desc, nullptr, black.GetAddressOf());
    if (FAILED(hr)) {
        return EncodeError("CreateTexture2D(probe NV12)", hr, true);
    }
    // NV12 "black" (Y=16, UV=128) is the legal black level; an all-zero
    // buffer is accepted equally by encoders, so no upload is required.

    const std::int64_t duration = FrameDuration100Ns(preset.fps);
    auto submitted = useGpuSamples_ ? SubmitFrameGpu(black.Get(), 0, duration)
                                    : SubmitFrameCpu(black.Get(), 0, duration);
    if (!submitted) return submitted;
    hr = writer_->Flush(streamIndex_);
    if (FAILED(hr)) {
        return EncodeError("IMFSinkWriter::Flush(probe)", hr, true);
    }
    // Reset the timestamp anchor: real frames start at t=0 on the next tick.
    lastTimestamp_ = -1;
    writtenFrames_.store(0);
    return Result<void>{};
}

H264Mp4Writer::~H264Mp4Writer() {
    CloseWriter();
}

void H264Mp4Writer::CloseWriter() noexcept {
    writer_.Reset();
    deviceManager_.Reset();
    staging_.Reset();
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

    D3D11_TEXTURE2D_DESC frameDesc = {};
    staging_->GetDesc(&frameDesc);
    const UINT lumaSize = frameDesc.Width * frameDesc.Height;
    const UINT chromaSize = lumaSize / 2;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(lumaSize + chromaSize, &buffer);
    if (FAILED(hr)) return EncodeError("MFCreateMemoryBuffer", hr, true);
    {
        BYTE* dst = nullptr;
        hr = buffer->Lock(&dst, nullptr, nullptr);
        if (FAILED(hr)) return EncodeError("IMFMediaBuffer::Lock", hr, true);
        const BYTE* srcY = nullptr;
        {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            hr = context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                buffer->Unlock();
                return EncodeError("ID3D11DeviceContext::Map", hr, true);
            }
            srcY = static_cast<const BYTE*>(mapped.pData);
            for (UINT row = 0; row < frameDesc.Height; ++row) {
                memcpy(dst + static_cast<size_t>(row) * frameDesc.Width,
                       srcY + static_cast<size_t>(row) * mapped.RowPitch, frameDesc.Width);
            }
            const BYTE* srcUv =
                static_cast<const BYTE*>(mapped.pData) + mapped.RowPitch * frameDesc.Height;
            BYTE* dstUv = dst + lumaSize;
            for (UINT row = 0; row < frameDesc.Height / 2; ++row) {
                memcpy(dstUv + static_cast<size_t>(row) * frameDesc.Width,
                       srcUv + static_cast<size_t>(row) * mapped.RowPitch, frameDesc.Width);
            }
            context->Unmap(staging_.Get(), 0);
        }
        buffer->Unlock();
        buffer->SetCurrentLength(lumaSize + chromaSize);
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

    auto result = useGpuSamples_ ? SubmitFrameGpu(nv12Frame, time100Ns, duration100Ns)
                                 : SubmitFrameCpu(nv12Frame, time100Ns, duration100Ns);
    if (result.IsOk()) lastTimestamp_ = time100Ns;
    return result;
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
