#include <Push/ApnsPayload.h>
#include <Push/PanePushGate.h>
#include <Push/PushNotice.h>

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

#include <string>

using namespace nano;
using namespace term::push;

namespace
{
constexpr auto noonMilliseconds = std::int64_t {1'763'380'800'000};
constexpr auto halfAMinuteMilliseconds = std::int64_t {30'000};
constexpr auto ninetySecondsMilliseconds = std::int64_t {90'000};
constexpr auto soundName = "ba-ding.caf";
constexpr auto longTextBytes = std::size_t {1000};
constexpr auto bodyLimitWithEllipsisBytes = std::size_t {243};
constexpr auto bytesPerAcuteE = std::size_t {2};

const auto nobodyLooking = PaneAttention {};

PaneStateChange makeChange(const std::string& state, const std::string& message = {})
{
    return {state, message, noonMilliseconds};
}

PushNotice makeNotice(const std::string& text)
{
    return {"tamby", "Grazer", "a1b2", finishedEvent, text};
}

struct DecodedAlert
{
    std::string title;
    std::string body;

    MIRO_REFLECT(title, body)
};

struct DecodedSystemFields
{
    DecodedAlert alert;
    std::string sound;
    std::string threadIdentifier;

    MIRO_REFLECT_MEMBERS(alert, "alert", sound, "sound", threadIdentifier, "thread-id")
};

struct DecodedPayload
{
    DecodedSystemFields aps;
    std::string host;
    std::string session;
    std::string pane;

    MIRO_REFLECT(aps, host, session, pane)
};
} // namespace

auto tFinishedPushes = test("PanePushGate/finishedPushes") = []
{
    auto gate = PanePushGate {};
    check(gate.reportStateChange(makeChange("finished", "Done."), nobodyLooking)
          == PushVerdict::push);
};

auto tWorkingNeverPushes = test("PanePushGate/workingNeverPushes") = []
{
    auto gate = PanePushGate {};
    check(gate.reportStateChange(makeChange("working"), nobodyLooking)
          == PushVerdict::notFinishedOrWaiting);
};

auto tTheSameEventPushesOnce = test("PanePushGate/theSameEventPushesOnce") = []
{
    auto gate = PanePushGate {};
    gate.reportStateChange(makeChange("finished", "Done."), nobodyLooking);
    check(gate.reportStateChange(makeChange("finished", "Done."), nobodyLooking)
          == PushVerdict::alreadyPushed);
};

auto tANewTurnPushesAgain = test("PanePushGate/aNewTurnPushesAgain") = []
{
    auto gate = PanePushGate {};
    gate.reportStateChange(makeChange("finished", "Done."), nobodyLooking);
    gate.reportStateChange(makeChange("working"), nobodyLooking);
    check(gate.reportStateChange(makeChange("finished", "Done."), nobodyLooking)
          == PushVerdict::push);
};

auto tANewQuestionPushes = test("PanePushGate/aNewQuestionPushes") = []
{
    auto gate = PanePushGate {};
    gate.reportStateChange(makeChange("waiting", "Allow Bash?"), nobodyLooking);
    check(gate.reportStateChange(makeChange("waiting", "Allow Edit?"), nobodyLooking)
          == PushVerdict::push);
    check(gate.reportStateChange(makeChange("finished", "Done."), nobodyLooking)
          == PushVerdict::push);
};

auto tThePhoneShowingThePaneSuppresses = test("PanePushGate/thePhoneShowingThePaneSuppresses") = []
{
    auto gate = PanePushGate {};
    const auto phoneShowsIt = PaneAttention {0, true};
    check(gate.reportStateChange(makeChange("finished", "Done."), phoneShowsIt)
          == PushVerdict::shownOnPhone);
};

auto tRecentDesktopTypingSuppresses = test("PanePushGate/recentDesktopTypingSuppresses") = []
{
    auto gate = PanePushGate {};
    const auto typedHalfAMinuteAgo = PaneAttention {noonMilliseconds - halfAMinuteMilliseconds, false};
    check(gate.reportStateChange(makeChange("finished", "Done."), typedHalfAMinuteAgo)
          == PushVerdict::typedOnDesktopRecently);
};

auto tOlderDesktopTypingPushes = test("PanePushGate/olderDesktopTypingPushes") = []
{
    auto gate = PanePushGate {};
    const auto typedNinetySecondsAgo =
        PaneAttention {noonMilliseconds - ninetySecondsMilliseconds, false};
    check(gate.reportStateChange(makeChange("finished", "Done."), typedNinetySecondsAgo)
          == PushVerdict::push);
};

auto tASuppressedEventPushesWhenRepeatedLater = test("PanePushGate/aSuppressedEventIsNotRemembered") = []
{
    auto gate = PanePushGate {};
    gate.reportStateChange(makeChange("waiting", "Allow Bash?"), PaneAttention {0, true});
    check(gate.reportStateChange(makeChange("waiting", "Allow Bash?"), nobodyLooking)
          == PushVerdict::push);
};

auto tEveryVerdictHasADescription = test("PanePushGate/everyVerdictHasADescription") = []
{
    check(describePushVerdict(PushVerdict::push) == "pushed");
    check(!describePushVerdict(PushVerdict::shownOnPhone).empty());
    check(!describePushVerdict(PushVerdict::typedOnDesktopRecently).empty());
};

auto tNoticeSurvivesJson = test("PushNotice/survivesJson") = []
{
    const auto notice = readPushNoticeJson(writePushNoticeJson(makeNotice("All tests pass.")));
    check(notice.has_value());

    if (notice)
    {
        check(notice->host == "tamby" && notice->session == "Grazer");
        check(notice->paneId == "a1b2" && notice->event == finishedEvent);
        check(notice->text == "All tests pass.");
    }
};

auto tNoticeWithoutHostOrKnownEventIsRefused = test("PushNotice/withoutHostOrKnownEventIsRefused") = []
{
    check(!readPushNoticeJson(R"({"paneId":"a1b2","event":"finished"})"));
    check(!readPushNoticeJson(R"({"host":"tamby","paneId":"a1b2","event":"working"})"));
    check(!readPushNoticeJson("not json"));
};

auto tPayloadHasTitleBodySoundAndGroup = test("ApnsPayload/hasTitleBodySoundAndGroup") = []
{
    const auto payload = Miro::createFromJSONString<DecodedPayload>(
        buildApnsPayloadJson(makeNotice("All 420 tests pass."), soundName));
    check(payload.aps.alert.title == "tamby/Grazer", payload.aps.alert.title);
    check(payload.aps.alert.body == "All 420 tests pass.");
    check(payload.aps.sound == soundName);
    check(payload.aps.threadIdentifier == "tamby/Grazer");
    check(payload.host == "tamby" && payload.session == "Grazer" && payload.pane == "a1b2");
};

auto tPayloadWithoutTextSaysWhatHappened = test("ApnsPayload/withoutTextSaysWhatHappened") = []
{
    auto waiting = makeNotice("");
    waiting.event = waitingEvent;
    check(getNotificationBody(waiting) == "Claude is waiting for you.");
    check(getNotificationBody(makeNotice("")) == "Claude finished.");
};

auto tPayloadWithoutSessionIsTitledByHost = test("ApnsPayload/withoutSessionIsTitledByHost") = []
{
    auto notice = makeNotice("Done.");
    notice.session.clear();
    check(getNotificationTitle(notice) == "tamby");
};

auto tLongTextIsCutOnACharacter = test("ApnsPayload/longTextIsCutOnACharacter") = []
{
    auto text = std::string {};

    while (text.size() < longTextBytes)
        text += "é";

    const auto body = getNotificationBody(makeNotice(text));
    check(body.size() <= bodyLimitWithEllipsisBytes, std::to_string(body.size()));
    check(body.ends_with("…"));
    const auto textBytes = body.size() - std::string {"…"}.size();
    check(textBytes % bytesPerAcuteE == 0, "cut inside a character");
};
