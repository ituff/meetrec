// Virtual Screen Recorder - stable error codes and error type.
// Requirements: FR-006, FR-063, FR-028, FR-060, NFR-043 (requirements.md §4).
#pragma once

#include <windows.h>
#include <string>

namespace vsr {

// Stable user-facing error codes. Values map 1:1 to the strings returned by
// ToString(); the string form is the contract and must not change.
enum class ErrorCode {
    None,
    InitOsUnsupported,      // VSR-INIT-OS-UNSUPPORTED
    InitDriverMissing,      // VSR-INIT-DRIVER-MISSING
    InitD3dFailed,          // VSR-INIT-D3D-FAILED
    DisplayNotFound,        // VSR-DISPLAY-NOT-FOUND
    DisplayDisconnected,    // VSR-DISPLAY-DISCONNECTED
    TargetInvalid,          // VSR-TARGET-INVALID
    TargetLost,             // VSR-TARGET-LOST
    CaptureInitFailed,      // VSR-CAPTURE-INIT-FAILED
    CaptureDeviceLost,      // VSR-CAPTURE-DEVICE-LOST
    EncoderUnavailable,     // VSR-ENCODER-UNAVAILABLE
    EncoderWriteFailed,     // VSR-ENCODER-WRITE-FAILED
    OutputNotWritable,      // VSR-OUTPUT-NOT-WRITABLE
    OutputDiskLow,          // VSR-OUTPUT-DISK-LOW
    FinalizeFailed,         // VSR-FINALIZE-FAILED
    InternalState,          // VSR-INTERNAL-STATE
};

// Returns the stable "VSR-*" code string, e.g. "VSR-INIT-D3D-FAILED".
const char* ToString(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::None;   // default-constructed Error = no error
    HRESULT hresult = E_FAIL;
    std::wstring userMessage;        // concise, actionable (NFR-043)
    std::wstring technicalMessage;   // context for the log
    bool recoverable = false;

    Error() = default;
    Error(ErrorCode code_, std::wstring user, std::wstring tech, HRESULT hr = E_FAIL, bool recoverable_ = false)
        : code(code_), hresult(hr), userMessage(std::move(user)),
          technicalMessage(std::move(tech)), recoverable(recoverable_) {}
};

// Maps an HRESULT to a typed error preserving the originating HRESULT.
Error ErrorFromHresult(HRESULT hr, ErrorCode code, std::wstring user, std::wstring tech,
                       bool recoverable = false);

// Maps a Win32 error code (HRESULT_FROM_WIN32 applied internally).
Error ErrorFromWin32(DWORD win32Error, ErrorCode code, std::wstring user, std::wstring tech,
                     bool recoverable = false);

// Generic internal-state error.
Error InternalError(std::wstring tech);

} // namespace vsr
