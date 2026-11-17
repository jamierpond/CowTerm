#pragma once

#include "Phone/PushTarget.h"

#include <functional>
#include <string>

namespace cowterm::mobile
{
struct PushRegistrationSettings
{
    std::string relayAddress;
    std::string deviceName;

    // "sandbox" for a development build, "production" for TestFlight.
    std::string environment;
};

// Rings the phone when a Claude finishes or asks: says what notifications are
// for and asks iOS's permission once, registers the device token with the
// relay on every launch, and hands the app the pane a tapped notification names.
class PhonePushNotifications
{
public:
    explicit PhonePushNotifications(PushRegistrationSettings settingsToUse);

    // Call once the app is up; the tap that launched it arrives after this.
    void start();

    std::function<void(const term::phone::NotificationTarget&)> onOpened =
        [](const term::phone::NotificationTarget&) {};

private:
    void explainThenAsk();
    void askIos();
    void registerWithApple();
    void sendTokenToRelay(const std::string& deviceToken);

    PushRegistrationSettings settings;
};
} // namespace cowterm::mobile
