#pragma once

#include "DiffViewLaunchSteps.h"
#include "PaneScreen.h"
#include "PaneScreenLayout.h"
#include "TypingControls.h"
#include "ViewsShownSometimes.h"

#include "Phone/DiffViewPane.h"
#include "TerminalView/TerminalView.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

// dv on the phone (docs/iOSDesign.md, "dv"): full screen, the host and folder
// on the top line, and a bar at the bottom that closes it, with the keyboard
// button at its right. A tap reaches dv as a left click and a drag as the
// mouse wheel, through PaneTouchSurface, and typing, the key row and the
// composer work as on the pane screen. When dv cannot start, a plain sentence
// says why.
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
    bool showsKeyboardButton = false;
    std::function<void()> onClose = [] {};
    std::function<void()> onKeyboard = [] {};

private:
    // The host already keeps the component inside the safe area.
    PaneScreenRects calculateRects() const;
    void paintSentence(eacp::UI::Graphics& g, Rect pane);
    void paintBar(eacp::UI::Graphics& g, Rect bar);

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

    // From WindowEvents::onSoftwareKeyboardChanged, as the pane screen takes it.
    void setSoftwareKeyboardChange(const eacp::Graphics::SoftwareKeyboardChange& change);

    // The bar's keyboard button: typing straight into dv, or into the composer when it is on.
    void raiseKeyboard();

    // One step of --diff-view-steps=, done as a finger would.
    void performLaunchStep(const DiffViewLaunchStep& step);

    std::function<void(const std::optional<term::phone::DiffViewPane>&)> onClosed =
        [](const std::optional<term::phone::DiffViewPane>&) {};

    void resized() override;
    void safeAreaInsetsChanged() override;

private:
    void layOutParts();
    bool isKeyboardUp() const;
    eacp::Graphics::Insets calculateInsetsAroundParts() const;
    void tapTextOnScreen(const std::string& text);
    void typeText(const std::string& text);
    void handleDiffViewEnded();
    void showSentence(const std::string& sentence);
    void removeTerminal();
    void makeTouchSurface();
    term::TerminalView& getTerminal();

    term::Theme theme;
    std::optional<term::phone::DiffViewPane> pane;
    std::unique_ptr<term::TerminalView> terminal;
    std::unique_ptr<PaneTouchSurface> touchSurface;
    eacp::UI::ComponentHost chromeHost;
    DiffViewChrome chrome {theme};
    TypingControls typing {theme, [this]() -> term::TerminalView& { return getTerminal(); }};
    float softwareKeyboardCoveredHeight = 0.0f;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace cowterm::mobile
