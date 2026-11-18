#pragma once

#include "PanePushNotifier.h"

#include "Multiplexer/PaneReadState.h"

#include <emberstore/Document.h>

#include <map>
#include <string>

// The daemon's record of which panes Jamie has not seen: one PaneReadState per
// pane, kept on disk so it survives a restart, and each new unread event posted
// to the push relay. Every client reads the same record, so seeing a pane on
// the Mac clears it on the phone and the other way round.
class UnreadPanes
{
public:
    UnreadPanes(emberstore::Document<term::UnreadPanes> storeToUse, PushRelayTarget pushRelayTarget);

    // A Claude finished or asked, or the shell rang or notified.
    void reportEvent(const PaneForPush& pane, const term::UnreadEvent& event);

    // Claude was given a prompt in the pane.
    void markRead(const std::string& paneId);

    void startViewing(const std::string& paneId, term::PaneReadState::ViewerId viewer);
    void stopViewing(const std::string& paneId, term::PaneReadState::ViewerId viewer);
    void stopViewingEverything(term::PaneReadState::ViewerId viewer);

    void forgetPane(const std::string& paneId);

    // "<event>\t<since>\t<text>" for the activity answer, empty columns when read.
    std::string describeForActivity(const std::string& paneId) const;

private:
    term::PaneReadState& findPane(const std::string& paneId);
    void save();

    emberstore::Document<term::UnreadPanes> store;
    PanePushNotifier pushNotifier;
    std::map<std::string, term::PaneReadState> panes;
};
