/*++

Virtual Screen Recorder - indirect display driver implementation.

Derived from the Microsoft Windows-driver-samples "IndirectDisplay"
(IddSampleDriver) sample, Copyright (c) Microsoft Corporation, MIT License.
All VSR modifications are marked with "VSR:" comments and audited in
docs/sample-audit-idd.md.

Environment:

    User Mode, UMDF

--*/

#include "Driver.h"
#include "Driver.tmh"

using namespace std;
using namespace vsr::driver;
using namespace Microsoft::WRL;

#pragma region helpers

static inline void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& Mode, DWORD Width, DWORD Height, DWORD VSync, bool bMonitorMode)
{
    Mode.totalSize.cx = Mode.activeSize.cx = Width;
    Mode.totalSize.cy = Mode.activeSize.cy = Height;

    Mode.AdditionalSignalInfo.vSyncFreqDivider = bMonitorMode ? 0 : 1;
    Mode.AdditionalSignalInfo.videoStandard = 255;

    Mode.vSyncFreq.Numerator = VSync;
    Mode.vSyncFreq.Denominator = 1;
    Mode.hSyncFreq.Numerator = VSync * Height;
    Mode.hSyncFreq.Denominator = 1;

    Mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;

    Mode.pixelRate = ((UINT64) VSync) * ((UINT64) Width) * ((UINT64) Height);
}

static IDDCX_MONITOR_MODE CreateIddCxMonitorMode(DWORD Width, DWORD Height, DWORD VSync, IDDCX_MONITOR_MODE_ORIGIN Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER)
{
    IDDCX_MONITOR_MODE Mode = {};

    Mode.Size = sizeof(Mode);
    Mode.Origin = Origin;
    FillSignalInfo(Mode.MonitorVideoSignalInfo, Width, Height, VSync, true);

    return Mode;
}

static IDDCX_TARGET_MODE CreateIddCxTargetMode(DWORD Width, DWORD Height, DWORD VSync)
{
    IDDCX_TARGET_MODE Mode = {};

    Mode.Size = sizeof(Mode);
    FillSignalInfo(Mode.TargetVideoSignalInfo.targetVideoSignalInfo, Width, Height, VSync, false);

    return Mode;
}

#pragma endregion

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD VsrDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY VsrDeviceD0Entry;

EVT_IDD_CX_ADAPTER_INIT_FINISHED VsrAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES VsrAdapterCommitModes;

EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION VsrParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES VsrMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES VsrMonitorQueryModes;

EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN VsrMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN VsrMonitorUnassignSwapChain;

struct IndirectDeviceContextWrapper
{
    IndirectDeviceContext* pContext;

    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

struct IndirectMonitorContextWrapper
{
    IndirectMonitorContext* pContext;

    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

// This macro creates the methods for accessing a context object as a WDF object context
WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);

WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContextWrapper);

extern "C" BOOL WINAPI DllMain(
    _In_ HINSTANCE hInstance,
    _In_ UINT dwReason,
    _In_opt_ LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(lpReserved);
    UNREFERENCED_PARAMETER(dwReason);

    return TRUE;
}

_Use_decl_annotations_
extern "C" NTSTATUS DriverEntry(
    PDRIVER_OBJECT  pDriverObject,
    PUNICODE_STRING pRegistryPath
)
{
    WDF_DRIVER_CONFIG Config;
    NTSTATUS Status;

    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);

    // VSR: shut down WPP when the driver object is cleaned up.
    Attributes.EvtCleanupCallback = [](WDFOBJECT Object)
    {
        UNREFERENCED_PARAMETER(Object);
        WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)Object));
    };

    WDF_DRIVER_CONFIG_INIT(&Config,
        VsrDeviceAdd
    );

    Status = WdfDriverCreate(pDriverObject, pRegistryPath, &Attributes, &Config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // VSR: WPP tracing for lifecycle diagnostics (NFR-034).
    WPP_INIT_TRACING(pDriverObject, pRegistryPath);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "Virtual Screen Recorder display driver loaded.");

    return Status;
}

_Use_decl_annotations_
NTSTATUS VsrDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;

    UNREFERENCED_PARAMETER(Driver);

    // Register for power callbacks - in this driver only power-on is needed
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDeviceD0Entry = VsrDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &PnpPowerCallbacks);

    IDD_CX_CLIENT_CONFIG IddConfig;
    IDD_CX_CLIENT_CONFIG_INIT(&IddConfig);

    IddConfig.EvtIddCxAdapterInitFinished = VsrAdapterInitFinished;

    IddConfig.EvtIddCxParseMonitorDescription = VsrParseMonitorDescription;
    IddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = VsrMonitorGetDefaultModes;
    IddConfig.EvtIddCxMonitorQueryTargetModes = VsrMonitorQueryModes;
    IddConfig.EvtIddCxAdapterCommitModes = VsrAdapterCommitModes;
    IddConfig.EvtIddCxMonitorAssignSwapChain = VsrMonitorAssignSwapChain;
    IddConfig.EvtIddCxMonitorUnassignSwapChain = VsrMonitorUnassignSwapChain;

    Status = IddCxDeviceInitConfig(pDeviceInit, &IddConfig);
    if (!NT_SUCCESS(Status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "IddCxDeviceInitConfig failed: 0x%08x", Status);
        return Status;
    }

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);
    Attr.EvtCleanupCallback = [](WDFOBJECT Object)
    {
        // Automatically cleanup the context when the WDF object is about to be deleted
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Object);
        if (pContext)
        {
            pContext->Cleanup();
        }
    };

    WDFDEVICE Device = nullptr;
    Status = WdfDeviceCreate(&pDeviceInit, &Attr, &Device);
    if (!NT_SUCCESS(Status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfDeviceCreate failed: 0x%08x", Status);
        return Status;
    }

    Status = IddCxDeviceInitialize(Device);

    // Create a new device context object and attach it to the WDF device object
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext = new IndirectDeviceContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Device added (status 0x%08x).", Status);

    return Status;
}

_Use_decl_annotations_
NTSTATUS VsrDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);

    // This function is called by WDF to start the device in the fully-on power state.
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Device entering D0.");

    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext->InitAdapter();

    return STATUS_SUCCESS;
}

#pragma region Direct3DDevice

Direct3DDevice::Direct3DDevice(LUID AdapterLuid) : AdapterLuid(AdapterLuid)
{

}

Direct3DDevice::Direct3DDevice()
{
    AdapterLuid = LUID{};
}

HRESULT Direct3DDevice::Init()
{
    // The DXGI factory could be cached, but if a new render adapter appears on the system, a new factory needs to be
    // created. If caching is desired, check DxgiFactory->IsCurrent() each time and recreate the factory if !IsCurrent.
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
    if (FAILED(hr))
    {
        return hr;
    }

    // Find the specified render adapter
    hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
    if (FAILED(hr))
    {
        return hr;
    }

    // Create a D3D device using the render adapter. BGRA support is required by the WHQL test suite.
    hr = D3D11CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &DeviceContext);
    if (FAILED(hr))
    {
        // If creating the D3D device failed, it's possible the render GPU was lost (e.g. detachable GPU) or else the
        // system is in a transient state.
        return hr;
    }

    return S_OK;
}

#pragma endregion

#pragma region SwapChainProcessor

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent)
    : m_hSwapChain(hSwapChain), m_Device(Device), m_hAvailableBufferEvent(NewFrameEvent)
{
    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));

    // Immediately create and run the swap-chain processing thread, passing 'this' as the thread parameter
    m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
}

SwapChainProcessor::~SwapChainProcessor()
{
    // Alert the swap-chain processing thread to terminate
    SetEvent(m_hTerminateEvent.Get());

    if (m_hThread.Get())
    {
        // Wait for the thread to terminate
        WaitForSingleObject(m_hThread.Get(), INFINITE);
    }
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument)
{
    reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
    return 0;
}

void SwapChainProcessor::Run()
{
    // For improved performance, make use of the Multimedia Class Scheduler Service, which will intelligently
    // prioritize this thread for improved throughput in high CPU-load scenarios.
    DWORD AvTask = 0;
    HANDLE AvTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &AvTask);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "Swap-chain processing thread started.");

    RunCore();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "Swap-chain processing thread finished.");

    // Always delete the swap-chain object when swap-chain processing loop terminates in order to kick the system to
    // provide a new swap-chain if necessary.
    WdfObjectDelete((WDFOBJECT)m_hSwapChain);
    m_hSwapChain = nullptr;

    if (AvTaskHandle != nullptr)
    {
        AvRevertMmThreadCharacteristics(AvTaskHandle);
    }
}

void SwapChainProcessor::RunCore()
{
    // Get the DXGI device interface
    ComPtr<IDXGIDevice> DxgiDevice;
    HRESULT hr = m_Device->Device.As(&DxgiDevice);
    if (FAILED(hr))
    {
        return;
    }

    IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
    SetDevice.pDevice = DxgiDevice.Get();

    hr = IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice);
    if (FAILED(hr))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "IddCxSwapChainSetDevice failed: 0x%08x", hr);
        return;
    }

    // Acquire and release buffers in a loop. VSR: the driver intentionally
    // performs no pixel processing here — IddCx composes the surfaces directly
    // to the desktop; the application captures the monitor output instead.
    for (;;)
    {
        ComPtr<IDXGIResource> AcquiredBuffer;

        // Ask for the next buffer from the producer
        IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer = {};
        hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &Buffer);

        // AcquireBuffer immediately returns STATUS_PENDING if no buffer is yet available
        if (hr == E_PENDING)
        {
            // We must wait for a new buffer
            HANDLE WaitHandles [] =
            {
                m_hAvailableBufferEvent,
                m_hTerminateEvent.Get()
            };
            DWORD WaitResult = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE, 16);
            if (WaitResult == WAIT_OBJECT_0 || WaitResult == WAIT_TIMEOUT)
            {
                // We have a new buffer, so try the AcquireBuffer again
                continue;
            }
            else if (WaitResult == WAIT_OBJECT_0 + 1)
            {
                // We need to terminate
                break;
            }
            else
            {
                // The wait was cancelled or something unexpected happened
                hr = HRESULT_FROM_WIN32(WaitResult);
                break;
            }
        }
        else if (SUCCEEDED(hr))
        {
            // We have new frame to process, the surface has a reference on it that the driver has to release.
            // The driver does no processing; release the reference immediately so the OS can prepare the next frame.
            AcquiredBuffer.Attach(Buffer.MetaData.pSurface);
            AcquiredBuffer.Reset();

            // Indicate to OS that we have finished initial processing of the frame, it is a hint that
            // OS could start preparing another frame
            hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
            if (FAILED(hr))
            {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_QUEUE, "IddCxSwapChainFinishedProcessingFrame failed: 0x%08x", hr);
                break;
            }
        }
        else
        {
            // The swap-chain was likely abandoned (e.g. DXGI_ERROR_ACCESS_LOST), so exit the processing loop
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_QUEUE, "Swap-chain buffer acquisition ended: 0x%08x", hr);
            break;
        }
    }
}

#pragma endregion

#pragma region IndirectDeviceContext

IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE WdfDevice) :
    m_WdfDevice(WdfDevice)
{
    m_Adapter = {};
}

IndirectDeviceContext::~IndirectDeviceContext()
{
}

void IndirectDeviceContext::InitAdapter()
{
    // VSR: product endpoint identity (matches VirtualDisplayIdentity in the
    // application). Reported for diagnostics; also surfaced in Display Settings.
    IDDCX_ADAPTER_CAPS AdapterCaps = {};
    AdapterCaps.Size = sizeof(AdapterCaps);

    // VSR: exactly one monitor for the MVP (FR-010).
    AdapterCaps.MaxMonitorsSupported = 1;
    AdapterCaps.EndPointDiagnostics.Size = sizeof(AdapterCaps.EndPointDiagnostics);
    AdapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    AdapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;

    AdapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"VSR Virtual Display";
    AdapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"VirtualScreenRecorder Project";
    AdapterCaps.EndPointDiagnostics.pEndPointModelName = L"Virtual Recording Display Adapter";

    // Declare your hardware and firmware versions (required)
    IDDCX_ENDPOINT_VERSION Version = {};
    Version.Size = sizeof(Version);
    Version.MajorVer = 1;
    Version.MinorVer = 0;
    AdapterCaps.EndPointDiagnostics.pFirmwareVersion = &Version;
    AdapterCaps.EndPointDiagnostics.pHardwareVersion = &Version;

    // Initialize a WDF context that can store a pointer to the device context object
    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);

    IDARG_IN_ADAPTER_INIT AdapterInit = {};
    AdapterInit.WdfDevice = m_WdfDevice;
    AdapterInit.pCaps = &AdapterCaps;
    AdapterInit.ObjectAttributes = &Attr;

    // Start the initialization of the adapter, which will trigger the AdapterFinishInit callback later
    IDARG_OUT_ADAPTER_INIT AdapterInitOut;
    NTSTATUS Status = IddCxAdapterInitAsync(&AdapterInit, &AdapterInitOut);

    if (NT_SUCCESS(Status))
    {
        // Store a reference to the WDF adapter handle
        m_Adapter = AdapterInitOut.AdapterObject;

        // Store the device context object into the WDF object context
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(AdapterInitOut.AdapterObject);
        pContext->pContext = this;

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Adapter initialization started.");
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "IddCxAdapterInitAsync failed: 0x%08x", Status);
    }
}

void IndirectDeviceContext::FinishInit(UINT ConnectorIndex)
{
    // VSR: the product EDID (see Edid.h) is reported for the single monitor;
    // the identity is stable across reboots and device cycles (FR-013).
    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectMonitorContextWrapper);

    IDDCX_MONITOR_INFO MonitorInfo = {};
    MonitorInfo.Size = sizeof(MonitorInfo);
    MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    MonitorInfo.ConnectorIndex = ConnectorIndex;

    MonitorInfo.MonitorDescription.Size = sizeof(MonitorInfo.MonitorDescription);
    MonitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    MonitorInfo.MonitorDescription.DataSize = VirtualDisplayEdid::kSize;
    MonitorInfo.MonitorDescription.pData = const_cast<BYTE*>(VirtualDisplayEdid::kBytes);

    // VSR: stable container ID (see Edid.h) instead of the sample's
    // per-arrival random GUID.
    MonitorInfo.MonitorContainerId = VirtualDisplayEdid::kContainerId;

    IDARG_IN_MONITORCREATE MonitorCreate = {};
    MonitorCreate.ObjectAttributes = &Attr;
    MonitorCreate.pMonitorInfo = &MonitorInfo;

    // Create a monitor object with the specified monitor descriptor
    IDARG_OUT_MONITORCREATE MonitorCreateOut;
    NTSTATUS Status = IddCxMonitorCreate(m_Adapter, &MonitorCreate, &MonitorCreateOut);
    if (NT_SUCCESS(Status))
    {
        // Create a new monitor context object and attach it to the Idd monitor object
        auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorCreateOut.MonitorObject);
        pMonitorContextWrapper->pContext = new IndirectMonitorContext(MonitorCreateOut.MonitorObject);

        // Tell the OS that the monitor has been plugged in
        IDARG_OUT_MONITORARRIVAL ArrivalOut;
        Status = IddCxMonitorArrival(MonitorCreateOut.MonitorObject, &ArrivalOut);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Monitor arrival reported (status 0x%08x).", Status);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "IddCxMonitorCreate failed: 0x%08x", Status);
    }
}

IndirectMonitorContext::IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor) :
    m_Monitor(Monitor)
{
}

IndirectMonitorContext::~IndirectMonitorContext()
{
    m_ProcessingThread.reset();
}

void IndirectMonitorContext::AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent)
{
    m_ProcessingThread.reset();

    auto Device = make_shared<Direct3DDevice>(RenderAdapter);
    if (FAILED(Device->Init()))
    {
        // It's important to delete the swap-chain if D3D initialization fails, so that the OS knows to generate a new
        // swap-chain and try again.
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Render D3D device creation failed; deleting swap-chain.");
        WdfObjectDelete(SwapChain);
    }
    else
    {
        // Create a new swap-chain processing thread
        m_ProcessingThread.reset(new SwapChainProcessor(SwapChain, Device, NewFrameEvent));
    }
}

void IndirectMonitorContext::UnassignSwapChain()
{
    // Stop processing the last swap-chain
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "Swap-chain unassigned.");
    m_ProcessingThread.reset();
}

#pragma endregion

#pragma region DDI Callbacks

_Use_decl_annotations_
NTSTATUS VsrAdapterInitFinished(IDDCX_ADAPTER AdapterObject, const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
    // This is called when the OS has finished setting up the adapter for use by the IddCx driver. It's now possible
    // to report attached monitors.

    auto* pDeviceContextWrapper = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
    if (NT_SUCCESS(pInArgs->AdapterInitStatus))
    {
        // VSR: one connector, one monitor (FR-010).
        pDeviceContextWrapper->pContext->FinishInit(0);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Adapter init failed: 0x%08x", pInArgs->AdapterInitStatus);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VsrAdapterCommitModes(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES* pInArgs)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(pInArgs);

    // VSR: no endpoint reconfiguration is needed when modes are committed —
    // the swap-chain is managed by IddCx, and the display is virtual.
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Adapter modes committed (%u paths).", pInArgs->PathCount);

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VsrParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    // VSR: the EDID is static and owned by this driver, so the monitor modes
    // are the known product modes (FR-011, FR-012).
    pOutArgs->MonitorModeBufferOutputCount = VirtualMonitorDefinition::szModeList;

    if (pInArgs->MonitorModeBufferInputCount < VirtualMonitorDefinition::szModeList)
    {
        // Return success if there was no buffer, since the caller was only asking for a count of modes
        return (pInArgs->MonitorModeBufferInputCount > 0) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
    }
    else
    {
        // The driver reports exactly one EDID; verify the description matches.
        if (pInArgs->MonitorDescription.DataSize != VirtualDisplayEdid::kSize)
            return STATUS_INVALID_PARAMETER;

        if (memcmp(pInArgs->MonitorDescription.pData, VirtualDisplayEdid::kBytes, VirtualDisplayEdid::kSize) != 0)
            return STATUS_INVALID_PARAMETER;

        for (DWORD ModeIndex = 0; ModeIndex < VirtualMonitorDefinition::szModeList; ModeIndex++)
        {
            pInArgs->pMonitorModes[ModeIndex] = CreateIddCxMonitorMode(
                VirtualMonitorDefinition::pModeList[ModeIndex].Width,
                VirtualMonitorDefinition::pModeList[ModeIndex].Height,
                VirtualMonitorDefinition::pModeList[ModeIndex].VSync,
                IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR
            );
        }

        // 1080p30 is the preferred mode (FR-012), consistent with EDID DTD1.
        pOutArgs->PreferredMonitorModeIdx = 0;

        return STATUS_SUCCESS;
    }
}

_Use_decl_annotations_
NTSTATUS VsrMonitorGetDefaultModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);

    // VSR: the product EDID is always present, so this path is only a
    // fallback that reports the same product modes.
    if (pInArgs->DefaultMonitorModeBufferInputCount == 0)
    {
        pOutArgs->DefaultMonitorModeBufferOutputCount = VirtualMonitorDefinition::szModeList;
    }
    else
    {
        for (DWORD ModeIndex = 0; ModeIndex < VirtualMonitorDefinition::szModeList; ModeIndex++)
        {
            pInArgs->pDefaultMonitorModes[ModeIndex] = CreateIddCxMonitorMode(
                VirtualMonitorDefinition::pModeList[ModeIndex].Width,
                VirtualMonitorDefinition::pModeList[ModeIndex].Height,
                VirtualMonitorDefinition::pModeList[ModeIndex].VSync,
                IDDCX_MONITOR_MODE_ORIGIN_DRIVER
            );
        }

        pOutArgs->DefaultMonitorModeBufferOutputCount = VirtualMonitorDefinition::szModeList;
        pOutArgs->PreferredMonitorModeIdx = 0;
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VsrMonitorQueryModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);

    // VSR: the endpoint can process exactly the product modes; the OS offers
    // the intersection of monitor and target modes to the user.
    vector<IDDCX_TARGET_MODE> TargetModes;

    TargetModes.push_back(CreateIddCxTargetMode(1920, 1080, 30));
    TargetModes.push_back(CreateIddCxTargetMode(1920, 1080, 60));
    TargetModes.push_back(CreateIddCxTargetMode(1280,  720, 30));

    pOutArgs->TargetModeBufferOutputCount = (UINT) TargetModes.size();

    if (pInArgs->TargetModeBufferInputCount >= TargetModes.size())
    {
        copy(TargetModes.begin(), TargetModes.end(), pInArgs->pTargetModes);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VsrMonitorAssignSwapChain(IDDCX_MONITOR MonitorObject, const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
    auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    pMonitorContextWrapper->pContext->AssignSwapChain(pInArgs->hSwapChain, pInArgs->RenderAdapterLuid, pInArgs->hNextSurfaceAvailable);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS VsrMonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject)
{
    auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    pMonitorContextWrapper->pContext->UnassignSwapChain();
    return STATUS_SUCCESS;
}

#pragma endregion
