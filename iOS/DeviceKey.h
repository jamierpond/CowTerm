#pragma once

#include "Multiplexer/EmbeddedSshLink.h"

#include <optional>
#include <string>

namespace cowterm::mobile
{
// The phone's own ssh key: ECDSA P-256 in the iOS keychain, made the first
// time it is asked for. On a phone it lives in the Secure Enclave and never
// leaves it; the simulator has no Secure Enclave, so there it is an ordinary
// keychain key. Either way it is readable only after the phone's first unlock
// and never moves to another device.
struct DeviceKey
{
    term::SshSigningKey signingKey;
    bool isInSecureEnclave = false;
};

// Empty, with the keychain's reason logged, when the key cannot be made.
std::optional<DeviceKey> loadOrCreateDeviceKey();
} // namespace cowterm::mobile
