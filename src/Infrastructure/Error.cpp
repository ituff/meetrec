#include "Error.h"

namespace vsr {

const char* ToString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None:                 return "VSR-OK";
        case ErrorCode::InitOsUnsupported:    return "VSR-INIT-OS-UNSUPPORTED";
        case ErrorCode::InitDriverMissing:    return "VSR-INIT-DRIVER-MISSING";
        case ErrorCode::InitD3dFailed:        return "VSR-INIT-D3D-FAILED";
        case ErrorCode::DisplayNotFound:      return "VSR-DISPLAY-NOT-FOUND";
        case ErrorCode::DisplayDisconnected:  return "VSR-DISPLAY-DISCONNECTED";
        case ErrorCode::TargetInvalid:        return "VSR-TARGET-INVALID";
        case ErrorCode::TargetLost:           return "VSR-TARGET-LOST";
        case ErrorCode::CaptureInitFailed:    return "VSR-CAPTURE-INIT-FAILED";
        case ErrorCode::CaptureDeviceLost:    return "VSR-CAPTURE-DEVICE-LOST";
        case ErrorCode::EncoderUnavailable:   return "VSR-ENCODER-UNAVAILABLE";
        case ErrorCode::EncoderWriteFailed:   return "VSR-ENCODER-WRITE-FAILED";
        case ErrorCode::OutputNotWritable:    return "VSR-OUTPUT-NOT-WRITABLE";
        case ErrorCode::OutputDiskLow:        return "VSR-OUTPUT-DISK-LOW";
        case ErrorCode::FinalizeFailed:       return "VSR-FINALIZE-FAILED";
        case ErrorCode::InternalState:        return "VSR-INTERNAL-STATE";
    }
    return "VSR-INTERNAL-STATE";
}

Error ErrorFromHresult(HRESULT hr, ErrorCode code, std::wstring user, std::wstring tech,
                       bool recoverable) {
    return Error(code, std::move(user), std::move(tech), hr, recoverable);
}

Error ErrorFromWin32(DWORD win32Error, ErrorCode code, std::wstring user, std::wstring tech,
                     bool recoverable) {
    return Error(code, std::move(user), std::move(tech), HRESULT_FROM_WIN32(win32Error),
                 recoverable);
}

Error InternalError(std::wstring tech) {
    return Error(ErrorCode::InternalState, L"An unexpected internal error occurred.",
                 std::move(tech), E_FAIL, false);
}

} // namespace vsr
