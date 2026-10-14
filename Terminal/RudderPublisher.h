#pragma once

#include "Config.h"
#include "DirWatcher.h"
#include "RudderContexts.h"
#include "Session.h"

#include <RudderClient.h>

#include <memory>
#include <string>
#include <vector>

namespace term
{
// Publishes every pane, and every ctrl+a f place nothing is open in yet, to
// Rudder (the app switcher) as contexts, and acts on Rudder's picks. With no
// Rudder running the client stays silent and this costs nothing: pushes
// happen only when something changed, never on a timer.
class RudderPublisher
{
public:
    RudderPublisher(const AppConfig& configToUse, SessionManager& managerToUse);
    ~RudderPublisher();

    // Something the list shows changed. Bursts (a split is a create, a
    // focus and a title) coalesce into one push at the end of the event loop
    // turn.
    void schedulePush();

    // Rudder picked a context (Enter, not a preview); the window should come
    // forward.
    eacp::Callback onBringToFront = [] {};

private:
    void push();
    void activate(const std::string& contextId, bool raise);
    void rescanPlaces();
    void schedulePlacesRescan();

    const AppConfig& config;
    SessionManager& manager;
    rudder::RudderClient client;

    // The ctrl+a f list (scanProjects), cached between directory changes.
    std::vector<std::string> places;
    DirWatcher placesWatcher;

    std::vector<rudder::protocol::Context> pushed;
    bool pushPending = false;
    bool rescanPending = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term
