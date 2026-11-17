#include "PhoneLaunch.h"

#include "Multiplexer/DataDirectories.h"

#include <eacp/Core/Utils/Logging.h>

#include <cstdlib>

namespace cowterm::mobile
{
using namespace eacp;
using namespace term::phone;

namespace
{
constexpr auto storedScreenFileName = "last-screen.json";

std::string readEnvironment(const std::string& name)
{
    const auto* value = std::getenv(name.c_str());

    if (value == nullptr)
        return {};

    return value;
}

std::string joinPaneIds(const std::vector<std::string>& paneIds)
{
    auto joined = std::string {};

    for (const auto& id: paneIds)
        joined += (joined.empty() ? "" : " ") + id;

    return joined;
}

void logStoredScreen(const std::optional<StoredScreen>& stored)
{
    if (!stored)
        return LOG("phone: no stored screen");

    const auto age = getMillisecondsSinceEpoch() - stored->savedAtMillisecondsSinceEpoch;
    LOG("phone: stored screen of ",
        stored->host,
        " pane ",
        stored->paneId,
        " from ",
        describeStoredScreenAge(age));
}
} // namespace

PhoneLaunch::PhoneLaunch(PhoneSettings settingsToUse)
    : settings(std::move(settingsToUse))
    , storedScreen(loadStoredScreen(settings.storedScreenPath))
    , connection(settings)
    , networkPathWatch(
          [this]
          {
              LOG("phone: network path changed");
              connection.handleWake();
          })
{
    logStoredScreen(storedScreen);
    logWhatTheConnectionHears();
}

std::optional<PhoneSettings> PhoneLaunch::readSettings(int argc, char* argv[])
{
    const auto arguments = std::vector<std::string> {argv + 1, argv + argc};
    const auto storedScreenPath = term::getDataDirectory() / storedScreenFileName;
    return parsePhoneSettings(arguments, readEnvironment, storedScreenPath);
}

term::TerminalConfig PhoneLaunch::makeTerminalConfig()
{
    auto config = term::TerminalConfig {};
    config.makeShell = [this](const std::string& paneId) { return connection.makeShell(paneId); };
    return config;
}

std::string PhoneLaunch::getPaneToOpen() const
{
    return settings.explicitPaneId;
}

void PhoneLaunch::handleSuspended(bool isSuspended)
{
    if (isSuspended)
    {
        LOG("phone: storing the shown screen");
        return connection.handleBackground();
    }

    LOG("phone: checking the link after waking");
    connection.handleWake();
}

void PhoneLaunch::logWhatTheConnectionHears()
{
    connection.onLinkStateChanged = [this](PhoneLinkState state)
    {
        LOG("phone: link ", describeLinkState(state), " ", connection.getFailureReason());
        onLinkStateChanged(state);
    };

    connection.onPaneChosen = [this](const ChosenPane& pane)
    {
        onPaneChosen(pane);
        LOG("phone: pane ",
            pane.paneId,
            " on ",
            pane.host,
            " in session ",
            pane.sessionName,
            " with panes ",
            joinPaneIds(pane.sessionPaneIds));
    };

    connection.onPaneStatus = [](const std::string& status) { LOG("phone: pane status ", status); };
}
} // namespace cowterm::mobile
