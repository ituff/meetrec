#include "AudioDevices.h"

#include <propsys.h>

#include <vector>

#pragma comment(lib, "ole32.lib")

namespace vsr::app::audio {

namespace {

// PKEY_Device_FriendlyName, spelled out so no extra SDK import lib is needed.
constexpr PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::wstring DeviceFriendlyName(IMMDevice* device) {
    if (device == nullptr) return L"";
    IPropertyStore* store = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store)) || store == nullptr) return L"";
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(store->GetValue(kPkeyDeviceFriendlyName, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    store->Release();
    return name;
}

} // namespace

ComScope::ComScope() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    uninit_ = hr == S_OK || hr == S_FALSE;
}

ComScope::~ComScope() {
    if (uninit_) CoUninitialize();
}

std::vector<EndpointItem> EnumerateEndpoints(EDataFlow flow) {
    std::vector<EndpointItem> items;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) ||
        enumerator == nullptr) {
        return items;
    }
    IMMDeviceCollection* collection = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)) &&
        collection != nullptr) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(i, &device)) || device == nullptr) continue;
            LPWSTR id = nullptr;
            if (SUCCEEDED(device->GetId(&id)) && id != nullptr) {
                EndpointItem item;
                item.id = id;
                item.name = DeviceFriendlyName(device);
                items.push_back(std::move(item));
                CoTaskMemFree(id);
            }
            device->Release();
        }
        collection->Release();
    }
    enumerator->Release();
    return items;
}

std::wstring DefaultEndpointName(EDataFlow flow) {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))) ||
        enumerator == nullptr) {
        return L"";
    }
    IMMDevice* device = nullptr;
    if (flow == eRender) {
        enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    } else {
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device))) {
            enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        }
    }
    std::wstring name = DeviceFriendlyName(device);
    if (device != nullptr) device->Release();
    enumerator->Release();
    return name;
}

} // namespace vsr::app::audio
