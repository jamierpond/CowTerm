#pragma once

#include <eacp/Graphics/Tray/TrayIcon.h>

namespace term
{
// The menu-bar presence that makes hide-on-close feel intentional: close
// the window and the icon stays, shells keep running.
class TrayController
{
public:
    TrayController();

    // Rebuilds the menu; call once the daemon has answered or not, since the
    // Quit items depend on it.
    void refresh();

    eacp::Callback onShowWindow = [] {};

    // Set only where there is a permissions panel (macOS); the item shows
    // only then.
    eacp::Callback onShowPermissions;

private:
    static eacp::Graphics::Image makeIcon();

    eacp::Graphics::TrayIcon icon;
};
} // namespace term
