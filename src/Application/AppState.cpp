#include "AppState.h"

namespace vsr::app {

const wchar_t* ToString(AppState state) noexcept {
    switch (state) {
        case AppState::Initializing: return L"Initializing";
        case AppState::Ready: return L"Ready";
        case AppState::Preparing: return L"Preparing";
        case AppState::Recording: return L"Recording";
        case AppState::Paused: return L"Paused";
        case AppState::Stopping: return L"Stopping";
        case AppState::Faulted: return L"Faulted";
        case AppState::ShuttingDown: return L"ShuttingDown";
    }
    return L"Unknown";
}

bool IsTransitionAllowed(AppState from, AppState to) noexcept {
    // spec.md §8 transition table:
    //   Initializing -> Ready | Faulted
    //   Ready -> Preparing | ShuttingDown
    //   Preparing -> Recording | Ready | Faulted
    //   Recording -> Paused | Stopping | Faulted
    //   Paused -> Recording | Stopping | Faulted
    //   Stopping -> Ready | Faulted
    //   Faulted -> Ready | ShuttingDown
    switch (from) {
        case AppState::Initializing:
            return to == AppState::Ready || to == AppState::Faulted;
        case AppState::Ready:
            return to == AppState::Preparing || to == AppState::ShuttingDown;
        case AppState::Preparing:
            return to == AppState::Recording || to == AppState::Ready ||
                   to == AppState::Faulted;
        case AppState::Recording:
            return to == AppState::Paused || to == AppState::Stopping ||
                   to == AppState::Faulted;
        case AppState::Paused:
            return to == AppState::Recording || to == AppState::Stopping ||
                   to == AppState::Faulted;
        case AppState::Stopping:
            return to == AppState::Ready || to == AppState::Faulted;
        case AppState::Faulted:
            return to == AppState::Ready || to == AppState::ShuttingDown;
        case AppState::ShuttingDown:
            return false;
    }
    return false;
}

bool IsCommandAllowed(AppState state, Command command) noexcept {
    switch (command) {
        case Command::None:
            return false;
        case Command::Start:
            // FR-070: Start only from Ready.
            return state == AppState::Ready;
        case Command::Pause:
            // Recording -> Paused.
            return state == AppState::Recording;
        case Command::Resume:
            // Paused -> Recording.
            return state == AppState::Paused;
        case Command::Stop:
            // FR-073: Stop is valid while recording or paused; the
            // coordinator treats Stop in non-recording states as a no-op.
            return state == AppState::Recording || state == AppState::Paused;
        case Command::EnableDisplay:
            return state == AppState::Ready || state == AppState::Faulted;
        case Command::DisableDisplay:
            // FR-017: rejected while Preparing/Recording/Paused/Stopping.
            return state == AppState::Ready || state == AppState::Faulted;
        case Command::Reset:
            // Faulted -> Ready recovery.
            return state == AppState::Faulted;
        case Command::Exit:
            return state != AppState::ShuttingDown;
    }
    return false;
}

} // namespace vsr::app
