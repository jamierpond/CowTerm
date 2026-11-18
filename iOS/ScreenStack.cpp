#include "ScreenStack.h"

#include "ViewsShownSometimes.h"

namespace cowterm::mobile
{
ScreenStack::ScreenStack(eacp::Graphics::View& paneScreenToUse)
    : paneScreen(paneScreenToUse)
{
    addSubview(paneScreen);
}

OverviewScreen& ScreenStack::showOverview()
{
    removeViewAndDestroyItSoon(*this, diffView);

    if (overview == nullptr)
    {
        overview = std::make_unique<OverviewScreen>();
        coverPaneScreen(*overview);
    }

    return *overview;
}

DiffViewScreen& ScreenStack::showDiffView()
{
    removeViewAndDestroyItSoon(*this, overview);

    if (diffView == nullptr)
    {
        diffView = std::make_unique<DiffViewScreen>();
        coverPaneScreen(*diffView);
    }

    return *diffView;
}

// The pane screen stays alive under the cover, hidden, with nothing beneath it.
void ScreenStack::coverPaneScreen(eacp::Graphics::View& cover)
{
    addSubview(cover);
    cover.setBounds(getLocalBounds());
    showOrHideLettingTouchesThrough(paneScreen, false);
}

void ScreenStack::showPaneScreen()
{
    removeViewAndDestroyItSoon(*this, overview);
    removeViewAndDestroyItSoon(*this, diffView);
    showOrHideLettingTouchesThrough(paneScreen, true);
}

std::optional<std::reference_wrapper<OverviewScreen>> ScreenStack::findOverview()
{
    if (overview == nullptr)
        return std::nullopt;

    return *overview;
}

std::optional<std::reference_wrapper<DiffViewScreen>> ScreenStack::findDiffView()
{
    if (diffView == nullptr)
        return std::nullopt;

    return *diffView;
}

void ScreenStack::resized()
{
    for (auto* screen: getSubviews())
        screen->setBounds(getLocalBounds());
}
} // namespace cowterm::mobile
