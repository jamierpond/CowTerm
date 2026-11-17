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
// The composer and the key row above the keyboard (docs/iOSDesign.md,
// "Typing"). The composer is a native text view, so dictation and autocorrect
// work; it sends as a bracketed paste then Enter. Raw mode hides it and sends
// the keyboard's keys straight to the pane. Ctrl latches for one key.
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
    bool showsComposer() const { return !isRawMode; }

    void raiseKeyboard();

    // Raw mode on, as the key row's switch turns it on.
    void switchToRawMode();

    // Raw mode changed, so the screen lays out again.
    std::function<void()> onLayoutChanged = [] {};

private:
    void pressKeyRowKey(KeyRowKey key);
    bool takeIncomingComposerText(const std::string& incoming);
    void sendComposedText();
    bool recallFromHistory(KeyRowKey key);
    bool canRecallIntoComposer() const;
    void sendKeyToPane(const eacp::Graphics::KeyEvent& key);
    void toggleRawMode();
    void lowerKeyboard();
    void showKeyRowState();

    term::Theme theme;
    std::function<term::TerminalView&()> findTerminal;

    eacp::Graphics::NativeTextView composer;
    eacp::UI::ComponentHost keyRowHost;
    KeyRow keyRow {theme};

    ComposerHistory history;
    ControlLatch controlLatch;
    bool isRawMode = false;
};
} // namespace cowterm::mobile
