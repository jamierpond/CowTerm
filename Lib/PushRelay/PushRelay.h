#pragma once

#include "ApnsDelivery.h"
#include "ApnsProviderToken.h"

#include <Push/PushNotice.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// `ct push-relay`'s work, without the HTTP server around it: phones register
// their device tokens, daemons hand in notices, and each notice goes to every
// registered phone through Apple, with Apple's answer in the log.
namespace term::push
{
struct RegisteredDevice
{
    std::string name;
    std::string token;
    std::string environment;

    MIRO_REFLECT(name, token, environment)
};

struct PushRelaySettings
{
    ApnsSigningKey signingKey;

    // The app's bundle id.
    std::string topic;
    std::string soundName;
    std::filesystem::path deviceFile;
    std::chrono::milliseconds firstRetryDelay {std::chrono::seconds {5}};
};

// What the relay does to the world, so tests can stand in for Apple and the clock.
struct PushRelayServices
{
    std::function<eacp::HTTP::Response(const eacp::HTTP::Request&)> sendToApple;
    std::function<std::int64_t()> getSecondsSinceEpoch;
    std::function<void(std::chrono::milliseconds)> wait;
    std::function<void(const std::string&)> log;
};

struct RelayAnswer
{
    int statusCode = 0;
    std::string text;
};

class PushRelay
{
public:
    PushRelay(PushRelaySettings settingsToUse, PushRelayServices servicesToUse);

    // Body: {"name":"pond","token":"<hex>","environment":"sandbox"}.
    RelayAnswer registerDevice(const std::string& body);

    // Body: a PushNotice as JSON. Answers once every phone has been tried.
    RelayAnswer deliverNotice(const std::string& body);

    std::vector<RegisteredDevice> getDevices() const;

private:
    enum class DeliveryResult
    {
        delivered,
        notDelivered,
    };

    DeliveryResult deliverToDevice(const PushNotice& notice, const RegisteredDevice& device);
    std::optional<std::string> getProviderToken(bool mustRenew);
    bool isProviderTokenFresh(std::int64_t nowSeconds) const;
    void forgetDevice(const RegisteredDevice& device);
    void saveDevices() const;

    PushRelaySettings settings;
    PushRelayServices services;
    mutable std::mutex lock;
    std::vector<RegisteredDevice> devices;
    std::string providerToken;
    std::int64_t providerTokenIssuedAtSeconds = 0;
};

std::vector<RegisteredDevice> readRegisteredDevices(const std::filesystem::path& deviceFile);
} // namespace term::push
