#include "CliParsing.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::cli;

namespace
{
const auto shotOptions =
    std::vector<std::string> {"-o", "--display", "--window", "--rect", "--interactive"};
const auto recordOptions =
    std::vector<std::string> {"-o", "--display", "--duration", "--audio"};
} // namespace

auto everyValueOptionLandsInItsField = test("CliParsing/everyValueOptionLandsInItsField") = []
{
    const auto parsed = parseCaptureOptions(
        {"-o", "a.png", "--display", "2", "--window", "77", "--rect", "1,2,3,4", "--interactive"},
        shotOptions);

    check(parsed.options.has_value());
    check(parsed.options->output == "a.png");
    check(parsed.options->display == "2");
    check(parsed.options->window == "77");
    check(parsed.options->rect == "1,2,3,4");
    check(parsed.options->interactive);
    check(!parsed.options->audio);
};

auto recordTakesDurationAndAudio = test("CliParsing/recordTakesDurationAndAudio") = []
{
    const auto parsed = parseCaptureOptions({"--audio", "--duration", "5"}, recordOptions);

    check(parsed.options.has_value());
    check(parsed.options->audio);
    check(parsed.options->duration == "5");
};

auto optionNotAllowedIsRejected = test("CliParsing/optionNotAllowedIsRejected") = []
{
    const auto parsed = parseCaptureOptions({"--audio"}, shotOptions);

    check(!parsed.options);
    check(parsed.error == "unknown option: --audio (see ct help)");
};

auto missingValueIsRejected = test("CliParsing/missingValueIsRejected") = []
{
    const auto parsed = parseCaptureOptions({"-o"}, shotOptions);

    check(!parsed.options);
    check(parsed.error == "-o needs a value");
};

auto noArgumentsGivesDefaults = test("CliParsing/noArgumentsGivesDefaults") = []
{
    const auto parsed = parseCaptureOptions({}, shotOptions);

    check(parsed.options.has_value());
    check(parsed.options->output.empty());
    check(!parsed.options->interactive);
};

auto notifyUserReadsEveryFlag = test("CliParsing/notifyUserReadsEveryFlag") = []
{
    const auto parsed = parseNotifyUserArguments(
        {"--content", "Merge?", "--options", "merge", "wait", "--allow-type-response",
         "--title", "T", "--pane", "p2"},
        "p1");

    check(parsed.request.has_value());
    check(parsed.request->content == "Merge?");
    check(parsed.request->options == std::vector<std::string> {"merge", "wait"});
    check(parsed.request->allowsTypedResponse);
    check(parsed.request->title == "T");
    check(parsed.request->paneId == "p2");
};

auto notifyUserPaneDefaultsToEnvironment = test("CliParsing/notifyUserPaneDefaultsToEnvironment") = []
{
    const auto parsed = parseNotifyUserArguments({"--content", "hi"}, "p1");

    check(parsed.request.has_value());
    check(parsed.request->paneId == "p1");
    check(parsed.request->options.empty());
};

auto notifyUserUsageErrors = test("CliParsing/notifyUserUsageErrors") = []
{
    check(parseNotifyUserArguments({}, "p").usageError == "--content is required");
    check(parseNotifyUserArguments({"--content", "x"}, "").usageError
          == "no pane: run it inside a CowTerm pane or pass --pane <id>");
    check(parseNotifyUserArguments({"--content", "x", "--options"}, "p").usageError
          == "--options needs at least one option");
    check(parseNotifyUserArguments({"--content", "x", "--options", "--title", "t"}, "p")
              .usageError
          == "--options needs at least one option");
    check(parseNotifyUserArguments({"--bogus"}, "p").usageError == "unknown option: --bogus");
    check(parseNotifyUserArguments({"--content"}, "p").usageError == "--content needs a value");
};

auto notifyUserOptionsStopAtNextFlag = test("CliParsing/notifyUserOptionsStopAtNextFlag") = []
{
    const auto parsed =
        parseNotifyUserArguments({"--options", "a", "--content", "c"}, "p");

    check(parsed.request.has_value());
    check(parsed.request->options == std::vector<std::string> {"a"});
    check(parsed.request->content == "c");
};

auto recordingStateRoundTrips = test("CliParsing/recordingStateRoundTrips") = []
{
    const auto original = Recording {1234, "/tmp/a \"q\"\\b.mov", 1700000000};
    const auto text = formatRecordingState(original);

    check(text == "{\"pid\":1234,\"path\":\"/tmp/a \\\"q\\\"\\\\b.mov\",\"started\":1700000000}\n");

    const auto parsed = parseRecordingState(text);
    check(parsed.has_value());
    check(parsed->pid == 1234);
    check(parsed->path == original.path);
    check(parsed->started == 1700000000);
};

auto recordingStateNeedsPidAndPath = test("CliParsing/recordingStateNeedsPidAndPath") = []
{
    check(!parseRecordingState(""));
    check(!parseRecordingState("{\"pid\":0,\"path\":\"/a\",\"started\":1}"));
    check(!parseRecordingState("{\"pid\":5,\"started\":1}"));
    check(parseRecordingState("{\"pid\":5,\"path\":\"/a\"}")->started == 0);
};
