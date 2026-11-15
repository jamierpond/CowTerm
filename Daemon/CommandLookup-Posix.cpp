#include "CommandLookup.h"

#include <eacp/Core/Process/Process.h>

#include <cstdlib>
#include <sstream>
#include <string_view>
#include <unistd.h>

namespace term
{
namespace
{
constexpr auto fallbackShell = "/bin/sh";

std::string readPathOfThisProcess()
{
    const auto* path = std::getenv("PATH");
    return path != nullptr ? path : "";
}

std::string getLoginShell()
{
    const auto* shell = std::getenv("SHELL");
    return shell != nullptr && *shell != '\0' ? shell : fallbackShell;
}
} // namespace

std::string findCommandSearchPath()
{
    constexpr auto pathMarker = "COWTERM_PATH=";
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = getLoginShell();
    options.arguments = {"-l", "-i", "-c", std::string {"printf '\\n%s%s\\n' "} + pathMarker + " \"$PATH\""};
    const auto output = eacp::Processes::run(options).output;
    const auto start = output.rfind(pathMarker);

    if (start == std::string::npos)
        return readPathOfThisProcess();

    const auto valueStart = start + std::string_view {pathMarker}.size();
    return output.substr(valueStart, output.find('\n', valueStart) - valueStart);
}

bool isCommandInstalled(const std::string& command, const std::string& searchPath)
{
    const auto program = getProgramName(command);

    if (program.empty())
        return false;

    auto directories = std::istringstream {searchPath};

    for (auto directory = std::string {}; std::getline(directories, directory, ':');)
    {
        if (!directory.empty() && access((directory + "/" + program).c_str(), X_OK) == 0)
            return true;
    }

    return false;
}
} // namespace term
