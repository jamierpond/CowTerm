#include "PhoneDiffViewBinding.h"

namespace cowterm::mobile
{
PhoneDiffViewBinding::PhoneDiffViewBinding(PhoneLaunch& phoneToUse,
                                           PaneScreen& paneScreen,
                                           ScreenSwitches switchesToUse)
    : phone(phoneToUse)
    , switches(std::move(switchesToUse))
{
    paneScreen.actions.onDiffView = [this] { openDiffView(); };
}

void PhoneDiffViewBinding::openDiffView()
{
    const auto opening = phone.openDiffView();
    auto& diffView = switches.showDiffViewScreen();
    diffView.onClosed = [this](const std::optional<term::phone::DiffViewPane>& pane) { leaveDiffView(pane); };

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
