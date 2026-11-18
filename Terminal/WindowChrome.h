#pragma once

#include "Multiplexer/Config.h"

#include <eacp/UI/UI.h>

#include <string>

namespace term
{
// What the strip across the window's top says, and in which colours.
struct WindowChromeLook
{
    std::string label;
    Rgb background = 0;
    Rgb text = 0;
};

// The session's name on its host's colour, or on the theme's background for a
// local session. A remote session's name starts with its host ("tamby/iac"),
// so the machine reads without telling hues apart.
WindowChromeLook describeWindowChrome(const AppConfig& config,
                                      const std::string& host,
                                      const std::string& sessionName);

// The strip along the window's top, and the rest of the window below it, where
// the session, its popup and the overlays go. On macOS the strip covers the
// title bar.
eacp::Graphics::Rect calculateWindowChromeBounds(eacp::Graphics::Rect window);
eacp::Graphics::Rect calculateAreaUnderWindowChrome(eacp::Graphics::Rect window);

// The strip across the window's top that says which machine the shown session
// runs on. On macOS it is the title bar, beside the traffic lights.
class WindowChrome final : public eacp::UI::Component
{
public:
    explicit WindowChrome(const AppConfig& configToUse);

    void showSession(const std::string& host, const std::string& sessionName);

    void paint(eacp::UI::Graphics& g) override;

private:
    const AppConfig& config;
    Theme theme;
    WindowChromeLook look;
    eacp::UI::Font labelFont;
};
} // namespace term
