// State machine tests (T042): every allowed transition plus rejection of all
// others (spec.md §8).
#include "TestFramework.h"
#include <Application/AppState.h>

using namespace vsr::app;

namespace {

constexpr AppState kAllStates[] = {
    AppState::Initializing, AppState::Ready,    AppState::Preparing,
    AppState::Recording,    AppState::Paused,   AppState::Stopping,
    AppState::Faulted,      AppState::ShuttingDown,
};

struct Transition {
    AppState from;
    AppState to;
};

constexpr Transition kAllowed[] = {
    {AppState::Initializing, AppState::Ready},
    {AppState::Initializing, AppState::Faulted},
    {AppState::Ready, AppState::Preparing},
    {AppState::Ready, AppState::ShuttingDown},
    {AppState::Preparing, AppState::Recording},
    {AppState::Preparing, AppState::Ready},
    {AppState::Preparing, AppState::Faulted},
    {AppState::Recording, AppState::Paused},
    {AppState::Recording, AppState::Stopping},
    {AppState::Recording, AppState::Faulted},
    {AppState::Paused, AppState::Recording},
    {AppState::Paused, AppState::Stopping},
    {AppState::Paused, AppState::Faulted},
    {AppState::Stopping, AppState::Ready},
    {AppState::Stopping, AppState::Faulted},
    {AppState::Faulted, AppState::Ready},
    {AppState::Faulted, AppState::ShuttingDown},
};

bool InAllowed(AppState from, AppState to) {
    for (const auto& t : kAllowed) {
        if (t.from == from && t.to == to) return true;
    }
    return false;
}

} // namespace

VSR_TEST(StateMachine, EveryAllowedTransitionIsAccepted) {
    for (const auto& t : kAllowed) {
        VSR_CHECK(IsTransitionAllowed(t.from, t.to));
    }
}

VSR_TEST(StateMachine, EveryOtherTransitionIsRejected) {
    for (AppState from : kAllStates) {
        for (AppState to : kAllStates) {
            const bool allowed = InAllowed(from, to);
            VSR_CHECK_EQ(IsTransitionAllowed(from, to), allowed);
        }
    }
}

VSR_TEST(StateMachine, StartOnlyFromReady) {
    for (AppState s : kAllStates) {
        VSR_CHECK_EQ(IsCommandAllowed(s, Command::Start), s == AppState::Ready);
    }
}

VSR_TEST(StateMachine, PauseOnlyWhileRecording) {
    for (AppState s : kAllStates) {
        VSR_CHECK_EQ(IsCommandAllowed(s, Command::Pause), s == AppState::Recording);
    }
}

VSR_TEST(StateMachine, ResumeOnlyWhilePaused) {
    for (AppState s : kAllStates) {
        VSR_CHECK_EQ(IsCommandAllowed(s, Command::Resume), s == AppState::Paused);
    }
}

VSR_TEST(StateMachine, StopWhileRecordingOrPaused) {
    VSR_CHECK(IsCommandAllowed(AppState::Recording, Command::Stop));
    VSR_CHECK(IsCommandAllowed(AppState::Paused, Command::Stop));
    VSR_CHECK(!IsCommandAllowed(AppState::Ready, Command::Stop));
    VSR_CHECK(!IsCommandAllowed(AppState::Preparing, Command::Stop));
    VSR_CHECK(!IsCommandAllowed(AppState::Stopping, Command::Stop));
}

VSR_TEST(StateMachine, DisplayDisableRejectedWhileActive) {
    // FR-017: rejected while Preparing, Recording, Paused, Stopping.
    VSR_CHECK(IsCommandAllowed(AppState::Ready, Command::DisableDisplay));
    VSR_CHECK(IsCommandAllowed(AppState::Faulted, Command::DisableDisplay));
    VSR_CHECK(!IsCommandAllowed(AppState::Preparing, Command::DisableDisplay));
    VSR_CHECK(!IsCommandAllowed(AppState::Recording, Command::DisableDisplay));
    VSR_CHECK(!IsCommandAllowed(AppState::Paused, Command::DisableDisplay));
    VSR_CHECK(!IsCommandAllowed(AppState::Stopping, Command::DisableDisplay));
}

VSR_TEST(StateMachine, ResetOnlyFromFaulted) {
    VSR_CHECK(IsCommandAllowed(AppState::Faulted, Command::Reset));
    VSR_CHECK(!IsCommandAllowed(AppState::Ready, Command::Reset));
    VSR_CHECK(!IsCommandAllowed(AppState::Recording, Command::Reset));
}
