#include "PhoneSettings.h"

#include <eacp/Core/Utils/Strings.h>

#include <array>
#include <string_view>

namespace term::phone
{
namespace
{
constexpr auto destinationSeparator = ',';

struct SettingsText
{
    std::string destination;
    std::string agentSocketPath;
    std::string privateKeyPath;
    std::string knownHostsPath;
    std::string paneId;
    std::string bridgeCommand;
};

struct SettingSource
{
    std::string_view flag;
    std::string_view environmentName;
    std::string SettingsText::*field;
};

constexpr auto settingSources = std::array {
    SettingSource {"--destination", "COWTERM_DESTINATION", &SettingsText::destination},
    SettingSource {"--agent-socket", "COWTERM_AGENT_SOCKET", &SettingsText::agentSocketPath},
    SettingSource {"--key-file", "COWTERM_KEY_FILE", &SettingsText::privateKeyPath},
    SettingSource {"--known-hosts", "COWTERM_KNOWN_HOSTS", &SettingsText::knownHostsPath},
    SettingSource {"--pane", "COWTERM_PANE", &SettingsText::paneId},
    SettingSource {"--bridge-command", "COWTERM_BRIDGE_COMMAND", &SettingsText::bridgeCommand},
};

std::optional<std::string> findFlagValue(const std::vector<std::string>& arguments, std::string_view flag)
{
    for (auto index = std::size_t {0}; index + 1 < arguments.size(); ++index)
    {
        if (arguments[index] == flag)
            return arguments[index + 1];
    }

    return std::nullopt;
}

SettingsText readSettingsText(const std::vector<std::string>& arguments,
                              const EnvironmentReader& readEnvironment)
{
    auto text = SettingsText {};

    for (const auto& source: settingSources)
    {
        const auto fromArguments = findFlagValue(arguments, source.flag);
        text.*source.field = fromArguments.value_or(readEnvironment(std::string {source.environmentName}));
    }

    return text;
}

std::optional<std::vector<SshDestination>> parseDestinationList(const std::string& text)
{
    auto destinations = std::vector<SshDestination> {};

    for (const auto& entry: eacp::Strings::split(text, destinationSeparator))
    {
        const auto destination = parseSshDestination(entry);

        if (!destination)
            return std::nullopt;

        destinations.push_back(*destination);
    }

    if (destinations.empty())
        return std::nullopt;

    return destinations;
}
} // namespace

std::string joinPairedHosts(const std::string& hostsFileText)
{
    auto joined = std::string {};

    for (const auto& line: eacp::Strings::split(hostsFileText, '\n'))
    {
        const auto host = eacp::Strings::trim(line);

        if (host.empty())
            continue;

        if (!joined.empty())
            joined += destinationSeparator;

        joined += host;
    }

    return joined;
}

std::string makeHostLabel(const SshDestination& destination)
{
    return destination.user + "@" + destination.host;
}

std::optional<PhoneSettings> parsePhoneSettings(const std::vector<std::string>& arguments,
                                                const EnvironmentReader& readEnvironment,
                                                const std::filesystem::path& storedScreenPath)
{
    const auto text = readSettingsText(arguments, readEnvironment);
    const auto destinations = parseDestinationList(text.destination);

    if (!destinations)
        return std::nullopt;

    auto settings = PhoneSettings {};
    settings.destination = destinations->front();
    settings.overviewDestinations = *destinations;
    settings.credentials = {text.agentSocketPath, text.privateKeyPath, text.knownHostsPath};
    settings.explicitPaneId = text.paneId;
    settings.storedScreenPath = storedScreenPath;

    if (!text.bridgeCommand.empty())
        settings.bridgeCommand = text.bridgeCommand;

    return settings;
}
} // namespace term::phone
