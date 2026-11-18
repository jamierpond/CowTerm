#include "PtyTests.h"

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>

using namespace nano;
using namespace term::tests;

const char* term::tests::getIdleCommand()
{
    return "sleep 3";
}

// A shell that traps SIGHUP must not be able to wedge quit: shutdown escalates
// to an unignorable SIGKILL after a short grace period.
auto stubbornShell = test("Pty shutdown kills a shell that ignores SIGHUP") = []
{ check(shutsDownWithin(startShell("trap '' HUP; sleep 3"), budget)); };

namespace
{
// What the command printed before it exited, or nothing if it did not exit.
std::string runCommandAndReadItsOutput(const term::PtyOptions& options)
{
    constexpr auto exitWait = std::chrono::milliseconds {5000};

    auto output = std::make_shared<std::string>();
    auto outputLock = std::make_shared<std::mutex>();
    auto exited = std::make_shared<std::promise<void>>();
    auto exitedFuture = exited->get_future();

    auto pty = term::Pty {};
    const auto started = pty.start(
        options,
        [output, outputLock](const std::string& data)
        {
            auto lock = std::scoped_lock {*outputLock};
            *output += data;
        },
        [exited] { exited->set_value(); });

    check(started, "pty.start");
    check(exitedFuture.wait_for(exitWait) == std::future_status::ready, "command exited");

    auto lock = std::scoped_lock {*outputLock};
    return *output;
}

void writeExecutableFile(const std::filesystem::path& path, const std::string& text)
{
    auto file = std::ofstream {path};
    file << text;
    file.close();
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
}
} // namespace

// PtyOptions::environment reaches the shell; `ct notify-user` finds its pane
// through COWTERM_PANE set this way.
auto environmentReachesShell = test("Pty sets the extra environment in the shell") = []
{
    constexpr auto variableValue = "pane-0123abcd";

    const auto home = std::filesystem::temp_directory_path() / "cowterm-environment-home";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);

    // An empty home: the user's own rc files are not what this test is about,
    // and some take longer than the test waits (Ubuntu's .bashrc loads bash
    // completion, seconds under the nix build shell's bash).
    auto options = term::PtyOptions {};
    options.command = "printf '<%s>' \"$COWTERM_TEST_VARIABLE\"";
    options.environment = {{"COWTERM_TEST_VARIABLE", variableValue},
                           {"HOME", home.string()},
                           {"ZDOTDIR", home.string()}};

    const auto output = runCommandAndReadItsOutput(options);
    std::filesystem::remove_all(home);
    check(output.find(std::string {"<"} + variableValue + ">") != std::string::npos);
};

// dv lives in ~/.local/bin, which Jamie's zsh adds in .zshrc only; a command
// pane started with `zsh -c` never read it and said "command not found: dv".
auto commandFindsProgramsTheRcFileAdds =
    test("Pty runs a command where the shell's rc file has set up PATH") = []
{
    constexpr auto programName = "cowterm-test-program-on-the-rc-files-path";
    constexpr auto programOutput = "found-through-the-rc-file";
    const auto home = std::filesystem::temp_directory_path() / "cowterm-rc-path-home";
    const auto programFolder = home / "rc-only-bin";
    const auto addFolderToPath = "PATH=\"" + programFolder.string() + ":$PATH\"\n";

    std::filesystem::remove_all(home);
    std::filesystem::create_directories(programFolder);
    writeExecutableFile(programFolder / programName, std::string {"#!/bin/sh\necho "} + programOutput + "\n");

    for (const auto* rcFile: {".zshrc", ".bashrc", ".profile"})
        writeExecutableFile(home / rcFile, addFolderToPath);

    auto options = term::PtyOptions {};
    options.command = programName;
    options.environment = {{"HOME", home.string()}, {"ZDOTDIR", home.string()}};

    const auto output = runCommandAndReadItsOutput(options);
    std::filesystem::remove_all(home);
    check(output.find(programOutput) != std::string::npos, output);
};
