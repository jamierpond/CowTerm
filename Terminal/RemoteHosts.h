#pragma once

#include "Config.h"
#include "DaemonClient.h"
#include "Projects.h"

#include <eacp/Core/Threads/Timer.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace term
{
// One machine in searchDirs: its link (`ssh host ct bridge`), the daemon
// client riding on it, and the last project scan. Reconnects with backoff
// while anything is configured for it; main-thread object.
class RemoteHost
{
public:
    enum class Status
    {
        Connecting,
        Connected,
        Unreachable
    };

    RemoteHost(std::string hostToUse, std::vector<std::string> searchPathsToUse);
    ~RemoteHost();

    const std::string& getHost() const { return host; }
    Status getStatus() const { return status; }
    const std::string& getFailureReason() const { return failureReason; }
    DaemonClient& getDaemon() { return *daemon; }

    // The projects found the last time a scan came back; empty before.
    const std::vector<ProjectDir>& getLastScan() const { return lastScan; }

    // Asks the host to list its projects again; onChanged fires when it answers.
    void requestScan();

    // Asks for the sessions every Mac attached to this host has open.
    void requestLayouts();

    // Status, scan or layouts moved: repaint whatever shows them.
    eacp::Callback onChanged = [] {};

    // A session layout another Mac (or an earlier run) published.
    std::function<void(RemoteHost& host, const std::string& name, const std::string& layout)>
        onLayout = [](RemoteHost&, const std::string&, const std::string&) {};

private:
    void connect();
    void handleConnected();
    void handleFailed(const std::string& reason);
    void scheduleReconnect();
    void handleOtherMessage(const std::string& verb, const std::string& body);
    void readScanResult(const std::string& payload);
    void readLayouts(const std::string& payload);

    std::string host;
    std::vector<std::string> searchPaths;
    std::unique_ptr<DaemonClient> daemon;
    Status status = Status::Connecting;
    std::string failureReason;
    std::vector<ProjectDir> lastScan;
    int failedAttempts = 0;
    std::unique_ptr<eacp::Threads::Timer> reconnectTimer;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// Every remote host named in the config, made once at startup.
namespace RemoteHosts
{
void configure(const AppConfig& config);
void teardown();

RemoteHost* find(const std::string& host);
DaemonClient* findDaemon(const std::string& host);
std::vector<RemoteHost*> all();
} // namespace RemoteHosts
} // namespace term
