#pragma once

#include "ApnsProviderToken.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// `ct push-relay --listen <address> [--port <n>] [--devices <file>]
// [--topic <bundle id>] [--sound <name>]`: runs the relay until stopped.
namespace term::push
{
constexpr auto defaultPushRelayPort = 8742;

struct PushRelayOptions
{
    std::string listenAddress;
    int port = defaultPushRelayPort;

    // Empty: the caller's default, in CowTerm's data folder.
    std::filesystem::path deviceFile;
    std::string topic = "audio.pond.cowterm.mobile";
    std::string soundName = "default";
};

struct ParsedPushRelayOptions
{
    std::optional<PushRelayOptions> options;
    std::string usageError;
};

ParsedPushRelayOptions parsePushRelayArguments(const std::vector<std::string>& args);

// Returns ct's exit code: 1 when the key is unusable or the address cannot be
// listened on; otherwise it runs until the process is stopped.
int runPushRelay(const PushRelayOptions& options, const ApnsSigningKey& signingKey);
} // namespace term::push
