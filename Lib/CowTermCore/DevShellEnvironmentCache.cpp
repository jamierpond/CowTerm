#include "DevShellEnvironmentCache.h"

#include <iterator>
#include <sstream>

namespace term
{
namespace
{
constexpr auto installationPathKey = "installationPath";
constexpr auto installationVersionKey = "installationVersion";
constexpr auto architectureKey = "architecture";

struct KeyAndValue
{
    std::string key;
    std::string value;
};

std::string withoutCarriageReturn(std::string line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();

    return line;
}

std::optional<KeyAndValue> splitAt(const std::string& line, const std::string& separator)
{
    const auto position = line.find(separator);

    if (position == std::string::npos || position == 0)
        return std::nullopt;

    return KeyAndValue {line.substr(0, position),
                        line.substr(position + separator.size())};
}

void setInstallField(VisualStudioInstall& install, const KeyAndValue& field)
{
    if (field.key == installationPathKey)
        install.installationPath = field.value;
    else if (field.key == installationVersionKey)
        install.installationVersion = field.value;
    else if (field.key == architectureKey)
        install.architecture = field.value;
}

bool isComplete(const VisualStudioInstall& install)
{
    return !install.installationPath.empty() && !install.installationVersion.empty()
           && !install.architecture.empty();
}
} // namespace

std::string serialiseDevShellEnvironmentCache(const DevShellEnvironmentCache& cache)
{
    auto text = std::string {};
    text += std::string {installationPathKey} + "=" + cache.install.installationPath + "\n";
    text += std::string {installationVersionKey} + "="
            + cache.install.installationVersion + "\n";
    text += std::string {architectureKey} + "=" + cache.install.architecture + "\n";
    text += "\n";

    for (const auto& variable: cache.variables)
        text += variable.name + "=" + variable.value + "\n";

    return text;
}

std::optional<DevShellEnvironmentCache>
    parseDevShellEnvironmentCache(const std::string& text)
{
    auto stream = std::istringstream {text};
    auto line = std::string {};
    auto header = std::string {};
    auto hasBlankLine = false;

    while (std::getline(stream, line))
    {
        if (withoutCarriageReturn(line).empty())
        {
            hasBlankLine = true;
            break;
        }

        header += line + "\n";
    }

    auto cache = DevShellEnvironmentCache {};

    for (const auto& field: parseEnvironmentVariableLines(header))
        setInstallField(cache.install, {field.name, field.value});

    if (!hasBlankLine || !isComplete(cache.install))
        return std::nullopt;

    auto body = std::string {std::istreambuf_iterator<char> {stream}, {}};
    cache.variables = parseEnvironmentVariableLines(body);
    return cache;
}

bool isDevShellEnvironmentCacheFor(const DevShellEnvironmentCache& cache,
                                   const VisualStudioInstall& install)
{
    return !cache.variables.empty()
           && cache.install.installationPath == install.installationPath
           && cache.install.installationVersion == install.installationVersion
           && cache.install.architecture == install.architecture;
}

std::vector<EnvironmentVariable> parseEnvironmentVariableLines(const std::string& text)
{
    auto variables = std::vector<EnvironmentVariable> {};
    auto stream = std::istringstream {text};
    auto line = std::string {};

    while (std::getline(stream, line))
    {
        if (auto variable = splitAt(withoutCarriageReturn(line), "="))
            variables.push_back({variable->key, variable->value});
    }

    return variables;
}

std::optional<VisualStudioInstall> parseVswhereOutput(const std::string& text,
                                                      const std::string& architecture)
{
    auto install = VisualStudioInstall {};
    install.architecture = architecture;

    auto stream = std::istringstream {text};
    auto line = std::string {};

    while (std::getline(stream, line))
    {
        if (auto field = splitAt(withoutCarriageReturn(line), ": "))
            setInstallField(install, *field);
    }

    if (!isComplete(install))
        return std::nullopt;

    return install;
}
} // namespace term
