#include "Pty.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>

// Prints the milliseconds from Pty::start to the moment the default interactive
// shell has run its first command: the time a new pane takes to be usable.
// Scripts/measure-shell-start.ps1 runs it repeatedly.

namespace
{
using Clock = std::chrono::steady_clock;

constexpr auto readyMarker = "COWTERM_READY";
constexpr auto timeout = std::chrono::seconds {30};

// Typed straight away; the shell runs it once its prompt is up. The marker is
// built from two halves so the echo of the typed line never matches it.
constexpr auto printReadyMarker = "'{0}{1}' -f 'COWTERM','_READY'\r";
} // namespace

int main()
{
    auto lock = std::mutex {};
    auto changed = std::condition_variable {};
    auto output = std::string {};
    auto hasExited = false;

    auto pty = term::Pty {};
    const auto startTime = Clock::now();

    const auto started = pty.start(
        {},
        [&](std::string text)
        {
            auto guard = std::lock_guard {lock};
            output += text;
            changed.notify_all();
        },
        [&]
        {
            auto guard = std::lock_guard {lock};
            hasExited = true;
            changed.notify_all();
        });

    if (!started)
    {
        std::fprintf(stderr, "Pty::start failed\n");
        return 1;
    }

    pty.write(printReadyMarker);

    auto guard = std::unique_lock {lock};
    const auto isReady = changed.wait_for(
        guard,
        timeout,
        [&] { return hasExited || output.find(readyMarker) != std::string::npos; });

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startTime);
    guard.unlock();
    pty.shutdown();

    if (!isReady || hasExited)
    {
        std::fprintf(stderr, "shell never printed the marker\n");
        return 1;
    }

    std::printf("%lld\n", (long long) elapsed.count());
    return 0;
}
