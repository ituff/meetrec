// Microphone mute state tests: the effective mute must be the OR of the
// manual control and the Windows endpoint state.
#include "TestFramework.h"

#include <Encoder/AudioCapture.h>

using vsr::encoder::MicrophoneMuteState;

VSR_TEST(AudioMute, ManualAndSystemCombineWithOr) {
    MicrophoneMuteState state;
    VSR_CHECK(!state.Muted());

    state.manual.store(true);
    VSR_CHECK(state.Muted());

    state.manual.store(false);
    state.system.store(true);
    VSR_CHECK(state.Muted());

    state.manual.store(true);
    VSR_CHECK(state.Muted());

    state.system.store(false);
    VSR_CHECK(state.Muted());

    state.manual.store(false);
    VSR_CHECK(!state.Muted());
}
