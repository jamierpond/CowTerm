#include "PaneScreenChrome.h"

#include "Multiplexer/HostColours.h"
#include "Multiplexer/TextLayout.h"
#include "Multiplexer/UnreadMark.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Logging.h>

#include <algorithm>
#include <chrono>

namespace cowterm::mobile
{
using namespace eacp;
using UI::Color;
using UI::Point;
using UI::Rect;

namespace
{
constexpr auto fontFamily = "JetBrains Mono";
constexpr auto topLineFontSize = 13.0f;
constexpr auto barFontSize = 15.0f;
constexpr auto barGlyphFontSize = 20.0f;
constexpr auto menuNameFontSize = 15.0f;
constexpr auto menuGestureFontSize = 12.0f;

constexpr auto sidePadding = 12.0f;
constexpr auto hostMarkSize = 8.0f;
constexpr auto hostMarkGap = 8.0f;
constexpr auto titleToLinkStateGap = 8.0f;
constexpr auto chipPadding = 10.0f;
constexpr auto chipCornerRadius = 8.0f;
constexpr auto keyGap = 3.0f;

// A lit key (latched Ctrl, compose on) also gets a bar under its label, so it
// is not told by its colour alone.
constexpr auto litKeyBarWidth = 16.0f;
constexpr auto litKeyBarHeight = 2.0f;
constexpr auto litKeyBarBelowCentre = 10.0f;

// iOS's own key repeat: a pause before the first repeat, then about twelve a second.
constexpr auto keyRepeatDelay = eacp::Time::MS {400};
constexpr auto keyRepeatInterval = eacp::Time::MS {80};
constexpr auto stopLabel = "Esc";
constexpr auto paneDotSize = 8.0f;
constexpr auto unreadPaneDotSize = 9.0f;
constexpr auto unreadCountSize = 16.0f;
constexpr auto unreadCountFontSize = 11.0f;

// From the ☰ glyph's centre to the count's, right and up.
constexpr auto unreadCountOffset = 11.0f;
constexpr auto ringThickness = 1.5f;
constexpr auto barDividerThickness = 1.0f;
constexpr auto pillCornerRadius = 16.0f;
constexpr auto menuCornerRadius = 14.0f;
constexpr auto menuMargin = 12.0f;
constexpr auto tallestMenuRow = 44.0f;
constexpr auto shortestMenuRow = 30.0f;

constexpr auto mutedTextAlpha = 0.55f;
constexpr auto greyedTextAlpha = 0.3f;
constexpr auto chipFillAlpha = 0.08f;
constexpr auto dividerAlpha = 0.12f;
constexpr auto menuBackdropAlpha = 0.5f;
constexpr auto barBrightening = 0.03f;
constexpr auto cardBrightening = 0.06f;

constexpr auto longPressDelay = Time::MS {500};

constexpr auto ansiRed = 1;
constexpr auto ansiGreen = 2;
constexpr auto ansiYellow = 3;
constexpr auto ansiBlue = 4;

constexpr auto previousSessionArrow = "\xe2\x80\xb9 ";
constexpr auto keyboardGlyph = "\xe2\x8c\xa8";
constexpr auto diffLabel = "diff";
constexpr auto menuGlyph = "\xe2\x98\xb0";

std::int64_t calculateMillisecondsSince(std::chrono::steady_clock::time_point then)
{
    const auto elapsed = std::chrono::steady_clock::now() - then;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

term::Rgb pickLinkStateColour(const term::Theme& theme, LinkState state)
{
    if (state == LinkState::Live)
        return theme.ansi[ansiGreen];

    if (state == LinkState::Connecting)
        return theme.ansi[ansiYellow];

    return theme.ansi[ansiRed];
}

void fillCircle(UI::Graphics& g, Point centre, float diameter)
{
    const auto radius = diameter / 2.0f;
    g.fillRoundedRect({centre.x - radius, centre.y - radius, diameter, diameter}, radius);
}

struct RingColours
{
    Color fill;
    Color ring;
};

// A pane dot for a pane that is not on screen: a ring, not a filled dot.
void drawHollowPaneDot(UI::Graphics& g, Point centre, RingColours colours)
{
    g.setColour(colours.ring);
    fillCircle(g, centre, paneDotSize);
    g.setColour(colours.fill);
    fillCircle(g, centre, paneDotSize - 2.0f * ringThickness);
}

UI::Font makeFont(float size)
{
    return UI::Font {fontFamily, size};
}

// An unread pane's dot: a yellow square edged in the bar's background, so it
// differs from the round dots by shape as well as colour.
void drawUnreadPaneDot(UI::Graphics& g, Point centre)
{
    const auto half = unreadPaneDotSize / 2.0f;
    g.setColour(term::toColor(term::getUnreadMarkFillColour()));
    g.fillRect({centre.x - half, centre.y - half, unreadPaneDotSize, unreadPaneDotSize});
}

// The count of other unread sessions, as a yellow circle with black digits at
// the top right of the ☰ button.
void drawUnreadCount(UI::Graphics& g, int count, Rect button)
{
    const auto centre = Point {button.center().x + unreadCountOffset, button.center().y - unreadCountOffset};
    const auto badge = Rect {centre.x - unreadCountSize / 2.0f, centre.y - unreadCountSize / 2.0f, unreadCountSize, unreadCountSize};
    g.setColour(term::toColor(term::getUnreadMarkFillColour()));
    fillCircle(g, centre, unreadCountSize);
    g.setColour(term::toColor(term::getUnreadMarkTextColour()));
    g.setFont(makeFont(unreadCountFontSize));
    g.drawText(std::to_string(count), badge, UI::Justification::Centred);
}

bool isPaneUnread(const PaneDescription& pane, int index)
{
    return std::ranges::find(pane.unreadPaneIndexes, index) != pane.unreadPaneIndexes.end();
}

void drawLitKeyBar(UI::Graphics& g, Rect keyFace)
{
    const auto centre = keyFace.center();
    const auto top = centre.y + litKeyBarBelowCentre;
    g.fillRect({centre.x - litKeyBarWidth / 2.0f, top, litKeyBarWidth, litKeyBarHeight});
}
} // namespace

TopLine::TopLine(const term::Theme& themeToUse)
    : theme(themeToUse)
{
}

void TopLine::paint(UI::Graphics& g)
{
    auto area = getLocalBounds().inset(sidePadding, 0.0f);
    g.setColour(term::toColor(theme.background).brighter(barBrightening));
    g.fillAll();
    g.setColour(term::toColor(theme.foreground).withAlpha(dividerAlpha));
    g.fillRect(getLocalBounds().fromBottom(barDividerThickness));

    g.setColour(term::toColor(hostColour));
    fillCircle(g, {area.x + hostMarkSize / 2.0f, area.center().y}, hostMarkSize);
    area.removeFromLeft(hostMarkSize + hostMarkGap);

    g.setFont(makeFont(topLineFontSize));
    const auto status =
        describeLinkStatus(link.state, calculateMillisecondsSince(link.pictureTakenAt));
    const auto statusArea = area.removeFromRight(g.measureText(status));

    g.setColour(term::toColor(pickLinkStateColour(theme, link.state)));
    g.drawText(status, statusArea, UI::Justification::Right);
    area.removeFromRight(titleToLinkStateGap);

    g.setColour(term::toColor(theme.foreground));
    const auto measure = [&g](const std::string& text) { return g.measureText(text); };
    const auto title = describePaneTitle(pane.hostName, pane.sessionName);
    g.drawText(term::truncateTextToWidth(title, area.w, measure), area);
}

BottomBar::BottomBar(const term::Theme& themeToUse)
    : theme(themeToUse)
    , font(makeFont(barFontSize))
{
    setInterceptsMouseClicks(true);
}

BottomBar::~BottomBar()
{
    *alive = false;
}

std::string BottomBar::describePreviousSession() const
{
    return previousSessionArrow + pane.previousSessionName.value_or("");
}

BottomBarLayout BottomBar::calculateLayout() const
{
    auto chipWidth = std::optional<float> {};

    if (pane.previousSessionName)
        chipWidth = previousSessionWidth;

    // The bar's own height at the top; below it the bar's colour runs on under
    // the home indicator, as Safari's does.
    const auto bar = getLocalBounds().fromTop(bottomBarHeight);
    return calculateBottomBarLayout(bar, chipWidth, pane.paneCount);
}

void BottomBar::paint(UI::Graphics& g)
{
    const auto background = term::toColor(theme.background).brighter(barBrightening);
    const auto foreground = term::toColor(theme.foreground);

    g.setColour(background);
    g.fillAll();
    g.setColour(foreground.withAlpha(dividerAlpha));
    g.fillRect(getLocalBounds().fromTop(barDividerThickness));

    g.setFont(font);
    previousSessionWidth = g.measureText(describePreviousSession()) + 2.0f * chipPadding;
    const auto layout = calculateLayout();

    if (layout.previousSession)
    {
        const auto chip = layout.previousSession->inset(0.0f, chipPadding);
        g.setColour(foreground.withAlpha(chipFillAlpha));
        g.fillRoundedRect(chip, chipCornerRadius);
        g.setColour(foreground);
        g.drawText(describePreviousSession(), chip, UI::Justification::Centred);
    }

    for (auto index = 0; index < (int) layout.paneDots.size(); ++index)
    {
        const auto centre = layout.paneDots[(std::size_t) index].center();

        if (index == pane.activePaneIndex)
        {
            g.setColour(term::toColor(theme.ansi[ansiBlue]));
            fillCircle(g, centre, paneDotSize);
        }
        else if (isPaneUnread(pane, index))
        {
            drawUnreadPaneDot(g, centre);
        }
        else
        {
            drawHollowPaneDot(g, centre, {background, foreground.withAlpha(mutedTextAlpha)});
        }
    }

    g.setColour(foreground);
    g.setFont(makeFont(barGlyphFontSize));
    g.drawText(keyboardGlyph, layout.keyboard, UI::Justification::Centred);
    g.drawText(menuGlyph, layout.overview, UI::Justification::Centred);

    if (pane.unreadSessionsElsewhere > 0)
        drawUnreadCount(g, pane.unreadSessionsElsewhere, layout.overview);

    g.setFont(font);
    g.drawText(diffLabel, layout.diff, UI::Justification::Centred);
}

void BottomBar::mouseDown(const UI::MouseEvent& event)
{
    LOG("bottom bar: finger down at ", event.position.x, ", ", event.position.y);
    isFingerDown = true;
    hasLongPressFired = false;
    ++pressGeneration;
    waitForLongPress();
}

void BottomBar::waitForLongPress()
{
    auto guard = std::weak_ptr<bool> {alive};
    const auto generation = pressGeneration;

    Threads::callAfter(longPressDelay,
                       [this, guard, generation]
                       {
                           if (guard.expired() || !isStillHeldSince(generation))
                               return;

                           hasLongPressFired = true;
                           actions.onOpenMenu();
                       });
}

bool BottomBar::isStillHeldSince(int generation) const
{
    return isFingerDown && generation == pressGeneration;
}

// A finger that starts to travel is a swipe, not a long press.
void BottomBar::mouseDrag(const UI::MouseEvent& event)
{
    if (hasMovedBeyondTapSlop(event.downPosition, event.position))
        ++pressGeneration;
}

void BottomBar::mouseUp(const UI::MouseEvent& event)
{
    isFingerDown = false;
    ++pressGeneration;

    if (hasLongPressFired)
        return;

    const auto gesture = classifyBottomBarRelease(event.downPosition, event.position);

    if (gesture == BottomBarGesture::Tap)
    {
        pressControlAt(event.downPosition);
        return;
    }

    if (gesture == BottomBarGesture::SwipeUp)
    {
        actions.onOverview();
        return;
    }

    const auto chosen = choosePaneAfterSwipe(pane.activePaneIndex, pane.paneCount, gesture);

    if (chosen != pane.activePaneIndex)
        actions.onChoosePane(chosen);
}

void BottomBar::pressControlAt(Point position)
{
    const auto hit = findBottomBarControlAt(calculateLayout(), moveOntoTheBarsRow(position));

    switch (hit.control)
    {
        case BottomBarControl::PreviousSession:
            actions.onPreviousSession();
            break;
        case BottomBarControl::PaneDot:
            actions.onChoosePane(hit.paneIndex);
            break;
        case BottomBarControl::Keyboard:
            actions.onKeyboard();
            break;
        case BottomBarControl::Diff:
            actions.onDiffView();
            break;
        case BottomBarControl::Overview:
            actions.onOverview();
            break;
        case BottomBarControl::None:
            break;
    }
}

NewLinesPill::NewLinesPill(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

void NewLinesPill::paint(UI::Graphics& g)
{
    g.setColour(term::toColor(theme.background));
    g.fillAll();
    g.setColour(term::toColor(theme.ansi[ansiBlue]));
    g.fillRoundedRect(getLocalBounds(), pillCornerRadius);
    g.setColour(term::toColor(theme.background));
    g.setFont(makeFont(barFontSize));
    g.drawText(formatNewLinesPill(newLineCount), getLocalBounds(), UI::Justification::Centred);
}

void NewLinesPill::mouseUp(const UI::MouseEvent&)
{
    onReturnToBottom();
}

KeyRow::KeyRow(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

bool KeyRow::isLit(KeyRowKey key) const
{
    if (key == KeyRowKey::Control)
        return isControlLatched;

    if (key == KeyRowKey::Composer)
        return isComposerOn;

    return false;
}

void KeyRow::paint(UI::Graphics& g)
{
    const auto foreground = term::toColor(theme.foreground);
    g.setColour(term::toColor(theme.background).brighter(barBrightening));
    g.fillAll();
    g.setFont(makeFont(barFontSize));

    for (const auto& slot: calculateKeyRowLayout(getLocalBounds(), page))
    {
        const auto keyFace = slot.bounds.inset(keyGap);

        if (isLit(slot.key))
            g.setColour(term::toColor(theme.ansi[ansiBlue]));
        else
            g.setColour(foreground.withAlpha(chipFillAlpha));

        g.fillRoundedRect(keyFace, chipCornerRadius);
        g.setColour(foreground);
        g.drawText(getKeyRowLabel(slot.key), keyFace, UI::Justification::Centred);

        if (isLit(slot.key))
            drawLitKeyBar(g, keyFace);
    }
}

std::optional<KeyRowKey> KeyRow::findKeyAt(Point position) const
{
    return findKeyRowKeyAt(calculateKeyRowLayout(getLocalBounds(), page), position);
}

void KeyRow::mouseDown(const UI::MouseEvent& event)
{
    ++pressGeneration;
    const auto key = findKeyAt(event.position);
    isRepeating = key && isRepeatingKeyRowKey(*key);

    if (!isRepeating)
        return;

    onKey(*key);
    repeatAfter(keyRepeatDelay, *key, pressGeneration);
}

void KeyRow::repeatAfter(Time::MS delay, KeyRowKey key, int generation)
{
    Threads::callAfter(delay,
                       [this, guard = std::weak_ptr<bool> {alive}, key, generation]
                       {
                           if (guard.expired() || generation != pressGeneration)
                               return;

                           onKey(key);
                           repeatAfter(keyRepeatInterval, key, generation);
                       });
}

void KeyRow::mouseUp(const UI::MouseEvent& event)
{
    ++pressGeneration;

    if (isRepeating || hasMovedBeyondTapSlop(event.downPosition, event.position))
        return;

    if (const auto key = findKeyAt(event.position))
        onKey(*key);
}

void KeyRow::mouseCancel(const UI::MouseEvent&)
{
    ++pressGeneration;
}

PromptButtons::PromptButtons(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

void PromptButtons::paint(UI::Graphics& g)
{
    const auto foreground = term::toColor(theme.foreground);
    g.setColour(term::toColor(theme.background));
    g.fillAll();

    if (!prompt)
        return;

    g.setFont(makeFont(barFontSize));
    auto rows = getLocalBounds();

    for (const auto& option: prompt->options)
    {
        const auto button = rows.removeFromTop(promptButtonHeight).inset(sidePadding, keyGap);
        g.setColour(foreground.withAlpha(chipFillAlpha));
        g.fillRoundedRect(button, chipCornerRadius);
        const auto textArea = button.inset(chipPadding, 0.0f);
        const auto label = term::truncateTextToWidth(
            std::to_string(option.number) + "  " + option.label,
            textArea.w,
            [&g](const std::string& text) { return g.measureText(text); });

        g.setColour(foreground);
        g.drawText(label, textArea);
    }
}

void PromptButtons::mouseUp(const UI::MouseEvent& event)
{
    if (!prompt || hasMovedBeyondTapSlop(event.downPosition, event.position))
        return;

    const auto row = (int) (event.position.y / promptButtonHeight);

    if (row < 0 || row >= (int) prompt->options.size())
        return;

    onChoose(*prompt, prompt->options[(std::size_t) row].number);
}

StopButton::StopButton(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

void StopButton::paint(UI::Graphics& g)
{
    g.setColour(term::toColor(theme.background).brighter(barBrightening));
    g.fillAll();
    g.setColour(term::toColor(theme.ansi[ansiRed]));
    g.fillRoundedRect(getLocalBounds().inset(keyGap * 2.0f), chipCornerRadius);
    g.setColour(term::toColor(theme.background));
    g.setFont(makeFont(barFontSize));
    g.drawText(stopLabel, getLocalBounds(), UI::Justification::Centred);
}

void StopButton::mouseUp(const UI::MouseEvent& event)
{
    if (!hasMovedBeyondTapSlop(event.downPosition, event.position))
        onStop();
}

ActionMenu::ActionMenu(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

float ActionMenu::calculateRowHeight() const
{
    const auto room = getHeight() - 2.0f * menuMargin;
    const auto fitted = room / (float) std::max((int) actions.size(), 1);
    return std::clamp(fitted, shortestMenuRow, tallestMenuRow);
}

// A card along the bottom, where the thumb that opened it already is.
Rect ActionMenu::calculateCardBounds() const
{
    const auto height = calculateRowHeight() * (float) actions.size();
    return getLocalBounds().inset(menuMargin).fromBottom(height);
}

void ActionMenu::paint(UI::Graphics& g)
{
    const auto foreground = term::toColor(theme.foreground);
    g.setColour(term::toColor(theme.background));
    g.fillAll();
    g.setColour(Color::black(menuBackdropAlpha));
    g.fillAll();

    const auto card = calculateCardBounds();
    g.setColour(term::toColor(theme.background).brighter(cardBrightening));
    g.fillRoundedRect(card, menuCornerRadius);

    auto rows = card.inset(sidePadding, 0.0f);
    const auto rowHeight = calculateRowHeight();

    for (const auto& action: actions)
    {
        auto row = rows.removeFromTop(rowHeight);
        const auto alpha = action.isBuilt ? 1.0f : greyedTextAlpha;

        g.setFont(makeFont(menuGestureFontSize));
        g.setColour(foreground.withAlpha(alpha * mutedTextAlpha));
        g.drawText(action.gesture, row, UI::Justification::Right);

        g.setFont(makeFont(menuNameFontSize));
        g.setColour(foreground.withAlpha(alpha));
        g.drawText(action.name, row);
    }
}

void ActionMenu::mouseUp(const UI::MouseEvent& event)
{
    const auto card = calculateCardBounds();

    if (!card.contains(event.position))
    {
        onClose();
        return;
    }

    const auto row = (int) ((event.position.y - card.y) / calculateRowHeight());

    if (row < 0 || row >= (int) actions.size())
        return;

    const auto& action = actions[(std::size_t) row];

    if (!action.isBuilt)
        return;

    onClose();
    action.perform();
}
} // namespace cowterm::mobile
