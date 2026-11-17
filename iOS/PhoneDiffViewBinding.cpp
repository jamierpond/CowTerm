#include "PhoneDiffViewBinding.h"

namespace cowterm::mobile
{
PhoneDiffViewBinding::PhoneDiffViewBinding(PhoneLaunch& phoneToUse,
                                           PaneScreen& paneScreen,
                                           DiffViewScreen& diffViewToUse,
                                           ScreenSwitches switchesToUse)
    : phone(phoneToUse)
    , diffView(diffViewToUse)
    , switches(std::move(switchesToUse))
{
    paneScreen.actions.onDiffView = [this] { openDiffView(); };
    diffView.onClosed = [this](const std::optional<term::phone::DiffViewPane>& pane) { leaveDiffView(pane); };
}

void PhoneDiffViewBinding::openDiffView()
{
    const auto opening = phone.openDiffView();
    switches.showDiffViewScreen();

    if (opening.pane)
        diffView.open(phone.makeDiffViewConfig(*opening.pane), *opening.pane);
    else
        diffView.showWhyItCannotOpen(opening.whyNot);
}

void PhoneDiffViewBinding::leaveDiffView(const std::optional<term::phone::DiffViewPane>& pane)
{
    if (pane)
        phone.closeDiffView(*pane);

    switches.showPaneScreen();
}
} // namespace cowterm::mobile
