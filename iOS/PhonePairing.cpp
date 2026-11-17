#include "PhonePairing.h"

#include "Pairing/SshKeyEncoding.h"

#include <string_view>

namespace cowterm::mobile
{
namespace
{
// Names the phone in each computer's authorized_keys, so it can be found and removed.
constexpr auto deviceKeyComment = "cowterm-iphone";
constexpr auto noDeviceKey = "This phone could not make its ssh key in the keychain, so it cannot sign in anywhere.";
constexpr auto pairWithPrefix = std::string_view {"--pair-with="};
constexpr auto pairTrustFlag = std::string_view {"--pair-trust"};
} // namespace

std::optional<term::phone::PhoneSettings> useDeviceKeyWithoutKeyFile(std::optional<term::phone::PhoneSettings> settings,
                                                                     const std::optional<DeviceKey>& deviceKey)
{
    if (!settings || !deviceKey)
        return settings;

    if (settings->credentials.privateKeyPath.empty())
        settings->credentials.signingKey = deviceKey->signingKey;

    return settings;
}

std::string describeDeviceKeyLine(const std::optional<DeviceKey>& deviceKey)
{
    if (!deviceKey)
        return noDeviceKey;

    return term::pairing::makeAuthorizedKeysLine(deviceKey->signingKey.publicKeyBlob, deviceKeyComment);
}

PairingLaunchFlags parsePairingLaunchFlags(const std::vector<std::string>& arguments)
{
    auto flags = PairingLaunchFlags {};

    for (const auto& argument: arguments)
    {
        if (argument.starts_with(pairWithPrefix))
            flags.hostToPairWith = argument.substr(pairWithPrefix.size());
        else if (argument == pairTrustFlag)
            flags.trustsOfferedHostKey = true;
    }

    return flags;
}
} // namespace cowterm::mobile
