#include "Command.h"

#include <eacp/Core/Threads/EventLoop.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace term
{
namespace
{
CommandOutput runCaptured(const std::string& command)
{
#if defined(_WIN32)
    auto* pipe = _popen(command.c_str(), "r");
#else
    auto* pipe = popen(command.c_str(), "r");
#endif

    if (pipe == nullptr)
        return {-1, "could not launch command"};

    auto text = std::string {};
    auto buffer = std::array<char, 512> {};

    while (std::fgets(buffer.data(), (int) buffer.size(), pipe) != nullptr)
        text += buffer.data();

#if defined(_WIN32)
    const auto status = _pclose(pipe);
#else
    const auto raw = pclose(pipe);
    const auto status = (raw != -1 && WIFEXITED(raw)) ? WEXITSTATUS(raw) : -1;
#endif

    return {status, std::move(text)};
}
} // namespace

std::string shellQuote(const std::string& text)
{
    auto out = std::string {"'"};

    for (const auto c: text)
        out += c == '\'' ? "'\\''" : std::string {c};

    out += "'";
    return out;
}

CommandOutput runViaLoginShell(const std::string& command)
{
#if defined(_WIN32)
    return runCaptured(command);
#else
    const auto* shell = std::getenv("SHELL");
    const auto wrapped = shellQuote(shell != nullptr ? shell : "/bin/sh")
                         + " -lc " + shellQuote(command);
    return runCaptured(wrapped);
#endif
}

void runViaLoginShellAsync(std::string command,
                           std::function<void(CommandOutput)> done)
{
    std::thread(
        [command = std::move(command), done = std::move(done)]() mutable
        {
            auto output = runViaLoginShell(command);
            eacp::Threads::callAsync(
                [done = std::move(done), output = std::move(output)]
                { done(output); });
        })
        .detach();
}
} // namespace term
