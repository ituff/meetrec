/*++

Virtual Screen Recorder - indirect display driver.

Derived from the Microsoft Windows-driver-samples "IndirectDisplay"
(IddSampleDriver) sample, Copyright (c) Microsoft Corporation, MIT License.
VSR modifications (all reviewed in docs/sample-audit-idd.md):
  - Exactly ONE adapter and ONE monitor (FR-010); multi-monitor sample data
    and edid-less monitor paths removed.
  - Mode list reduced to the product modes: 1280x720@30, 1920x1080@30
    (preferred), 1920x1080@60 (FR-011, FR-012).
  - Product EDID with stable monitor identity (FR-013) and a fixed container
    ID so the monitor identity survives reboot and device cycles.
  - WPP trace events on every lifecycle callback (NFR-034).

Environment:

    User Mode, UMDF 2

--*/

#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>

#include "Edid.h"
#include "Trace.h"

namespace Microsoft
{
    namespace WRL
    {
        namespace Wrappers
        {
            // Adds a wrapper for thread handles to the existing set of WRL handle wrapper classes
            typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
        }
    }
}

namespace vsr
{
    namespace driver
    {
        // VSR: monitor identity for the single virtual monitor.
        struct VirtualMonitorDefinition
        {
            static constexpr size_t szModeList = 3;

            struct MonitorMode
            {
                DWORD Width;
                DWORD Height;
                DWORD VSync;
            };

            // Order matches the EDID-preferred ordering; index 0 is preferred.
            static constexpr MonitorMode pModeList[szModeList] = {
                { 1920, 1080, 30 }, // preferred (FR-012)
                { 1920, 1080, 60 },
                { 1280,  720, 30 },
            };
        };

        /// <summary>
        /// Manages the creation and lifetime of a Direct3D render device.
        /// (Imported unchanged in structure from the Microsoft sample; see audit.)
        /// </summary>
        struct Direct3DDevice
        {
            Direct3DDevice(LUID AdapterLuid);
            Direct3DDevice();
            HRESULT Init();

            LUID AdapterLuid;
            Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
            Microsoft::WRL::ComPtr<ID3D11Device> Device;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
        };

        /// <summary>
        /// Manages a thread that consumes buffers from an indirect display swap-chain object.
        /// (Imported from the Microsoft sample; frame processing intentionally leaves the
        /// surface to IddCx composition — the driver does not encode or copy pixels.)
        /// </summary>
        class SwapChainProcessor
        {
        public:
            SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent);
            ~SwapChainProcessor();

        private:
            static DWORD CALLBACK RunThread(LPVOID Argument);

            void Run();
            void RunCore();

            IDDCX_SWAPCHAIN m_hSwapChain;
            std::shared_ptr<Direct3DDevice> m_Device;
            HANDLE m_hAvailableBufferEvent;
            Microsoft::WRL::Wrappers::Thread m_hThread;
            Microsoft::WRL::Wrappers::Event m_hTerminateEvent;
        };

        /// <summary>
        /// Provides the Virtual Screen Recorder indirect display device context.
        /// </summary>
        class IndirectDeviceContext
        {
        public:
            IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
            virtual ~IndirectDeviceContext();

            void InitAdapter();
            void FinishInit(UINT ConnectorIndex);

        protected:
            WDFDEVICE m_WdfDevice;
            IDDCX_ADAPTER m_Adapter;
        };

        class IndirectMonitorContext
        {
        public:
            IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor);
            virtual ~IndirectMonitorContext();

            void AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent);
            void UnassignSwapChain();

        private:
            IDDCX_MONITOR m_Monitor;
            std::unique_ptr<SwapChainProcessor> m_ProcessingThread;
        };
    }
}
