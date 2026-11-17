#pragma once

#include "PaneScreenChrome.h"
#include "PaneScreenLayout.h"
#include "Typing.h"

#include "TerminalView/TerminalView.h"

#include <eacp/Graphics/View/NativeTextView.h>
#include <eacp/UI/UI.h>

#include <functional>

namespace cowterm::mobile
{
// The key row above the keyboard and the composer (docs/iOSDesign.md,
// "Typing"). Keys go straight to the pane, as in any terminal. The key row's
// "compose" switch brings up the composer, a native text view for dictation
// and autocorrect that sends as a bracketed paste then Enter. Ctrl latches for
// one key.
class TypingControls
{
public:
    TypingControls(const term::Theme& themeToUse,
                   std::function<term::TerminalView&()> findTerminalToUse);

    // Adds the composer and the key row to the screen, above the pane.
    void addTo(eacp::Graphics::View& screen);

    // Lets a latched Ctrl reach keys typed straight into a new pane view.
    void connectTerminal(term::TerminalView& terminal);

    void layOut(const TypingRects& rects, bool isKeyboardUp);
    bool showsComposer() const { return isComposerOn; }

    void raiseKeyboard();

    // Puts text in the composer and sends it, as return would.
    void composeAndSend(const std::string& text);

    // The composer on, as the key row's switch turns it on.
    void switchToComposer();

    // The key row's second page (symbols, compose, hide), as its last key turns to it.
    void showKeyRowSecondPage();

    // While the link is down a sent message waits in the composer, marked
    // unsent, and goes when the link is live again.
    void setLinkLive(bool isLive);

    // On a Claude pane a stop button (Esc) sits at the composer's left.
    void setShowsStopButton(bool showsStop);

    // The composer came or went, so the screen lays out again.
    std::function<void()> onLayoutChanged = [] {};

private:
    void pressKeyRowKey(KeyRowKey key);
    bool takeIncomingComposerText(const std::string& incoming);
    void sendComposedText();
    bool recallFromHistory(KeyRowKey key);
    bool canRecallIntoComposer() const;
    bool recallOnHardwareArrow(const eacp::Graphics::KeyEvent& key);
    void sendKeyToPane(const eacp::Graphics::KeyEvent& key);
    void toggleComposer();
    void lowerKeyboard();
    void showKeyRowState();
    void showWhetherUnsent();

    term::Theme theme;
    std::function<term::TerminalView&()> findTerminal;

    eacp::Graphics::NativeTextView composer;
    eacp::UI::ComponentHost keyRowHost;
    KeyRow keyRow {theme};
    eacp::UI::ComponentHost stopButtonHost;
    StopButton stopButton {theme};
    bool showsStopButton = false;

    ComposerHistory history;
    ControlLatch controlLatch;
    bool isComposerOn = false;
    bool isLinkLive = true;
    bool isHoldingUnsent = false;
};
} // namespace cowterm::mobile
