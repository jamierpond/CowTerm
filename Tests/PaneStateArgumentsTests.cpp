#include <Multiplexer/PaneStateCommand.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using term::cli::parsePaneStateArguments;
using term::cli::readClaudeHookReport;

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

auto tClaudeHookFlagNeedsNoState = test("PaneStateArguments/claudeHookFlagNeedsNoState") = []
{
    const auto parsed = parsePaneStateArguments({"--claude-hook"}, paneFromEnvironment);
    check(parsed.readsClaudeHookInput);
    check(parsed.report && parsed.report->paneId == paneFromEnvironment, parsed.usageError);
};

auto tClaudeHookFlagWithAStateIsAUsageError = test("PaneStateArguments/claudeHookFlagWithAStateIsAUsageError") = []
{
    const auto parsed = parsePaneStateArguments({"--claude-hook", "working"}, paneFromEnvironment);
    check(!parsed.report.has_value());
};

auto tPromptSubmittedIsWorking = test("ClaudeHookReport/promptSubmittedIsWorking") = []
{
    const auto report = readClaudeHookReport(
        R"({"session_id":"s","hook_event_name":"UserPromptSubmit","prompt":"fix the build"})",
        paneFromEnvironment);
    check(report && report->state == "working" && report->message.empty());
};

auto tPermissionQuestionIsWaitingWithTheQuestion = test("ClaudeHookReport/permissionQuestionIsWaitingWithTheQuestion") = []
{
    const auto report = readClaudeHookReport(
        R"({"hook_event_name":"Notification","notification_type":"permission_prompt","message":"Claude needs your permission to use Bash"})",
        paneFromEnvironment);
    check(report && report->state == "waiting");
    check(report && report->message == "Claude needs your permission to use Bash");
    check(report && report->paneId == paneFromEnvironment);
};

auto tEndOfTurnIsFinishedWithClaudesLastLine = test("ClaudeHookReport/endOfTurnIsFinishedWithClaudesLastLine") = []
{
    const auto report = readClaudeHookReport(
        R"({"hook_event_name":"Stop","stop_hook_active":false,"last_assistant_message":"Fixed it.\n\n  All 420 tests pass.  \n\n"})",
        paneFromEnvironment);
    check(report && report->state == "finished");
    check(report && report->message == "All 420 tests pass.", report ? report->message : "");
};

auto tOtherHookEventsReportNothing = test("ClaudeHookReport/otherHookEventsReportNothing") = []
{
    check(!readClaudeHookReport(R"({"hook_event_name":"PreToolUse","tool_name":"Bash"})",
                                paneFromEnvironment));
    check(!readClaudeHookReport("not json", paneFromEnvironment));
};
