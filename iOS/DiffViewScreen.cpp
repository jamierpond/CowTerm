#include "DiffViewScreen.h"

#include "Multiplexer/RemoteLocation.h"
#include "Multiplexer/TextLayout.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Logging.h>

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

    g.setFont(makeFont(barFontSize));
    g.drawText(closeLabel, rects.bottomBar.inset(sidePadding, 0.0f));

    paintSentence(g, rects.pane);
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

// All of the bar closes dv.
void DiffViewChrome::mouseUp(const UI::MouseEvent& event)
{
    if (calculateRects().bottomBar.contains(event.position))
    {
        LOG("dv: closed from the bar");
        onClose();
    }
}

DiffViewScreen::DiffViewScreen()
{
    chromeHost.setBackgroundColour(term::toColor(theme.background));
    chromeHost.setRootComponent(chrome);
    chrome.onClose = [this] { close(); };
    touchSurface.onSwipeFromTheLeftEdge = [this]
    {
        LOG("dv: closed by a swipe from the left edge");
        close();
    };
    addChildren({chromeHost, touchSurface});
    touchSurface.setVisible(false);
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
        shell->onCommandNotInstalled = [this, guard](const std::string&)
        {
            if (!guard.expired() && pane)
                showSentence(term::phone::describeDiffViewNotInstalled(term::remote::getHostLabel(pane->host)));
        };
        return shell;
    };

    terminal = std::make_unique<term::TerminalView>(configWithNotice, paneToOpen.workingDirectory, paneToOpen.paneId, term::phone::diffViewCommand);
    terminal->onShellExit = [this] { handleDiffViewEnded(); };
    makeLinksOpenOnTap(*terminal);

    removeSubview(touchSurface);
    addChildren({*terminal, touchSurface});
    touchSurface.setVisible(true);
    resized();
    chrome.repaint();
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

void DiffViewScreen::removeTerminal()
{
    touchSurface.setVisible(false);

    if (terminal == nullptr)
        return;

    removeSubview(*terminal);
    Threads::callAsync([leaving = std::shared_ptr<term::TerminalView> {std::move(terminal)}] {});
}

term::TerminalView& DiffViewScreen::getTerminal()
{
    return *terminal;
}

void DiffViewScreen::resized()
{
    const auto bounds = getLocalBounds();
    const auto rects = calculatePaneScreenRects(bounds, getSafeAreaInsets());
    chromeHost.setBounds(bounds);
    touchSurface.setBounds(rects.pane);

    if (terminal != nullptr)
        terminal->setBounds(rects.pane);

    chrome.repaint();
}

void DiffViewScreen::safeAreaInsetsChanged()
{
    resized();
}
} // namespace cowterm::mobile
