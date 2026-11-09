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

struct ShellOutput
{
    std::mutex lock;
    std::condition_variable changed;
    std::string text;
    bool hasExited = false;

    bool isReady() const
    {
        return hasExited || text.find(readyMarker) != std::string::npos;
    }
};

bool startShell(term::Pty& pty, ShellOutput& output)
{
    return pty.start(
        {},
        [&](std::string text)
        {
            auto guard = std::lock_guard {output.lock};
            output.text += text;
            output.changed.notify_all();
        },
        [&]
        {
            auto guard = std::lock_guard {output.lock};
            output.hasExited = true;
            output.changed.notify_all();
        });
}

bool waitForReadyMarker(ShellOutput& output)
{
    auto guard = std::unique_lock {output.lock};
    output.changed.wait_for(guard, timeout, [&] { return output.isReady(); });
    return output.isReady() && !output.hasExited;
}
} // namespace

int main()
{
    auto output = ShellOutput {};
    auto pty = term::Pty {};
    const auto startTime = Clock::now();

    if (!startShell(pty, output))
    {
        std::fprintf(stderr, "Pty::start failed\n");
        return 1;
    }

    pty.write(printReadyMarker);
    const auto isReady = waitForReadyMarker(output);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startTime);
    pty.shutdown();

    if (!isReady)
    {
        std::fprintf(stderr, "shell never printed the marker\n");
        return 1;
    }

    std::printf("%lld\n", (long long) elapsed.count());
    return 0;
}
