#include "PtyTests.h"

#include <NanoTest/NanoTest.h>

#include <future>
#include <memory>
#include <mutex>
#include <string>

using namespace nano;
using namespace term::tests;

const char* const term::tests::idleCommand = "sleep 3";

// A shell that traps SIGHUP must not be able to wedge quit: shutdown escalates
// to an unignorable SIGKILL after a short grace period.
auto stubbornShell = test("Pty shutdown kills a shell that ignores SIGHUP") = []
{ check(shutsDownWithin(startShell("trap '' HUP; sleep 3"), budget)); };

// PtyOptions::environment reaches the shell; `ct notify-user` finds its pane
// through COWTERM_PANE set this way.
auto environmentReachesShell = test("Pty sets the extra environment in the shell") = []
{
    constexpr auto variableValue = "pane-0123abcd";
    constexpr auto exitWait = std::chrono::milliseconds {3000};

    auto output = std::make_shared<std::string>();
    auto outputLock = std::make_shared<std::mutex>();
    auto exited = std::make_shared<std::promise<void>>();
    auto exitedFuture = exited->get_future();

    auto pty = term::Pty {};
    auto options = term::PtyOptions {};
    options.command = "printf '<%s>' \"$COWTERM_TEST_VARIABLE\"";
    options.environment = {{"COWTERM_TEST_VARIABLE", variableValue}};

    const auto started = pty.start(
        options,
        [output, outputLock](std::string data)
        {
            auto lock = std::scoped_lock {*outputLock};
            *output += data;
        },
        [exited] { exited->set_value(); });

    check(started, "pty.start");
    check(exitedFuture.wait_for(exitWait) == std::future_status::ready, "command exited");

    auto lock = std::scoped_lock {*outputLock};
    check(output->find(std::string {"<"} + variableValue + ">") != std::string::npos);
};
