#include "PhoneConnection.h"

#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Threads/Async.h>

#include <algorithm>
#include <chrono>
#include <random>

namespace term::phone
{
using namespace eacp;

namespace
{
constexpr auto tickInterval = Time::MS {500};
constexpr auto lastInputAnswerWait = Milliseconds {2000};

// After this the phone stays on the overview rather than jump to a pane a
// slow host names late, under a finger already choosing a row.
constexpr auto launchPaneChoiceWindow = Milliseconds {10000};
constexpr auto storeIntervalWhileLive = Milliseconds {10000};
constexpr auto heldPaneFileName = "held-pane.json";
constexpr auto temporaryPanesFileName = "temporary-panes.json";
constexpr auto quietBeforeOutputIsReported = Milliseconds {1000};

std::vector<std::string> listOverviewHosts(const PhoneSettings& settings)
{
    auto hosts = std::vector<std::string> {getHostName(settings.destination)};

    for (const auto& destination: settings.overviewDestinations)
    {
        const auto hostName = getHostName(destination);

        if (std::ranges::find(hosts, hostName) == hosts.end())
            hosts.push_back(hostName);
    }

    return hosts;
}

// The phone has no cowterm.json, so each host is searched where the Mac
// searches with none: ~/projects and ~.
std::vector<std::string> makeSearchDirs(const std::vector<std::string>& hosts)
{
    auto searchDirs = std::vector<std::string> {};

    for (const auto& host: hosts)
    {
        for (const auto& defaultSearchDir: AppConfig {}.searchDirs)
            searchDirs.push_back(remote::makeRemoteProjectDir(host, defaultSearchDir));
    }

    return searchDirs;
}

std::uint64_t makeRandomNumber()
{
    auto randomNumbers = std::mt19937_64 {std::random_device {}()};
    return randomNumbers();
}
} // namespace

Milliseconds readSteadyClock()
{
    const auto sinceStart = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sinceStart).count();
}

BridgeLinkFactory makeEmbeddedSshLinkFactory(const PhoneSettings& settings)
{
    return [settings](const std::string& hostName) -> std::unique_ptr<BridgeLink>
    {
        auto destination = settings.destination;

        for (const auto& candidate: settings.overviewDestinations)
        {
            if (getHostName(candidate) == hostName)
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
    , host(getHostName(settings.destination))
    , overviewHosts(listOverviewHosts(settings))
    , overview(overviewHosts)
    , heldPanePath(settings.storedScreenPath.parent_path() / heldPaneFileName)
    , heldByEarlierRun(loadHeldPane(heldPanePath))
    , temporaryPanesPath(settings.storedScreenPath.parent_path() / temporaryPanesFileName)
    , temporaryPanesOfEarlierRuns(loadTemporaryPanes(temporaryPanesPath))
    , startedAt(readClock())
{
    DaemonClient::setClientName(proto::phoneClientName);
    DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
    DaemonClient::setOpensOnlyListedPanes(true);
    DaemonClient::setOnlyCommandItMayStart(diffViewCommand);

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
    mirror->onOutput = [this, guard = std::weak_ptr<bool> {alive}](std::size_t bytes)
    {
        if (!guard.expired())
            noteShownPaneOutput(bytes);
    };
    hasReportedShownPaneDrawn = false;
    lastShownPaneOutputAt.reset();
    auto sessionName = std::string {};

    if (chosenPane && chosenPane->paneId == paneId)
        sessionName = chosenPane->sessionName;

    const auto& paneHost = findShownHost();

    auto noteStatus = [this, guard = std::weak_ptr<bool> {alive}](const std::string& status)
    {
        if (!guard.expired())
            onPaneStatus(status);
    };

    auto paneShell = makeRemoteShell(paneId, paneHost, sessionName);

    if (const auto starting = projectSessionsStarting.extract(paneId))
    {
        auto projectShell = std::make_unique<ProjectShell>(std::move(paneShell), starting.mapped());
        projectShell->onLoginShellStarted = [this, session = starting.mapped()] { publishSessionLayout(session); };
        paneShell = std::move(projectShell);
    }

    return std::make_unique<MirroredShell>(std::move(paneShell), mirror, noteStatus);
}

DiffViewOpening PhoneConnection::openDiffView()
{
    if (shownPaneId.empty())
        return {std::nullopt, "No pane is on screen."};

    const auto& shownHost = findShownHost();
    const auto folder = getShownRemoteHost().getDaemon().infoFor(shownPaneId).workingDirectory;
    auto pane = makeDiffViewPane(shownHost, folder, makeRandomNumber());

    if (!pane)
        return {std::nullopt, "The phone does not know this pane's folder yet."};

    if (!addTemporaryPane(temporaryPanesPath, {pane->host, pane->paneId}))
        return {std::nullopt, "dv did not start: the phone could not save the note it needs to close it."};

    return {pane, {}};
}

std::unique_ptr<Shell> PhoneConnection::makeDiffViewShell(const DiffViewPane& pane)
{
    return std::make_unique<DiffViewShell>(makeRemoteShell(pane.paneId, pane.host, {}), pane);
}

void PhoneConnection::closeDiffView(const DiffViewPane& pane)
{
    if (auto* remote = RemoteHosts::find(pane.host))
        remote->getDaemon().kill(pane.paneId);

    removeTemporaryPane(temporaryPanesPath, {pane.host, pane.paneId});
}

// A reinstall or a crash kills the app with dv open, and nothing kills dv's
// pane then; the next launch kills it once its host answers.
void PhoneConnection::killTemporaryPanesOfEarlierRuns(const std::string& fromHost)
{
    auto& daemon = RemoteHosts::find(fromHost)->getDaemon();

    for (const auto& pane: temporaryPanesOfEarlierRuns)
    {
        if (pane.host != fromHost)
            continue;

        daemon.send(proto::make("kill", {pane.paneId}));
        removeTemporaryPane(temporaryPanesPath, pane);
        onEarlierTemporaryPaneKilled(pane);
    }

    std::erase_if(temporaryPanesOfEarlierRuns, [&](const TemporaryPane& pane) { return pane.host == fromHost; });
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
    isInForeground = false;
    updateViewedPane();
    storeShownScreen();

    for (const auto& overviewHost: overviewHosts)
        returnBorrowedSizes(overviewHost);

    forgetHeldPane(heldPanePath);
}

void PhoneConnection::handleWake()
{
    isInForeground = true;
    updateViewedPane();
    claimReturnedSizesAgain();
    rememberHeldPane();

    for (auto& [watchedHost, watch]: watches)
        watch.noteWokeUp(readClock());

    handleTick();
}

void PhoneConnection::setPaneScreenShown(bool isShown)
{
    isPaneScreenShown = isShown;
    overview.setPaneScreenShown(isShown);
    updateViewedPane();
}

void PhoneConnection::updateViewedPane()
{
    auto shownPane = std::optional<ViewedPane> {};

    if (chosenPane)
        shownPane = ViewedPane {chosenPane->host, chosenPane->paneId};

    viewing.setViewedPane(choosePhoneViewedPane({shownPane, isPaneScreenShown, isInForeground}));
}

void PhoneConnection::sendToHost(const std::string& toHost, const std::string& message)
{
    auto* remote = RemoteHosts::find(toHost);

    if (remote != nullptr && remote->getStatus() == RemoteHost::Status::Connected)
        remote->getDaemon().send(message);
}

void PhoneConnection::returnBorrowedSizes(const std::string& onHost)
{
    const auto returned =
        RemoteHosts::find(onHost)->getDaemon().returnBorrowedSizes();

    if (!returned.empty())
        onSizesReturned(onHost, returned);
}

void PhoneConnection::rememberHeldPane()
{
    if (shownPaneId.empty())
        return;

    const auto& shownHost = findShownHost();

    if (const auto borrowed = getShownRemoteHost().getDaemon().findBorrowedSize(shownPaneId))
        saveHeldPane(heldPanePath, {shownHost, shownPaneId, *borrowed});
}

// The daemon-hello says whether this daemon keeps size owners; one that does
// not still has the pane an earlier run was killed holding at phone size.
void PhoneConnection::returnSizeHeldByEarlierRun(const std::string& fromHost, const std::string& daemonHello)
{
    if (!heldByEarlierRun || heldByEarlierRun->host != fromHost)
        return;

    const auto hello = proto::parse(daemonHello);
    const auto held = *std::exchange(heldByEarlierRun, std::nullopt);
    forgetHeldPane(heldPanePath);

    if (hello.args.empty() || std::stoi(hello.args[0]) >= proto::sizeOwnerProtocolVersion)
        return;

    const auto& size = held.sizeBeforeThePhone;
    auto& daemon = RemoteHosts::find(fromHost)->getDaemon();
    daemon.send(proto::make("resize", {held.paneId, std::to_string(size.cols), std::to_string(size.rows)}));
    onSizesReturned(fromHost, {{held.paneId, size}});
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
// silence means a stall; the others, and every host while no pane is shown,
// are redialled from backoff when the app wakes.
bool PhoneConnection::shouldRedial(const std::string& overviewHost, Milliseconds now) const
{
    const auto status = RemoteHosts::find(overviewHost)->getStatus();
    const auto& watch = watches.at(overviewHost);
    const auto isShowingAPaneOnHost = !shownPaneId.empty() && overviewHost == findShownHost();

    if (isShowingAPaneOnHost)
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
    updateViewedPane();
    updateLinkState();
    return paneId;
}

std::optional<std::string> PhoneConnection::openProject(const overview::OverviewRow& row)
{
    auto* remote = RemoteHosts::find(row.host);

    if (!row.isProject || remote == nullptr)
        return std::nullopt;

    const auto session = makeProjectSession(row.host, remote->getLastScan(), row.projectFolder, makeRandomNumber());

    if (!session)
        return std::nullopt;

    remote->getDaemon().allowLoginShellInNewPane({session->paneId, session->folder});
    projectSessionsStarting[session->paneId] = *session;
    chosenPane = ChosenPane {row.host, session->paneId, session->sessionName, {session->paneId}};
    onPaneChosen(*chosenPane);
    updateViewedPane();
    updateLinkState();
    return session->paneId;
}

void PhoneConnection::publishSessionLayout(const ProjectSession& session)
{
    if (auto* remote = RemoteHosts::find(session.host))
        remote->getDaemon().send(makeLayoutMessage(session.makeLayout()));
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

    if (status == RemoteHost::Status::Connected)
        viewing.sendViewingAgainTo(changedHost);
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

    if (verb == "daemon-hello")
    {
        killTemporaryPanesOfEarlierRuns(fromHost);
        return returnSizeHeldByEarlierRun(fromHost, body);
    }

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

    if (!settings.choosesPaneAtLaunch)
        return std::nullopt;

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

    const auto listed = removeDiffViewPaneIds(remote->getDaemon().listShellIds());
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
    updateViewedPane();
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
    updateViewedPane();
    return true;
}

std::vector<ListedPane> PhoneConnection::listPanes(const std::string& onHost) const
{
    auto& daemon = RemoteHosts::find(onHost)->getDaemon();
    auto panes = std::vector<ListedPane> {};

    for (const auto& id: removeDiffViewPaneIds(daemon.listShellIds()))
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
    rememberHeldPane();
    onPaneDrawn(*chosenPane, {screen.columns(), screen.rows()}, borrowed);
}

void PhoneConnection::noteShownPaneOutput(std::size_t bytes)
{
    const auto now = readClock();
    const auto wasQuiet = !lastShownPaneOutputAt || now - *lastShownPaneOutputAt >= quietBeforeOutputIsReported;
    lastShownPaneOutputAt = now;

    if (wasQuiet && chosenPane)
        onPaneOutputAfterQuiet(*chosenPane, bytes);
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
