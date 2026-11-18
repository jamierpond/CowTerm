#include <Multiplexer/PaneReadState.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

namespace
{
constexpr auto noonMilliseconds = std::int64_t {1'763'380'800'000};
constexpr auto phone = PaneReadState::ViewerId {1};
constexpr auto mac = PaneReadState::ViewerId {2};

UnreadEvent makeEvent(const std::string& event, const std::string& text)
{
    return {event, text, noonMilliseconds};
}

const auto finished = makeEvent(finishedUnreadEvent, "All 420 tests pass.");
const auto asking = makeEvent(waitingUnreadEvent, "Claude needs your permission to use Bash");
} // namespace

auto tFinishingUnseenIsUnreadAndNew = test("PaneReadState/finishingUnseenIsUnreadAndNew") = []
{
    auto pane = PaneReadState {};
    check(pane.reportEvent(finished));
    check(pane.getUnread().has_value());
    check(pane.getUnread() && pane.getUnread()->text == "All 420 tests pass.");
    check(pane.getUnread() && pane.getUnread()->sinceMilliseconds == noonMilliseconds);
};

auto tAskingUnseenIsUnreadAndNew = test("PaneReadState/askingUnseenIsUnreadAndNew") = []
{
    auto pane = PaneReadState {};
    check(pane.reportEvent(asking));
    check(pane.getUnread() && pane.getUnread()->event == waitingUnreadEvent);
};

auto tTheSameEventIsNotNewAgain = test("PaneReadState/theSameEventIsNotNewAgain") = []
{
    auto pane = PaneReadState {};
    pane.reportEvent(finished);
    check(!pane.reportEvent(finished));
    check(pane.getUnread().has_value());
};

auto tAQuestionAfterAnUnreadFinishIsNew = test("PaneReadState/aQuestionAfterAnUnreadFinishIsNew") = []
{
    auto pane = PaneReadState {};
    pane.reportEvent(finished);
    check(pane.reportEvent(asking));
    check(pane.getUnread() && pane.getUnread()->event == waitingUnreadEvent);
};

auto tSeenOnThePhoneIsRead = test("PaneReadState/seenOnThePhoneIsRead") = []
{
    auto pane = PaneReadState {};
    pane.reportEvent(finished);
    pane.startViewing(phone);
    check(!pane.getUnread().has_value());
};

auto tSeenOnTheMacIsReadForThePhoneToo = test("PaneReadState/seenOnTheMacIsReadForThePhoneToo") = []
{
    auto pane = PaneReadState {};
    pane.reportEvent(finished);
    pane.startViewing(mac);
    pane.stopViewing(mac);
    check(!pane.getUnread().has_value(), "the daemon holds one state for every client");
};

auto tFinishingWhileViewedIsSeenAndNotNew = test("PaneReadState/finishingWhileViewedIsSeenAndNotNew") = []
{
    auto pane = PaneReadState {};
    pane.startViewing(mac);
    check(!pane.reportEvent(finished));
    check(!pane.getUnread().has_value());
};

auto tAPromptSubmittedIsRead = test("PaneReadState/aPromptSubmittedIsRead") = []
{
    auto pane = PaneReadState {};
    pane.reportEvent(asking);
    pane.markRead();
    check(!pane.getUnread().has_value());
    check(pane.reportEvent(finished), "the next event is new again");
};

auto tTheSameEventAfterReadingIsNewAgain = test("PaneReadState/theSameEventAfterReadingIsNewAgain") = []
{
    auto pane = PaneReadState {};
    pane.reportEvent(finished);
    pane.markRead();
    check(pane.reportEvent(finished));
};

auto tTwoViewersKeepItSeenUntilBothStop = test("PaneReadState/twoViewersKeepItSeenUntilBothStop") = []
{
    auto pane = PaneReadState {};
    pane.startViewing(phone);
    pane.startViewing(mac);
    pane.stopViewing(phone);
    check(!pane.reportEvent(finished), "the Mac still shows it");
    pane.stopViewing(mac);
    check(pane.reportEvent(asking));
};

auto tAnUnreadEventSurvivesARestore = test("PaneReadState/anUnreadEventSurvivesARestore") = []
{
    auto kept = UnreadPanes {};
    kept.panes["a1b2"] = finished;
    const auto reloaded = Miro::createFromJSONString<UnreadPanes>(Miro::toJSONString(kept));

    auto pane = PaneReadState {};
    pane.restoreUnread(reloaded.panes.at("a1b2"));
    check(pane.getUnread() && pane.getUnread()->text == "All 420 tests pass.");
    check(!pane.reportEvent(finished), "the restored event is not announced twice");
};
