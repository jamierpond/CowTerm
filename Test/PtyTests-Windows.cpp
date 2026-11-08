#include "PtyTests.h"

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

using namespace nano;
using namespace term::tests;

// Commands run through PowerShell (cmd only when it is missing).
const char* const term::tests::idleCommand = "Start-Sleep -Seconds 3";

// ConPTY's own hazard: ClosePseudoConsole waits for the console to flush its
// output, so with a shell writing flat out and nobody draining the pipe it
// never returns. shutdown() closes the console while the reader is still
// draining, so a flood must tear down as promptly as silence.
auto floodingShell = test("Pty shutdown returns while the shell floods output") = []
{ check(shutsDownWithin(startShell("while ($true) { 'x' * 200 }"), budget)); };

// A pane's PowerShell gets the Visual Studio developer environment from the
// cache (or a fresh capture) rather than by running Enter-VsDevShell, so the
// MSVC tools must still be on its PATH. Passes trivially without VS.
auto developerToolsOnPath =
    test("Pty interactive PowerShell has cl and cmake on PATH") = []
{
    const auto vswhere = std::filesystem::path {std::getenv("ProgramFiles(x86)")}
                         / "Microsoft Visual Studio/Installer/vswhere.exe";

    if (!std::filesystem::exists(vswhere))
        return;

    constexpr auto toolCountLine =
        "'TOOLS{0}' -f @(Get-Command cl.exe,cmake.exe -EA SilentlyContinue).Count\r";
    constexpr auto bothToolsFound = "TOOLS2";
    constexpr auto timeout = std::chrono::seconds {30};

    auto lock = std::mutex {};
    auto changed = std::condition_variable {};
    auto output = std::string {};
    auto pty = term::Pty {};

    pty.start(
        {},
        [&](std::string text)
        {
            auto guard = std::lock_guard {lock};
            output += text;
            changed.notify_all();
        },
        [] {});

    pty.write(toolCountLine);

    auto guard = std::unique_lock {lock};
    const auto hasBothTools = changed.wait_for(
        guard,
        timeout,
        [&] { return output.find(bothToolsFound) != std::string::npos; });
    guard.unlock();
    pty.shutdown();

    check(hasBothTools);
};
