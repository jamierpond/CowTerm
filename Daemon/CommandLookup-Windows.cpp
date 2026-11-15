#include "CommandLookup.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace term
{
namespace
{
constexpr auto pathSeparator = ';';
constexpr auto defaultExtensions = ".COM;.EXE;.BAT;.CMD";

std::string readEnvironment(const char* name, const char* fallback)
{
    const auto* value = std::getenv(name);
    return value != nullptr ? value : fallback;
}

bool existsInDirectory(const std::filesystem::path& directory, const std::string& program)
{
    auto extensions = std::istringstream {readEnvironment("PATHEXT", defaultExtensions)};
    auto error = std::error_code {};

    if (std::filesystem::is_regular_file(directory / program, error))
        return true;

    for (auto extension = std::string {}; std::getline(extensions, extension, pathSeparator);)
    {
        if (std::filesystem::is_regular_file(directory / (program + extension), error))
            return true;
    }

    return false;
}
} // namespace

std::string findCommandSearchPath()
{
    return readEnvironment("PATH", "");
}

bool isCommandInstalled(const std::string& command, const std::string& searchPath)
{
    const auto program = getProgramName(command);

    if (program.empty())
        return false;

    auto directories = std::istringstream {searchPath};

    for (auto directory = std::string {}; std::getline(directories, directory, pathSeparator);)
    {
        if (!directory.empty() && existsInDirectory(directory, program))
            return true;
    }

    return false;
}
} // namespace term
