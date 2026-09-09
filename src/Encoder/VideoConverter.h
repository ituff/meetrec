// Virtual Screen Recorder - GPU BGRA->NV12 conversion (design.md §4.7; T031).
//
// Uses the D3D11 Video Processor to convert and scale captured BGRA frames
// into NV12 textures sized for the encoder. CPU readback is prohibited in the
// normal path. A small ring of NV12 textures lets the encoder consume the
// previous frame while the next conversion runs.
#pragma once

#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>
#include <vector>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>
#include <Capture/GraphicsDevice.h>

namespace vsr::encoder {

class VideoConverter {
public:
    // Prepares a converter producing `dstWidth x dstHeight` NV12 textures.
    // The source size is a hint; source-size changes are handled per frame.
    static Result<std::unique_ptr<VideoConverter>> Create(const capture::GraphicsDevice& device,
                                                          std::uint32_t dstWidth,
                                                          std::uint32_t dstHeight);

    // Converts one BGRA frame into a pooled NV12 texture. The returned raw
    // texture stays valid until the next Convert call or object destruction
    // (the ring is sized so the encoder consumes promptly).
    Result<ID3D11Texture2D*> Convert(ID3D11Texture2D* bgraSource);

    std::uint32_t OutputWidth() const noexcept { return dstWidth_; }
    std::uint32_t OutputHeight() const noexcept { return dstHeight_; }

private:
    VideoConverter() = default;
    ID3D11Texture2D* NextOutput();

    const capture::GraphicsDevice* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor_;

    struct OutputSlot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> view;
    };
    std::vector<OutputSlot> outputs_;
    std::size_t nextOutput_ = 0;

    std::uint32_t dstWidth_ = 0;
    std::uint32_t dstHeight_ = 0;
    std::uint32_t lastSrcWidth_ = 0;
    std::uint32_t lastSrcHeight_ = 0;
};

} // namespace vsr::encoder
