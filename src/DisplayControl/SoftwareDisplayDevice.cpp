#include "SoftwareDisplayDevice.h"
#include "VirtualDisplayIdentity.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <cfgmgr32.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <swdevice.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace vsr::display {

namespace {

bool WaitForMonitor(std::chrono::milliseconds timeout, MonitorDescriptor* out) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto found = TopologyService::FindVirtualMonitor();
        if (found) {
            if (out != nullptr) *out = found.Value();
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        Sleep(250);
    }
}

} // namespace

class SoftwareDisplayDevice::Impl {
public:
    HSWDEVICE handle = nullptr;

    static VOID CALLBACK CreationCallback(HSWDEVICE hSwDevice, HRESULT hrCreateResult,
                                          PVOID context, PCWSTR deviceInstanceId) {
        UNREFERENCED_PARAMETER(hSwDevice);
        UNREFERENCED_PARAMETER(hrCreateResult);
        UNREFERENCED_PARAMETER(deviceInstanceId);
        auto* ev = static_cast<HANDLE*>(context);
        if (ev != nullptr && *ev != nullptr) SetEvent(*ev);
    }
};

SoftwareDisplayDevice::SoftwareDisplayDevice() = default;

SoftwareDisplayDevice::~SoftwareDisplayDevice() {
    ReleaseDisplay();
}

bool SoftwareDisplayDevice::IsAdapterDevicePresent() {
    const HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr,
                                                  DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    bool present = false;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA data = {};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(devInfo, i, &data)) break;
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(data.DevInst, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS &&
            MatchesVirtualInstancePath(id)) {
            present = true;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return present;
}

bool SoftwareDisplayDevice::IsMonitorActive() {
    return TopologyService::FindVirtualMonitor().IsOk();
}

Result<MonitorDescriptor> SoftwareDisplayDevice::EnsureDisplayAvailable(
    std::chrono::milliseconds timeout) {
    // Fast path: the monitor is already attached (device created previously
    // and still present, or attached by the installer workflow).
    if (IsMonitorActive()) {
        MonitorDescriptor found{};
        if (WaitForMonitor(std::chrono::milliseconds(2000), &found)) {
            return found;
        }
    }

    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    if (impl_->handle == nullptr) {
        HANDLE createdEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        SW_DEVICE_CREATE_INFO createInfo = {};
        createInfo.cbSize = sizeof(createInfo);
        createInfo.pszInstanceId = VirtualDisplayIdentity::kInstanceId;
        // Multi-sz (double-null terminated) hardware/compatible ID lists.
        const wchar_t hardwareIds[] = L"VirtualScreenRecorder\0";
        const wchar_t compatibleIds[] = L"VirtualScreenRecorder\0";
        createInfo.pszzHardwareIds = hardwareIds;
        createInfo.pszzCompatibleIds = compatibleIds;
        createInfo.pszDeviceDescription = VirtualDisplayIdentity::kDeviceDescription;
        createInfo.CapabilityFlags = SWDeviceCapabilitiesRemovable |
                                     SWDeviceCapabilitiesSilentInstall |
                                     SWDeviceCapabilitiesDriverRequired;

        const HRESULT hr = SwDeviceCreate(
            VirtualDisplayIdentity::kEnumeratorName, L"HTREE\\ROOT\\0", &createInfo, 0, nullptr,
            &Impl::CreationCallback, &createdEvent, &impl_->handle);
        CloseHandle(createdEvent);
        if (FAILED(hr)) {
            impl_->handle = nullptr;
            log::Error("DisplayControl", "DISPLAY_SWDEVICE_CREATE_FAILED", L"",
                       L"SwDeviceCreate failed. Is the virtual display driver installed?",
                       {{"hresult", WideToUtf8(FormatHresult(hr))}});
            return Error(ErrorCode::InitDriverMissing,
                         L"The virtual display driver is not installed. Run the installer or "
                         L"scripts\\install-driver.ps1 (test-signed builds) and try again.",
                         L"SwDeviceCreate failed: " + FormatHresult(hr), hr, true);
        }
        log::Info("DisplayControl", "DISPLAY_SWDEVICE_CREATED", L"",
                  L"Virtual display adapter device created.");
    }

    MonitorDescriptor found{};
    if (!WaitForMonitor(timeout, &found)) {
        if (!IsAdapterDevicePresent()) {
            return Error(ErrorCode::InitDriverMissing,
                         L"The virtual display driver is not installed. Run the installer or "
                         L"scripts\\install-driver.ps1 (test-signed builds) and try again.",
                         L"Timed out waiting for the virtual monitor; adapter device absent",
                         S_OK, true);
        }
        return Error(ErrorCode::DisplayNotFound,
                     L"The virtual display did not come up. Check Display Settings for a "
                     L"disabled monitor named 'Virtual Recording Display' and enable it.",
                     L"Timed out waiting for the virtual monitor to appear in the topology",
                     S_OK, true);
    }
    log::Info("DisplayControl", "DISPLAY_MONITOR_ARRIVED", L"",
              L"Virtual recording display is active.");
    return found;
}

void SoftwareDisplayDevice::ReleaseDisplay() {
    if (impl_ != nullptr && impl_->handle != nullptr) {
        SwDeviceClose(impl_->handle);
        impl_->handle = nullptr;
        log::Info("DisplayControl", "DISPLAY_SWDEVICE_RELEASED", L"",
                  L"Virtual display adapter device removed.");
    }
}

Result<void> SoftwareDisplayDevice::SetAdapterEnabled(bool enabled) {
    const HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr,
                                                  DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return ErrorFromWin32(GetLastError(), ErrorCode::DisplayNotFound,
                              L"Could not access the display device list.",
                              L"SetupDiGetClassDevsW failed");
    }

    Result<void> result{};
    bool found = false;
    for (DWORD i = 0;; ++i) {
        SP_DEVINFO_DATA data = {};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(devInfo, i, &data)) break;

        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(data.DevInst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS ||
            !MatchesVirtualInstancePath(id)) {
            continue;
        }
        found = true;

        SP_PROPCHANGE_PARAMS params = {};
        params.ClassInstallHeader.cbSize = sizeof(params.ClassInstallHeader);
        params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        params.StateChange = enabled ? DICS_ENABLE : DICS_DISABLE;
        params.Scope = DICS_FLAG_CONFIGSPECIFIC;
        params.HwProfile = 0;
        if (!SetupDiSetClassInstallParamsW(devInfo, &data, &params.ClassInstallHeader,
                                           sizeof(params))) {
            result = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                    L"Could not change the virtual display power state.",
                                    L"SetupDiSetClassInstallParamsW failed");
            break;
        }
        if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devInfo, &data)) {
            result = ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                                    L"Windows rejected the virtual display power-state change.",
                                    L"SetupDiCallClassInstaller(DIF_PROPERTYCHANGE) failed");
        }
        break;
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    if (!found && result.IsOk()) {
        result = Error(ErrorCode::DisplayNotFound,
                       L"The virtual display adapter device is not present.",
                       L"No present device matched the virtual display instance path", S_OK,
                       true);
    }
    return result;
}

} // namespace vsr::display
