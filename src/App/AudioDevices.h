// MeetRec - audio endpoint discovery shared by the main window and settings.
#pragma once

#include <windows.h>
#include <mmdeviceapi.h>

#include <string>
#include <vector>

namespace vsr::app::audio {

struct EndpointItem {
    std::wstring id;   // empty only for the synthetic "Windows default" entry
    std::wstring name; // endpoint friendly name
};

// COM may not be initialized on the UI thread; MMDeviceEnumerator needs it.
class ComScope {
public:
    ComScope();
    ~ComScope();
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool uninit_ = false;
};

std::vector<EndpointItem> EnumerateEndpoints(EDataFlow flow);

// Name of the Windows default endpoint for the role the recorder uses
// (console render, communications capture with a console fallback).
std::wstring DefaultEndpointName(EDataFlow flow);

} // namespace vsr::app::audio
