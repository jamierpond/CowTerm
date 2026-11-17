#include "HostTrust.h"

#include "SshKeyEncoding.h"

#include <eacp/Core/Utils/Base64.h>
#include <eacp/Core/Utils/Sha256.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>

namespace term::pairing
{
namespace
{
constexpr auto sshCommandPrefix = std::string_view {"ssh "};
constexpr auto fingerprintPrefix = "SHA256:";
constexpr auto hashedHostMarker = '|';
constexpr auto hostKeyCheckCommandStart = "ssh-keygen -lf /etc/ssh/ssh_host_";
constexpr auto hostKeyCheckCommandEnd = "_key.pub";

struct HostKeyFileName
{
    std::string_view keyType;
    std::string_view fileKind;
};

constexpr HostKeyFileName hostKeyFileNames[] = {
    {"ssh-ed25519", "ed25519"},
    {"ecdsa-sha2-nistp256", "ecdsa"},
    {"ecdsa-sha2-nistp384", "ecdsa"},
    {"ecdsa-sha2-nistp521", "ecdsa"},
    {"ssh-rsa", "rsa"},
};

std::string_view findHostKeyFileKind(const std::string& keyType)
{
    for (const auto& entry: hostKeyFileNames)
    {
        if (entry.keyType == keyType)
            return entry.fileKind;
    }

    return "*";
}

std::string describeHostField(const SshDestination& destination)
{
    if (destination.port == defaultSshPort)
        return destination.host;

    return "[" + destination.host + "]:" + std::to_string(destination.port);
}

bool isWhitespace(char character)
{
    return character == ' ' || character == '\t';
}

constexpr auto knownHostsFieldCount = 3;

bool isCommentOrEmpty(const std::string& firstField)
{
    return firstField.empty() || firstField.front() == '#';
}

struct KnownHostsEntry
{
    std::string hosts;
    std::string keyType;
    std::string keyBase64;
};

std::optional<KnownHostsEntry> parseKnownHostsLine(const std::string& line)
{
    const auto fields = eacp::Strings::split(eacp::Strings::trim(line), ' ');

    if (fields.size() < knownHostsFieldCount || isCommentOrEmpty(fields[0]))
        return std::nullopt;

    return KnownHostsEntry {fields[0], fields[1], fields[2]};
}

bool doesEntryNameHost(const KnownHostsEntry& entry, const std::string& hostField)
{
    if (entry.hosts.front() == hashedHostMarker)
        return false;

    const auto names = eacp::Strings::split(entry.hosts, ',');
    return std::ranges::find(names, hostField) != names.end();
}

bool isEntryFor(const KnownHostsEntry& entry, const std::string& hostField, const std::string& keyType)
{
    return entry.keyType == keyType && doesEntryNameHost(entry, hostField);
}
} // namespace

std::optional<SshDestination> parseTypedHost(std::string_view typed)
{
    auto text = eacp::Strings::trim(typed);

    if (text.starts_with(sshCommandPrefix))
        text = eacp::Strings::trim(text.substr(sshCommandPrefix.size()));

    if (std::ranges::any_of(text, isWhitespace))
        return std::nullopt;

    return parseSshDestination(text);
}

std::string formatHostKeyFingerprint(std::string_view keyBlob)
{
    auto hash = eacp::Sha256 {};
    hash.update(keyBlob);
    const auto digest = hash.finish();
    const auto digestBytes = std::string_view {reinterpret_cast<const char*>(digest.data()), digest.size()};

    auto encoded = eacp::Base64::encode(digestBytes);
    std::erase(encoded, '=');
    return fingerprintPrefix + encoded;
}

std::string makeHostKeyCheckCommand(std::string_view keyBlob)
{
    const auto fileKind = findHostKeyFileKind(readKeyTypeName(keyBlob));
    return hostKeyCheckCommandStart + std::string {fileKind} + hostKeyCheckCommandEnd;
}

std::string makeKnownHostsLine(const SshDestination& destination, std::string_view keyBlob)
{
    return describeHostField(destination) + " " + readKeyTypeName(keyBlob) + " " + eacp::Base64::encode(keyBlob);
}

HostKeyTrust checkHostKey(std::string_view knownHostsText,
                          const SshDestination& destination,
                          std::string_view keyBlob)
{
    const auto hostField = describeHostField(destination);
    const auto keyType = readKeyTypeName(keyBlob);
    const auto keyBase64 = eacp::Base64::encode(keyBlob);
    auto trust = HostKeyTrust::Unknown;

    for (const auto& line: eacp::Strings::split(knownHostsText, '\n'))
    {
        const auto entry = parseKnownHostsLine(line);

        if (!entry || !isEntryFor(*entry, hostField, keyType))
            continue;

        if (entry->keyBase64 == keyBase64)
            return HostKeyTrust::Known;

        trust = HostKeyTrust::Changed;
    }

    return trust;
}
} // namespace term::pairing
