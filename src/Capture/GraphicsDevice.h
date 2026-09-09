// Virtual Screen Recorder - D3D11 graphics device service (FR-041, FR-042,
// NFR-010, NFR-012; T020).
//
// RAII ownership of the capture/encode D3D device. One device instance is
// shared by the capture session, the BGRA->NV12 converter, and the encoder so
// frames stay on the same GPU without cross-device copies.
#pragma once

#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <string>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::capture {

class GraphicsDevice {
public:
    // Creates a D3D11 device with BGRA support on the default adapter.
    static Result<GraphicsDevice> Create();

    // Creates a device on the adapter holding the given LUID (used when the
    // IddCx driver reports a specific render adapter).
    static Result<GraphicsDevice> CreateForAdapter(LUID adapterLuid);

    GraphicsDevice() = default;

    ID3D11Device* Device() const noexcept { return device_.Get(); }
    ID3D11DeviceContext* Context() const noexcept { return context_.Get(); }
    IDXGIDevice* DxgiDevice() const noexcept { return dxgiDevice_.Get(); }
    IDXGIAdapter1* Adapter() const noexcept { return adapter_.Get(); }

    // Adapter description without device-instance strings (diagnostics safe).
    const std::wstring& AdapterDescription() const noexcept { return adapterDescription_; }
    LUID AdapterLuid() const noexcept { return adapterLuid_; }
    D3D_FEATURE_LEVEL FeatureLevel() const noexcept { return featureLevel_; }

    // Returns DXGI_ERROR_DEVICE_REMOVED reason, or S_OK when healthy
    // (FR-012 device-loss path).
    HRESULT CheckDeviceRemoved() const noexcept;

    bool IsValid() const noexcept { return device_ != nullptr; }

private:
    static Result<GraphicsDevice> CreateInternal(IDXGIAdapter1* adapter);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    std::wstring adapterDescription_;
    LUID adapterLuid_{};
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
};

} // namespace vsr::capture
