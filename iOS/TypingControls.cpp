#include "TypingControls.h"

namespace cowterm::mobile
{
using namespace eacp;

namespace
{
constexpr auto composerFont = "JetBrains Mono";
constexpr auto composerFontSize = 15.0f;
constexpr auto composerBrightening = 0.06f;
constexpr auto newline = "\n";
constexpr auto enter = "\r";

bool isRecallKey(KeyRowKey key)
{
    return key == KeyRowKey::Up || key == KeyRowKey::Down;
}
} // namespace

TypingControls::TypingControls(const term::Theme& themeToUse,
                               std::function<term::TerminalView&()> findTerminalToUse)
    : theme(themeToUse)
    , findTerminal(std::move(findTerminalToUse))
{
    composer.setFont(composerFont, composerFontSize);
    composer.setColours(term::toColor(theme.foreground),
                        term::toColor(theme.background).brighter(composerBrightening));
    composer.onIncomingText = [this](const std::string& incoming)
    { return takeIncomingComposerText(incoming); };
    composer.onTextChanged = [this] { history.stopRecalling(); };

    keyRowHost.setBackgroundColour(term::toColor(theme.background));
    keyRowHost.setRootComponent(keyRow);
    keyRow.onKey = [this](KeyRowKey key) { pressKeyRowKey(key); };
}

void TypingControls::addTo(Graphics::View& screen)
{
    screen.addChildren({composer, keyRowHost});
}

// A latched Ctrl takes the next key typed straight into the pane.
void TypingControls::connectTerminal(term::TerminalView& terminal)
{
    terminal.interceptKey = [this](const Graphics::KeyEvent& key)
    {
        const auto isTypedKey = key.type == Graphics::KeyEventType::Down
                                && !key.characters.empty();

        if (!controlLatch.isHeld() || !isTypedKey)
            return false;

        sendKeyToPane(controlLatch.applyTo(key));
        showKeyRowState();
        return true;
    };
}

void TypingControls::layOut(const TypingRects& rects, bool isKeyboardUp)
{
    composer.setBounds(rects.composer);
    keyRowHost.setBounds(rects.keyRow);
    composer.setVisible(isKeyboardUp && showsComposer());
    keyRowHost.setVisible(isKeyboardUp);
}

void TypingControls::raiseKeyboard()
{
    auto& terminal = findTerminal();

    if (isRawMode)
    {
        terminal.setWantsTextInput(true);
        terminal.focus();
        return;
    }

    composer.startEditing();
}

void TypingControls::lowerKeyboard()
{
    composer.stopEditing();
    findTerminal().setWantsTextInput(false);
}

void TypingControls::pressKeyRowKey(KeyRowKey key)
{
    switch (key)
    {
        case KeyRowKey::Control:
            controlLatch.toggle();
            break;
        case KeyRowKey::RawMode:
            toggleRawMode();
            break;
        case KeyRowKey::HideKeyboard:
            lowerKeyboard();
            break;
        case KeyRowKey::NextPage:
            keyRow.page = (keyRow.page + 1) % keyRowPageCount;
            break;
        default:
            if (!(isRecallKey(key) && recallFromHistory(key)))
                sendKeyToPane(controlLatch.applyTo(*makeKeyRowKeyEvent(key)));
            break;
    }

    showKeyRowState();
}

// Up in an empty composer, or one already showing a recalled message, walks
// the history; anywhere else the arrows go to the pane.
bool TypingControls::recallFromHistory(KeyRowKey key)
{
    if (!canRecallIntoComposer())
        return false;

    const auto recalled = [this, key]
    {
        if (key == KeyRowKey::Up)
            return history.recallOlder();

        return history.recallNewer();
    }();

    if (!recalled)
        return false;

    composer.setText(*recalled);
    return true;
}

bool TypingControls::canRecallIntoComposer() const
{
    if (isRawMode)
        return false;

    return composer.getText().empty() || history.isRecalling();
}

// The return key sends instead of starting a new line, and a latched Ctrl
// takes the next typed character to the pane.
bool TypingControls::takeIncomingComposerText(const std::string& incoming)
{
    if (incoming == newline)
    {
        sendComposedText();
        return true;
    }

    if (!controlLatch.isHeld())
        return false;

    sendKeyToPane(controlLatch.applyTo(makeTypedKeyEvent(incoming)));
    showKeyRowState();
    return true;
}

void TypingControls::sendComposedText()
{
    const auto text = composer.getText();
    auto& terminal = findTerminal();

    if (text.empty())
    {
        terminal.sendText(enter);
        return;
    }

    history.remember(text);
    terminal.pasteThenPressEnter(text);
    composer.setText({});
}

void TypingControls::sendKeyToPane(const Graphics::KeyEvent& key)
{
    findTerminal().dispatchKeyEvent(key);
}

void TypingControls::toggleRawMode()
{
    const auto wasKeyboardUp = composer.isEditing() || findTerminal().hasFocus();
    isRawMode = !isRawMode;
    composer.stopEditing();
    findTerminal().setWantsTextInput(isRawMode);

    if (wasKeyboardUp)
        raiseKeyboard();

    onLayoutChanged();
}

void TypingControls::switchToRawMode()
{
    if (!isRawMode)
        toggleRawMode();

    showKeyRowState();
}

void TypingControls::showKeyRowState()
{
    keyRow.isControlLatched = controlLatch.isHeld();
    keyRow.isRawMode = isRawMode;
    keyRow.repaint();
}
} // namespace cowterm::mobile
