#pragma once

#include "Multiplexer/EmbeddedSshLink.h"
#include "Multiplexer/RemoteLocation.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace term::phone
{
// What the phone connects to and how. The app builds it at its edge, from its
// launch arguments and environment, and hands it to PhoneConnection.
struct PhoneSettings
{
    // The host the phone opens its first pane on.
    SshDestination destination;

    // Every host the overview lists, destination first. "--destination
    // jamie@tamby,jamiepond@macprorack" names several.
    std::vector<SshDestination> overviewDestinations;
    SshCredentials credentials;

    // The pane to open; empty lets the daemon's last-input answer choose.
    std::string explicitPaneId;

    // Where the shown pane's last screen is kept between launches.
    std::filesystem::path storedScreenPath;

    // What runs `ct bridge` on the host; a test instance passes its own.
    std::string bridgeCommand = remote::bridgeCommand;
};

using EnvironmentReader = std::function<std::string(const std::string& name)>;

// Each setting from a launch argument ("--destination jamie@tamby") or, when
// that is absent, an environment variable (COWTERM_DESTINATION), which
// `simctl launch` passes on from SIMCTL_CHILD_COWTERM_DESTINATION. Empty when
// there is no destination, or one of them is not user@host[:port].
// "user@host": how RemoteHosts and the overview name a destination.
std::string makeHostLabel(const SshDestination& destination);

// The hosts file `just ios-pair` writes, one "user@host" a line, as the
// comma-separated list --destination takes.
std::string joinPairedHosts(const std::string& hostsFileText);

std::optional<PhoneSettings> parsePhoneSettings(const std::vector<std::string>& arguments,
                                                const EnvironmentReader& readEnvironment,
                                                const std::filesystem::path& storedScreenPath);
} // namespace term::phone
