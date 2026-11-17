#include "PushRelay.h"

#include <Push/ApnsPayload.h>

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>

namespace term::push
{
namespace
{
constexpr auto providerTokenLifetimeSeconds = std::int64_t {40 * 60};
constexpr auto maximumDeliveryAttempts = 3;
constexpr auto okStatus = 200;
constexpr auto badRequestStatus = 400;
constexpr auto badGatewayStatus = 502;
constexpr auto shortestDeviceTokenHexDigits = std::size_t {64};
constexpr auto longestDeviceTokenHexDigits = std::size_t {200};

struct DeviceList
{
    std::vector<RegisteredDevice> devices;

    MIRO_REFLECT(devices)
};

bool isHexDigit(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')
           || (character >= 'A' && character <= 'F');
}

bool isPlausibleDeviceToken(const std::string& token)
{
    return token.size() >= shortestDeviceTokenHexDigits
           && token.size() <= longestDeviceTokenHexDigits && std::ranges::all_of(token, isHexDigit);
}

std::string findProblemWithRegistration(const RegisteredDevice& device)
{
    if (device.name.empty())
        return "the device has no name";

    if (!isPlausibleDeviceToken(device.token))
        return "the device token is not a hex string of 64 to 200 digits";

    if (!readApnsEnvironment(device.environment))
        return "the environment is neither sandbox nor production";

    return {};
}

std::string describeDevice(const RegisteredDevice& device)
{
    return device.name + " (" + device.environment + ", token ending "
           + device.token.substr(device.token.size() - 8) + ")";
}
} // namespace

std::vector<RegisteredDevice> readRegisteredDevices(const std::filesystem::path& deviceFile)
{
    const auto text = eacp::Files::readFile(eacp::FilePath {deviceFile.string()});
    return Miro::createFromJSONString<DeviceList>(text).devices;
}

PushRelay::PushRelay(PushRelaySettings settingsToUse, PushRelayServices servicesToUse)
    : settings(std::move(settingsToUse))
    , services(std::move(servicesToUse))
    , devices(readRegisteredDevices(settings.deviceFile))
{
}

RelayAnswer PushRelay::registerDevice(const std::string& body)
{
    const auto device = Miro::createFromJSONString<RegisteredDevice>(body);

    if (const auto problem = findProblemWithRegistration(device); !problem.empty())
    {
        services.log("registration refused: " + problem);
        return {badRequestStatus, problem};
    }

    auto guard = std::scoped_lock {lock};
    std::erase_if(devices, [&](const auto& known) { return known.name == device.name; });
    devices.push_back(device);
    saveDevices();
    services.log("registered " + describeDevice(device));
    return {okStatus, "registered"};
}

RelayAnswer PushRelay::deliverNotice(const std::string& body)
{
    const auto notice = readPushNoticeJson(body);

    if (!notice)
    {
        services.log("notice refused: it lacks a host, a pane or a known event");
        return {badRequestStatus, "a notice needs host, paneId and event finished or waiting"};
    }

    auto guard = std::scoped_lock {lock};
    services.log("notice from " + getNotificationTitle(*notice) + " pane " + notice->paneId + ": "
                 + notice->event + " \"" + getNotificationBody(*notice) + "\"");

    if (devices.empty())
    {
        services.log("no phone has registered; nothing sent");
        return {okStatus, "no phone has registered"};
    }

    auto deliveredCount = 0;
    const auto devicesToTry = devices;

    for (const auto& device: devicesToTry)
    {
        if (deliverToDevice(*notice, device) == DeliveryResult::delivered)
            ++deliveredCount;
    }

    const auto summary = "delivered to " + std::to_string(deliveredCount) + " of "
                         + std::to_string(devicesToTry.size()) + " phones";

    if (deliveredCount == 0)
        return {badGatewayStatus, summary};

    return {okStatus, summary};
}

std::vector<RegisteredDevice> PushRelay::getDevices() const
{
    auto guard = std::scoped_lock {lock};
    return devices;
}

PushRelay::DeliveryResult PushRelay::deliverToDevice(const PushNotice& notice,
                                                     const RegisteredDevice& device)
{
    const auto environment = readApnsEnvironment(device.environment);
    auto mustRenewProviderToken = false;
    auto retryDelay = settings.firstRetryDelay;

    for (auto attempt = 1; attempt <= maximumDeliveryAttempts; ++attempt)
    {
        const auto token = getProviderToken(mustRenewProviderToken);

        if (!token)
            return DeliveryResult::notDelivered;

        const auto delivery = ApnsDelivery {device.token,
                                            *environment,
                                            settings.topic,
                                            buildApnsPayloadJson(notice, settings.soundName),
                                            *token};
        const auto response = services.sendToApple(buildApnsRequest(delivery));
        const auto result = interpretApnsResponse(response);
        services.log("push to " + describeDevice(device) + ", attempt " + std::to_string(attempt)
                     + ": Apple answered " + std::to_string(response.statusCode) + " "
                     + result.reason + ": " + result.explanation);

        switch (result.outcome)
        {
            case ApnsOutcome::delivered:
                return DeliveryResult::delivered;
            case ApnsOutcome::renewProviderTokenAndRetry:
                mustRenewProviderToken = true;
                break;
            case ApnsOutcome::retryLater:
                services.wait(retryDelay);
                retryDelay *= 2;
                break;
            case ApnsOutcome::forgetDevice:
                forgetDevice(device);
                return DeliveryResult::notDelivered;
            case ApnsOutcome::giveUp:
                return DeliveryResult::notDelivered;
        }
    }

    services.log("gave up on " + describeDevice(device) + " after "
                 + std::to_string(maximumDeliveryAttempts) + " attempts");
    return DeliveryResult::notDelivered;
}

std::optional<std::string> PushRelay::getProviderToken(bool mustRenew)
{
    const auto now = services.getSecondsSinceEpoch();

    if (!mustRenew && isProviderTokenFresh(now))
        return providerToken;

    const auto made = createApnsProviderToken(settings.signingKey, now);

    if (!made.error.empty())
    {
        services.log("cannot sign a token for Apple: " + made.error);
        return std::nullopt;
    }

    providerToken = made.token;
    providerTokenIssuedAtSeconds = now;
    return providerToken;
}

bool PushRelay::isProviderTokenFresh(std::int64_t nowSeconds) const
{
    const auto ageSeconds = nowSeconds - providerTokenIssuedAtSeconds;
    return !providerToken.empty() && ageSeconds < providerTokenLifetimeSeconds;
}

void PushRelay::forgetDevice(const RegisteredDevice& device)
{
    std::erase_if(devices, [&](const auto& known) { return known.token == device.token; });
    saveDevices();
    services.log("forgot " + describeDevice(device));
}

void PushRelay::saveDevices() const
{
    const auto text = Miro::toJSONString(DeviceList {devices}, 4);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());

    try
    {
        eacp::Files::writeFileAtomically(eacp::FilePath {settings.deviceFile.string()},
                                         {bytes, text.size()});
    }
    catch (const std::exception& error)
    {
        services.log("could not save the registered phones to " + settings.deviceFile.string()
                     + ": " + error.what());
    }
}
} // namespace term::push
