#include "PhoneSettings.h"

#include <array>
#include <string_view>

namespace term::phone
{
namespace
{
struct SettingsText
{
    std::string destination;
    std::string agentSocketPath;
    std::string privateKeyPath;
    std::string knownHostsPath;
    std::string paneId;
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
} // namespace

std::optional<PhoneSettings> parsePhoneSettings(const std::vector<std::string>& arguments,
                                                const EnvironmentReader& readEnvironment,
                                                const std::filesystem::path& storedScreenPath)
{
    const auto text = readSettingsText(arguments, readEnvironment);
    const auto destination = parseSshDestination(text.destination);

    if (!destination)
        return std::nullopt;

    auto settings = PhoneSettings {};
    settings.destination = *destination;
    settings.credentials = {text.agentSocketPath, text.privateKeyPath, text.knownHostsPath};
    settings.explicitPaneId = text.paneId;
    settings.storedScreenPath = storedScreenPath;
    return settings;
}
} // namespace term::phone
