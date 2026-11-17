#pragma once

#include <Push/PanePushGate.h>
#include <Push/PushNotice.h>

#include <map>
#include <string>

// The daemon's half of ringing the phone: each `ct pane-state` report goes
// through the pane's gate, and a report that passes is posted to the relay.
struct PushRelayTarget
{
    // "http://macprorack:8742"; empty when this machine has no relay set.
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

    void reportPaneState(const PaneForPush& pane,
                         const term::push::PaneStateChange& change,
                         const term::push::PaneAttention& attention);

    void forgetPane(const std::string& paneId);

private:
    void postToRelay(const term::push::PushNotice& notice) const;

    PushRelayTarget target;
    std::map<std::string, term::push::PanePushGate> gates;
};
