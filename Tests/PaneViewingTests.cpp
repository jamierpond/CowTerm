#include <Multiplexer/PaneViewing.h>
#include <Multiplexer/UnreadMark.h>
#include <Multiplexer/ActivityReports.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <vector>

// Which pane a client says it is viewing, the Mac's one-minute idle rule, and
// the unread marks the Mac and the phone draw and sort by.

using namespace nano;
using namespace term;

namespace
{
constexpr auto now = std::int64_t {1'800'000'000'000};
constexpr auto oneSecond = std::int64_t {1'000};
constexpr auto oneMinute = std::int64_t {60'000};
constexpr auto rack = "macprorack";

struct SentMessage
{
    std::string host;
    std::string message;
};

struct RecordingReporter
{
    PaneViewingReporter reporter {[this](const std::string& host, const std::string& message)
                                  { sent.push_back({host, message}); }};
    std::vector<SentMessage> sent;
};

std::vector<std::string> listMessages(const std::vector<SentMessage>& sent)
{
    auto messages = std::vector<std::string> {};

    for (const auto& each: sent)
        messages.push_back(each.host + ": " + each.message);

    return messages;
}

DesktopAttention makeDesktopAttention(std::int64_t lastInput)
{
    return {ViewedPane {{}, "pane-1"}, true, lastInput, now};
}

struct Row
{
    std::string name;
    std::optional<UnreadMark> unread;
};

std::vector<std::string> listNames(const std::vector<Row>& rows)
{
    auto names = std::vector<std::string> {};

    for (const auto& row: rows)
        names.push_back(row.name);

    return names;
}
} // namespace

auto tPhoneViewsTheShownPaneInTheForeground = test("PaneViewing/phoneViewsTheShownPaneOnlyOnThePaneScreenInTheForeground") = []
{
    const auto shown = ViewedPane {rack, "pane-1"};

    check(choosePhoneViewedPane({shown, true, true}).has_value());
    check(!choosePhoneViewedPane({shown, false, true}).has_value(), "the overview or dv's screen is up");
    check(!choosePhoneViewedPane({shown, true, false}).has_value(), "the app is in the background");
    check(!choosePhoneViewedPane({std::nullopt, true, true}).has_value());
};

auto tDesktopStopsViewingAfterAMinuteIdle = test("PaneViewing/desktopStopsViewingAfterAMinuteWithNoInput") = []
{
    check(chooseDesktopViewedPane(makeDesktopAttention(now - oneSecond)).has_value());
    check(chooseDesktopViewedPane(makeDesktopAttention(now - oneMinute + oneSecond)).has_value());
    check(!chooseDesktopViewedPane(makeDesktopAttention(now - oneMinute)).has_value());

    auto unfocused = makeDesktopAttention(now - oneSecond);
    unfocused.isWindowFocused = false;
    check(!chooseDesktopViewedPane(unfocused).has_value());
};

auto tReporterSendsOnlyChanges = test("PaneViewing/reporterSendsViewingAndNotViewingOnlyWhenThePaneChanges") = []
{
    auto recording = RecordingReporter {};

    recording.reporter.setViewedPane(ViewedPane {rack, "pane-1"});
    recording.reporter.setViewedPane(ViewedPane {rack, "pane-1"});
    recording.reporter.setViewedPane(ViewedPane {{}, "pane-2"});
    recording.reporter.setViewedPane(std::nullopt);
    recording.reporter.setViewedPane(std::nullopt);

    check(listMessages(recording.sent)
          == std::vector<std::string> {"macprorack: viewing pane-1",
                                       "macprorack: not-viewing pane-1",
                                       ": viewing pane-2",
                                       ": not-viewing pane-2"});
};

auto tReporterSendsViewingAgainAfterReconnect = test("PaneViewing/reporterSendsViewingAgainWhenItsHostComesBack") = []
{
    auto recording = RecordingReporter {};
    recording.reporter.setViewedPane(ViewedPane {rack, "pane-1"});
    recording.sent.clear();

    recording.reporter.sendViewingAgainTo("tamby");
    check(recording.sent.empty());

    recording.reporter.sendViewingAgainTo(rack);
    check(listMessages(recording.sent) == std::vector<std::string> {"macprorack: viewing pane-1"});
};

auto tUnreadRowsComeFirstNewestFirst = test("UnreadMark/unreadRowsComeFirstNewestFirstOthersKeepTheirOrder") = []
{
    auto rows = std::vector<Row> {{"a", std::nullopt},
                                  {"b", UnreadMark {"finished", now - oneMinute, {}}},
                                  {"c", std::nullopt},
                                  {"d", UnreadMark {"waiting", now, {}}}};

    moveUnreadRowsFirst(rows, [](const Row& row) -> const auto& { return row.unread; });

    check(listNames(rows) == std::vector<std::string> {"d", "b", "a", "c"});
};

auto tMarkSaysTheEventInWords = test("UnreadMark/markSaysTheEventInWords") = []
{
    check(describeUnreadMark({"finished", 0, {}}) == "finished");
    check(describeUnreadMark({"waiting", 0, {}}) == "waiting");
    check(describeUnreadMark({"bell", 0, {}}) == "bell");
    check(describeUnreadMark({"notification", 0, {}}) == "notified");
    check(describeUnreadMark({"something-new", 0, {}}) == "unread");
};

auto tReportsGiveTheNewestMarkOfAPaneSet = test("ActivityReports/newestMarkAmongASessionsPanes") = []
{
    auto reports = ActivityReports {};
    reports.takeActivityReport(rack,
                               "a\t0\t0\tfinished\t0\t\tfinished\t100\tDone.\n"
                               "b\t0\t0\twaiting\t0\t\twaiting\t200\tRun it?\n"
                               "c\t0\t0\t\t0\t\t\t0\t\n");

    const auto newest = reports.findNewestUnreadMark(rack, {"a", "b", "c"});
    check(newest.has_value() && newest->event == "waiting" && newest->text == "Run it?");
    check(!reports.findNewestUnreadMark(rack, {"c"}).has_value());
    check(!reports.findNewestUnreadMark({}, {"a"}).has_value(), "another host's daemon");

    reports.takeActivityReport(rack, "a\t0\t0\t\t0\t\t\t0\t\n");
    check(!reports.findNewestUnreadMark(rack, {"a", "b"}).has_value(), "the next answer replaces the last");
};

auto tReportsGiveTheLastUseOfAPaneSet = test("ActivityReports/lastUseIsTheLatestTypingOrFileChangeAmongASessionsPanes") = []
{
    auto reports = ActivityReports {};
    reports.takeActivityReport(rack,
                               "a\t100\t0\t\t0\t\t\t\t\t300\n"
                               "b\t200\t0\t\t0\t\t\t\t\t0\n"
                               "c\t50\t0\t\t0\t\n");

    check(reports.findLastUseMilliseconds(rack, {"a", "b"}) == 300, "a's file change");
    check(reports.findLastUseMilliseconds(rack, {"b", "c"}) == 200, "b's typing; c's daemon sends no file change");
    check(reports.findLastUseMilliseconds({}, {"a"}) == 0, "another host's daemon");
};
