#include "PhoneConnection.h"

#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Threads/Async.h>

#include <algorithm>
#include <chrono>

namespace term::phone
{
using namespace eacp;

namespace
{
constexpr auto phoneClientName = "iPhone";
constexpr auto remoteHomeDirectory = "~";
constexpr auto tickInterval = Time::MS {500};
constexpr auto lastInputAnswerWait = Milliseconds {2000};
constexpr auto storeIntervalWhileLive = Milliseconds {10000};

std::vector<std::string> listOverviewHosts(const PhoneSettings& settings)
{
    auto hosts = std::vector<std::string> {makeHostLabel(settings.destination)};

    for (const auto& destination: settings.overviewDestinations)
    {
        const auto label = makeHostLabel(destination);

        if (std::ranges::find(hosts, label) == hosts.end())
            hosts.push_back(label);
    }

    return hosts;
}

std::vector<std::string> makeSearchDirs(const std::vector<std::string>& hosts)
{
    auto searchDirs = std::vector<std::string> {};

    for (const auto& host: hosts)
        searchDirs.push_back(remote::makeRemoteProjectDir(host, remoteHomeDirectory));

    return searchDirs;
}
} // namespace

Milliseconds readSteadyClock()
{
    const auto sinceStart = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sinceStart).count();
}

BridgeLinkFactory makeEmbeddedSshLinkFactory(const PhoneSettings& settings)
{
    return [settings](const std::string& hostLabel) -> std::unique_ptr<BridgeLink>
    {
        auto destination = settings.destination;

        for (const auto& candidate: settings.overviewDestinations)
        {
            if (makeHostLabel(candidate) == hostLabel)
                destination = candidate;
        }

        return std::make_unique<EmbeddedSshLink>(destination, settings.credentials, settings.bridgeCommand);
    };
}

Milliseconds getMillisecondsSinceEpoch()
{
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count();
}

PhoneConnection::PhoneConnection(PhoneSettings settingsToUse,
                                 BridgeLinkFactory makeLinkToUse,
                                 Clock readClockToUse)
    : settings(std::move(settingsToUse))
    , makeLink(std::move(makeLinkToUse))
    , readClock(std::move(readClockToUse))
    , host(makeHostLabel(settings.destination))
    , overviewHosts(listOverviewHosts(settings))
    , overview(overviewHosts)
{
    DaemonClient::setClientName(phoneClientName);
    DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
    DaemonClient::setOpensOnlyListedPanes(true);
    watch.noteReconnecting(readClock());

    auto config = AppConfig {};
    config.searchDirs = makeSearchDirs(overviewHosts);
    RemoteHosts::configure(config, [this](const std::string& requestedHost) { return makeWatchedLink(requestedHost); });

    for (const auto& overviewHost: overviewHosts)
        connectOverviewHost(*RemoteHosts::find(overviewHost));

    auto& remote = getRemoteHost();
    remote.onChanged = [this]
    {
        handleHostChanged();
        overview.handleInfoChanged();
    };
    remote.getDaemon().onInfoChanged = [this]
    {
        choosePaneWhenKnown();
        overview.handleInfoChanged();
    };

    tickTimer = std::make_unique<Threads::Timer>(
        [this, guard = std::weak_ptr<bool> {alive}]
        {
            Threads::callAsync([this, guard]
                               {
                                   if (!guard.expired())
                                       handleTick();
                               });
        },
        tickInterval);
}

void PhoneConnection::connectOverviewHost(RemoteHost& remote)
{
    remote.onChanged = [this] { overview.handleInfoChanged(); };
    remote.getDaemon().onInfoChanged = [this] { overview.handleInfoChanged(); };
    remote.onUnhandledMessage = [this](RemoteHost& from, const std::string& verb, const std::string& body)
    { handleUnhandledMessage(from.getHost(), verb, body); };
}

PhoneConnection::~PhoneConnection()
{
    *alive = false;
    tickTimer.reset();
    RemoteHosts::teardown();
}

std::unique_ptr<Shell> PhoneConnection::makeShell(const std::string& paneId)
{
    shownPaneId = paneId;
    auto sessionName = std::string {};

    if (chosenPane && chosenPane->paneId == paneId)
        sessionName = chosenPane->sessionName;

    const auto& paneHost = findShownHost();

    auto noteStatus = [this, guard = std::weak_ptr<bool> {alive}](const std::string& status)
    {
        if (!guard.expired())
            onPaneStatus(status);
    };

    return std::make_unique<MirroredShell>(makeRemoteShell(paneId, paneHost, sessionName), mirror, noteStatus);
}

std::string PhoneConnection::getFailureReason() const
{
    return getRemoteHost().getFailureReason();
}

void PhoneConnection::handleBackground()
{
    storeShownScreen();
}

void PhoneConnection::handleWake()
{
    watch.noteWokeUp(readClock());
    redialUnreachableOverviewHosts();
    handleTick();
}

// The shown host has the link watch; the others only need waking from backoff.
void PhoneConnection::redialUnreachableOverviewHosts()
{
    for (const auto& overviewHost: overviewHosts)
    {
        auto& remote = *RemoteHosts::find(overviewHost);

        if (overviewHost != host && remote.getStatus() == RemoteHost::Status::Unreachable)
            remote.reconnectNow();
    }
}

const std::string& PhoneConnection::findShownHost() const
{
    if (chosenPane)
        return chosenPane->host;

    return host;
}

std::optional<std::string> PhoneConnection::openSession(const overview::OverviewRow& row)
{
    const auto paneId = overview.findPaneToOpen(row);

    if (!paneId)
        return std::nullopt;

    chosenPane = ChosenPane {row.host, *paneId, row.sessionName, row.paneIds};
    onPaneChosen(*chosenPane);
    return paneId;
}

RemoteHost& PhoneConnection::getRemoteHost() const
{
    return *RemoteHosts::find(host);
}

std::unique_ptr<BridgeLink> PhoneConnection::makeWatchedLink(const std::string& requestedHost)
{
    auto link = makeLink(requestedHost);

    if (requestedHost == host)
        link->onBytesReceived = [this] { watch.noteBytesHeard(readClock()); };

    return link;
}

bool PhoneConnection::isWaitingToAskForLastInput() const
{
    const auto isConnected = getRemoteHost().getStatus() == RemoteHost::Status::Connected;
    return isConnected && !chosenPane && !askedForLastInputAt;
}

// A daemon from before last-input never answers it; the phone stops waiting.
bool PhoneConnection::hasGivenUpOnLastInput(Milliseconds now) const
{
    const auto isStillWaiting = askedForLastInputAt && !hasLastInputAnswer;
    return isStillWaiting && now - *askedForLastInputAt > lastInputAnswerWait;
}

void PhoneConnection::handleHostChanged()
{
    if (isWaitingToAskForLastInput())
        askForLastInput();

    choosePaneWhenKnown();
    updateLinkState();
}

void PhoneConnection::askForLastInput()
{
    askedForLastInputAt = readClock();

    if (!settings.explicitPaneId.empty())
        return;

    auto& daemon = getRemoteHost().getDaemon();
    daemon.send(proto::make("last-input"));
    daemon.send(proto::make("info"));
}

void PhoneConnection::handleUnhandledMessage(const std::string& fromHost,
                                             const std::string& verb,
                                             const std::string& body)
{
    if (verb == "activity")
        return overview.handleActivity(fromHost, proto::parse(body).payload);

    if (verb != "last-input" || fromHost != host)
        return;

    lastInputPane = parseLastInputPane(body);
    hasLastInputAnswer = true;
    choosePaneWhenKnown();
}

void PhoneConnection::handleTick()
{
    const auto now = readClock();
    auto& remote = getRemoteHost();

    if (hasGivenUpOnLastInput(now))
    {
        hasLastInputAnswer = true;
        choosePaneWhenKnown();
    }

    if (watch.shouldReconnect(remote.getStatus(), now))
    {
        watch.noteReconnecting(now);
        remote.reconnectNow();
    }

    updateLinkState();
    storeShownScreenWhileLive(now);
}

void PhoneConnection::storeShownScreenWhileLive(Milliseconds now)
{
    if (linkState != PhoneLinkState::Live || now - lastStoredAt <= storeIntervalWhileLive)
        return;

    lastStoredAt = now;
    storeShownScreen();
}

void PhoneConnection::choosePaneWhenKnown()
{
    if (!chosenPane)
    {
        const auto isReadyToChoose = !settings.explicitPaneId.empty() || hasLastInputAnswer;
        const auto paneId = choosePaneToOpen(settings.explicitPaneId, lastInputPane, listPanes(host));

        if (!isReadyToChoose || !paneId)
            return;

        chosenPane = ChosenPane {host, *paneId, {}, {}};
    }

    const auto panes = listPanes(chosenPane->host);
    auto& daemon = RemoteHosts::find(chosenPane->host)->getDaemon();
    chosenPane->sessionName = daemon.infoFor(chosenPane->paneId).sessionName;
    chosenPane->sessionPaneIds = listPanesOfSession(panes, chosenPane->sessionName);
    onPaneChosen(*chosenPane);
}

bool PhoneConnection::choosePaneOfSession(const std::string& paneId)
{
    if (!chosenPane)
        return false;

    const auto& sessionPanes = chosenPane->sessionPaneIds;

    if (std::ranges::find(sessionPanes, paneId) == sessionPanes.end())
        return false;

    chosenPane->paneId = paneId;
    onPaneChosen(*chosenPane);
    return true;
}

std::vector<ListedPane> PhoneConnection::listPanes(const std::string& onHost) const
{
    auto& daemon = RemoteHosts::find(onHost)->getDaemon();
    auto panes = std::vector<ListedPane> {};

    for (const auto& id: daemon.listShellIds())
        panes.push_back({id, daemon.infoFor(id).sessionName});

    return panes;
}

void PhoneConnection::storeShownScreen()
{
    if (shownPaneId.empty() || !mirror->hasContent)
        return;

    auto sessionName = std::string {};

    if (chosenPane)
        sessionName = chosenPane->sessionName;

    saveStoredScreen(settings.storedScreenPath,
                     {findShownHost(), shownPaneId, sessionName, getMillisecondsSinceEpoch(), mirror->model.captureState()});
}

void PhoneConnection::updateLinkState()
{
    const auto current = watch.classify(getRemoteHost().getStatus(), readClock());

    if (current == linkState)
        return;

    linkState = current;
    onLinkStateChanged(current);
}
} // namespace term::phone
