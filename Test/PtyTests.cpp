// Regression coverage for the PTY teardown that once hung CowTerm on quit.
//
// The daemon's App destructor destroys every pane, and each pane's ~Pty runs
// Pty::shutdown(). The original order — close the master fd first, while a
// reader thread was still blocked in read() on it — deadlocked in the kernel on
// the tty lock: the app went unresponsive and needed a force quit (see the hang
// report in cowcrash.txt). These tests drive real shells through shutdown() and
// assert it always returns quickly, so that ordering can never come back.

#include "Pty.h"

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

using namespace nano;
using namespace std::chrono_literals;

namespace
{
using term::Pty;

std::unique_ptr<Pty> startShell(const std::string& command)
{
    auto pty = std::make_unique<Pty>();

    const auto started = pty->start({{80, 24}, {}, command},
                                    [](std::string) {},
                                    [] {});
    check(started, "pty.start");

    // Let the reader thread reach its blocking read() on the master fd. The
    // deadlock this suite guards only bites once a read is genuinely in flight,
    // so settle first to make the regression reproduce every run, not just when
    // teardown happens to lose the startup race.
    std::this_thread::sleep_for(150ms);
    return pty;
}

// Runs pty->shutdown() but never lets a regression hang the whole suite: if
// shutdown() has not returned within `budget`, the Pty is intentionally leaked
// (so its ~Pty can't hang too) and the call reports failure instead of blocking
// forever. The tests only run short, self-terminating shells, so a leaked child
// exits on its own moments later.
bool shutsDownWithin(std::unique_ptr<Pty> pty, std::chrono::milliseconds budget)
{
    auto* raw = pty.get();
    auto done = std::promise<void> {};
    auto ready = done.get_future();

    auto worker = std::thread(
        [raw, done = std::move(done)]() mutable
        {
            raw->shutdown();
            done.set_value();
        });

    if (ready.wait_for(budget) == std::future_status::ready)
    {
        worker.join();
        return true;
    }

    // shutdown() is wedged — the regression. Detach and leak so neither the
    // worker nor the destructor blocks the rest of the run.
    worker.detach();
    [[maybe_unused]] auto* leaked = pty.release();
    return false;
}

constexpr auto budget = 2000ms;
} // namespace

// A shell with nothing to do still tears down without hanging.
auto normalShell = test("Pty shutdown returns promptly for a normal shell") = []
{
    check(shutsDownWithin(startShell("sleep 3"), budget));
};

// The exact crash: a silent shell leaves the reader thread parked in read() on
// the master fd. Closing that fd out from under the read used to deadlock.
auto silentShell = test("Pty shutdown does not deadlock while the reader is blocked") = []
{
    check(shutsDownWithin(startShell("sleep 3"), budget));
};

// A shell that traps SIGHUP must not be able to wedge quit: shutdown escalates
// to an unignorable SIGKILL after a short grace period.
auto stubbornShell = test("Pty shutdown kills a shell that ignores SIGHUP") = []
{
    check(shutsDownWithin(startShell("trap '' HUP; sleep 3"), budget));
};

// terminate()/detach() and ~Pty can both call shutdown(); the second is a no-op.
auto doubleShutdown = test("Pty shutdown is safe to call twice") = []
{
    auto pty = startShell("sleep 3");
    pty->shutdown();
    check(shutsDownWithin(std::move(pty), budget));
};
