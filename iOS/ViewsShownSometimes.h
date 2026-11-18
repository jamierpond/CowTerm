#pragma once

#include "Emulator/TermTypes.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/View/View.h>
#include <eacp/UI/UI.h>

#include <memory>

// A view the phone shows only some of the time either exists only while it is
// shown, or, where it must stay alive, is hidden and lets touches through
// (docs/iOSDesign.md, "Decisions made while building").
namespace cowterm::mobile
{
// A component on a native host of its own: one view a screen makes when it
// shows the component and destroys when it hides it.
template <typename ComponentType>
class HostedComponent final : public eacp::Graphics::View
{
public:
    explicit HostedComponent(const term::Theme& theme)
        : component(theme)
    {
        host.setBackgroundColour(term::toColor(theme.background));
        host.setRootComponent(component);
        addSubview(host);
    }

    void resized() override { host.setBounds(getLocalBounds()); }

    ComponentType component;

private:
    eacp::UI::ComponentHost host;
};

// Takes the view off the screen at once, so no touch reaches it, and destroys
// it on the next loop tick, since this often runs inside one of its own callbacks.
template <typename ViewType>
void removeViewAndDestroyItSoon(eacp::Graphics::View& parent, std::unique_ptr<ViewType>& view)
{
    if (view == nullptr)
        return;

    parent.removeSubview(*view);
    eacp::Threads::callAsync([leaving = std::shared_ptr<ViewType> {std::move(view)}] {});
}

// For a view that stays alive while hidden. eacp picks a touch's view by
// position whether it is shown or not; ignoring mouse events sends the touch on
// as a mouse event to the view beneath, but never to a view beneath that takes
// touches (PaneTouchSurface), so this is only for a view with none beneath it.
inline void showOrHideLettingTouchesThrough(eacp::Graphics::View& view, bool isShown)
{
    view.setVisible(isShown);
    view.setIgnoresMouseEvents(!isShown);
}
} // namespace cowterm::mobile
