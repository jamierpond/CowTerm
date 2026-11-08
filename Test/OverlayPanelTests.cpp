#include "OverlayPanel.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace eacp;
using namespace term;

namespace
{
constexpr auto metrics = ListPanelMetrics {.maximumWidth = 760.0f,
                                           .rowHeight = 48.0f,
                                           .headerHeight = 46.0f,
                                           .hintHeight = 22.0f,
                                           .maximumRows = 10,
                                           .topFraction = 0.14f};

const auto window = UI::Rect {0.0f, 0.0f, 1000.0f, 800.0f};

UI::KeyEvent makeKey(std::uint16_t keyCode, const std::string& characters, bool control)
{
    auto event = UI::KeyEvent {};
    event.keyCode = keyCode;
    event.characters = characters;
    event.charactersIgnoringModifiers = characters;
    event.modifiers.control = control;
    return event;
}
} // namespace

auto tListPanelBounds = test("OverlayPanel/listPanelIsCentredAndSizedToItsRows") = []
{
    const auto panel = calculateListPanelBounds(window, metrics, 3);

    check(panel.w == 760.0f);
    check(panel.x == 120.0f);
    check(panel.y == 112.0f);
    check(panel.h == 46.0f + 3.0f * 48.0f + 22.0f + 12.0f);

    const auto empty = calculateListPanelBounds(window, metrics, 0);
    check(empty.h == 46.0f + 48.0f + 22.0f + 12.0f, "an empty list keeps one row");

    const auto full = calculateListPanelBounds(window, metrics, 50);
    check(full.h == 46.0f + 10.0f * 48.0f + 22.0f + 12.0f, "rows stop at the maximum");
};

auto tListPanelOnASmallWindow = test("OverlayPanel/listPanelShrinksAndKeepsAMinimumTop") = []
{
    const auto narrow = UI::Rect {0.0f, 0.0f, 400.0f, 100.0f};
    const auto panel = calculateListPanelBounds(narrow, metrics, 1);

    check(panel.w == 340.0f);
    check(panel.x == 30.0f);
    check(panel.y == 20.0f);
};

auto tFindListRow = test("OverlayPanel/findListRowAtMapsPointsToRows") = []
{
    const auto panel = calculateListPanelBounds(window, metrics, 3);
    const auto firstRowTop = panel.y + 46.0f + 4.0f;

    check(calculateListRowTop(panel, metrics, 0) == firstRowTop);
    check(calculateListRowTop(panel, metrics, 2) == firstRowTop + 96.0f);

    check(findListRowAt(panel, metrics, 3, {panel.x + 10.0f, firstRowTop + 1.0f}) == 0);
    check(findListRowAt(panel, metrics, 3, {panel.x + 10.0f, firstRowTop + 50.0f}) == 1);
    check(findListRowAt(panel, metrics, 3, {panel.x + 10.0f, firstRowTop + 150.0f}) == -1);
    check(findListRowAt(panel, metrics, 3, {panel.x + 10.0f, firstRowTop - 1.0f}) == -1);
    check(findListRowAt(panel, metrics, 3, {panel.x - 1.0f, firstRowTop + 1.0f}) == -1);
    check(findListRowAt(panel, metrics, 3, {panel.right() + 1.0f, firstRowTop + 1.0f}) == -1);
};

auto tWrapSelection = test("OverlayPanel/wrapSelectionWrapsBothWays") = []
{
    check(wrapSelection(0, -1, 3) == 2);
    check(wrapSelection(2, 1, 3) == 0);
    check(wrapSelection(1, 1, 3) == 2);
};

auto tSelectionKeys = test("OverlayPanel/selectionKeysAreArrowsAndEmacsCtrlKeys") = []
{
    check(isSelectPreviousKey(makeKey(UI::KeyCode::UpArrow, "", false)));
    check(isSelectPreviousKey(makeKey(0, "p", true)));
    check(!isSelectPreviousKey(makeKey(0, "p", false)));
    check(!isSelectPreviousKey(makeKey(UI::KeyCode::DownArrow, "", false)));

    check(isSelectNextKey(makeKey(UI::KeyCode::DownArrow, "", false)));
    check(isSelectNextKey(makeKey(0, "n", true)));
    check(!isSelectNextKey(makeKey(0, "n", false)));
    check(!isSelectNextKey(makeKey(0, "j", false)));
};
