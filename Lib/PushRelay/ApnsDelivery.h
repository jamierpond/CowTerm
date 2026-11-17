#pragma once

#include <eacp/Network/HTTP/Http.h>

#include <string>

// One push to one phone through Apple's push service, and what Apple's answer
// means for the relay.
namespace term::push
{
// A development build of the app gets its device token from Apple's sandbox,
// a TestFlight or App Store build from production.
enum class ApnsEnvironment
{
    sandbox,
    production,
};

struct ApnsDelivery
{
    std::string deviceToken;
    ApnsEnvironment environment = ApnsEnvironment::sandbox;
    std::string topic;
    std::string payloadJson;
    std::string providerToken;
};

eacp::HTTP::Request buildApnsRequest(const ApnsDelivery& delivery);

enum class ApnsOutcome
{
    delivered,
    renewProviderTokenAndRetry,
    forgetDevice,
    retryLater,
    giveUp,
};

struct ApnsResult
{
    ApnsOutcome outcome = ApnsOutcome::giveUp;

    // Apple's reason ("BadDeviceToken"), empty when Apple gave none.
    std::string reason;

    // What it means, in a sentence, for the relay's log.
    std::string explanation;
};

ApnsResult interpretApnsResponse(const eacp::HTTP::Response& response);

// "sandbox" or "production", as the phone names its environment.
std::optional<ApnsEnvironment> readApnsEnvironment(const std::string& name);
std::string getApnsEnvironmentName(ApnsEnvironment environment);
} // namespace term::push
