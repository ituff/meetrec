// Window destination geometry tests (T051): destination rect stays wholly
// inside the virtual work area (FR-024).
#include "TestFramework.h"
#include <WindowManagement/WindowPlacement.h>

using vsr::window::ComputeDestinationRect;

namespace {
RECT Rect(long x, long y, long w, long h) { return RECT{x, y, x + w, y + h}; }
}

VSR_TEST(Geometry, CentersWindowInWorkArea) {
    const RECT dest = ComputeDestinationRect(Rect(0, 0, 800, 600), Rect(1920, 0, 1920, 1040));
    VSR_CHECK_EQ(dest.left, 1920 + (1920 - 800) / 2);
    VSR_CHECK_EQ(dest.top, (1040 - 600) / 2);
}

VSR_TEST(Geometry, ClampsOversizedWindow) {
    const RECT dest = ComputeDestinationRect(Rect(0, 0, 4000, 2000), Rect(0, 0, 1920, 1040));
    VSR_CHECK_EQ(dest.right - dest.left, 1920);
    VSR_CHECK_EQ(dest.bottom - dest.top, 1040);
    VSR_CHECK(dest.left >= 0 && dest.top >= 0);
}

VSR_TEST(Geometry, RespectsWorkAreaOffset) {
    // Virtual monitor at (1920, 100); result must sit inside ITS work area.
    const RECT work = Rect(1920, 100, 1920, 1000);
    const RECT dest = ComputeDestinationRect(Rect(100, 100, 500, 400), work);
    VSR_CHECK(dest.left >= work.left);
    VSR_CHECK(dest.top >= work.top);
    VSR_CHECK(dest.right <= work.right);
    VSR_CHECK(dest.bottom <= work.bottom);
    VSR_CHECK_EQ(dest.left, 1920 + (1920 - 500) / 2);
    VSR_CHECK_EQ(dest.top, 100 + (1000 - 400) / 2);
}

VSR_TEST(Geometry, DegenerateWindowDefaultsToSaneSize) {
    const RECT dest = ComputeDestinationRect(Rect(0, 0, 0, 0), Rect(0, 0, 1920, 1040));
    VSR_CHECK(dest.right - dest.left > 0);
    VSR_CHECK(dest.bottom - dest.top > 0);
}

VSR_TEST(Geometry, DegenerateWorkAreaPinsToOrigin) {
    const RECT dest = ComputeDestinationRect(Rect(0, 0, 800, 600), Rect(10, 20, 11, 21));
    VSR_CHECK_EQ(dest.left, 10);
    VSR_CHECK_EQ(dest.top, 20);
}

// --- Driverless off-screen parking geometry (T-E2E-WINDOW) ------------------

VSR_TEST(OffScreenPark, PlacesWindowLeftOfDesktopUnion) {
    using vsr::window::ComputeOffScreenRect;
    const RECT park = ComputeOffScreenRect(Rect(100, 100, 256, 156), Rect(0, 0, 1920, 1080));
    VSR_CHECK(park.right <= 0);
    VSR_CHECK_EQ(park.right - park.left, 256L);
    VSR_CHECK_EQ(park.bottom - park.top, 156L);
}

VSR_TEST(OffScreenPark, CoversDesktopsWithNegativeOrigin) {
    // Multi-monitor union may start left of the origin; parking must still be
    // fully outside the union, not just off the primary monitor.
    using vsr::window::ComputeOffScreenRect;
    const RECT park = ComputeOffScreenRect(Rect(0, 0, 1920, 1080), Rect(-1920, -500, 3840, 2160));
    VSR_CHECK(park.right < -1920);
}

VSR_TEST(OffScreenPark, KeepsOversizedWindowOutside) {
    using vsr::window::ComputeOffScreenRect;
    const RECT park = ComputeOffScreenRect(Rect(0, 0, 5000, 3000), Rect(0, 0, 1920, 1080));
    VSR_CHECK(park.right <= 0);
    VSR_CHECK_EQ(park.right - park.left, 5000L);
}
