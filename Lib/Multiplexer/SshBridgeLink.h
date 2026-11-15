#pragma once

#include "DaemonLink.h"

#include "BridgeFraming.h"

#include <memory>
#include <string>

namespace eacp::Processes
{
class Process;
}

namespace term
{
// A remote session daemon, reached by running `ct bridge` on host through
// the system ssh (key auth only: BatchMode, no prompts). Connected once the
// bridge's hello arrives with a protocol version this app speaks.
class SshBridgeLink final : public DaemonLink
{
public:
    explicit SshBridgeLink(std::string hostToUse);
    ~SshBridgeLink() override;

    bool isConnected() const override { return connected; }
    void send(const std::string& message) override;

    // The hello arrived; messages flow from here on.
    eacp::Callback onReady = [] {};

    // The link never came up, or went down: why, in words for a pane,
    // naming the fix when there is one. onDisconnected follows.
    std::function<void(const std::string& reason)> onFailed = [](const std::string&) {};

private:
    void receiveBytes(const std::string& bytes);
    void receiveMessage(std::string message);
    void acceptHello(const bridge::BridgeHello& hello);
    void handleExit(int exitCode, const std::string& errorOutput);
    void fail(const std::string& reason);

    std::string host;
    std::shared_ptr<eacp::Processes::Process> process;
    bridge::FrameDecoder decoder;
    bool connected = false;
    bool finished = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// What an ssh / ct bridge failure means for the user, from ssh's exit code
// and stderr. Pure, for tests.
std::string describeBridgeFailure(const std::string& host,
                                  int exitCode,
                                  const std::string& errorOutput);
} // namespace term
