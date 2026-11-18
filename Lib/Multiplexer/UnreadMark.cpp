#include "UnreadMark.h"

#include "PaneReadState.h"

#include <array>
#include <string_view>

namespace term
{
namespace
{
constexpr auto unreadMarkTextColour = Rgb {0x000000};

struct UnreadEventWord
{
    std::string_view event;
    std::string_view word;
};

constexpr auto unreadEventWords = std::array {
    UnreadEventWord {finishedUnreadEvent, "finished"},
    UnreadEventWord {waitingUnreadEvent, "waiting"},
    UnreadEventWord {bellUnreadEvent, "bell"},
    UnreadEventWord {notificationUnreadEvent, "notified"},
};

constexpr auto wordForAnyOtherEvent = "unread";
} // namespace

std::optional<UnreadMark> chooseNewerUnreadMark(const std::optional<UnreadMark>& first,
                                                const std::optional<UnreadMark>& second)
{
    if (!first)
        return second;

    if (!second || first->sinceMilliseconds >= second->sinceMilliseconds)
        return first;

    return second;
}

std::string describeUnreadMark(const UnreadMark& mark)
{
    const auto found = std::ranges::find(unreadEventWords, std::string_view {mark.event}, &UnreadEventWord::event);

    if (found == unreadEventWords.end())
        return wordForAnyOtherEvent;

    return std::string {found->word};
}

Rgb getUnreadMarkTextColour()
{
    return unreadMarkTextColour;
}
} // namespace term
