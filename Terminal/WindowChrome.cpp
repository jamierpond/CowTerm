#include "WindowChrome.h"

#include "Multiplexer/HostColours.h"
#include "Multiplexer/TextLayout.h"

#include <eacp/Core/Platform/Platform.h>

namespace term
{
using namespace eacp;

namespace
{
// macOS 26's title bar is 32 points (28 before it); covering it keeps every
// terminal row out of the band where a press drags the window.
constexpr auto macTitleBarHeight = 32.0f;
constexpr auto stripHeightUnderANativeTitleBar = 22.0f;

// The traffic lights end 69 points in; the label keeps that clear on both
// sides so it stays centred on the window.
constexpr auto macTrafficLightsClearance = 78.0f;
constexpr auto sidePadding = 12.0f;

constexpr auto labelFontSize = 12.0f;
constexpr auto dividerThickness = 1.0f;

float getWindowChromeHeight()
{
    if constexpr (Platform::isMac())
        return macTitleBarHeight;

    return stripHeightUnderANativeTitleBar;
}

float getLabelSideClearance()
{
    if constexpr (Platform::isMac())
        return macTrafficLightsClearance;

    return sidePadding;
}
} // namespace

WindowChromeLook describeWindowChrome(const AppConfig& config,
                                      const std::string& host,
                                      const std::string& sessionName)
{
    if (host.empty())
    {
        const auto theme = themeByName(config.theme);
        return {sessionName, theme.background, theme.foreground};
    }

    const auto colours = getColourBlindSafeHostRowColours(config, host);
    return {sessionName, colours.background, colours.text};
}

Graphics::Rect calculateWindowChromeBounds(Graphics::Rect window)
{
    return window.fromTop(getWindowChromeHeight());
}

Graphics::Rect calculateAreaUnderWindowChrome(Graphics::Rect window)
{
    window.removeFromTop(getWindowChromeHeight());
    return window;
}

WindowChrome::WindowChrome(const AppConfig& configToUse)
    : config(configToUse)
    , theme(themeByName(configToUse.theme))
    , labelFont({config.font, labelFontSize})
{
}

void WindowChrome::showSession(const std::string& host,
                               const std::string& sessionName)
{
    look = describeWindowChrome(config, host, sessionName);
    repaint();
}

void WindowChrome::paint(UI::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour(toColor(look.background));
    g.fillRect(bounds);

    g.setColour(toColor(theme.paneBorder));
    g.fillRect(bounds.fromBottom(dividerThickness));

    const auto labelArea = bounds.inset(getLabelSideClearance(), 0.0f);
    g.setFont(labelFont);
    const auto measureWidth = [&g](const std::string& text)
    { return g.measureText(text); };
    g.setColour(toColor(look.text));
    g.drawText(truncateTextToWidth(look.label, labelArea.w, measureWidth),
               labelArea,
               UI::Justification::Centred);
}
} // namespace term
