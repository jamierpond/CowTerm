#include "LeaderKeys.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto resizeKeysMapToLowercaseDirections = test("LeaderKeys/resizeKeysMapToLowercaseDirections") = []
{
    check(findPaneResizeDirection("H") == std::optional<char> {'h'});
    check(findPaneResizeDirection("J") == std::optional<char> {'j'});
    check(findPaneResizeDirection("K") == std::optional<char> {'k'});
    check(findPaneResizeDirection("L") == std::optional<char> {'l'});
    check(!findPaneResizeDirection("h"));
    check(!findPaneResizeDirection("HJ"));
    check(!findPaneResizeDirection(""));
};

auto focusKeysAreLowercaseOnly = test("LeaderKeys/focusKeysAreLowercaseOnly") = []
{
    check(findPaneFocusDirection("h") == std::optional<char> {'h'});
    check(findPaneFocusDirection("l") == std::optional<char> {'l'});
    check(!findPaneFocusDirection("H"));
    check(!findPaneFocusDirection("x"));
};

auto digitsOneToNineAreSessionIndices = test("LeaderKeys/digitsOneToNineAreSessionIndices") = []
{
    check(findSessionIndexForDigit("1") == std::optional<int> {0});
    check(findSessionIndexForDigit("9") == std::optional<int> {8});
    check(!findSessionIndexForDigit("0"));
    check(!findSessionIndexForDigit("12"));
    check(!findSessionIndexForDigit("a"));
};
