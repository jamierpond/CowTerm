#include "PairingLayout.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace cowterm::mobile;
using eacp::Graphics::Insets;
using eacp::Graphics::Rect;

namespace
{
constexpr auto miniWidth = 375.0f;
constexpr auto miniHeight = 812.0f;
constexpr auto keyboardHeight = 336.0f;

Insets makePortraitSafeArea()
{
    constexpr auto statusBar = 50.0f;
    constexpr auto homeIndicator = 34.0f;
    return {.top = statusBar, .bottom = homeIndicator};
}

Insets makeLandscapeSafeArea()
{
    constexpr auto notchSide = 50.0f;
    constexpr auto homeIndicator = 21.0f;
    return {.left = notchSide, .bottom = homeIndicator, .right = notchSide};
}

bool isInside(Rect inner, Rect outer)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w
           && inner.y + inner.h <= outer.y + outer.h;
}
} // namespace

auto portraitPutsTheQrCodeAboveTheColumn = test("PairingLayout/portraitPutsTheQrCodeAboveTheColumn") = []
{
    const auto screen = Rect {0.0f, 0.0f, miniWidth, miniHeight};
    const auto areas = calculatePairingAreas(screen, makePortraitSafeArea(), 0.0f, true);

    check(areas.qrCode.has_value());
    check(areas.qrCode->y + areas.qrCode->h <= areas.column.y);
    check(areas.title.y + areas.title.h <= areas.qrCode->y);
    check(isInside(*areas.qrCode, screen));
};

auto landscapePutsTheQrCodeBesideTheColumn = test("PairingLayout/landscapePutsTheQrCodeBesideTheColumn") = []
{
    const auto screen = Rect {0.0f, 0.0f, miniHeight, miniWidth};
    const auto safeArea = makeLandscapeSafeArea();
    const auto areas = calculatePairingAreas(screen, safeArea, 0.0f, true);

    check(areas.qrCode.has_value());
    check(areas.qrCode->x + areas.qrCode->w <= areas.column.x);
    check(areas.qrCode->x >= safeArea.left);
    check(isInside(*areas.qrCode, screen));
};

auto keyboardHidesTheQrCode = test("PairingLayout/keyboardHidesTheQrCode") = []
{
    const auto screen = Rect {0.0f, 0.0f, miniWidth, miniHeight};
    const auto areas = calculatePairingAreas(screen, makePortraitSafeArea(), keyboardHeight, true);

    check(!areas.qrCode.has_value());
    check(areas.column.y + areas.column.h <= miniHeight - keyboardHeight);
};

auto noQrCodeOnceTheKeyIsNotShown = test("PairingLayout/noQrCodeOnceTheKeyIsNotShown") = []
{
    const auto screen = Rect {0.0f, 0.0f, miniWidth, miniHeight};
    const auto areas = calculatePairingAreas(screen, makePortraitSafeArea(), 0.0f, false);

    check(!areas.qrCode.has_value());
};
