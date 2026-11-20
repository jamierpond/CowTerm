#include "DiffViewScreen.h"

#include "Multiplexer/RemoteLocation.h"
#include "Multiplexer/TextLayout.h"

#include <eacp/Core/Utils/Logging.h>

#include <algorithm>

namespace cowterm::mobile
{
using namespace eacp;
using UI::Rect;

namespace
{
constexpr auto fontFamily = "JetBrains Mono";
constexpr auto titleFontSize = 13.0f;
constexpr auto barFontSize = 15.0f;
constexpr auto sentenceFontSize = 15.0f;
constexpr auto sentenceLineHeight = 24.0f;
constexpr auto sidePadding = 12.0f;
constexpr auto barBrightening = 0.03f;
constexpr auto dividerAlpha = 0.12f;
constexpr auto dividerThickness = 1.0f;
constexpr auto closeLabel = "\xe2\x80\xb9 Back to the pane";
constexpr auto keyboardGlyph = "\xe2\x8c\xa8";
constexpr auto tapFingerId = 1;
constexpr auto ansiYellow = 3;

UI::Font makeFont(float size)
{
    return UI::Font {fontFamily, size};
}

std::string describeDiffViewTitle(const term::phone::DiffViewPane& pane)
{
    return term::remote::getHostLabel(pane.host) + " \xc2\xb7 dv \xc2\xb7 " + pane.workingDirectory;
}
} // namespace

DiffViewChrome::DiffViewChrome(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

PaneScreenRects DiffViewChrome::calculateRects() const
{
    return calculatePaneScreenRects(getLocalBounds(), {});
}

void DiffViewChrome::paint(UI::Graphics& g)
{
    const auto rects = calculateRects();
    const auto background = term::toColor(theme.background);
    const auto foreground = term::toColor(theme.foreground);
    const auto barBackground = background.brighter(barBrightening);
    const auto& bar = rects.bottomBar;

    g.setColour(background);
    g.fillAll();
    g.setColour(barBackground);
    g.fillRect(rects.topLine);
    g.fillRect(bar);
    g.setColour(foreground.withAlpha(dividerAlpha));
    g.fillRect(rects.topLine.fromBottom(dividerThickness));
    g.fillRect(bar.fromTop(dividerThickness));

    g.setColour(foreground);
    g.setFont(makeFont(titleFontSize));
    const auto titleArea = rects.topLine.inset(sidePadding, 0.0f);
    const auto measure = [&g](const std::string& text) { return g.measureText(text); };
    g.drawText(term::truncateTextToWidth(title, titleArea.w, measure), titleArea);

    paintBar(g, bar);
    paintSentence(g, rects.pane);
}

void DiffViewChrome::paintBar(UI::Graphics& g, Rect bar)
{
    g.setFont(makeFont(barFontSize));

    if (showsKeyboardButton)
        g.drawText(keyboardGlyph, bar.removeFromRight(barButtonWidth), UI::Justification::Centred);

    g.drawText(closeLabel, bar.inset(sidePadding, 0.0f));
}

void DiffViewChrome::paintSentence(UI::Graphics& g, Rect pane)
{
    if (sentence.empty())
        return;

    g.setFont(makeFont(sentenceFontSize));
    g.setColour(term::toColor(theme.ansi[ansiYellow]));
    const auto area = pane.inset(sidePadding, 0.0f);
    const auto measure = [&g](const std::string& text) { return g.measureText(text); };
    const auto lines = term::wrapTextToWidth(sentence, area.w, measure);
    const auto sentenceHeight = sentenceLineHeight * static_cast<float>(lines.size());
    auto lineBounds = area;
    lineBounds.removeFromTop((lineBounds.h - sentenceHeight) / 2.0f);

    for (const auto& line: lines)
        g.drawText(line, lineBounds.removeFromTop(sentenceLineHeight), UI::Justification::Centred);
}

// The keyboard button raises the keyboard; the rest of the bar closes dv.
void DiffViewChrome::mouseUp(const UI::MouseEvent& event)
{
    auto bar = calculateRects().bottomBar;

    if (!bar.contains(event.position))
        return;

    if (showsKeyboardButton && bar.removeFromRight(barButtonWidth).contains(event.position))
    {
        LOG("dv: keyboard");
        onKeyboard();
        return;
    }

    LOG("dv: closed from the bar");
    onClose();
}

DiffViewScreen::DiffViewScreen()
{
    chromeHost.setBackgroundColour(term::toColor(theme.background));
    chromeHost.setRootComponent(chrome);
    chrome.onClose = [this] { close(); };
    chrome.onKeyboard = [this] { raiseKeyboard(); };
    addChildren({chromeHost});
    typing.addTo(*this);
}

// The touch surface comes and goes with dv's pane: with no pane under it, a
// drag or a tap would have no terminal to reach.
void DiffViewScreen::makeTouchSurface()
{
    touchSurface = std::make_unique<PaneTouchSurface>([this]() -> term::TerminalView& { return getTerminal(); });
    touchSurface->onSwipeFromTheLeftEdge = [this]
    {
        LOG("dv: closed by a swipe from the left edge");
        close();
    };
}

void DiffViewScreen::open(const term::TerminalConfig& config, const term::phone::DiffViewPane& paneToOpen)
{
    removeTerminal();
    pane = paneToOpen;
    chrome.title = describeDiffViewTitle(paneToOpen);
    chrome.sentence.clear();

    auto configWithNotice = config;
    configWithNotice.makeCommandShell = [this, makeShell = config.makeCommandShell, guard = std::weak_ptr<bool> {alive}](const std::string& paneId)
    {
        auto shell = makeShell(paneId);
        shell->onCommandNotInstalled = [this, guard](const term::MissingCommand& missing)
        {
            if (!guard.expired() && pane)
                showSentence(term::phone::describeDiffViewNotInstalled(missing, term::remote::getHostLabel(pane->host)));
        };
        return shell;
    };

    terminal = std::make_unique<term::TerminalView>(configWithNotice, paneToOpen.workingDirectory, paneToOpen.paneId, term::phone::diffViewCommand);
    terminal->onShellExit = [this] { handleDiffViewEnded(); };
    makeLinksOpenOnTap(*terminal);
    typing.connectTerminal(*terminal);
    makeTouchSurface();

    const auto children = getSubviews();

    for (auto* child: children)
        removeSubview(*child);

    addChildren({chromeHost, *terminal, *touchSurface});
    typing.addTo(*this);
    chrome.showsKeyboardButton = true;
    layOutParts();
}

// dv leaves the screen blank when it quits; text left there says why it could
// not start. The view goes on the next loop tick, never inside its own callback.
void DiffViewScreen::handleDiffViewEnded()
{
    if (terminal == nullptr || !pane)
        return;

    terminal->waitForParsedOutput();
    const auto sentence = term::phone::describeDiffViewFailure(terminal->captureVisibleText(), pane->workingDirectory);
    LOG("dv: ended");

    if (sentence.empty())
        return close();

    showSentence(sentence);
}

void DiffViewScreen::showSentence(const std::string& sentence)
{
    LOG("dv: shows \"", sentence, "\"");
    removeTerminal();
    chrome.sentence = sentence;
    chrome.repaint();
}

void DiffViewScreen::showWhyItCannotOpen(const std::string& sentence)
{
    removeTerminal();
    pane.reset();
    chrome.title = term::phone::diffViewCommand;
    showSentence(sentence);
}

void DiffViewScreen::close()
{
    removeTerminal();
    onClosed(std::exchange(pane, std::nullopt));
}

// The keyboard goes with the terminal it typed into.
void DiffViewScreen::removeTerminal()
{
    removeViewAndDestroyItSoon(*this, touchSurface);
    chrome.showsKeyboardButton = false;

    if (terminal == nullptr)
        return;

    typing.lowerKeyboard();
    typing.layOut({}, false);
    removeViewAndDestroyItSoon(*this, terminal);
}

term::TerminalView& DiffViewScreen::getTerminal()
{
    return *terminal;
}

void DiffViewScreen::resized()
{
    layOutParts();
}

void DiffViewScreen::safeAreaInsetsChanged()
{
    layOutParts();
}

void DiffViewScreen::setSoftwareKeyboardChange(const Graphics::SoftwareKeyboardChange& change)
{
    softwareKeyboardCoveredHeight = change.coveredHeight;
    layOutParts();
}

// The keyboard, while it is up, takes the place of the home indicator's inset.
Graphics::Insets DiffViewScreen::calculateInsetsAroundParts() const
{
    auto insets = getSafeAreaInsets();
    insets.bottom = std::max(insets.bottom, softwareKeyboardCoveredHeight);
    return insets;
}

bool DiffViewScreen::isKeyboardUp() const
{
    return softwareKeyboardCoveredHeight > 0.0f && terminal != nullptr;
}

// With the keyboard up the key row sits on it, over the bar.
void DiffViewScreen::layOutParts()
{
    const auto bounds = getLocalBounds();
    const auto paneArea = [&]
    {
        if (!isKeyboardUp())
        {
            typing.layOut({}, false);
            return calculatePaneScreenRects(bounds, getSafeAreaInsets()).pane;
        }

        const auto rects = calculateTypingRects(bounds, calculateInsetsAroundParts());
        typing.layOut(rects, true);
        return rects.pane;
    }();

    chromeHost.setBounds(bounds);

    if (touchSurface != nullptr)
        touchSurface->setBounds(paneArea);

    if (terminal != nullptr)
        terminal->setBounds(paneArea);

    chrome.repaint();
}

void DiffViewScreen::raiseKeyboard()
{
    if (terminal != nullptr)
        typing.raiseKeyboard();
}

void DiffViewScreen::performLaunchStep(const DiffViewLaunchStep& step)
{
    if (terminal == nullptr)
        return;

    switch (step.action)
    {
        case DiffViewLaunchAction::RaiseKeyboard:
            return raiseKeyboard();
        case DiffViewLaunchAction::TapText:
            return tapTextOnScreen(step.text);
        case DiffViewLaunchAction::TypeText:
            return typeText(step.text);
        case DiffViewLaunchAction::PressKeyRowKey:
            return typing.pressKeyRowKey(step.key);
    }
}

// A finger down and up again on the text, through the touch surface a real tap reaches.
void DiffViewScreen::tapTextOnScreen(const std::string& text)
{
    const auto position = findLastTextOnScreen(terminal->captureVisibleText(), text);

    if (!position)
    {
        LOG("dv step: no \"", text, "\" on screen to tap");
        return;
    }

    LOG("dv step: tap \"", text, "\" at row ", position->row, " column ", position->column);
    auto touch = Graphics::TouchEvent {};
    touch.id = tapFingerId;
    touch.pos = terminal->calculateGridCellCentre(position->row, position->column);
    touch.downPos = touch.pos;
    touchSurface->touchBegan(touch);
    touch.phase = Graphics::TouchPhase::Ended;
    touchSurface->touchEnded(touch);
}

// Each character as a key the keyboard sends, so a latched Ctrl takes the first.
void DiffViewScreen::typeText(const std::string& text)
{
    LOG("dv step: type ", text.size(), " bytes");

    for (const auto& character: splitIntoCharacters(text))
        terminal->dispatchKeyEvent(makeTypedKeyEvent(character));
}
} // namespace cowterm::mobile
