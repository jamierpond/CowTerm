#include "DaemonClient.h"
#include "Protocol.h"
#include "RemoteHosts.h"

#include "DaemonDialer.h"

#include "CowTermCore/Shell.h"

namespace term
{
using namespace eacp;

namespace
{
std::unique_ptr<DaemonClient> instance;

bool isPaneMessage(const std::string& verb)
{
    return verb == "output" || verb == "state" || verb == "exit";
}

std::string toString(int value)
{
    return std::to_string(value);
}
} // namespace

void DaemonClient::initialize(const Callback& whenReady)
{
    daemon::dialOrLaunchDaemon(
        [whenReady](std::unique_ptr<IPC::Messenger> messenger)
        {
            if (messenger != nullptr)
            {
                instance = std::make_unique<DaemonClient>(
                    std::make_unique<MessengerDaemonLink>(std::move(messenger)), false);
            }

            whenReady();
        });
}

DaemonClient* DaemonClient::get()
{
    return instance.get();
}

void DaemonClient::teardown()
{
    instance.reset();
}

DaemonClient::DaemonClient(std::unique_ptr<DaemonLink> linkToUse, bool keepsPanes)
    : link(std::move(linkToUse))
    , keepsPanesWhenLinkDrops(keepsPanes)
{
    wireLink();
    infoTimer = std::make_unique<Threads::Timer>([this] { requestInfo(); }, 1);
}

void DaemonClient::wireLink()
{
    if (link == nullptr)
        return;

    link->onMessage = [this](const std::string& body) { handleMessage(body); };
    link->onDisconnected = [this] { handleLinkDropped(); };
}

void DaemonClient::handleLinkDropped()
{
    // A remote host reports the drop itself, with the reason (RemoteHosts).
    if (keepsPanesWhenLinkDrops)
        return;

    // The local daemon vanished mid-flight; every pane's shell is gone from
    // our point of view.
    auto pending = std::move(routes);
    routes.clear();

    for (auto& [id, route]: pending)
        route.routes.onExit();
}

void DaemonClient::reportLinkLost(const std::string& reason)
{
    for (auto& [id, route]: routes)
        route.routes.onLinkLost(reason);
}

void DaemonClient::replaceLink(std::unique_ptr<DaemonLink> linkToUse)
{
    link = std::move(linkToUse);
    wireLink();
}

void DaemonClient::reattachPanes()
{
    for (const auto& [id, route]: routes)
        sendSpawn(route.request);
}

bool DaemonClient::isConnected() const
{
    return link != nullptr && link->isConnected();
}

void DaemonClient::send(const std::string& message)
{
    if (isConnected())
        link->send(message);
}

void DaemonClient::handleMessage(const std::string& body)
{
    auto message = proto::parse(body);

    if (message.verb == "info")
        return handleInfo(message.payload);

    if (!isPaneMessage(message.verb))
        return onOtherMessage(message.verb, body);

    if (message.args.empty())
        return;

    if (message.verb == "output")
        handleOutput(message.args[0], std::move(message.payload));
    else if (message.verb == "state")
        handleState(message.args[0], message.payload);
    else
        handleExit(message.args[0]);
}

void DaemonClient::handleOutput(const std::string& id, std::string payload)
{
    if (auto found = routes.find(id); found != routes.end())
        found->second.routes.onOutput(std::move(payload));
}

void DaemonClient::handleState(const std::string& id, const std::string& payload)
{
    if (auto found = routes.find(id); found != routes.end())
        found->second.routes.onState(payload);
}

void DaemonClient::handleExit(const std::string& id)
{
    if (auto found = routes.find(id); found != routes.end())
    {
        auto route = std::move(found->second);
        routes.erase(found);
        route.routes.onExit();
    }
}

// One "<id>\t<foreground process>\t<working directory>[\t<session>]" line per shell.
void DaemonClient::handleInfo(const std::string& payload)
{
    auto previous = std::move(info);
    info.clear();
    auto start = std::size_t {0};

    while (start < payload.size())
    {
        const auto end = std::min(payload.find('\n', start), payload.size());
        readInfoLine(payload.substr(start, end - start));
        start = end + 1;
    }

    if (info != previous)
        onInfoChanged();
}

void DaemonClient::readInfoLine(const std::string& line)
{
    const auto firstTab = line.find('\t');
    const auto secondTab = line.find('\t', firstTab + 1);

    if (firstTab == std::string::npos || secondTab == std::string::npos)
        return;

    const auto thirdTab = line.find('\t', secondTab + 1);
    auto& entry = info[line.substr(0, firstTab)];
    entry.foregroundProcess = line.substr(firstTab + 1, secondTab - firstTab - 1);
    entry.workingDirectory = line.substr(secondTab + 1, thirdTab - secondTab - 1);

    if (thirdTab != std::string::npos)
        entry.sessionName = line.substr(thirdTab + 1);
}

void DaemonClient::requestInfo()
{
    if (!routes.empty())
        send(proto::make("info"));
}

void DaemonClient::sendSpawn(const SpawnRequest& request)
{
    send(proto::make("spawn",
                     {request.id,
                      toString(request.size.cols),
                      toString(request.size.rows),
                      proto::encodeArgument(request.sessionName)},
                     request.workingDirectory));
}

void DaemonClient::spawn(const SpawnRequest& request, Routes routesToUse)
{
    routes[request.id] = {std::move(routesToUse), request};
    sendSpawn(request);
}

void DaemonClient::write(const std::string& id, std::string_view data)
{
    if (isConnected())
        link->send(proto::make("write", {id}, data));
}

void DaemonClient::resize(const std::string& id, const PtySize& size)
{
    if (auto found = routes.find(id); found != routes.end())
        found->second.request.size = size;

    send(proto::make("resize", {id, toString(size.cols), toString(size.rows)}));
}

void DaemonClient::kill(const std::string& id)
{
    routes.erase(id);
    send(proto::make("kill", {id}));
}

void DaemonClient::release(const std::string& id)
{
    routes.erase(id);
    send(proto::make("release", {id}));
}

DaemonClient::ShellInfo DaemonClient::infoFor(const std::string& id) const
{
    if (auto found = info.find(id); found != info.end())
        return found->second;

    return {};
}

void DaemonClient::killServer()
{
    routes.clear();
    send(proto::make("quit-server"));
}

// ---- DaemonShell -----------------------------------------------------------

namespace
{
using FindDaemon = std::function<DaemonClient*()>;

// A pane's shell living in a daemon, here or on another machine. The pane
// talks to it exactly like a local PTY; detach() leaves the process running
// for the next attach.
class DaemonShell final : public Shell
{
public:
    DaemonShell(std::string idToUse, std::string sessionNameToUse, FindDaemon findDaemonToUse)
        : id(std::move(idToUse))
        , sessionName(std::move(sessionNameToUse))
        , findDaemon(std::move(findDaemonToUse))
    {
    }

    ~DaemonShell() override { detach(); }

    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutput,
               std::function<void()> onExit) override
    {
        auto* client = findDaemon();

        if (client == nullptr)
            return false;

        auto routes = DaemonClient::Routes {};
        routes.onOutput = onOutput;
        routes.onState = onRestoreState;
        routes.onExit = std::move(onExit);
        routes.onLinkLost = [onOutput](const std::string& reason)
        { onOutput(describeLinkLost(reason)); };

        client->spawn({id, options.size, options.workingDirectory, sessionName},
                      std::move(routes));
        attached = true;
        return true;
    }

    void write(std::string_view data) override
    {
        if (auto* client = findDaemon())
            client->write(id, data);
    }

    void resize(const PtySize& size) override
    {
        if (auto* client = findDaemon())
            client->resize(id, size);
    }

    std::string foregroundProcess() const override
    {
        if (auto* client = findDaemon())
            return client->infoFor(id).foregroundProcess;

        return {};
    }

    std::string currentWorkingDirectory() const override
    {
        if (auto* client = findDaemon())
            return client->infoFor(id).workingDirectory;

        return {};
    }

    void terminate() override
    {
        if (attached)
        {
            attached = false;

            if (auto* client = findDaemon())
                client->kill(id);
        }
    }

    void detach() override
    {
        if (attached)
        {
            attached = false;

            if (auto* client = findDaemon())
                client->release(id);
        }
    }

private:
    // Drawn into the pane; the redraw on re-attach replaces it.
    static std::string describeLinkLost(const std::string& reason)
    {
        return "\r\n\x1b[7m cowterm: " + reason + " \x1b[0m\r\n";
    }

    std::string id;
    std::string sessionName;
    FindDaemon findDaemon;
    bool attached = false;
};

std::unique_ptr<Shell> makeLocalShell(const std::string& shellId)
{
    auto* client = DaemonClient::get();

    if (client != nullptr && client->isConnected())
        return std::make_unique<DaemonShell>(shellId, std::string {}, [] { return DaemonClient::get(); });

    return std::make_unique<LocalShell>();
}

std::unique_ptr<Shell> makeRemoteShell(const std::string& shellId,
                                       const std::string& host,
                                       const std::string& sessionName)
{
    return std::make_unique<DaemonShell>(
        shellId, sessionName, [host] { return RemoteHosts::findDaemon(host); });
}
} // namespace

TerminalConfig terminalConfig(const AppConfig& config,
                              const std::string& host,
                              const std::string& sessionName)
{
    auto result = TerminalConfig {};
    result.font = config.font;
    result.fontSize = config.fontSize;
    result.theme = themeByName(config.theme);

    result.makeShell = [host, sessionName](const std::string& shellId)
    {
        if (host.empty())
            return makeLocalShell(shellId);

        return makeRemoteShell(shellId, host, sessionName);
    };

    result.environmentForShell = [](const std::string& shellId)
    {
        return std::vector<EnvironmentVariable> {
            {proto::paneEnvironmentVariable, shellId}};
    };

    return result;
}
} // namespace term
