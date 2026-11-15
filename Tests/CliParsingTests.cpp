#include "Multiplexer/CliParsing.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::cli;

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
