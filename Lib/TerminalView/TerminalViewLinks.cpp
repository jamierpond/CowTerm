#include "TerminalView.h"

namespace term
{
using namespace eacp;
using Graphics::MouseEvent;

std::vector<bool> TerminalView::findCellsToUnderlineAsLinks(long absoluteRow) const
{
    if (!underlinesEveryLink)
        return {};

    return findCellsInLinksOnRow(screen, absoluteRow);
}

std::optional<DetectedLink>
    TerminalView::findLinkUnderPointer(const MouseEvent& event) const
{
    const auto isLinkModifierHeld = Platform::isMac() ? event.modifiers.command
                                                      : event.modifiers.control;

    // A touch screen has no modifier to hold, so a tap opens the link.
    if (!isLinkModifierHeld && !event.fromTouch)
        return std::nullopt;

    return findLinkAt(screen, cellRefAt(event.pos));
}

bool TerminalView::openHyperlinkUnderPointer(const MouseEvent& event)
{
    if (event.button != Graphics::MouseButton::Left)
        return false;

    const auto link = findLinkUnderPointer(event);

    if (!link)
        return false;

    onOpenHyperlink(link->uri);
    return true;
}

eacp::Graphics::MouseCursor TerminalView::getCursorForHoveredLink() const
{
    if (hoveredLink)
        return eacp::Graphics::MouseCursor::PointingHand;

    return eacp::Graphics::MouseCursor::Default;
}

void TerminalView::setUnderlinesEveryLink(bool shouldUnderline)
{
    const auto lock = lockTerminal();
    underlinesEveryLink = shouldUnderline;
    rowDrawings.invalidateAllRows();
    repaintIfShown();
}

void TerminalView::setHoveredLink(std::optional<LinkSpan> link)
{
    if (!hoveredLink && !link)
        return;

    hoveredLink = link;
    setMouseCursor(getCursorForHoveredLink());
    rowDrawings.invalidateAllRows();
    repaintIfShown();
}
} // namespace term
