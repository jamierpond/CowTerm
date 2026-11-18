#include "TypingControls.h"
#include "ViewsShownSometimes.h"

#include <eacp/Core/Utils/Logging.h>

namespace cowterm::mobile
{
using namespace eacp;

namespace
{
constexpr auto composerFont = "JetBrains Mono";
constexpr auto composerFontSize = 15.0f;
constexpr auto composerBrightening = 0.06f;
constexpr auto ansiRed = 1;
constexpr auto unsentTintAmount = 0.25f;
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
    showWhetherUnsent();
    composer.onIncomingText = [this](const std::string& incoming)
    { return takeIncomingComposerText(incoming); };
    composer.onTextChanged = [this]
    {
        history.stopRecalling();

        if (composer.getText().empty() && isHoldingUnsent)
        {
            isHoldingUnsent = false;
            showWhetherUnsent();
        }
    };

    composer.onKeyDown = [this](const Graphics::KeyEvent& key)
    { return recallOnHardwareArrow(key); };

    keyRowHost.setBackgroundColour(term::toColor(theme.background));
    keyRowHost.setRootComponent(keyRow);

    // A ComponentHost takes focus on a press, which would take it from the
    // pane and close the keyboard under the finger pressing a key.
    keyRowHost.setGrabsFocusOnMouseDown(false);
    stopButtonHost.setGrabsFocusOnMouseDown(false);
    keyRow.onKey = [this](KeyRowKey key) { pressKeyRowKey(key); };

    stopButtonHost.setBackgroundColour(term::toColor(theme.background));
    stopButtonHost.setRootComponent(stopButton);
    stopButton.onStop = [this] { sendKeyToPane(*makeKeyRowKeyEvent(KeyRowKey::Escape)); };
}

void TypingControls::setShowsStopButton(bool showsStop)
{
    if (showsStopButton == showsStop)
        return;

    showsStopButton = showsStop;
    onLayoutChanged();
}

void TypingControls::addTo(Graphics::View& screen)
{
    screen.addChildren({composer, stopButtonHost, keyRowHost});
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
    auto composerArea = rects.composer;
    const auto isComposerShown = isKeyboardUp && showsComposer();

    if (showsStopButton)
        stopButtonHost.setBounds(composerArea.removeFromLeft(stopButtonWidth));

    composer.setBounds(composerArea);
    keyRowHost.setBounds(rects.keyRow);
    showOrHideLettingTouchesThrough(composer, isComposerShown);
    showOrHideLettingTouchesThrough(stopButtonHost, isComposerShown && showsStopButton);
    showOrHideLettingTouchesThrough(keyRowHost, isKeyboardUp);
}

void TypingControls::raiseKeyboard()
{
    LOG("typing: raising the keyboard");
    auto& terminal = findTerminal();

    if (!isComposerOn)
    {
        terminal.setWantsTextInput(true);
        terminal.focus();
        return;
    }

    composer.startEditing();
}

void TypingControls::composeAndSend(const std::string& text)
{
    composer.setText(text);
    sendComposedText();
}

void TypingControls::lowerKeyboard()
{
    composer.stopEditing();
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
        case KeyRowKey::Composer:
            toggleComposer();
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

// A hardware keyboard's up and down arrows walk the history the way the key
// row's do.
bool TypingControls::recallOnHardwareArrow(const Graphics::KeyEvent& key)
{
    if (key.keyCode == Graphics::KeyCode::UpArrow)
        return recallFromHistory(KeyRowKey::Up);

    if (key.keyCode == Graphics::KeyCode::DownArrow)
        return recallFromHistory(KeyRowKey::Down);

    return false;
}

bool TypingControls::canRecallIntoComposer() const
{
    if (!isComposerOn)
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

void TypingControls::setLinkLive(bool isLive)
{
    isLinkLive = isLive;

    if (isLinkLive && isHoldingUnsent)
        sendComposedText();
}

// The composer's field turns red while it holds a message that has not gone.
void TypingControls::showWhetherUnsent()
{
    const auto field = term::toColor(theme.background).brighter(composerBrightening);
    const auto unsentRed = term::toColor(theme.ansi[ansiRed]);

    const auto text = term::toColor(theme.foreground);

    if (isHoldingUnsent)
        composer.setColours(text, field.interpolatedWith(unsentRed, unsentTintAmount));
    else
        composer.setColours(text, field);
}

void TypingControls::sendComposedText()
{
    const auto text = composer.getText();
    auto& terminal = findTerminal();

    if (!isLinkLive)
    {
        isHoldingUnsent = !text.empty();
        showWhetherUnsent();
        return;
    }

    isHoldingUnsent = false;
    showWhetherUnsent();

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

void TypingControls::toggleComposer()
{
    const auto wasKeyboardUp = composer.isEditing() || findTerminal().hasFocus();
    isComposerOn = !isComposerOn;
    composer.stopEditing();
    findTerminal().setWantsTextInput(!isComposerOn);

    if (wasKeyboardUp)
        raiseKeyboard();

    onLayoutChanged();
}

void TypingControls::showKeyRowSecondPage()
{
    constexpr auto secondPage = 1;
    keyRow.page = secondPage;
    keyRow.repaint();
}

void TypingControls::switchToComposer()
{
    if (!isComposerOn)
        toggleComposer();

    showKeyRowState();
}

void TypingControls::showKeyRowState()
{
    keyRow.isControlLatched = controlLatch.isHeld();
    keyRow.isComposerOn = isComposerOn;
    keyRow.repaint();
}
} // namespace cowterm::mobile
