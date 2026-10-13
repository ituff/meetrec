// Virtual Screen Recorder - DXGI/WinRT and capture-item interop (T022).
//
// Bridges the D3D11 device into the Windows.Graphics.Capture world and
// declares the IGraphicsCaptureItemInterop activation interface. The
// declaration is kept local because the SDK does not ship a stable C++/WinRT
// header for it; the GUID is defined by the Windows.Graphics.Capture contract.
#pragma once

#include <d3d11.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>

#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>

namespace vsr::capture {

// Creates the WinRT IDirect3DDevice wrapper for a D3D11 device.
Result<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>
CreateWinrtDevice(ID3D11Device* device);

// Extracts the D3D11 texture backing a captured IDirect3DSurface. The returned
// pointer is valid only while the surface object is alive.
Microsoft::WRL::ComPtr<ID3D11Texture2D>
GetTextureFromSurface(winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const&
                          surface);

// Creates a GraphicsCaptureItem for a monitor handle. Returns
// CaptureInitFailed when the monitor has disappeared or the API is unavailable.
Result<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>
CreateCaptureItemForMonitor(HMONITOR monitor);

// Creates a GraphicsCaptureItem for a top-level window. The item tracks the
// window's content even while it is occluded or parked off-screen; a minimized
// window stops presenting frames. Returns CaptureInitFailed when the window is
// gone or the API is unavailable.
Result<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>
CreateCaptureItemForWindow(HWND window);

// True when the OS supports monitor/window capture via Windows.Graphics.Capture
// (Windows 10 1903+). Used for the startup capability check (FR-004).
bool IsGraphicsCaptureSupported();

} // namespace vsr::capture
