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

// After this the phone stays on the overview rather than jump to a pane a
// slow host names late, under a finger already choosing a row.
constexpr auto launchPaneChoiceWindow = Milliseconds {10000};
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
    , startedAt(readClock())
{
    DaemonClient::setClientName(phoneClientName);
    DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
    DaemonClient::setOpensOnlyListedPanes(true);
    for (const auto& overviewHost: overviewHosts)
        watches[overviewHost].noteReconnecting(readClock());

    auto config = AppConfig {};
    config.searchDirs = makeSearchDirs(overviewHosts);
    RemoteHosts::configure(config, [this](const std::string& requestedHost) { return makeWatchedLink(requestedHost); });

    for (const auto& overviewHost: overviewHosts)
        connectOverviewHost(*RemoteHosts::find(overviewHost));

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
    const auto& remoteHost = remote.getHost();
    remote.onChanged = [this, remoteHost] { handleHostChanged(remoteHost); };
    remote.getDaemon().onInfoChanged = [this]
    {
        choosePaneWhenKnown();
        overview.handleInfoChanged();
    };
    remote.onUnhandledMessage =
        [this](RemoteHost& from, const std::string& verb, const std::string& body)
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
    mirror = std::make_shared<PaneMirror>();
    hasReportedShownPaneDrawn = false;
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
    return getShownRemoteHost().getFailureReason();
}

void PhoneConnection::setPaneShownLastTime(const PaneOnHostId& pane)
{
    paneShownLastTime = pane;
}

void PhoneConnection::handleBackground()
{
    storeShownScreen();

    for (const auto& overviewHost: overviewHosts)
        returnBorrowedSizes(overviewHost);
}

void PhoneConnection::handleWake()
{
    claimReturnedSizesAgain();

    for (auto& [watchedHost, watch]: watches)
        watch.noteWokeUp(readClock());

    handleTick();
}

void PhoneConnection::returnBorrowedSizes(const std::string& onHost)
{
    const auto returned =
        RemoteHosts::find(onHost)->getDaemon().returnBorrowedSizes();

    if (!returned.empty())
        onSizesReturned(onHost, returned);
}

void PhoneConnection::claimReturnedSizesAgain()
{
    for (const auto& overviewHost: overviewHosts)
        RemoteHosts::find(overviewHost)->getDaemon().claimReturnedSizesAgain();
}

void PhoneConnection::redialHosts(Milliseconds now)
{
    for (const auto& overviewHost: overviewHosts)
    {
        if (!shouldRedial(overviewHost, now))
            continue;

        watches[overviewHost].noteReconnecting(now);
        RemoteHosts::find(overviewHost)->reconnectNow();
    }
}

// Only the shown pane's host is asked something every second, so only its
// silence means a stall; the others are redialled from backoff when the app wakes.
bool PhoneConnection::shouldRedial(const std::string& overviewHost, Milliseconds now) const
{
    const auto status = RemoteHosts::find(overviewHost)->getStatus();
    const auto& watch = watches.at(overviewHost);

    if (overviewHost == findShownHost())
        return watch.shouldReconnect(status, now);

    return status == RemoteHost::Status::Unreachable && watch.shouldReconnect(status, now);
}

RemoteHost& PhoneConnection::getShownRemoteHost() const
{
    return *RemoteHosts::find(findShownHost());
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
    updateLinkState();
    return paneId;
}

std::unique_ptr<BridgeLink>
    PhoneConnection::makeWatchedLink(const std::string& requestedHost)
{
    auto link = makeLink(requestedHost);
    link->onBytesReceived = [this, requestedHost] { watches[requestedHost].noteBytesHeard(readClock()); };
    return link;
}

void PhoneConnection::handleHostChanged(const std::string& changedHost)
{
    reportHostStatus(changedHost);

    const auto isConnected =
        RemoteHosts::find(changedHost)->getStatus() == RemoteHost::Status::Connected;

    if (isConnected && !hostsAskedForLastInput.contains(changedHost))
        askForLastInput(changedHost);

    choosePaneWhenKnown();
    overview.handleInfoChanged();
    updateLinkState();
}

void PhoneConnection::reportHostStatus(const std::string& changedHost)
{
    const auto& remote = *RemoteHosts::find(changedHost);
    const auto status = remote.getStatus();
    const auto found = reportedHostStatus.find(changedHost);

    if (found != reportedHostStatus.end() && found->second == status)
        return;

    reportedHostStatus[changedHost] = status;
    onHostStatusChanged(changedHost, status, remote.getFailureReason());
}

void PhoneConnection::askForLastInput(const std::string& askedHost)
{
    hostsAskedForLastInput.insert(askedHost);

    if (!firstAskedForLastInputAt)
        firstAskedForLastInputAt = readClock();

    if (!settings.explicitPaneId.empty())
        return;

    auto& daemon = RemoteHosts::find(askedHost)->getDaemon();
    daemon.send(proto::make("last-input"));
    daemon.send(proto::make("info"));
}

void PhoneConnection::handleUnhandledMessage(const std::string& fromHost,
                                             const std::string& verb,
                                             const std::string& body)
{
    if (verb == "activity")
        return overview.handleActivity(fromHost, proto::parse(body).payload);

    if (verb != "last-input")
        return;

    hostsThatAnsweredLastInput.insert(fromHost);

    if (const auto answer = parseLastInput(fromHost, body))
        lastInputs.push_back(*answer);

    choosePaneWhenKnown();
}

void PhoneConnection::handleTick()
{
    const auto now = readClock();

    if (!chosenPane)
        choosePaneWhenKnown();

    redialHosts(now);
    updateLinkState();
    reportShownPaneDrawn();
    storeShownScreenWhileLive(now);
}

// Every host has answered last-input, or the first asked has had its wait:
// a daemon from before last-input never answers.
bool PhoneConnection::isReadyToChooseLaunchPane() const
{
    if (hostsThatAnsweredLastInput.size() == overviewHosts.size())
        return true;

    return firstAskedForLastInputAt
           && readClock() - *firstAskedForLastInputAt > lastInputAnswerWait;
}

std::optional<PaneOnHostId> PhoneConnection::findLaunchPane() const
{
    if (!settings.explicitPaneId.empty())
        return PaneOnHostId {host, settings.explicitPaneId};

    const auto hasLaunchPassed = readClock() - startedAt > launchPaneChoiceWindow;

    if (hasLaunchPassed || !isReadyToChooseLaunchPane())
        return std::nullopt;

    return chooseLaunchPane(lastInputs,
                            paneShownLastTime,
                            [this](const PaneOnHostId& pane)
                            { return isPaneListed(pane); });
}

bool PhoneConnection::isPaneListed(const PaneOnHostId& pane) const
{
    auto* remote = RemoteHosts::find(pane.host);

    if (remote == nullptr)
        return false;

    const auto listed = remote->getDaemon().listShellIds();
    return std::ranges::find(listed, pane.paneId) != listed.end();
}

void PhoneConnection::choosePaneWhenKnown()
{
    if (!chosenPane)
    {
        const auto launchPane = findLaunchPane();

        if (!launchPane)
            return;

        chosenPane = ChosenPane {launchPane->host, launchPane->paneId, {}, {}};
    }

    updateChosenPanesSession();
}

void PhoneConnection::updateChosenPanesSession()
{
    const auto panes = listPanes(chosenPane->host);
    const auto info =
        RemoteHosts::find(chosenPane->host)->getDaemon().infoFor(chosenPane->paneId);
    const auto sessionName =
        overview::nameSessionOfPane(info.sessionName, info.workingDirectory);

    if (!sessionName.empty())
    {
        chosenPane->sessionName = sessionName;
        chosenPane->sessionPaneIds = listPanesOfSession(panes, sessionName);
    }

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
    {
        const auto info = daemon.infoFor(id);
        panes.push_back(
            {id,
             overview::nameSessionOfPane(info.sessionName, info.workingDirectory)});
    }

    return panes;
}

void PhoneConnection::storeShownScreenWhileLive(Milliseconds now)
{
    if (linkState != PhoneLinkState::Live
        || now - lastStoredAt <= storeIntervalWhileLive)
        return;

    lastStoredAt = now;
    storeShownScreen();
}

bool PhoneConnection::isShownPaneDrawnButNotReported() const
{
    const auto isShowingChosenPane = chosenPane && chosenPane->paneId == shownPaneId;
    return isShowingChosenPane && mirror->hasContent && !hasReportedShownPaneDrawn;
}

void PhoneConnection::reportShownPaneDrawn()
{
    if (!isShownPaneDrawnButNotReported())
        return;

    hasReportedShownPaneDrawn = true;
    const auto& screen = mirror->model.screen;
    const auto borrowed =
        getShownRemoteHost().getDaemon().findBorrowedSize(shownPaneId);
    onPaneDrawn(*chosenPane, {screen.columns(), screen.rows()}, borrowed);
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
    const auto& shownHost = findShownHost();
    const auto current = watches.at(shownHost).classify(getShownRemoteHost().getStatus(), readClock());

    if (current == linkState)
        return;

    linkState = current;

    if (current == PhoneLinkState::Stalled)
        returnBorrowedSizes(shownHost);
    else if (current == PhoneLinkState::Live)
        claimReturnedSizesAgain();

    onLinkStateChanged(current);
}
} // namespace term::phone
