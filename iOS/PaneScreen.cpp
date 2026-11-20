#include "PaneScreen.h"

#include "ScreenAwake.h"

#include "Multiplexer/Config.h"
#include "Multiplexer/HostColours.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Utils/Logging.h>

#include <algorithm>

namespace cowterm::mobile
{
using namespace eacp;
using Graphics::TouchEvent;

namespace
{
constexpr auto notLiveOpacity = 0.45f;
constexpr auto refreshesPerSecond = 1;
constexpr auto outputWatchesPerSecond = 4;

// How long after the last output the auto-lock stays off.
constexpr auto keepAwakeAfterOutput = std::chrono::seconds {30};
constexpr auto pillWidth = 132.0f;

Graphics::MouseEvent makeTapMouseEvent(const TouchEvent& touch,
                                       Graphics::MouseEventType type)
{
    auto mouse = Graphics::MouseEvent {};
    mouse.pos = touch.pos;
    mouse.downPos = touch.downPos;
    mouse.type = type;
    mouse.timestamp = touch.timestamp;
    mouse.clickCount = touch.tapCount;
    mouse.fromTouch = true;
    return mouse;
}
} // namespace

void makeLinksOpenOnTap(term::TerminalView& terminal)
{
    terminal.setUnderlinesEveryLink(true);
    terminal.onOpenHyperlink = [](const std::string& uri)
    {
        LOG("pane screen: opening a link");
        Apps::openExternalURL(uri);
    };
}

PaneTouchSurface::PaneTouchSurface(std::function<term::TerminalView&()> findTerminalToUse)
    : findTerminal(std::move(findTerminalToUse))
{
    setHandlesTouchEvents(true);
}

void PaneTouchSurface::touchBegan(const TouchEvent& event)
{
    if (fingerDown)
        return;

    LOG("pane screen: finger down at ", event.pos.x, ", ", event.pos.y);
    fingerDown = event;
    isScrolling = false;
}

void PaneTouchSurface::touchMoved(const TouchEvent& event)
{
    if (!fingerDown || event.id != fingerDown->id)
        return;

    // A back swipe from the edge never scrolls.
    if (isStartingAtTheLeftEdge(fingerDown->pos))
        return;

    if (!isScrolling && hasMovedBeyondTapSlop(fingerDown->pos, event.pos))
    {
        isScrolling = true;
        LOG("pane screen: drag scrolls from ", fingerDown->pos.x, ", ", fingerDown->pos.y);
        findTerminal().beginTouchScroll(*fingerDown);
    }

    if (isScrolling)
        findTerminal().moveTouchScroll(event);
}

void PaneTouchSurface::touchEnded(const TouchEvent& event)
{
    if (!fingerDown || event.id != fingerDown->id)
        return;

    const auto isBackSwipe = isSwipeFromTheLeftEdge(fingerDown->pos, event.pos);

    if (isScrolling)
        findTerminal().endTouchScroll(event);
    else if (isBackSwipe)
        onSwipeFromTheLeftEdge();
    else if (event.phase == Graphics::TouchPhase::Ended && !hasMovedBeyondTapSlop(fingerDown->pos, event.pos))
        clickTerminalAt(event);

    fingerDown.reset();
    isScrolling = false;
}

void PaneTouchSurface::clickTerminalAt(const TouchEvent& event)
{
    auto& terminal = findTerminal();
    terminal.dispatchMouseEvent(makeTapMouseEvent(event, Graphics::MouseEventType::Down));
    terminal.dispatchMouseEvent(makeTapMouseEvent(event, Graphics::MouseEventType::Up));
}

PaneScreen::PaneScreen(const term::TerminalConfig& config, const std::string& paneId)
    : theme(config.theme)
    , refreshTimer([this] { refreshWhatChangesOverTime(); }, refreshesPerSecond)
    , outputWatchTimer([this] { watchPaneOutput(); }, outputWatchesPerSecond)
{
    const auto background = term::toColor(theme.background);

    for (auto* host: {&topLineHost, &bottomBarHost, &promptHost})
        host->setBackgroundColour(background);

    topLineHost.setRootComponent(topLine);
    bottomBarHost.setRootComponent(bottomBar);
    promptHost.setRootComponent(promptButtons);
    promptButtons.onChoose = [this](const PermissionPrompt& drawnFor, int optionNumber)
    { answerPrompt(drawnFor, optionNumber); };

    bottomBar.actions.onPreviousSession = [this] { actions.onPreviousSession(); };
    bottomBar.actions.onChoosePane = [this](int index) { actions.onChoosePane(index); };
    bottomBar.actions.onKeyboard = [this]
    {
        LOG("bottom bar: keyboard");
        raiseKeyboard();
    };
    bottomBar.actions.onOpenMenu = [this] { openMenu(); };
    bottomBar.actions.onDiffView = [this]
    {
        LOG("bottom bar: diff");
        actions.onDiffView();
    };
    touchSurface.onSwipeFromTheLeftEdge = [this]
    {
        LOG("pane screen: back swipe from the left edge");
        actions.onOverview();
    };
    bottomBar.actions.onOverview = [this]
    {
        LOG("bottom bar: overview");
        actions.onOverview();
    };

    showPane(config, paneId);
}

term::TerminalView& PaneScreen::getTerminal()
{
    return *terminal;
}

// The old view's shell detaches as it goes, which releases the old pane to
// the daemon; the new one adopts its pane and claims its size.
void PaneScreen::showPane(const term::TerminalConfig& config, const std::string& paneId)
{
    const auto children = getSubviews();

    for (auto* child: children)
        removeSubview(*child);

    auto watchedConfig = config;
    watchedConfig.makeShell = [makePaneShell = config.makeShell, flag = outputArrived](const std::string& id)
    { return std::make_unique<OutputWatchingShell>(makePaneShell(id), flag); };

    terminal = std::make_unique<term::TerminalView>(watchedConfig, std::string {}, paneId);
    terminal->onScrolledBackStateChanged = [this](term::ScrolledBackState state)
    { showScrolledBackState(state); };
    terminal->confirmUnsafePaste = [this](const std::string& text, const Callback& paste)
    { pasteConfirmation.ask(text, paste); };
    makeLinksOpenOnTap(*terminal);

    addChildren({*terminal, touchSurface, topLineHost, bottomBarHost, promptHost});
    typing.addTo(*this);
    typing.connectTerminal(*terminal);
    removeViewAndDestroyItSoon(*this, pill);
    raiseMenu();
    promptButtons.prompt.reset();
    showLinkStateOnPane();
    layOutParts();
}

void PaneScreen::setHostColourConfig(const term::AppConfig& config)
{
    hostColourConfig = config;
    setPaneDescription(pane);
}

void PaneScreen::setPaneDescription(const PaneDescription& paneToShow)
{
    pane = paneToShow;
    topLine.pane = pane;
    topLine.hostColour = term::getColourBlindSafeHostRowColours(hostColourConfig, pane.hostName).stripe;
    bottomBar.pane = pane;
    topLine.repaint();
    bottomBar.repaint();
}

void PaneScreen::setLinkStatus(const LinkStatus& linkToShow)
{
    link = linkToShow;
    topLine.link = link;
    topLine.repaint();
    showLinkStateOnPane();
}

void PaneScreen::showLinkStateOnPane()
{
    if (isLive())
        terminal->setOpacity(1.0f);
    else
        terminal->setOpacity(notLiveOpacity);
}

bool PaneScreen::isLive() const
{
    return link.state == LinkState::Live;
}

void PaneScreen::setSoftwareKeyboardChange(const Graphics::SoftwareKeyboardChange& change)
{
    softwareKeyboardCoveredHeight = change.coveredHeight;
    layOutParts();
}

void PaneScreen::resized()
{
    layOutParts();
}

void PaneScreen::safeAreaInsetsChanged()
{
    const auto insets = getSafeAreaInsets();
    LOG("safe area top ", insets.top, " left ", insets.left, " bottom ", insets.bottom,
        " right ", insets.right);
    layOutParts();
}

// The keyboard, while it is up, takes the place of the home indicator's inset.
Graphics::Insets PaneScreen::calculateInsetsAroundParts() const
{
    auto insets = getSafeAreaInsets();
    insets.bottom = std::max(insets.bottom, softwareKeyboardCoveredHeight);
    return insets;
}

bool PaneScreen::isKeyboardUp() const
{
    return softwareKeyboardCoveredHeight > 0.0f;
}

// With the keyboard up the bottom bar gives way to the key row.
void PaneScreen::layOutParts()
{
    const auto bounds = getLocalBounds();
    const auto insets = calculateInsetsAroundParts();
    auto paneArea = Graphics::Rect {};

    if (isKeyboardUp())
    {
        const auto rects = calculateTypingRects(bounds, insets);
        paneArea = rects.pane;
        topLineHost.setBounds(rects.topLine);
        typing.layOut(rects, true);
    }
    else
    {
        const auto rects = calculatePaneScreenRects(bounds, insets);
        paneArea = rects.pane;
        topLineHost.setBounds(rects.topLine);
        bottomBarHost.setBounds(rects.bottomBar.withHeight(bounds.bottom() - rects.bottomBar.y));
        typing.layOut({}, false);
    }

    // In landscape with the keyboard up the buttons can be all the pane has room for.
    const auto promptHeight = std::min(calculatePromptButtonsHeight(), paneArea.h);
    promptHost.setBounds(paneArea.removeFromBottom(promptHeight));
    showOrHideLettingTouchesThrough(promptHost, promptButtons.prompt.has_value());
    showOrHideLettingTouchesThrough(bottomBarHost, !isKeyboardUp());
    terminal->setBounds(paneArea);
    touchSurface.setBounds(paneArea);

    if (pill != nullptr)
        pill->setBounds(calculateNewLinesPillBounds(paneArea, pillWidth));

    if (menu != nullptr)
        menu->setBounds(bounds.inset(getSafeAreaInsets()));

    // A new width rewraps the screen with no output to say so: read it again.
    outputArrived->hasArrived = true;
}

float PaneScreen::calculatePromptButtonsHeight() const
{
    if (!promptButtons.prompt)
        return 0.0f;

    return promptButtonHeight * (float) promptButtons.prompt->options.size();
}

// Output raises the flag on the shell's thread; here, on the main thread, the
// screen looks for a Claude prompt again and holds the auto-lock off.
void PaneScreen::watchPaneOutput()
{
    const auto now = std::chrono::steady_clock::now();

    if (outputArrived->hasArrived.exchange(false))
    {
        lastOutputAt = now;
        showClaudePrompt(findPermissionPrompt(terminal->captureVisibleText()));
    }

    setScreenKeptAwake(now - lastOutputAt < keepAwakeAfterOutput);
}

// The layout changes only when buttons come, go or change in number.
void PaneScreen::showClaudePrompt(std::optional<PermissionPrompt> prompt)
{
    const auto oldHeight = calculatePromptButtonsHeight();
    promptButtons.prompt = std::move(prompt);
    promptButtons.repaint();

    if (calculatePromptButtonsHeight() != oldHeight)
        layOutParts();
}

// A tap drawn for a prompt that is no longer on screen sends nothing; the
// buttons change to the prompt that is.
void PaneScreen::answerPrompt(const PermissionPrompt& drawnFor, int optionNumber)
{
    const auto onScreen = findPermissionPrompt(terminal->captureVisibleText());

    if (const auto keys = chooseKeysForTappedOption(drawnFor, optionNumber, onScreen))
    {
        terminal->sendText(*keys);
        return;
    }

    LOG("prompt changed before the tap landed; showing the new one");
    showClaudePrompt(onScreen);
}

void PaneScreen::showScrolledBackState(term::ScrolledBackState state)
{
    if (!state.isScrolledBack)
        return removeViewAndDestroyItSoon(*this, pill);

    showNewLinesPill();
    pill->component.newLineCount = state.linesArrivedWhileScrolledBack;
    pill->component.repaint();
}

// Above the pane, and below the menu when the menu is open.
void PaneScreen::showNewLinesPill()
{
    if (pill != nullptr)
        return;

    pill = std::make_unique<HostedComponent<NewLinesPill>>(theme);
    pill->component.onReturnToBottom = [this] { returnToLiveBottom(); };
    addSubview(*pill);
    raiseMenu();
    layOutParts();
}

void PaneScreen::raiseMenu()
{
    if (menu == nullptr)
        return;

    removeSubview(*menu);
    addSubview(*menu);
}

// Only the age of a picture that is not live changes with nothing happening.
void PaneScreen::refreshWhatChangesOverTime()
{
    if (!isLive())
        topLine.repaint();
}

void PaneScreen::raiseKeyboard()
{
    typing.raiseKeyboard();
}

void PaneScreen::showKeyRowSecondPage()
{
    typing.showKeyRowSecondPage();
}

void PaneScreen::returnToLiveBottom()
{
    terminal->scrollToLiveBottom();
}

std::vector<MenuAction> PaneScreen::listMenuActions()
{
    const auto hasOtherPanes = pane.paneCount > 1;
    const auto nextPane = std::min(pane.activePaneIndex + 1, pane.paneCount - 1);
    const auto previousPane = std::max(pane.activePaneIndex - 1, 0);

    return {
        {"Keyboard", "\xe2\x8c\xa8", true, [this] { raiseKeyboard(); }},
        {"Back to the bottom", "\xe2\x86\x93 n new", true, [this] { returnToLiveBottom(); }},
        {"Next pane", "swipe left on bar", hasOtherPanes,
         [this, nextPane] { actions.onChoosePane(nextPane); }},
        {"Previous pane", "swipe right on bar", hasOtherPanes,
         [this, previousPane] { actions.onChoosePane(previousPane); }},
        {"Previous session", "\xe2\x80\xb9 or edge swipe",
         false},
        {"Overview", "\xe2\x98\xb0 or swipe up", true, [this] { actions.onOverview(); }},
        {"Select", "long press", false},
        {"Search", "", false},
        {"Text size", "pinch", false},
        {"Diff", "diff", true, [this] { actions.onDiffView(); }},
        {"Close pane", "", false},
    };
}

void PaneScreen::openMenu()
{
    if (menu == nullptr)
    {
        menu = std::make_unique<HostedComponent<ActionMenu>>(theme);
        menu->component.onClose = [this] { closeMenu(); };
        addSubview(*menu);
        layOutParts();
    }

    menu->component.actions = listMenuActions();
    menu->component.repaint();
}

void PaneScreen::closeMenu()
{
    removeViewAndDestroyItSoon(*this, menu);
}
} // namespace cowterm::mobile
