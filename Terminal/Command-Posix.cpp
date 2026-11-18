#include "Command.h"
#include "Multiplexer/UsageLog.h"

#include <eacp/Core/Platform/Platform.h>

#include <array>
#include <cstdio>
#include <cstdlib>

#include <sys/wait.h>

namespace term
{
namespace
{
CommandOutput runCaptured(const std::string& command)
{
    auto* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr)
        return {-1, "could not launch command"};

    auto text = std::string {};
    auto buffer = std::array<char, 512> {};

    while (std::fgets(buffer.data(), (int) buffer.size(), pipe) != nullptr)
        text += buffer.data();

    const auto raw = pclose(pipe);
    const auto status = (raw != -1 && WIFEXITED(raw)) ? WEXITSTATUS(raw) : -1;

    return {status, std::move(text)};
}
} // namespace

CommandOutput runViaLoginShell(const std::string& command)
{
    const auto* shell = std::getenv("SHELL");
    const auto wrapped = shellQuote(shell != nullptr ? shell : "/bin/sh") + " -lc "
                         + shellQuote(command);
    return runCaptured(wrapped);
}

void openInBrowser(const std::string& url)
{
    constexpr auto* opener = eacp::Platform::isMac() ? "open" : "xdg-open";
    logUsage("link.open", {{"url", url}});
    runViaLoginShellAsync(std::string {opener} + " " + shellQuote(url),
                          [](const CommandOutput& output)
                          {
                              if (output.status != 0)
                                  logUsage("link.open-failed",
                                           {{"status", output.status}, {"output", output.text}});
                          });
}
} // namespace term
