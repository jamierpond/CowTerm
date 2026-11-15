#include "CommandLookup.h"

#include <eacp/Core/Process/Process.h>

#include <cstdlib>

namespace term
{
namespace
{
constexpr auto fallbackShell = "/bin/sh";

std::string getLoginShell()
{
    const auto* shell = std::getenv("SHELL");
    return shell != nullptr && *shell != '\0' ? shell : fallbackShell;
}
} // namespace

bool isCommandInstalled(const std::string& command)
{
    const auto program = getProgramName(command);

    if (program.empty())
        return false;

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = getLoginShell();
    options.arguments = {"-l", "-c", "command -v \"$0\" >/dev/null 2>&1", program};
    return eacp::Processes::run(options).exitCode == 0;
}
} // namespace term
