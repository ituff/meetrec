#include "VideoConverter.h"

#include <Infrastructure/Strings.h>

#include <vector>

namespace vsr::encoder {

Result<std::unique_ptr<VideoConverter>> VideoConverter::Create(
    const capture::GraphicsDevice& device, std::uint32_t dstWidth, std::uint32_t dstHeight) {
    if (device.Device() == nullptr || dstWidth == 0 || dstHeight == 0) {
        return Error(ErrorCode::EncoderUnavailable, L"Internal encoder configuration error.",
                     L"VideoConverter::Create invalid arguments");
    }
    auto converter = std::unique_ptr<VideoConverter>(new VideoConverter());
    converter->device_ = &device;
    converter->dstWidth_ = dstWidth;
    converter->dstHeight_ = dstHeight;

    HRESULT hr = device.Device()->QueryInterface(IID_PPV_ARGS(&converter->videoDevice_));
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"This GPU driver does not expose the video processing needed "
                                L"for recording.",
                                L"VideoConverter: QueryInterface(ID3D11VideoDevice) failed: " +
                                    FormatHresult(hr));
    }
    hr = device.Context()->QueryInterface(IID_PPV_ARGS(&converter->videoContext_));
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"This GPU driver does not expose the video processing needed "
                                L"for recording.",
                                L"VideoConverter: QueryInterface(ID3D11VideoContext) failed: " +
                                    FormatHresult(hr));
    }
    // Some drivers (e.g. AMD) reject a null content descriptor with
    // E_INVALIDARG — describe the conversion using the preset geometry; the
    // per-stream source rect adapts to actual capture sizes at run time.
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = dstWidth;
    contentDesc.InputHeight = dstHeight;
    contentDesc.OutputWidth = dstWidth;
    contentDesc.OutputHeight = dstHeight;
    hr = converter->videoDevice_->CreateVideoProcessorEnumerator(&contentDesc,
                                                                 &converter->enumerator_);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"This GPU driver does not support the color conversion needed "
                                L"for recording (BGRA to NV12).",
                                L"VideoConverter: CreateVideoProcessorEnumerator failed: " +
                                    FormatHresult(hr));
    }
    hr = converter->videoDevice_->CreateVideoProcessor(converter->enumerator_.Get(), 0,
                                                       &converter->processor_);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderUnavailable,
                                L"This GPU driver does not support the color conversion needed "
                                L"for recording (BGRA to NV12).",
                                L"VideoConverter: CreateVideoProcessor failed: " +
                                    FormatHresult(hr));
    }

    // Capture is full-range sRGB BGRA; output full-range BT.709 NV12.
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE colorSpace = {};
    colorSpace.Usage = 0;
    colorSpace.RGB_Range = 0;      // 0-255
    colorSpace.YCbCr_Matrix = 1;   // BT.709 (matches HD H.264 convention)
    colorSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    converter->videoContext_->VideoProcessorSetStreamColorSpace(converter->processor_.Get(), 0,
                                                                &colorSpace);
    converter->videoContext_->VideoProcessorSetOutputColorSpace(converter->processor_.Get(),
                                                                &colorSpace);
    // Disable driver auto processing (denoise etc.) for predictable cost.
    converter->videoContext_->VideoProcessorSetStreamAutoProcessingMode(
        converter->processor_.Get(), 0, FALSE);
    converter->videoContext_->VideoProcessorSetStreamFrameFormat(
        converter->processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

    const RECT dstRect{0, 0, static_cast<LONG>(dstWidth), static_cast<LONG>(dstHeight)};
    converter->videoContext_->VideoProcessorSetStreamDestRect(converter->processor_.Get(), 0, TRUE,
                                                              &dstRect);

    // Preallocate the output ring.
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = dstWidth;
    desc.Height = dstHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // MFCreateDXGISurfaceBuffer requires shareable D3D11 textures.
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    for (std::size_t i = 0; i < 3; ++i) {
        OutputSlot slot;
        HRESULT thr =
            device.Device()->CreateTexture2D(&desc, nullptr, slot.texture.GetAddressOf());
        if (FAILED(thr)) {
            return ErrorFromHresult(thr, ErrorCode::EncoderUnavailable,
                                    L"Video memory for the recording pipeline could not be "
                                    L"allocated.",
                                    L"CreateTexture2D(NV12) failed");
        }
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC viewDesc = {};
        viewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        thr = converter->videoDevice_->CreateVideoProcessorOutputView(
            slot.texture.Get(), converter->enumerator_.Get(), &viewDesc, slot.view.GetAddressOf());
        if (FAILED(thr)) {
            return ErrorFromHresult(thr, ErrorCode::EncoderUnavailable,
                                    L"This GPU driver could not create the recording conversion "
                                    L"pipeline.",
                                    L"CreateVideoProcessorOutputView failed");
        }
        converter->outputs_.push_back(std::move(slot));
    }
    return converter;
}

Result<ID3D11Texture2D*> VideoConverter::Convert(ID3D11Texture2D* bgraSource) {
    if (bgraSource == nullptr) {
        return Error(ErrorCode::EncoderWriteFailed, L"Internal capture error.",
                     L"Convert called with null source");
    }
    D3D11_TEXTURE2D_DESC srcDesc = {};
    bgraSource->GetDesc(&srcDesc);

    // Input views are per-resource; the capture pool rotates textures, so a
    // fresh view is created for every frame (cheap driver-side object).
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inDesc = {};
    inDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inDesc.Texture2D.MipSlice = 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hr = videoDevice_->CreateVideoProcessorInputView(bgraSource, enumerator_.Get(),
                                                             &inDesc, inputView.GetAddressOf());
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::CaptureDeviceLost,
                                L"The GPU rejected a captured frame. Recording stops safely.",
                                L"CreateVideoProcessorInputView failed", true);
    }

    if (srcDesc.Width != lastSrcWidth_ || srcDesc.Height != lastSrcHeight_) {
        const RECT srcRect{0, 0, static_cast<LONG>(srcDesc.Width),
                           static_cast<LONG>(srcDesc.Height)};
        videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &srcRect);
        lastSrcWidth_ = srcDesc.Width;
        lastSrcHeight_ = srcDesc.Height;
    }

    const std::size_t slotIndex = nextOutput_;
    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    hr = videoContext_->VideoProcessorBlt(processor_.Get(), outputs_[slotIndex].view.Get(), 0, 1,
                                          &stream);    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                L"A frame could not be converted for encoding.",
                                L"VideoProcessorBlt failed", true);
    }
    nextOutput_ = (nextOutput_ + 1) % outputs_.size();
    return outputs_[slotIndex].texture.Get();
}

} // namespace vsr::encoder
