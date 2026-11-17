#include "HostPairing.h"

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>

namespace term::pairing
{
namespace
{
constexpr auto hostsFileName = "hosts";
constexpr auto knownHostsFileName = "known_hosts";
constexpr auto typingHelp = "Type it as user@computer, or user@computer:port";

std::string readFileIfPresent(const std::filesystem::path& path)
{
    auto error = std::error_code {};

    if (!std::filesystem::exists(path, error))
        return {};

    return eacp::Files::readFile(path.string());
}

std::string describeDestination(const SshDestination& destination)
{
    auto text = destination.user + "@" + destination.host;

    if (destination.port != defaultSshPort)
        text += ":" + std::to_string(destination.port);

    return text;
}

void appendLine(const std::filesystem::path& path, const std::string& line)
{
    auto error = std::error_code {};
    std::filesystem::create_directories(path.parent_path(), error);
    auto text = readFileIfPresent(path);

    if (!text.empty() && text.back() != '\n')
        text += '\n';

    eacp::Files::writeFileAtomically(path.string(), text + line + "\n");
}
} // namespace

PairedHosts::PairedHosts(std::filesystem::path folderToUse)
    : folder(std::move(folderToUse))
{
}

std::vector<std::string> PairedHosts::readHostLines() const
{
    auto lines = std::vector<std::string> {};

    for (const auto& line: eacp::Strings::split(readFileIfPresent(folder / hostsFileName), '\n'))
    {
        if (auto trimmed = eacp::Strings::trim(line); !trimmed.empty())
            lines.push_back(std::move(trimmed));
    }

    return lines;
}

std::string PairedHosts::readKnownHosts() const
{
    return readFileIfPresent(getKnownHostsPath());
}

std::filesystem::path PairedHosts::getKnownHostsPath() const
{
    return folder / knownHostsFileName;
}

bool PairedHosts::addHost(const SshDestination& destination) const
{
    const auto line = describeDestination(destination);
    const auto existing = readHostLines();

    if (std::ranges::find(existing, line) != existing.end())
        return false;

    appendLine(folder / hostsFileName, line);
    return true;
}

void PairedHosts::trustHostKey(const SshDestination& destination, const std::string& keyBlob) const
{
    appendLine(getKnownHostsPath(), makeKnownHostsLine(destination, keyBlob));
}

HostPairing::HostPairing(const PairedHosts& pairedHostsToUse)
    : pairedHosts(pairedHostsToUse)
{
}

bool HostPairing::submitHost(std::string_view typed)
{
    destination = parseTypedHost(typed);

    if (!destination)
    {
        message = typingHelp;
        return false;
    }

    message.clear();
    step = HostPairingStep::FetchingHostKey;
    return true;
}

void HostPairing::receiveHostKey(const std::string& keyBlob)
{
    if (step != HostPairingStep::FetchingHostKey || !destination)
        return;

    offeredKeyBlob = keyBlob;
    fingerprint = formatHostKeyFingerprint(keyBlob);

    switch (checkHostKey(pairedHosts.readKnownHosts(), *destination, keyBlob))
    {
        case HostKeyTrust::Known:
            return pair();
        case HostKeyTrust::Unknown:
            step = HostPairingStep::ConfirmingFingerprint;
            return;
        case HostKeyTrust::Changed:
            step = HostPairingStep::HostKeyChanged;
            return;
    }
}

void HostPairing::receiveFailure(const std::string& reason)
{
    if (step != HostPairingStep::FetchingHostKey)
        return;

    message = reason;
    step = HostPairingStep::Failed;
}

void HostPairing::confirm()
{
    if (step != HostPairingStep::ConfirmingFingerprint || !destination)
        return;

    pairedHosts.trustHostKey(*destination, offeredKeyBlob);
    pair();
}

void HostPairing::startAgain()
{
    step = HostPairingStep::EnteringHost;
    offeredKeyBlob.clear();
    fingerprint.clear();
    message.clear();
}

void HostPairing::pair()
{
    pairedHosts.addHost(*destination);
    step = HostPairingStep::Paired;
}
} // namespace term::pairing
