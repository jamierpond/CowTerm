#pragma once

#include "PaneScreenChrome.h"
#include "PaneScreenLayout.h"
#include "Typing.h"

#include "TerminalView/TerminalView.h"

#include <eacp/UI/UI.h>

#include <functional>

namespace cowterm::mobile
{
// The key row above the keyboard (docs/iOSDesign.md, "Typing"). Keys go
// straight to the pane, as in any terminal. Ctrl latches for one key.
class TypingControls
{
public:
    TypingControls(const term::Theme& themeToUse,
                   std::function<term::TerminalView&()> findTerminalToUse);

    // Adds the key row to the screen, above the pane.
    void addTo(eacp::Graphics::View& screen);

    // Lets a latched Ctrl reach keys typed straight into a new pane view.
    void connectTerminal(term::TerminalView& terminal);

    void layOut(const TypingRects& rects, bool isKeyboardUp);

    void raiseKeyboard();
    void lowerKeyboard();

    // What a finger lifting off a key row key does.
    void pressKeyRowKey(KeyRowKey key);

    // The key row's second page (symbols, hide), as its last key turns to it.
    void showKeyRowSecondPage();

private:
    void sendKeyToPane(const eacp::Graphics::KeyEvent& key);
    void showKeyRowState();

    term::Theme theme;
    std::function<term::TerminalView&()> findTerminal;

    eacp::UI::ComponentHost keyRowHost;
    KeyRow keyRow {theme};

    ControlLatch controlLatch;
};
} // namespace cowterm::mobile
