#include "PhoneConnection.h"

#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Threads/Async.h>

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

} // namespace

Milliseconds readSteadyClock()
{
    const auto sinceStart = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(sinceStart).count();
}

BridgeLinkFactory makeEmbeddedSshLinkFactory(const PhoneSettings& settings)
{
    return [settings](const std::string&) -> std::unique_ptr<BridgeLink>
    { return std::make_unique<EmbeddedSshLink>(settings.destination, settings.credentials, settings.bridgeCommand); };
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
    , host(settings.destination.user + "@" + settings.destination.host)
{
    DaemonClient::setClientName(phoneClientName);
    DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
    DaemonClient::setOpensOnlyListedPanes(true);
    watch.noteReconnecting(readClock());

    auto config = AppConfig {};
    config.searchDirs = {remote::makeRemoteProjectDir(host, remoteHomeDirectory)};
    RemoteHosts::configure(config, [this](const std::string&) { return makeWatchedLink(); });

    auto& remote = getRemoteHost();
    remote.onChanged = [this] { handleHostChanged(); };
    remote.onUnhandledMessage = [this](RemoteHost&, const std::string& verb, const std::string& body)
    { handleUnhandledMessage(verb, body); };
    remote.getDaemon().onInfoChanged = [this] { choosePaneWhenKnown(); };

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

    auto noteStatus = [this, guard = std::weak_ptr<bool> {alive}](const std::string& status)
    {
        if (!guard.expired())
            onPaneStatus(status);
    };

    return std::make_unique<MirroredShell>(makeRemoteShell(paneId, host, sessionName), mirror, noteStatus);
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
    handleTick();
}

RemoteHost& PhoneConnection::getRemoteHost() const
{
    return *RemoteHosts::find(host);
}

std::unique_ptr<BridgeLink> PhoneConnection::makeWatchedLink()
{
    auto link = makeLink(host);
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

void PhoneConnection::handleUnhandledMessage(const std::string& verb, const std::string& body)
{
    if (verb != "last-input")
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
    const auto panes = listPanes();

    if (!chosenPane)
    {
        const auto isReadyToChoose = !settings.explicitPaneId.empty() || hasLastInputAnswer;
        const auto paneId = choosePaneToOpen(settings.explicitPaneId, lastInputPane, panes);

        if (!isReadyToChoose || !paneId)
            return;

        chosenPane = ChosenPane {host, *paneId, {}, {}};
    }

    chosenPane->sessionName = getRemoteHost().getDaemon().infoFor(chosenPane->paneId).sessionName;
    chosenPane->sessionPaneIds = listPanesOfSession(panes, chosenPane->sessionName);
    onPaneChosen(*chosenPane);
}

std::vector<ListedPane> PhoneConnection::listPanes() const
{
    auto& daemon = getRemoteHost().getDaemon();
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
                     {host, shownPaneId, sessionName, getMillisecondsSinceEpoch(), mirror->model.captureState()});
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
