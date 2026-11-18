#include <Push/ApnsPayload.h>
#include <Push/PushNotice.h>

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

#include <string>

using namespace nano;
using namespace term::push;

namespace
{
constexpr auto soundName = "ba-ding.caf";
constexpr auto longTextBytes = std::size_t {1000};
constexpr auto bodyLimitWithEllipsisBytes = std::size_t {243};
constexpr auto bytesPerAcuteE = std::size_t {2};

PushNotice makeNotice(const std::string& text)
{
    return {"tamby", "Grazer", "a1b2", term::finishedUnreadEvent, text};
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

auto tNoticeSurvivesJson = test("PushNotice/survivesJson") = []
{
    const auto notice = readPushNoticeJson(writePushNoticeJson(makeNotice("All tests pass.")));
    check(notice.has_value());

    if (notice)
    {
        check(notice->host == "tamby" && notice->session == "Grazer");
        check(notice->paneId == "a1b2" && notice->event == term::finishedUnreadEvent);
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
    waiting.event = term::waitingUnreadEvent;
    auto bell = makeNotice("");
    bell.event = term::bellUnreadEvent;
    check(getNotificationBody(waiting) == "Claude is waiting for you.");
    check(getNotificationBody(makeNotice("")) == "Claude finished.");
    check(getNotificationBody(bell) == "The terminal rang its bell.");
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
