#pragma once

#include "BridgeLink.h"
#include "RemoteLocation.h"

#include <memory>
#include <optional>
#include <string>

namespace term
{
struct EmbeddedSshConnection;

inline constexpr int defaultSshPort = 22;

// Where to ssh: "user@host", "user@host:port", or "user@host=address[:port]"
// for a machine known by one name and reached at another address
// ("jamiepond@tamby=jamies-mac-mini.tail8acf23.ts.net").
struct SshDestination
{
    std::string user;

    // The machine's name, which sessions, host colours and frecency key on.
    std::string host;
    int port = defaultSshPort;

    // What to dial and check the host key against; empty dials the name.
    std::string address;
};

// Empty when text has no user, no host, or a port that is not a number.
std::optional<SshDestination> parseSshDestination(const std::string& text);

// The address when there is one, else the name.
const std::string& getAddressToDial(const SshDestination& destination);

// What the link authenticates and checks the host with. The app reads these
// at its edge; the link never looks at the environment or the home folder.
struct SshCredentials
{
    // An ssh agent's socket (SSH_AUTH_SOCK); empty skips the agent.
    std::string agentSocketPath;

    // A private key file, tried after the agent; empty skips it.
    std::string privateKeyPath;

    // The OpenSSH known_hosts file the host's key must be listed in.
    std::string knownHostsPath;

    // The private key's public half ("<key>.pub"). libssh2 on mbedTLS can
    // derive it only from an RSA key, so an ECDSA key file needs it; empty
    // lets libssh2 derive it.
    std::string publicKeyPath;
};

// A remote session daemon reached by running `ct bridge` on an ssh exec
// channel opened inside this process (libssh2), for platforms that cannot
// start the system ssh. Its own thread does all the ssh work and hands the
// channel's bytes to the main thread.
class EmbeddedSshLink final : public BridgeLink
{
public:
    // remoteCommand runs `ct bridge` there; a test instance passes its own.
    EmbeddedSshLink(const SshDestination& destination,
                    const SshCredentials& credentials,
                    const std::string& remoteCommand = remote::bridgeCommand);
    ~EmbeddedSshLink() override;

private:
    void writeBytes(const std::string& bytes) override;
    void stopTransport() override;
    void handleEnd(int exitCode, const std::string& errorOutput);

    std::shared_ptr<EmbeddedSshConnection> connection;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term
