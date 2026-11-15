#include "DaemonClient.h"
#include "Protocol.h"
#include "RemoteHosts.h"
#include "RemoteLocation.h"

#include "DaemonDialer.h"

#include "Shell/Shell.h"

#include <utility>

namespace term
{
using namespace eacp;

namespace
{
std::unique_ptr<DaemonClient> instance;

bool isPaneMessage(const std::string& verb)
{
    return verb == "output" || verb == "state" || verb == "exit" || verb == "missing-command";
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
    else if (message.verb == "missing-command")
        handleMissingCommand(message.args[0], message.payload);
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

void DaemonClient::handleMissingCommand(const std::string& id, const std::string& program)
{
    if (auto found = routes.find(id); found != routes.end())
    {
        auto route = std::move(found->second);
        routes.erase(found);
        route.routes.onMissingCommand(program);
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
                     proto::makeSpawnPayload({request.workingDirectory, request.command})));
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
    DaemonShell(std::string idToUse,
                std::string sessionNameToUse,
                std::string machineLabelToUse,
                FindDaemon findDaemonToUse)
        : id(std::move(idToUse))
        , sessionName(std::move(sessionNameToUse))
        , machineLabel(std::move(machineLabelToUse))
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
        routes.onOutput = std::move(onOutput);
        routes.onState = [restore = onRestoreState, status = onStatusChanged](const std::string& state)
        {
            status({});
            restore(state);
        };
        routes.onExit = onExit;
        routes.onLinkLost = onStatusChanged;
        routes.onMissingCommand = [this, onOutput](const std::string& program)
        {
            closeOnNextKey = std::move(exitAfterNotice);
            onOutput(describeMissingCommand(program));
        };
        exitAfterNotice = std::move(onExit);

        client->spawn({id, options.size, options.workingDirectory, sessionName, options.command},
                      std::move(routes));
        attached = true;
        return true;
    }

    void write(std::string_view data) override
    {
        if (closeOnNextKey)
        {
            attached = false;
            return std::exchange(closeOnNextKey, nullptr)();
        }

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
    std::string describeMissingCommand(const std::string& program) const
    {
        return "\r\n  " + program + " is not installed on " + machineLabel
               + " (not found on its PATH).\r\n\r\n  Press any key to close.";
    }

    std::string id;
    std::string sessionName;
    std::string machineLabel;
    FindDaemon findDaemon;
    std::function<void()> exitAfterNotice;
    std::function<void()> closeOnNextKey;
    bool attached = false;
};
} // namespace

std::unique_ptr<Shell> makeLocalShell(const std::string& shellId)
{
    auto* client = DaemonClient::get();

    if (client != nullptr && client->isConnected())
        return std::make_unique<DaemonShell>(
            shellId, std::string {}, std::string {"this machine"}, [] { return DaemonClient::get(); });

    return std::make_unique<LocalShell>();
}

std::unique_ptr<Shell> makeRemoteShell(const std::string& shellId,
                                       const std::string& host,
                                       const std::string& sessionName)
{
    return std::make_unique<DaemonShell>(shellId,
                                         sessionName,
                                         remote::getHostLabel(host),
                                         [host] { return RemoteHosts::findDaemon(host); });
}
} // namespace term
