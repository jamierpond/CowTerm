#include "PaneScreen.h"

#include "Multiplexer/Config.h"
#include "Multiplexer/HostColours.h"

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

PaneTouchSurface::PaneTouchSurface(term::TerminalView& terminalToUse)
    : terminal(terminalToUse)
{
    setHandlesTouchEvents(true);
}

void PaneTouchSurface::touchBegan(const TouchEvent& event)
{
    if (fingerDown)
        return;

    fingerDown = event;
    isScrolling = false;
}

void PaneTouchSurface::touchMoved(const TouchEvent& event)
{
    if (!fingerDown || event.id != fingerDown->id)
        return;

    if (!isScrolling && hasMovedBeyondTapSlop(fingerDown->pos, event.pos))
    {
        isScrolling = true;
        terminal.beginTouchScroll(*fingerDown);
    }

    if (isScrolling)
        terminal.moveTouchScroll(event);
}

void PaneTouchSurface::touchEnded(const TouchEvent& event)
{
    if (!fingerDown || event.id != fingerDown->id)
        return;

    if (isScrolling)
        terminal.endTouchScroll(event);
    else if (event.phase == Graphics::TouchPhase::Ended)
        clickTerminalAt(event);

    fingerDown.reset();
    isScrolling = false;
}

void PaneTouchSurface::clickTerminalAt(const TouchEvent& event)
{
    terminal.dispatchMouseEvent(makeTapMouseEvent(event, Graphics::MouseEventType::Down));
    terminal.dispatchMouseEvent(makeTapMouseEvent(event, Graphics::MouseEventType::Up));
}

PaneScreen::PaneScreen(const term::TerminalConfig& config, const std::string& paneId)
    : terminal(config, {}, paneId)
    , theme(config.theme)
    , refreshTimer([this] { refreshWhatChangesOverTime(); }, refreshesPerSecond)
{
    const auto background = term::toColor(theme.background);

    for (auto* host: {&topLineHost, &bottomBarHost, &pillHost, &menuHost})
        host->setBackgroundColour(background);

    topLineHost.setRootComponent(topLine);
    bottomBarHost.setRootComponent(bottomBar);
    pillHost.setRootComponent(pill);
    menuHost.setRootComponent(menu);

    bottomBar.actions.onPreviousSession = [this] { actions.onPreviousSession(); };
    bottomBar.actions.onChoosePane = [this](int index) { actions.onChoosePane(index); };
    bottomBar.actions.onKeyboard = [this] { showSoftwareKeyboard(); };
    bottomBar.actions.onOpenMenu = [this] { openMenu(); };
    pill.onReturnToBottom = [this] { returnToLiveBottom(); };
    terminal.onScrolledBackStateChanged = [this](term::ScrolledBackState state)
    { showScrolledBackState(state); };
    menu.onClose = [this] { closeMenu(); };

    addChildren({terminal, touchSurface, topLineHost, bottomBarHost, pillHost, menuHost});
    pillHost.setVisible(false);
    menuHost.setVisible(false);
}

void PaneScreen::setPaneDescription(const PaneDescription& paneToShow)
{
    pane = paneToShow;
    topLine.pane = pane;
    topLine.hostColour = term::getHostColour(term::AppConfig {}, pane.hostName);
    bottomBar.pane = pane;
    topLine.repaint();
    bottomBar.repaint();
}

void PaneScreen::setLinkStatus(const LinkStatus& linkToShow)
{
    link = linkToShow;
    topLine.link = link;
    topLine.repaint();

    if (isLive())
        terminal.setOpacity(1.0f);
    else
        terminal.setOpacity(notLiveOpacity);
}

bool PaneScreen::isLive() const
{
    return link.state == LinkState::Live;
}

void PaneScreen::setSoftwareKeyboardChange(const Graphics::SoftwareKeyboardChange& change)
{
    softwareKeyboardCoveredHeight = change.coveredHeight;

    if (change.coveredHeight > 0.0f && !isLandscape())
        portraitSoftwareKeyboardHeight = change.coveredHeight;

    layOutParts();
}

bool PaneScreen::isLandscape() const
{
    return getLocalBounds().w > getLocalBounds().h;
}

// In landscape the keyboard, composer and key row leave about four rows, too
// few to fix a shell at, so there the grid follows the pane instead.
void PaneScreen::fixGridAtKeyboardUpSize()
{
    const auto paneWidth = terminal.getLocalBounds().w;

    if (paneWidth <= 0.0f)
        return;

    if (isLandscape())
    {
        terminal.setFixedGridSize(std::nullopt);
        return;
    }

    auto heights = KeyboardUpHeights {};
    heights.screenHeight = getLocalBounds().h;
    heights.safeAreaTop = getSafeAreaInsets().top;
    heights.softwareKeyboardHeight =
        portraitSoftwareKeyboardHeight.value_or(defaultSoftwareKeyboardHeight);

    const auto keyboardUpHeight = calculateKeyboardUpPaneHeight(heights);
    const auto grid = terminal.calculateGridSizeFor({paneWidth, keyboardUpHeight});
    LOG("pane grid ", grid.cols, " x ", grid.rows, " (keyboard-up height ", keyboardUpHeight, ")");
    terminal.setFixedGridSize(grid);
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

void PaneScreen::layOutParts()
{
    const auto bounds = getLocalBounds();
    const auto rects = calculatePaneScreenRects(bounds, calculateInsetsAroundParts());

    terminal.setBounds(rects.pane);
    touchSurface.setBounds(rects.pane);
    topLineHost.setBounds(rects.topLine);
    bottomBarHost.setBounds(rects.bottomBar.withHeight(bounds.bottom() - rects.bottomBar.y));
    pillHost.setBounds(calculateNewLinesPillBounds(rects.pane, pillWidth));
    menuHost.setBounds(bounds.inset(getSafeAreaInsets()));
    fixGridAtKeyboardUpSize();
}

void PaneScreen::showScrolledBackState(term::ScrolledBackState state)
{
    pill.newLineCount = state.linesArrivedWhileScrolledBack;
    pill.repaint();
    pillHost.setVisible(state.isScrolledBack);
}

// Only the age of a picture that is not live changes with nothing happening.
void PaneScreen::refreshWhatChangesOverTime()
{
    if (!isLive())
        topLine.repaint();
}

void PaneScreen::showSoftwareKeyboard()
{
    terminal.setWantsTextInput(true);
    terminal.focus();
}

void PaneScreen::returnToLiveBottom()
{
    terminal.scrollToLiveBottom();
}

std::vector<MenuAction> PaneScreen::listMenuActions()
{
    const auto hasOtherPanes = pane.paneCount > 1;
    const auto nextPane = std::min(pane.activePaneIndex + 1, pane.paneCount - 1);
    const auto previousPane = std::max(pane.activePaneIndex - 1, 0);

    return {
        {"Keyboard", "\xe2\x8c\xa8", true, [this] { showSoftwareKeyboard(); }},
        {"Back to the bottom", "\xe2\x86\x93 n new", true, [this] { returnToLiveBottom(); }},
        {"Next pane", "swipe left on bar", hasOtherPanes,
         [this, nextPane] { actions.onChoosePane(nextPane); }},
        {"Previous pane", "swipe right on bar", hasOtherPanes,
         [this, previousPane] { actions.onChoosePane(previousPane); }},
        {"Previous session", "\xe2\x80\xb9 or edge swipe",
         false},
        {"Overview", "\xe2\x98\xb0 or swipe up", false},
        {"Select", "long press", false},
        {"Search", "", false},
        {"Text size", "pinch", false},
        {"git", "git", false},
        {"Close pane", "", false},
    };
}

void PaneScreen::openMenu()
{
    menu.actions = listMenuActions();
    menu.repaint();
    menuHost.setVisible(true);
}

void PaneScreen::closeMenu()
{
    menuHost.setVisible(false);
}
} // namespace cowterm::mobile
