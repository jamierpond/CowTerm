#include "Command.h"

#include <eacp/Core/Threads/EventLoop.h>

#include <thread>

namespace term
{
std::string shellQuote(const std::string& text)
{
    auto out = std::string {"'"};

    for (const auto c: text)
        out += c == '\'' ? "'\''" : std::string {c};

    out += "'";
    return out;
}

void runViaLoginShellAsync(std::string command,
                           std::function<void(CommandOutput)> done)
{
    std::thread(
        [command = std::move(command), done = std::move(done)]() mutable
        {
            auto output = runViaLoginShell(command);
            eacp::Threads::callAsync([done = std::move(done),
                                      output = std::move(output)] { done(output); });
        })
        .detach();
}
} // namespace term
