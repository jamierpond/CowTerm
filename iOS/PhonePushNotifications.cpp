#include "PhonePushNotifications.h"

#include <eacp/Core/App/Alert.h>
#include <eacp/Core/Notifications/Notifications.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Network/HTTP/HttpAsync.h>

#include <chrono>

namespace cowterm::mobile
{
namespace
{
namespace Notifications = eacp::Notifications;
using eacp::LOG;

constexpr auto devicesPath = "/devices";
constexpr auto relayAnswerTimeout = std::chrono::milliseconds {std::chrono::seconds {15}};
constexpr auto allowButton = 0;
constexpr auto notNowButton = 1;

eacp::Apps::AlertOptions makeExplanation()
{
    auto options = eacp::Apps::AlertOptions {};
    options.title = "Ring this phone when a Claude needs you?";
    options.message = "When a Claude on one of your machines finishes or asks you something while "
                      "you are away from the desk, CowTerm can show it on the lock screen with a "
                      "sound. Tapping it opens that pane. iOS asks you next.";
    options.buttons = {"Allow", "Not now"};
    options.defaultButton = allowButton;
    options.cancelButton = notNowButton;
    return options;
}
} // namespace

PhonePushNotifications::PhonePushNotifications(PushRegistrationSettings settingsToUse)
    : settings(std::move(settingsToUse))
{
}

void PhonePushNotifications::start()
{
    Notifications::setOpenedHandler(
        [this](const Notifications::ReceivedNotification& notification)
        {
            const auto target = term::phone::readNotificationTarget(notification.userInfo);
            LOG("push: tapped \"", notification.title, "\"");

            if (target)
                onOpened(*target);
        });

    if (settings.relayAddress.empty())
        return LOG("push: no relay set up, so no notifications are asked for");

    Notifications::queryAuthorizationStatus(
        [this](Notifications::AuthorizationStatus status)
        {
            if (status == Notifications::AuthorizationStatus::notDetermined)
                return explainThenAsk();

            if (status == Notifications::AuthorizationStatus::denied)
                return LOG("push: notifications are off for CowTerm in Settings");

            registerWithApple();
        });
}

void PhonePushNotifications::explainThenAsk()
{
    eacp::Apps::showAlert(makeExplanation(),
                          [this](int button)
                          {
                              if (button == allowButton)
                                  return askIos();

                              LOG("push: not now; asking again on the next launch");
                          });
}

void PhonePushNotifications::askIos()
{
    Notifications::requestAuthorization(
        [this](bool isGranted)
        {
            if (!isGranted)
                return LOG("push: notifications not allowed");

            registerWithApple();
        });
}

void PhonePushNotifications::registerWithApple()
{
    auto handlers = Notifications::RemoteRegistrationHandlers {};
    handlers.onRegistered = [this](const std::string& deviceToken) { sendTokenToRelay(deviceToken); };
    handlers.onFailed = [](const std::string& reason)
    { LOG("push: Apple gave no device token: ", reason); };
    Notifications::registerForRemoteNotifications(handlers);
}

void PhonePushNotifications::sendTokenToRelay(const std::string& deviceToken)
{
    auto request = eacp::HTTP::Request::post(
        settings.relayAddress + devicesPath,
        term::phone::makeDeviceRegistrationJson(settings.deviceName, deviceToken, settings.environment));
    request.headers["content-type"] = "application/json";
    request.timeout = eacp::Time::MS {relayAnswerTimeout.count()};

    eacp::HTTP::asyncRequest(request,
                             [relay = settings.relayAddress](const eacp::HTTP::Response& response)
                             {
                                 LOG("push: registered with ",
                                     relay,
                                     ": ",
                                     response.statusCode,
                                     " ",
                                     response.content,
                                     response.error);
                             });
}
} // namespace cowterm::mobile
