#pragma once

#include "OverviewScreen.h"
#include "PhoneLaunch.h"

#include <functional>
#include <string>

namespace cowterm::mobile
{
// Feeds the overview the hosts' real sessions while it is shown, and opens a
// tapped session's pane: the one typed into last, else its first, only when
// its daemon still lists it.
class PhoneOverviewBinding
{
public:
    using ShowPane = std::function<void(const std::string& paneId)>;

    PhoneOverviewBinding(PhoneLaunch& phoneToUse, OverviewScreen& screenToUse, ShowPane showPaneToUse);

    // Call with true as the overview comes on screen: the hosts are asked
    // every few seconds until a session opens.
    void setShown(bool isShown);

private:
    void openSession(const term::overview::OverviewRow& row);

    PhoneLaunch& phone;
    OverviewScreen& screen;
    ShowPane showPane;
};

OverviewInputs makeOverviewInputs(const term::phone::OverviewReport& report);
} // namespace cowterm::mobile
