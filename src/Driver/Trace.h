/*++

Module Name:

    Trace.h

Abstract:

    WPP tracing configuration for the Virtual Screen Recorder display driver.

    VSR: unique tracing control GUID (was the sample's shared GUID) and
    product tracing id.

Environment:

    Windows User-Mode Driver Framework 2

--*/

//
// Define the tracing flags.
//
// Tracing GUID - 7d2a1f4e-93c8-4b6d-a1e5-3f0c8d2b9a71 (VSR-specific)
//

// WPP task is unavailable in the hand-rolled toolset chain (official WDK VSIX
// provides it). Trace call-sites compile to no-ops; re-enable by restoring
// this block and setting WppEnabled=true once the VSIX toolset is present.
#define TRACE_LEVEL_INFORMATION 0x0004
#define TRACE_LEVEL_ERROR 0x0002
#define TRACE_LEVEL_WARNING 0x0003
#define TRACE_DRIVER 0
#define TRACE_DEVICE 0
#define TRACE_QUEUE 0
#define MYDRIVER_ALL_INFO 0
inline void TraceEvents(unsigned long, unsigned long, const char*, ...)
{
}
#define WPP_CLEANUP(obj) ((void)(obj))

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAG=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp

//
// Driver specific #defines
//

#define MYDRIVER_TRACING_ID L"VirtualScreenRecorder\\UMDF2\\VirtualDisplayDriver v1.0"
