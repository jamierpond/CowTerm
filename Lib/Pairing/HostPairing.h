#pragma once

#include "HostTrust.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace term::pairing
{
// The phone's paired computers, in the folder `just ios-pair` also fills:
// "hosts", one user@host a line, and "known_hosts", OpenSSH's format.
class PairedHosts
{
public:
    explicit PairedHosts(std::filesystem::path folderToUse);

    std::vector<std::string> readHostLines() const;
    std::string readKnownHosts() const;
    std::filesystem::path getKnownHostsPath() const;

    // Appends the host's line once; true when it was not there yet.
    bool addHost(const SshDestination& destination) const;
    void trustHostKey(const SshDestination& destination, const std::string& keyBlob) const;

private:
    std::filesystem::path folder;
};

enum class HostPairingStep
{
    EnteringHost,
    FetchingHostKey,
    ConfirmingFingerprint,
    HostKeyChanged,
    Failed,
    Paired
};

// Adding one computer: what was typed, the host key it offered, the person's
// answer. A key seen before pairs at once; an unknown one waits for confirm();
// a changed one is refused. Only confirm() and a known key write anything.
class HostPairing
{
public:
    explicit HostPairing(const PairedHosts& pairedHostsToUse);

    // False, with a message, when typed is not user@host[:port].
    bool submitHost(std::string_view typed);
    void receiveHostKey(const std::string& keyBlob);
    void receiveFailure(const std::string& reason);
    void confirm();

    // Back to typing a host, from any step.
    void startAgain();

    HostPairingStep getStep() const { return step; }
    const std::optional<SshDestination>& getDestination() const { return destination; }
    const std::string& getFingerprint() const { return fingerprint; }
    const std::string& getMessage() const { return message; }

private:
    void pair();

    const PairedHosts& pairedHosts;
    HostPairingStep step = HostPairingStep::EnteringHost;
    std::optional<SshDestination> destination;
    std::string offeredKeyBlob;
    std::string fingerprint;
    std::string message;
};
} // namespace term::pairing
