#pragma once

#include "PaneScreen.h"
#include "PaneScreenLayout.h"

#include "Phone/DiffViewPane.h"
#include "TerminalView/TerminalView.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

// dv on the phone (docs/iOSDesign.md, "dv"): full screen, the host and folder
// on the top line, and a bar at the bottom that closes it. A tap reaches dv as
// a left click and a drag as the mouse wheel, through PaneTouchSurface as on
// the pane screen. When dv cannot start, a plain sentence says why.
namespace cowterm::mobile
{
class DiffViewChrome final : public eacp::UI::Component
{
public:
    explicit DiffViewChrome(const term::Theme& themeToUse);

    void paint(eacp::UI::Graphics& g) override;
    void mouseUp(const eacp::UI::MouseEvent& event) override;

    std::string title;
    std::string sentence;
    std::function<void()> onClose = [] {};

private:
    // The host already keeps the component inside the safe area.
    PaneScreenRects calculateRects() const;
    void paintSentence(eacp::UI::Graphics& g, Rect pane);

    term::Theme theme;
};

class DiffViewScreen final : public eacp::Graphics::View
{
public:
    DiffViewScreen();

    // Starts dv as pane says, through config's command shell.
    void open(const term::TerminalConfig& config, const term::phone::DiffViewPane& pane);

    // Says why dv could not open, with no pane started.
    void showWhyItCannotOpen(const std::string& sentence);

    // The bar or a swipe from the left edge: leaves dv and fires onClosed,
    // with the pane dv ran in, if one was started.
    void close();

    std::function<void(const std::optional<term::phone::DiffViewPane>&)> onClosed =
        [](const std::optional<term::phone::DiffViewPane>&) {};

    void resized() override;
    void safeAreaInsetsChanged() override;

private:
    void handleDiffViewEnded();
    void showSentence(const std::string& sentence);
    void removeTerminal();
    term::TerminalView& getTerminal();

    term::Theme theme;
    std::optional<term::phone::DiffViewPane> pane;
    std::unique_ptr<term::TerminalView> terminal;
    PaneTouchSurface touchSurface {[this]() -> term::TerminalView& { return getTerminal(); }};
    eacp::UI::ComponentHost chromeHost;
    DiffViewChrome chrome {theme};
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace cowterm::mobile
