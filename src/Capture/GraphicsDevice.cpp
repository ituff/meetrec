#include "GraphicsDevice.h"

#include <dxgi1_4.h>

namespace vsr::capture {

Result<GraphicsDevice> GraphicsDevice::CreateInternal(IDXGIAdapter1* adapter) {
    GraphicsDevice result;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(adapter, adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN
                                                               : D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                   result.device_.GetAddressOf(), &result.featureLevel_,
                                   result.context_.GetAddressOf());
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed,
                                L"Direct3D 11 could not start on this machine. Update the GPU "
                                L"driver and try again.",
                                L"D3D11CreateDevice failed", false);
    }
    hr = result.device_.As(&result.dxgiDevice_);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed,
                                L"Direct3D 11 could not start on this machine.",
                                L"ID3D11Device::As(IDXGIDevice) failed");
    }
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapterBase;
        hr = result.dxgiDevice_->GetAdapter(&adapterBase);
        if (FAILED(hr)) {
            return ErrorFromHresult(hr, ErrorCode::InitD3dFailed,
                                    L"Direct3D 11 could not start on this machine.",
                                    L"IDXGIDevice::GetAdapter failed");
        }
        hr = adapterBase.As(&result.adapter_);
        if (FAILED(hr)) {
            return ErrorFromHresult(hr, ErrorCode::InitD3dFailed,
                                    L"Direct3D 11 could not start on this machine.",
                                    L"IDXGIAdapter::As(IDXGIAdapter1) failed");
        }
    }
    DXGI_ADAPTER_DESC1 desc = {};
    if (SUCCEEDED(result.adapter_->GetDesc1(&desc))) {
        result.adapterDescription_.assign(desc.Description);
        result.adapterLuid_ = desc.AdapterLuid;
    }
    return result;
}

Result<GraphicsDevice> GraphicsDevice::Create() {
    return CreateInternal(nullptr);
}

Result<GraphicsDevice> GraphicsDevice::CreateForAdapter(LUID adapterLuid) {
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed,
                                L"Direct3D 11 could not start on this machine.",
                                L"CreateDXGIFactory1 failed");
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(&adapter));
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed,
                                L"The GPU that drives the virtual display is unavailable.",
                                L"EnumAdapterByLuid failed", true);
    }
    return CreateInternal(adapter.Get());
}

HRESULT GraphicsDevice::CheckDeviceRemoved() const noexcept {
    if (device_ == nullptr) return E_POINTER;
    return device_->GetDeviceRemovedReason();
}

} // namespace vsr::capture
