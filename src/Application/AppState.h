// Virtual Screen Recorder - application state machine (spec.md §8, T042).
#pragma once

#include <string>

namespace vsr::app {

enum class AppState {
    Initializing,
    Ready,
    Preparing,
    Recording,
    Paused,
    Stopping,
    Faulted,
    ShuttingDown,
};

const wchar_t* ToString(AppState state) noexcept;

// True when `from -> to` appears in the allowed transition table.
bool IsTransitionAllowed(AppState from, AppState to) noexcept;

// Commands driving the state machine.
enum class Command { None, Start, Pause, Resume, Stop, EnableDisplay, DisableDisplay, Reset, Exit };

// Pure command validation used by both the coordinator and the UI to enable
// or disable controls (spec.md §8: invalid commands are disabled and
// rejected).
bool IsCommandAllowed(AppState state, Command command) noexcept;

} // namespace vsr::app
