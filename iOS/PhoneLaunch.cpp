#include "PhoneLaunch.h"

#include "Multiplexer/DataDirectories.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Logging.h>

#include <cstdlib>
#include <filesystem>
#include <map>

namespace cowterm::mobile
{
using namespace eacp;
using namespace term::phone;

namespace
{
constexpr auto storedScreenFileName = "last-screen.json";
constexpr auto sessionOpeningsFileName = "session-openings.json";

// What `just ios-pair` copies into the app's container, under the data folder.
constexpr auto pairingFolderName = "pairing";
constexpr auto pairedHostsFileName = "hosts";
constexpr auto pairedKeyFileName = "phone-key";
constexpr auto pairedPublicKeyFileName = "phone-key.pub";
constexpr auto pairedKnownHostsFileName = "known_hosts";

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

std::string readFileIfPresent(const std::filesystem::path& path)
{
    auto error = std::error_code {};

    if (!std::filesystem::exists(path, error))
        return {};

    return Files::readFile(path.string());
}

std::string describePathIfPresent(const std::filesystem::path& path)
{
    auto error = std::error_code {};

    if (!std::filesystem::exists(path, error))
        return {};

    return path.string();
}

// The paired setup, read as the settings' environment names: the launch's own
// arguments still come first.
EnvironmentReader makePairedSetupReader(const std::filesystem::path& folder)
{
    const auto values = std::map<std::string, std::string> {
        {"COWTERM_DESTINATION", joinPairedHosts(readFileIfPresent(folder / pairedHostsFileName))},
        {"COWTERM_KEY_FILE", describePathIfPresent(folder / pairedKeyFileName)},
        {"COWTERM_PUBLIC_KEY_FILE", describePathIfPresent(folder / pairedPublicKeyFileName)},
        {"COWTERM_KNOWN_HOSTS", describePathIfPresent(folder / pairedKnownHostsFileName)},
    };

    return [values](const std::string& name) -> std::string
    {
        if (const auto found = values.find(name); found != values.end())
            return found->second;

        return {};
    };
}

std::string describeSize(term::PtySize size)
{
    return std::to_string(size.cols) + " x " + std::to_string(size.rows);
}

std::string describeHostStatus(term::RemoteHost::Status status)
{
    switch (status)
    {
        case term::RemoteHost::Status::Connected:
            return "link up";
        case term::RemoteHost::Status::Unreachable:
            return "failed:";
        case term::RemoteHost::Status::Connecting:
            return "connecting";
    }

    return {};
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
    , sessionOpenings(settings.storedScreenPath.parent_path() / sessionOpeningsFileName)
    , connection(settings, makeEmbeddedSshLinkFactory(settings), readSteadyClock)
    , networkPathWatch(
          [this]
          {
              LOG("phone: network path changed");
              connection.handleWake();
          })
{
    logStoredScreen(storedScreen);
    logWhatTheConnectionHears();

    if (storedScreen)
        connection.setPaneShownLastTime({storedScreen->host, storedScreen->paneId});
}

std::optional<PhoneSettings> PhoneLaunch::readSettings(int argc, char* argv[])
{
    const auto arguments = std::vector<std::string> {argv + 1, argv + argc};
    const auto storedScreenPath = term::getDataDirectory() / storedScreenFileName;

    if (auto fromLaunch = parsePhoneSettings(arguments, readEnvironment, storedScreenPath))
    {
        fromLaunch->choosesPaneAtLaunch = false;
        return fromLaunch;
    }

    const auto pairingFolder = term::getDataDirectory() / pairingFolderName;
    auto paired = parsePhoneSettings(arguments, makePairedSetupReader(pairingFolder), storedScreenPath);

    if (paired)
        LOG("phone: hosts from ", pairingFolder.string(), ": ", describeHostList(*paired));
    else
        LOG("phone: no hosts set up in ", pairingFolder.string());

    return paired;
}

std::string PhoneLaunch::describeHostList(const PhoneSettings& settings)
{
    auto hosts = std::string {};

    for (const auto& destination: settings.overviewDestinations)
        hosts += (hosts.empty() ? "" : ", ") + makeHostLabel(destination);

    return hosts;
}

term::TerminalConfig PhoneLaunch::makeTerminalConfig()
{
    auto config = term::TerminalConfig {};
    config.makeShell = [this](const std::string& paneId) { return connection.makeShell(paneId); };
    return config;
}

DiffViewOpening PhoneLaunch::openDiffView()
{
    auto opening = connection.openDiffView();

    if (opening.pane)
        LOG("phone: dv pane ", opening.pane->paneId, " on ", opening.pane->host, " in ", opening.pane->workingDirectory);
    else
        LOG("phone: no dv: ", opening.whyNot);

    return opening;
}

term::TerminalConfig PhoneLaunch::makeDiffViewConfig(const DiffViewPane& pane)
{
    auto config = term::TerminalConfig {};
    config.makeCommandShell = [this, pane](const std::string&) { return connection.makeDiffViewShell(pane); };
    return config;
}

void PhoneLaunch::closeDiffView(const DiffViewPane& pane)
{
    LOG("phone: killed dv pane ", pane.paneId, " on ", pane.host);
    connection.closeDiffView(pane);
}

std::string PhoneLaunch::getPaneToOpen() const
{
    return settings.explicitPaneId;
}

bool PhoneLaunch::choosePaneOfSession(const std::string& paneId)
{
    return connection.choosePaneOfSession(paneId);
}

std::optional<std::string> PhoneLaunch::openSession(const term::overview::OverviewRow& row)
{
    return connection.openSession(row);
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

    connection.onHostStatusChanged =
        [](const std::string& host, term::RemoteHost::Status status, const std::string& reason)
    { LOG("phone: ", host, " ", describeHostStatus(status), " ", reason); };

    connection.onPaneDrawn = [](const ChosenPane& pane, term::PtySize size, std::optional<term::PtySize> borrowed)
    {
        LOG("phone: pane ",
            pane.paneId,
            " on ",
            pane.host,
            " opened at ",
            describeSize(size),
            ", its size before the phone took it ",
            borrowed ? describeSize(*borrowed) : std::string {"unknown"});
    };

    connection.onEarlierTemporaryPaneKilled = [](const TemporaryPane& pane)
    { LOG("phone: killed dv pane ", pane.paneId, " on ", pane.host, ", left by an earlier launch"); };

    connection.onPaneOutputAfterQuiet = [](const ChosenPane& pane, std::size_t bytes)
    { LOG("phone: output from pane ", pane.paneId, " on ", pane.host, ", ", bytes, " bytes"); };

    connection.onSizesReturned = [](const std::string& host, const std::vector<term::DaemonClient::ReturnedSize>& returned)
    {
        for (const auto& given: returned)
            LOG("phone: gave pane ", given.paneId, " on ", host, " its size back, ", describeSize(given.size));
    };
}
} // namespace cowterm::mobile
