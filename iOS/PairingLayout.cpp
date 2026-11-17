#include "PairingLayout.h"

#include <algorithm>

namespace cowterm::mobile
{
using eacp::Graphics::Rect;

namespace
{
constexpr auto sidePadding = 16.0f;
constexpr auto topPadding = 12.0f;
constexpr auto qrCodeGap = 16.0f;
constexpr auto largestQrCodeSide = 220.0f;
constexpr auto titleHeight = 36.0f;

// Of the shorter side, in portrait the width and in landscape the height.
constexpr auto qrCodeShareOfShorterSide = 0.6f;

bool isLandscape(Rect area)
{
    return area.w > area.h;
}

float calculateQrCodeSide(Rect area)
{
    const auto shorterSide = std::min(area.w, area.h);
    return std::min(largestQrCodeSide, shorterSide * qrCodeShareOfShorterSide);
}

Rect removeSafeArea(Rect screen, eacp::Graphics::Insets safeArea, float keyboardCoveredHeight)
{
    auto area = screen;
    area.removeFromTop(safeArea.top + topPadding);
    area.removeFromBottom(std::max(safeArea.bottom, keyboardCoveredHeight));
    area.removeFromLeft(safeArea.left);
    area.removeFromRight(safeArea.right);
    return area.inset(sidePadding, 0.0f);
}
} // namespace

PairingAreas calculatePairingAreas(Rect screen,
                                   eacp::Graphics::Insets safeArea,
                                   float keyboardCoveredHeight,
                                   bool showsQrCode)
{
    auto area = removeSafeArea(screen, safeArea, keyboardCoveredHeight);
    auto areas = PairingAreas {};
    areas.title = area.removeFromTop(titleHeight);

    if (!showsQrCode || keyboardCoveredHeight > 0.0f)
    {
        areas.column = area;
        return areas;
    }

    const auto side = calculateQrCodeSide(area);

    if (isLandscape(area))
    {
        auto qrColumn = area.removeFromLeft(side);
        area.removeFromLeft(qrCodeGap);
        areas.qrCode = Rect {qrColumn.x, qrColumn.y + (qrColumn.h - side) / 2.0f, side, side};
        areas.column = area;
        return areas;
    }

    auto qrRow = area.removeFromTop(side);
    area.removeFromTop(qrCodeGap);
    areas.qrCode = Rect {qrRow.x + (qrRow.w - side) / 2.0f, qrRow.y, side, side};
    areas.column = area;
    return areas;
}
} // namespace cowterm::mobile
