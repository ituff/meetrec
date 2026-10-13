#include "CaptureInterop.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.UI.h>

// IGraphicsCaptureItemInterop — declared here because the Windows SDK does not
// ship a public header for this activation-factory interface. The interface
// identifier is part of the public Windows.Graphics.Capture contract.
struct __declspec(uuid("79C3F95B-31F7-4EC2-A464-632EF5D30760"))
    IGraphicsCaptureItemInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND window, REFIID riid,
                                                      void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID riid,
                                                       void** result) = 0;
};

namespace vsr::capture {

Result<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>
CreateWinrtDevice(ID3D11Device* device) {
    if (device == nullptr) {
        return Error(ErrorCode::InitD3dFailed, L"Internal graphics error.",
                     L"CreateWinrtDevice called with null device");
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed, L"Internal graphics error.",
                                L"QueryInterface(IDXGIDevice) failed");
    }
    Microsoft::WRL::ComPtr<IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), &inspectable);
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed, L"Internal graphics error.",
                                L"CreateDirect3D11DeviceFromDXGIDevice failed");
    }
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice = nullptr;
    hr = inspectable->QueryInterface(
        winrt::guid_of<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>(),
        winrt::put_abi(winrtDevice));
    if (FAILED(hr)) {
        return ErrorFromHresult(hr, ErrorCode::InitD3dFailed, L"Internal graphics error.",
                                L"QueryInterface(IDirect3DDevice) failed");
    }
    return winrtDevice;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D>
GetTextureFromSurface(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const&
                          surface) {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (surface == nullptr) return texture;
    Microsoft::WRL::ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>
        access;
    if (SUCCEEDED(winrt::get_unknown(surface)->QueryInterface(IID_PPV_ARGS(&access))) &&
        access != nullptr) {
        access->GetInterface(IID_PPV_ARGS(&texture));
    }
    return texture;
}

Result<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>
CreateCaptureItemForMonitor(HMONITOR monitor) {
    if (monitor == nullptr) {
        return Error(ErrorCode::CaptureInitFailed, L"The virtual display handle is invalid.",
                     L"CreateCaptureItemForMonitor called with null HMONITOR", E_POINTER, true);
    }
    try {
        const auto interopFactory = winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            ::IGraphicsCaptureItemInterop>();
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = nullptr;
        const HRESULT hr = interopFactory->CreateForMonitor(
            monitor,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(item)));
        if (FAILED(hr) || item == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::CaptureInitFailed,
                                    L"Could not attach to the virtual display for capture. "
                                    L"Verify the display is still active, then try again.",
                                    L"IGraphicsCaptureItemInterop::CreateForMonitor failed", true);
        }
        return item;
    } catch (const winrt::hresult_error& e) {
        return ErrorFromHresult(static_cast<HRESULT>(e.code().value), ErrorCode::CaptureInitFailed,
                                L"Could not attach to the virtual display for capture.",
                                L"WinRT error in CreateCaptureItemForMonitor: " +
                                    std::wstring(e.message().c_str()),
                                true);
    } catch (...) {
        return Error(ErrorCode::CaptureInitFailed,
                     L"Could not attach to the virtual display for capture.",
                     L"Unknown error in CreateCaptureItemForMonitor", E_FAIL, true);
    }
}

Result<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>
CreateCaptureItemForWindow(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return Error(ErrorCode::CaptureInitFailed,
                     L"The selected window is no longer available for capture.",
                     L"CreateCaptureItemForWindow called with an invalid hwnd", E_POINTER, true);
    }
    // Preferred: the projected TryCreateFromWindowId API (Windows 11 24H2+).
    // On some newer builds the legacy COM interop below answers E_NOINTERFACE
    // even though window capture itself works.
    const bool hasModernApi = winrt::Windows::Foundation::Metadata::ApiInformation::
        IsMethodPresent(L"Windows.Graphics.Capture.GraphicsCaptureItem",
                        L"TryCreateFromWindowId");
    if (hasModernApi) {
        try {
            winrt::Windows::UI::WindowId id{};
            id.Value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
            auto item = winrt::Windows::Graphics::Capture::GraphicsCaptureItem::
                TryCreateFromWindowId(id);
            if (item) return item;
            return Error(ErrorCode::CaptureInitFailed,
                         L"Could not attach to the selected window for capture. Verify the "
                         L"window is still open, then try again.",
                         L"TryCreateFromWindowId returned null", S_OK, true);
        } catch (const winrt::hresult_error& e) {
            return ErrorFromHresult(static_cast<HRESULT>(e.code().value),
                                    ErrorCode::CaptureInitFailed,
                                    L"Could not attach to the selected window for capture.",
                                    L"WinRT error in TryCreateFromWindowId: " +
                                        std::wstring(e.message().c_str()),
                                    true);
        } catch (...) {
            return Error(ErrorCode::CaptureInitFailed,
                         L"Could not attach to the selected window for capture.",
                         L"Unknown error in TryCreateFromWindowId", E_FAIL, true);
        }
    }
    // Legacy: the IGraphicsCaptureItemInterop activation interface.
    try {
        const auto interopFactory = winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            ::IGraphicsCaptureItemInterop>();
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = nullptr;
        const HRESULT hr = interopFactory->CreateForWindow(
            window,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(winrt::put_abi(item)));
        if (FAILED(hr) || item == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::CaptureInitFailed,
                                    L"Could not attach to the selected window for capture. "
                                    L"Verify the window is still open, then try again.",
                                    L"IGraphicsCaptureItemInterop::CreateForWindow failed", true);
        }
        return item;
    } catch (const winrt::hresult_error& e) {
        return ErrorFromHresult(static_cast<HRESULT>(e.code().value), ErrorCode::CaptureInitFailed,
                                L"Could not attach to the selected window for capture.",
                                L"WinRT error in CreateCaptureItemForWindow: " +
                                    std::wstring(e.message().c_str()),
                                true);
    } catch (...) {
        return Error(ErrorCode::CaptureInitFailed,
                     L"Could not attach to the selected window for capture.",
                     L"Unknown error in CreateCaptureItemForWindow", E_FAIL, true);
    }
}

bool IsGraphicsCaptureSupported() {
    try {
        return winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession");
    } catch (...) {
        // NFR-011: exceptions must not escape module boundaries.
        return false;
    }
}

} // namespace vsr::capture
