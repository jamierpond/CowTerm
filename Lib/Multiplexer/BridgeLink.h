#pragma once

#include "DaemonLink.h"

#include "BridgeFraming.h"

#include <functional>
#include <memory>
#include <string>

namespace term
{
// A remote session daemon reached through a byte stream that runs `ct bridge`
// on host: the system ssh (SshBridgeLink) or an ssh client inside the process
// (EmbeddedSshLink). Connected once the bridge's hello arrives with a protocol
// version this app speaks. Main-thread object.
class BridgeLink : public DaemonLink
{
public:
    explicit BridgeLink(std::string hostToUse);

    bool isConnected() const override { return connected; }
    void send(const std::string& message) override;

    // The hello arrived; messages flow from here on.
    eacp::Callback onReady = [] {};

    // The link never came up, or went down: why, in words for a pane,
    // naming the fix when there is one. onDisconnected follows.
    std::function<void(const std::string& reason)> onFailed = [](const std::string&) {};

    // Any bytes at all arrived from the bridge: the link is not stalled.
    eacp::Callback onBytesReceived = [] {};

protected:
    const std::string& getHost() const { return host; }
    bool hasFinished() const { return finished; }

    // Bytes the bridge wrote, in order, on the main thread.
    void receiveBytes(const std::string& bytes);
    void fail(const std::string& reason);

private:
    virtual void writeBytes(const std::string& bytes) = 0;
    virtual void stopTransport() = 0;

    void receiveMessage(std::string message);
    void acceptHello(const bridge::BridgeHello& hello);

    std::string host;
    bridge::FrameDecoder decoder;
    bool connected = false;
    bool finished = false;
};

// Makes the link RemoteHost talks to a host through.
using BridgeLinkFactory = std::function<std::unique_ptr<BridgeLink>(const std::string& host)>;

// ssh's own exit code when ssh itself failed (no connection, auth refused).
inline constexpr int sshFailedExitCode = 255;

// What an ssh / ct bridge failure means for the user, from ssh's exit code
// and stderr. Pure, for tests.
std::string describeBridgeFailure(const std::string& host,
                                  int exitCode,
                                  const std::string& errorOutput);
} // namespace term
