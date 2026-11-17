#pragma once

#include "DeviceKey.h"

#include "Phone/PhoneSettings.h"

#include <optional>
#include <string>
#include <vector>

// The app's edge for pairing: the phone's own key in the settings, the line
// a computer's authorized_keys takes, and the launch flags the pairing
// screenshots use, which a TestFlight build cannot be given.
namespace cowterm::mobile
{
// A paired setup with no key file (`just ios-pair` writes one) signs with the
// phone's keychain key instead.
std::optional<term::phone::PhoneSettings> useDeviceKeyWithoutKeyFile(std::optional<term::phone::PhoneSettings> settings,
                                                                     const std::optional<DeviceKey>& deviceKey);

// The authorized_keys line for the phone's key, or a sentence saying why
// there is none.
std::string describeDeviceKeyLine(const std::optional<DeviceKey>& deviceKey);

struct PairingLaunchFlags
{
    // --pair-with=user@host: typed and connected at launch.
    std::string hostToPairWith;

    // --pair-trust: the offered host key is trusted once shown.
    bool trustsOfferedHostKey = false;
};

PairingLaunchFlags parsePairingLaunchFlags(const std::vector<std::string>& arguments);
} // namespace cowterm::mobile
