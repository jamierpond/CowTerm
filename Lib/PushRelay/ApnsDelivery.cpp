#include "ApnsDelivery.h"

#include <Miro/Reflect.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>

namespace term::push
{
namespace
{
constexpr auto sandboxAddress = "https://api.sandbox.push.apple.com";
constexpr auto productionAddress = "https://api.push.apple.com";
constexpr auto deviceTokenPath = "/3/device/";
constexpr auto alertPushType = "alert";
constexpr auto sendImmediatelyPriority = "10";
constexpr auto requestTimeout = std::chrono::milliseconds {std::chrono::seconds {15}};
constexpr auto deliveredStatus = 200;
constexpr auto goneStatus = 410;
constexpr auto tooManyRequestsStatus = 429;
constexpr auto firstServerErrorStatus = 500;
constexpr auto noStatus = 0;

struct KnownReason
{
    std::string_view reason;
    ApnsOutcome outcome;
    std::string_view explanation;
};

constexpr auto knownReasons = std::array {
    KnownReason {"ExpiredProviderToken",
                 ApnsOutcome::renewProviderTokenAndRetry,
                 "the signed token is more than an hour old; signing a new one and sending again"},
    KnownReason {"InvalidProviderToken",
                 ApnsOutcome::giveUp,
                 "Apple refused the signed token: check the key id, the team id and the key"},
    KnownReason {"MissingProviderToken", ApnsOutcome::giveUp, "the request carried no signed token"},
    KnownReason {"TooManyProviderTokenUpdates",
                 ApnsOutcome::retryLater,
                 "the relay signed new tokens too often; trying again later"},
    KnownReason {"BadDeviceToken",
                 ApnsOutcome::forgetDevice,
                 "Apple does not know this device token in this environment (a development "
                 "build's token sent to production, or the reverse); forgetting it until the "
                 "phone registers again"},
    KnownReason {"Unregistered",
                 ApnsOutcome::forgetDevice,
                 "the app was deleted from the phone or turned notifications off; forgetting "
                 "the token"},
    KnownReason {"ExpiredToken",
                 ApnsOutcome::forgetDevice,
                 "the device token has expired; forgetting it until the phone registers again"},
    KnownReason {"DeviceTokenNotForTopic",
                 ApnsOutcome::forgetDevice,
                 "the device token belongs to another app; forgetting it"},
    KnownReason {"BadTopic", ApnsOutcome::giveUp, "Apple does not accept this app's bundle id as the topic"},
    KnownReason {"TopicDisallowed", ApnsOutcome::giveUp, "Apple does not allow pushes to this topic"},
    KnownReason {"PayloadTooLarge", ApnsOutcome::giveUp, "the notification is over Apple's size limit"},
    KnownReason {"TooManyRequests",
                 ApnsOutcome::retryLater,
                 "too many pushes to this phone in a short time; trying again later"},
    KnownReason {"InternalServerError", ApnsOutcome::retryLater, "Apple's server failed; trying again later"},
    KnownReason {"ServiceUnavailable", ApnsOutcome::retryLater, "Apple's service is unavailable; trying again later"},
    KnownReason {"Shutdown", ApnsOutcome::retryLater, "Apple's server is shutting down; trying again later"},
};

struct EnvironmentName
{
    ApnsEnvironment environment;
    std::string_view name;
    std::string_view address;
};

constexpr auto environmentNames = std::array {
    EnvironmentName {ApnsEnvironment::sandbox, "sandbox", sandboxAddress},
    EnvironmentName {ApnsEnvironment::production, "production", productionAddress},
};

const EnvironmentName& findEnvironmentName(ApnsEnvironment environment)
{
    return *std::ranges::find(environmentNames, environment, &EnvironmentName::environment);
}

ApnsResult interpretUnknownReason(int statusCode, const std::string& reason)
{
    const auto statusText = "Apple answered " + std::to_string(statusCode);

    if (statusCode == goneStatus)
        return {ApnsOutcome::forgetDevice, reason, statusText + "; forgetting the device token"};

    if (statusCode == tooManyRequestsStatus || statusCode >= firstServerErrorStatus)
        return {ApnsOutcome::retryLater, reason, statusText + "; trying again later"};

    return {ApnsOutcome::giveUp, reason, statusText + " " + reason + "; not sending this one"};
}
} // namespace

eacp::HTTP::Request buildApnsRequest(const ApnsDelivery& delivery)
{
    const auto address = std::string {findEnvironmentName(delivery.environment).address};
    auto request = eacp::HTTP::Request::post(address + deviceTokenPath + delivery.deviceToken,
                                             delivery.payloadJson);
    request.headers["authorization"] = "bearer " + delivery.providerToken;
    request.headers["apns-topic"] = delivery.topic;
    request.headers["apns-push-type"] = alertPushType;
    request.headers["apns-priority"] = sendImmediatelyPriority;
    request.headers["content-type"] = "application/json";
    request.timeout = eacp::Time::MS {requestTimeout.count()};
    request.followRedirects = false;
    return request;
}

namespace
{
struct ApnsErrorBody
{
    std::string reason;

    MIRO_REFLECT(reason)
};

} // namespace

ApnsResult interpretApnsResponse(const eacp::HTTP::Response& response)
{
    if (response.statusCode == deliveredStatus)
        return {ApnsOutcome::delivered, {}, "Apple accepted the notification"};

    if (response.statusCode == noStatus)
        return {ApnsOutcome::retryLater, {}, "could not reach Apple (" + response.error + "); trying again later"};

    const auto reason = Miro::createFromJSONString<ApnsErrorBody>(response.content).reason;
    const auto found = std::ranges::find(knownReasons, reason, &KnownReason::reason);

    if (found == knownReasons.end())
        return interpretUnknownReason(response.statusCode, reason);

    return {found->outcome, reason, std::string {found->explanation}};
}

std::optional<ApnsEnvironment> readApnsEnvironment(const std::string& name)
{
    const auto found = std::ranges::find(environmentNames, name, &EnvironmentName::name);

    if (found == environmentNames.end())
        return std::nullopt;

    return found->environment;
}

std::string getApnsEnvironmentName(ApnsEnvironment environment)
{
    return std::string {findEnvironmentName(environment).name};
}
} // namespace term::push
