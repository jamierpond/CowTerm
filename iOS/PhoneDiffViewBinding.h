#pragma once

#include "DiffViewScreen.h"
#include "PaneScreen.h"
#include "PhoneLaunch.h"

namespace cowterm::mobile
{
// Brings up one of the app's screens. dv's screen is made as it comes up.
struct ScreenSwitches
{
    std::function<DiffViewScreen&()> showDiffViewScreen;
    eacp::Callback showPaneScreen;
};

// The bar's diff button: dv for the pane on screen, on its own screen, and
// back to that pane when dv quits or is closed, with its pane killed.
class PhoneDiffViewBinding
{
public:
    PhoneDiffViewBinding(PhoneLaunch& phoneToUse, PaneScreen& paneScreen, ScreenSwitches switchesToUse);

    void openDiffView();

private:
    void leaveDiffView(const std::optional<term::phone::DiffViewPane>& pane);

    PhoneLaunch& phone;
    ScreenSwitches switches;
};
} // namespace cowterm::mobile
