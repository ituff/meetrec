// Owned-popup relocation decision tests (T100, FR-029).
#include "TestFramework.h"
#include <WindowManagement/PopupTracker.h>

using vsr::window::PopupCandidate;
using vsr::window::ShouldRelocatePopup;

namespace {

PopupCandidate Eligible() {
    PopupCandidate c;
    c.visible = true;
    c.topLevel = true;
    c.hasCaption = true;
    c.sameProcess = true;
    c.emptyTitle = false;
    return c;
}

} // namespace

VSR_TEST(PopupTracker, RelocatesEligibleTargetDialog) {
    VSR_CHECK(ShouldRelocatePopup(Eligible()));
}

VSR_TEST(PopupTracker, IgnoresInvisibleOrChildWindows) {
    PopupCandidate c = Eligible();
    c.visible = false;
    VSR_CHECK(!ShouldRelocatePopup(c));

    c = Eligible();
    c.topLevel = false;
    VSR_CHECK(!ShouldRelocatePopup(c));
}

VSR_TEST(PopupTracker, NeverTouchesOtherProcesses) {
    PopupCandidate c = Eligible();
    c.sameProcess = false;
    VSR_CHECK(!ShouldRelocatePopup(c));
}

VSR_TEST(PopupTracker, IgnoresToolWindowsAndCloakedFrames) {
    PopupCandidate c = Eligible();
    c.isToolWindow = true;
    VSR_CHECK(!ShouldRelocatePopup(c));

    c = Eligible();
    c.cloaked = true;
    VSR_CHECK(!ShouldRelocatePopup(c));
}

VSR_TEST(PopupTracker, IgnoresBorderlessSurfaces) {
    PopupCandidate c = Eligible();
    c.hasCaption = false;
    VSR_CHECK(!ShouldRelocatePopup(c));
}

VSR_TEST(PopupTracker, AlreadyOnVirtualNeedsNoMove) {
    PopupCandidate c = Eligible();
    c.alreadyOnVirtual = true;
    VSR_CHECK(!ShouldRelocatePopup(c));
}

VSR_TEST(PopupTracker, CaptionlessTitleRequiresOwnershipLink) {
    PopupCandidate c = Eligible();
    c.emptyTitle = true;
    VSR_CHECK(!ShouldRelocatePopup(c)); // untitled and unowned -> skip

    c.ownedByTarget = true;
    VSR_CHECK(ShouldRelocatePopup(c)); // untitled but owned by the target -> move
}

VSR_TEST(PopupTracker, RecordedMainFrameItselfIsNeverRehandled) {
    PopupCandidate c = Eligible();
    c.mainFrame = true;
    VSR_CHECK(!ShouldRelocatePopup(c));
}
