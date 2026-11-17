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

PaneTouchSurface::PaneTouchSurface(std::function<term::TerminalView&()> findTerminalToUse)
    : findTerminal(std::move(findTerminalToUse))
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
        findTerminal().beginTouchScroll(*fingerDown);
    }

    if (isScrolling)
        findTerminal().moveTouchScroll(event);
}

void PaneTouchSurface::touchEnded(const TouchEvent& event)
{
    if (!fingerDown || event.id != fingerDown->id)
        return;

    if (isScrolling)
        findTerminal().endTouchScroll(event);
    else if (event.phase == Graphics::TouchPhase::Ended)
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
    bottomBar.actions.onKeyboard = [this] { raiseKeyboard(); };
    bottomBar.actions.onOpenMenu = [this] { openMenu(); };
    bottomBar.actions.onOverview = [this] { actions.onOverview(); };
    pill.onReturnToBottom = [this] { returnToLiveBottom(); };
    menu.onClose = [this] { closeMenu(); };
    typing.onLayoutChanged = [this] { layOutParts(); };

    pillHost.setVisible(false);
    menuHost.setVisible(false);
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

    terminal = std::make_unique<term::TerminalView>(config, std::string {}, paneId);
    terminal->onScrolledBackStateChanged = [this](term::ScrolledBackState state)
    { showScrolledBackState(state); };

    addChildren({*terminal, touchSurface, topLineHost, bottomBarHost, pillHost});
    typing.addTo(*this);
    typing.connectTerminal(*terminal);
    addSubview(menuHost);
    pillHost.setVisible(false);
    showLinkStateOnPane();
    layOutParts();
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

    // The tallest keyboard seen, so moving between the composer's keyboard and
    // raw mode's, which has no suggestion bar, never resizes the shell.
    if (change.coveredHeight > 0.0f && !isLandscape())
    {
        portraitSoftwareKeyboardHeight =
            std::max(portraitSoftwareKeyboardHeight.value_or(0.0f), change.coveredHeight);
    }

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
    const auto paneWidth = terminal->getLocalBounds().w;

    if (paneWidth <= 0.0f)
        return;

    if (isLandscape())
    {
        terminal->setFixedGridSize(std::nullopt);
        return;
    }

    auto heights = KeyboardUpHeights {};
    heights.screenHeight = getLocalBounds().h;
    heights.safeAreaTop = getSafeAreaInsets().top;
    heights.softwareKeyboardHeight =
        portraitSoftwareKeyboardHeight.value_or(defaultSoftwareKeyboardHeight);

    const auto keyboardUpHeight = calculateKeyboardUpPaneHeight(heights);
    const auto grid = terminal->calculateGridSizeFor({paneWidth, keyboardUpHeight});
    LOG("pane grid ", grid.cols, " x ", grid.rows, " (keyboard-up height ", keyboardUpHeight, ")");
    terminal->setFixedGridSize(grid);
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

// With the keyboard up the bottom bar gives way to the composer and key row.
void PaneScreen::layOutParts()
{
    const auto bounds = getLocalBounds();
    const auto insets = calculateInsetsAroundParts();
    auto paneArea = Graphics::Rect {};

    if (isKeyboardUp())
    {
        const auto rects = calculateTypingRects(bounds, insets, typing.showsComposer());
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

    bottomBarHost.setVisible(!isKeyboardUp());
    terminal->setBounds(paneArea);
    touchSurface.setBounds(paneArea);
    pillHost.setBounds(calculateNewLinesPillBounds(paneArea, pillWidth));
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

void PaneScreen::raiseKeyboard()
{
    typing.raiseKeyboard();
}

void PaneScreen::switchToRawMode()
{
    typing.switchToRawMode();
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
