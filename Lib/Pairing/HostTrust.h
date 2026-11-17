#pragma once

#include "Multiplexer/EmbeddedSshLink.h"

#include <optional>
#include <string>
#include <string_view>

namespace term::pairing
{
// What a person types for a computer: "user@host" or "user@host:port", with
// spaces around it and a leading "ssh " forgiven. Empty when it is not that.
std::optional<SshDestination> parseTypedHost(std::string_view typed);

// OpenSSH's fingerprint of a wire-form key, as `ssh-keygen -l` prints it:
// "SHA256:" and the unpadded base64 of the key's SHA-256.
std::string formatHostKeyFingerprint(std::string_view keyBlob);

// The known_hosts line that trusts keyBlob for the destination: the host
// alone on port 22, "[host]:port" otherwise, as OpenSSH writes it.
std::string makeKnownHostsLine(const SshDestination& destination, std::string_view keyBlob);

enum class HostKeyTrust
{
    // known_hosts has this key for the host.
    Known,

    // known_hosts has no key of this type for the host: ask the person.
    Unknown,

    // known_hosts has a different key of this type for the host: refuse.
    Changed
};

// How known_hosts text regards the key a host offered. Hashed host names are
// skipped: the app writes plain ones.
HostKeyTrust checkHostKey(std::string_view knownHostsText,
                          const SshDestination& destination,
                          std::string_view keyBlob);
} // namespace term::pairing
