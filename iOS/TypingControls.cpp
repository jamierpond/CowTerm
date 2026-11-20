#include "TypingControls.h"
#include "ViewsShownSometimes.h"

#include <eacp/Core/Utils/Logging.h>

namespace cowterm::mobile
{
using namespace eacp;

TypingControls::TypingControls(const term::Theme& themeToUse,
                               std::function<term::TerminalView&()> findTerminalToUse)
    : theme(themeToUse)
    , findTerminal(std::move(findTerminalToUse))
{
    keyRowHost.setBackgroundColour(term::toColor(theme.background));
    keyRowHost.setRootComponent(keyRow);

    // A ComponentHost takes focus on a press, which would take it from the
    // pane and close the keyboard under the finger pressing a key.
    keyRowHost.setGrabsFocusOnMouseDown(false);
    keyRow.onKey = [this](KeyRowKey key) { pressKeyRowKey(key); };
}

void TypingControls::addTo(Graphics::View& screen)
{
    screen.addChildren({keyRowHost});
}

// A latched Ctrl takes the next key typed straight into the pane.
void TypingControls::connectTerminal(term::TerminalView& terminal)
{
    terminal.interceptKey = [this](const Graphics::KeyEvent& key)
    {
        const auto isTypedKey = key.type == Graphics::KeyEventType::Down
                                && !key.characters.empty();

        if (key.keyCode == Graphics::KeyCode::Delete && key.type == Graphics::KeyEventType::Down)
            LOG("typing: delete");

        if (!controlLatch.isHeld() || !isTypedKey)
            return false;

        sendKeyToPane(controlLatch.applyTo(key));
        showKeyRowState();
        return true;
    };
}

void TypingControls::layOut(const TypingRects& rects, bool isKeyboardUp)
{
    keyRowHost.setBounds(rects.keyRow);
    showOrHideLettingTouchesThrough(keyRowHost, isKeyboardUp);
}

void TypingControls::raiseKeyboard()
{
    LOG("typing: raising the keyboard");
    auto& terminal = findTerminal();
    terminal.setWantsTextInput(true);
    terminal.focus();
}

void TypingControls::lowerKeyboard()
{
    findTerminal().setWantsTextInput(false);
}

void TypingControls::pressKeyRowKey(KeyRowKey key)
{
    LOG("key row: ", getKeyRowLabel(key));
    switch (key)
    {
        case KeyRowKey::Control:
            controlLatch.toggle();
            break;
        case KeyRowKey::HideKeyboard:
            lowerKeyboard();
            break;
        case KeyRowKey::NextPage:
            keyRow.page = (keyRow.page + 1) % keyRowPageCount;
            break;
        default:
            sendKeyToPane(controlLatch.applyTo(*makeKeyRowKeyEvent(key)));
            break;
    }

    showKeyRowState();
}

void TypingControls::sendKeyToPane(const Graphics::KeyEvent& key)
{
    findTerminal().dispatchKeyEvent(key);
}

void TypingControls::showKeyRowSecondPage()
{
    constexpr auto secondPage = 1;
    keyRow.page = secondPage;
    keyRow.repaint();
}

void TypingControls::showKeyRowState()
{
    keyRow.isControlLatched = controlLatch.isHeld();
    keyRow.repaint();
}
} // namespace cowterm::mobile
