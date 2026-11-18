#pragma once

#include "Multiplexer/PaneReadState.h"

#include <string>

// Posts a pane's new unread event to the push relay, which rings the phone.
struct PushRelayTarget
{
    // "http://macprorack.<tailnet>.ts.net:8742"; empty when this machine has no relay set.
    std::string relayAddress;

    // This machine as the phone dials it ("big-cow").
    std::string hostName;
};

struct PaneForPush
{
    std::string paneId;
    std::string sessionName;
    std::string workingDirectory;
};

class PanePushNotifier
{
public:
    explicit PanePushNotifier(PushRelayTarget targetToUse);

    void postNotice(const PaneForPush& pane, const term::UnreadEvent& event) const;

private:
    PushRelayTarget target;
};
