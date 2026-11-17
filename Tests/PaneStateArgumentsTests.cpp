#include <Multiplexer/PaneStateCommand.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using term::cli::parsePaneStateArguments;

namespace
{
constexpr auto paneFromEnvironment = "pane-from-environment";
} // namespace

auto tStateAndMessageAreRead = test("PaneStateArguments/stateAndMessageAreRead") = []
{
    const auto parsed =
        parsePaneStateArguments({"waiting", "--message", "Claude needs your permission"},
                                paneFromEnvironment);

    check(parsed.report.has_value(), parsed.usageError);

    if (parsed.report)
    {
        check(parsed.report->state == "waiting");
        check(parsed.report->message == "Claude needs your permission");
        check(parsed.report->paneId == paneFromEnvironment);
    }
};

auto tPaneFlagOverridesTheEnvironment = test("PaneStateArguments/paneFlagOverridesTheEnvironment") = []
{
    const auto parsed = parsePaneStateArguments({"--pane", "other", "finished"}, paneFromEnvironment);
    check(parsed.report && parsed.report->paneId == "other");
};

auto tUnknownStateIsAUsageError = test("PaneStateArguments/unknownStateIsAUsageError") = []
{
    const auto parsed = parsePaneStateArguments({"sleeping"}, paneFromEnvironment);
    check(!parsed.report.has_value());
    check(!parsed.usageError.empty());
};

auto tOutsideAPaneIsAUsageError = test("PaneStateArguments/outsideAPaneIsAUsageError") = []
{
    const auto parsed = parsePaneStateArguments({"working"}, "");
    check(!parsed.report.has_value());
};
