#pragma once

#include "Emulator/TermTypes.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The mark the Mac and the phone draw on a session or pane Jamie has not seen
// (PaneReadState): a pill filled with Okabe-Ito yellow that names the event in
// words, so it reads without telling colours apart.
namespace term
{
struct UnreadMark
{
    std::string event;
    std::int64_t sinceMilliseconds = 0;
    std::string text;
};

// The newer of two marks; either may be empty.
std::optional<UnreadMark> chooseNewerUnreadMark(const std::optional<UnreadMark>& first,
                                                const std::optional<UnreadMark>& second);

// The pill's word: "finished", "waiting", "bell", "notified".
std::string describeUnreadMark(const UnreadMark& mark);

// Black: the pill's fill is the brightest colour in the palette.
Rgb getUnreadMarkTextColour();

// Unread rows first, the newest first; the rest keep their order. readMark
// gives a row's mark, empty when it is read.
template <typename Row, typename ReadMark>
void moveUnreadRowsFirst(std::vector<Row>& rows, ReadMark readMark)
{
    const auto shouldComeFirst = [&readMark](const Row& first, const Row& second)
    {
        const auto& firstMark = readMark(first);
        const auto& secondMark = readMark(second);

        if (firstMark.has_value() != secondMark.has_value())
            return firstMark.has_value();

        if (!firstMark)
            return false;

        return firstMark->sinceMilliseconds > secondMark->sinceMilliseconds;
    };

    std::ranges::stable_sort(rows, shouldComeFirst);
}
} // namespace term
