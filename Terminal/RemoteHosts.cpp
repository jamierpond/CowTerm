#include "RemoteHosts.h"

#include "HostColours.h"
#include "Protocol.h"
#include "RemoteLocation.h"
#include "SshBridgeLink.h"

#include <algorithm>

namespace term
{
using namespace eacp;

namespace
{
constexpr auto firstReconnectDelay = Time::MS {1000};
constexpr auto longestReconnectDelay = Time::MS {30000};
constexpr int backoffDoublingLimit = 5;

Time::MS calculateReconnectDelay(int failedAttempts)
{
    const auto doublings = std::min(failedAttempts, backoffDoublingLimit);
    const auto delay = firstReconnectDelay.count << doublings;
    return Time::MS {std::min<std::int64_t>(delay, longestReconnectDelay.count)};
}

std::map<std::string, std::unique_ptr<RemoteHost>>& getHosts()
{
    static auto hosts = std::map<std::string, std::unique_ptr<RemoteHost>> {};
    return hosts;
}

std::vector<std::string> splitLines(const std::string& text)
{
    auto lines = std::vector<std::string> {};
    auto start = std::size_t {0};

    while (start < text.size())
    {
        const auto end = std::min(text.find('\n', start), text.size());

        if (end > start)
            lines.push_back(text.substr(start, end - start));

        start = end + 1;
    }

    return lines;
}
} // namespace

RemoteHost::RemoteHost(std::string hostToUse, std::vector<std::string> searchPathsToUse)
    : host(std::move(hostToUse))
    , searchPaths(std::move(searchPathsToUse))
    , daemon(std::make_unique<DaemonClient>(nullptr, true))
{
    daemon->onOtherMessage = [this](const std::string& verb, const std::string& body)
    { handleOtherMessage(verb, body); };

    connect();
}

RemoteHost::~RemoteHost()
{
    *alive = false;
}

void RemoteHost::connect()
{
    reconnectTimer.reset();
    status = Status::Connecting;

    auto link = std::make_unique<SshBridgeLink>(host);
    link->onReady = [this] { handleConnected(); };
    link->onFailed = [this](const std::string& reason) { handleFailed(reason); };
    daemon->replaceLink(std::move(link));
}

void RemoteHost::handleConnected()
{
    status = Status::Connected;
    failedAttempts = 0;
    failureReason.clear();
    daemon->reattachPanes();
    requestScan();
    requestLayouts();
    onChanged();
}

void RemoteHost::handleFailed(const std::string& reason)
{
    status = Status::Unreachable;
    failureReason = reason;
    ++failedAttempts;
    daemon->reportLinkLost(getHostLabel(host) + ": " + reason + "; reconnecting…");
    onChanged();
    scheduleReconnect();
}

void RemoteHost::scheduleReconnect()
{
    reconnectTimer = std::make_unique<Threads::Timer>(
        [this, guard = std::weak_ptr<bool> {alive}]
        {
            if (!guard.expired())
                Threads::callAsync([this, guard]
                                   {
                                       if (!guard.expired())
                                           connect();
                                   });
        },
        calculateReconnectDelay(failedAttempts));
}

void RemoteHost::requestScan()
{
    auto payload = std::string {};

    for (const auto& path: searchPaths)
        payload += path + "\n";

    daemon->send(proto::make("scan", {}, payload));
}

void RemoteHost::requestLayouts()
{
    daemon->send(proto::make("layouts"));
}

void RemoteHost::handleOtherMessage(const std::string& verb, const std::string& body)
{
    const auto message = proto::parse(body);

    if (verb == "scan-result")
        readScanResult(message.payload);
    else if (verb == "layouts")
        readLayouts(message.payload);
    else if (verb == "nu-request" && !message.args.empty())
        onNotifyUserRequest(*this, message.args[0], message.payload);
    else if (verb == "nu-cancel" && !message.args.empty())
        onNotifyUserCancelled(*this, message.args[0]);
}

void RemoteHost::sendNotifyUserReply(const std::string& requestId, const std::string& reply)
{
    daemon->send(proto::make("nu-reply", {requestId}, reply));
}

void RemoteHost::readScanResult(const std::string& payload)
{
    lastScan.clear();

    for (const auto& path: splitLines(payload))
        lastScan.push_back({remote::makeRemoteProjectDir(host, path), sessionNameFor(path)});

    onChanged();
}

void RemoteHost::readLayouts(const std::string& payload)
{
    auto start = std::size_t {0};

    while (start < payload.size())
    {
        const auto end = std::min(payload.find('\0', start), payload.size());
        const auto entry = proto::parse(payload.substr(start, end - start));

        if (entry.verb == "layout" && !entry.args.empty())
            onLayout(*this, proto::decodeArgument(entry.args[0]), entry.payload);

        start = end + 1;
    }
}

namespace RemoteHosts
{
void configure(const AppConfig& config)
{
    auto pathsByHost = std::map<std::string, std::vector<std::string>> {};

    for (const auto& entry: config.searchDirs)
    {
        if (const auto remote = remote::parseRemoteSearchDir(entry))
            pathsByHost[remote->host].push_back(remote->path);
    }

    for (auto& [host, paths]: pathsByHost)
        getHosts()[host] = std::make_unique<RemoteHost>(host, paths);
}

void teardown()
{
    getHosts().clear();
}

RemoteHost* find(const std::string& host)
{
    auto& hosts = getHosts();

    if (auto found = hosts.find(host); found != hosts.end())
        return found->second.get();

    return nullptr;
}

DaemonClient* findDaemon(const std::string& host)
{
    auto* remote = find(host);
    return remote != nullptr ? &remote->getDaemon() : nullptr;
}

std::vector<RemoteHost*> all()
{
    auto result = std::vector<RemoteHost*> {};

    for (auto& [host, remote]: getHosts())
        result.push_back(remote.get());

    return result;
}
} // namespace RemoteHosts
} // namespace term
