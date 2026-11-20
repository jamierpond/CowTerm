#pragma once

#include "DiffViewScreen.h"
#include "OverviewScreen.h"

#include <eacp/Graphics/View/View.h>

#include <functional>
#include <memory>
#include <optional>

namespace cowterm::mobile
{
// The window's one content view. The pane screen is always in it, since it
// holds the pane's link; the overview and dv exist only while they are shown,
// over it, so a screen that has gone cannot take a touch. eacp sends every
// touch to the window's root view and hit-tests from there, so the root never
// changes.
class ScreenStack final : public eacp::Graphics::View
{
public:
    explicit ScreenStack(eacp::Graphics::View& paneScreenToUse);

    // Each makes its screen if it is not up already, and destroys the other.
    OverviewScreen& showOverview();
    DiffViewScreen& showDiffView();

    // Destroys whichever screen covers the pane screen.
    void showPaneScreen();

    std::optional<std::reference_wrapper<OverviewScreen>> findOverview();
    std::optional<std::reference_wrapper<DiffViewScreen>> findDiffView();

    void resized() override;

private:
    template <typename Cover, typename OtherCover>
    Cover& showCoverRemovingTheOtherCover(std::unique_ptr<Cover>& cover,
                                          std::unique_ptr<OtherCover>& otherCover);

    void coverPaneScreen(eacp::Graphics::View& cover);

    eacp::Graphics::View& paneScreen;
    std::unique_ptr<OverviewScreen> overview;
    std::unique_ptr<DiffViewScreen> diffView;
};
} // namespace cowterm::mobile
