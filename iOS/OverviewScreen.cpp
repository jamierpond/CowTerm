#include "OverviewScreen.h"

#include "PaneScreenLayout.h"

#include "Multiplexer/Config.h"
#include "Multiplexer/HostColours.h"
#include "Multiplexer/PaletteItem.h"
#include "Multiplexer/RemoteLocation.h"
#include "Multiplexer/TextLayout.h"
#include "Multiplexer/UnreadMark.h"

#include <eacp/Core/Utils/Logging.h>

#include <algorithm>

namespace cowterm::mobile
{
using namespace eacp;
using term::overview::OverviewRow;
using UI::Color;
using UI::Rect;

namespace
{
constexpr auto fontFamily = "JetBrains Mono";
constexpr auto nameFontSize = 16.0f;
constexpr auto detailFontSize = 13.0f;
constexpr auto searchFontSize = 16.0f;
constexpr auto bannerFontSize = 15.0f;
constexpr auto bannerLineHeight = 24.0f;

constexpr auto sidePadding = 12.0f;
constexpr auto stripeWidth = 4.0f;
constexpr auto stripeGap = 10.0f;
constexpr auto cardPadding = 12.0f;
constexpr auto cornerRadius = 10.0f;

constexpr auto lineGap = 8.0f;
constexpr auto unreadPillFontSize = 12.0f;
constexpr auto unreadPillSidePadding = 7.0f;
constexpr auto unreadPillHeightFraction = 0.8f;
constexpr auto pressedOutlineThickness = 2.0f;

// A pressed row darkens and gains an outline, so it shows without telling hue
// from hue, and darker keeps the light text readable.
constexpr auto pressedDarkening = 0.35f;
constexpr auto pressedBlend = term::Rgb {0x000000};

constexpr auto ansiRed = 1;
constexpr auto ansiBlue = 4;

constexpr auto searchPlaceholder = "Search sessions and projects";

UI::Font makeFont(float size)
{
    return UI::Font {fontFamily, size};
}

term::PaletteItem::Kind findPaletteItemKind(const OverviewRow& row)
{
    if (row.isProject)
        return term::PaletteItem::Kind::Project;

    return term::PaletteItem::Kind::Session;
}

// The Mac palette's mark, so an open session and a project differ without
// colour: ✳ Claude, ● a session, ▸ a project with none open.
std::string describeSessionName(const OverviewRow& row)
{
    auto item = term::PaletteItem {};
    item.kind = findPaletteItemKind(row);
    item.claude = row.isClaude;
    return term::getPaletteItemIcon(item) + " " + row.sessionName;
}

term::HostRowColours darkenForPress(term::HostRowColours colours)
{
    colours.background = term::blendColours(colours.background, pressedBlend, pressedDarkening);
    return colours;
}

void drawPressedOutline(UI::Graphics& g, const term::HostRowColours& colours, Rect bounds)
{
    g.setColour(term::toColor(colours.text));
    g.drawRoundedRect(bounds, cornerRadius, pressedOutlineThickness);
}

// The machine's colour behind the whole card or row, at full strength down its
// left edge; returns the area left for text.
Rect paintHostBackground(UI::Graphics& g, const term::HostRowColours& colours, Rect bounds)
{
    g.setColour(term::toColor(colours.background));
    g.fillRoundedRect(bounds, cornerRadius);
    g.setColour(term::toColor(colours.stripe));
    g.fillRect(bounds.removeFromLeft(stripeWidth).inset(0.0f, cornerRadius));
    bounds.removeFromLeft(stripeGap);
    bounds.removeFromRight(sidePadding);
    return bounds;
}

std::string describeHost(const std::string& host)
{
    return term::remote::getHostLabel(host);
}

struct TextLine
{
    std::string left;
    std::string right;
};

// The left text is cut short where it would run into the right one.
void drawTextLine(UI::Graphics& g, const TextLine& text, Rect line)
{
    const auto rightWidth = g.measureText(text.right);
    g.drawText(text.right, line.fromRight(rightWidth), UI::Justification::Right);
    line.removeFromRight(rightWidth + lineGap);
    const auto measure = [&g](const std::string& part) { return g.measureText(part); };
    g.drawText(term::truncateTextToWidth(text.left, line.w, measure), line);
}

// The unread mark (UnreadMark.h) at the left of line: a yellow pill naming the
// event in black, the same mark the Mac draws. Returns its width.
float paintUnreadPill(UI::Graphics& g, const term::UnreadMark& mark, Rect line)
{
    const auto word = term::describeUnreadMark(mark);
    g.setFont(makeFont(unreadPillFontSize));
    const auto width = g.measureText(word) + 2.0f * unreadPillSidePadding;
    const auto height = line.h * unreadPillHeightFraction;
    const auto pill = Rect {line.x, line.center().y - height / 2.0f, width, height};

    g.setColour(term::toColor(term::getUnreadMarkFillColour()));
    g.fillRoundedRect(pill, height / 2.0f);
    g.setColour(term::toColor(term::getUnreadMarkTextColour()));
    g.drawText(word, pill, UI::Justification::Centred);
    return width;
}

// The name and host on the top half, the state and last use under them, after
// the unread pill when there is one.
void paintSessionText(UI::Graphics& g, const OverviewItem& item, Rect area, Color textColour)
{
    const auto nameLine = area.removeFromTop(area.h / 2.0f);
    const auto& session = item.session;

    g.setFont(makeFont(detailFontSize));
    const auto host = describeHost(session.host);
    g.drawText(host, nameLine.fromRight(g.measureText(host)), UI::Justification::Right);

    if (session.unread)
    {
        area.removeFromLeft(paintUnreadPill(g, *session.unread, area) + lineGap);
        g.setColour(textColour);
        g.setFont(makeFont(detailFontSize));
    }

    drawTextLine(g, {session.stateLine, item.lastUse}, area);

    auto nameArea = nameLine;
    nameArea.removeFromRight(g.measureText(host) + lineGap);
    g.setFont(makeFont(nameFontSize));
    const auto measure = [&g](const std::string& part) { return g.measureText(part); };
    g.drawText(term::truncateTextToWidth(describeSessionName(session), nameArea.w, measure), nameArea);
}
} // namespace

OverviewList::OverviewList()
{
    setInterceptsMouseClicks(true);
}

void OverviewList::setItems(std::vector<OverviewItem> itemsToShow)
{
    items = std::move(itemsToShow);
    setScrollOffset(scrollOffset);
}

void OverviewList::setHostColourConfig(const term::AppConfig& config)
{
    hostColourConfig = config;
    repaint();
}

void OverviewList::setScrollOffset(float offset)
{
    const auto maximum = calculateMaximumOverviewScroll(listOverviewItemHeights(items), getHeight());
    scrollOffset = std::clamp(offset, 0.0f, maximum);
    repaint();
}

std::vector<Rect> OverviewList::layOutItems() const
{
    return layOutFromTheBottom(listOverviewItemHeights(items), getLocalBounds(), scrollOffset);
}

bool OverviewList::scrollByAndCheckIfStoppedAtEdge(float pixels)
{
    const auto maximum = calculateMaximumOverviewScroll(listOverviewItemHeights(items), getHeight());
    const auto wanted = scrollOffset + pixels;
    scrollOffset = std::clamp(wanted, 0.0f, maximum);
    repaint();
    return scrollOffset != wanted;
}

void OverviewList::paint(UI::Graphics& g)
{
    const auto bounds = layOutItems();

    for (auto index = std::size_t {0}; index < items.size(); ++index)
    {
        if (bounds[index].intersects(getLocalBounds()))
            paintItem(g, items[index], bounds[index]);
    }
}

bool OverviewList::isPressed(const OverviewItem& item) const
{
    return pressedItemIndex && *pressedItemIndex < items.size() && &items[*pressedItemIndex] == &item;
}

void OverviewList::paintItem(UI::Graphics& g, const OverviewItem& item, Rect bounds)
{
    if (item.kind == OverviewItemKind::UnreachableHostRow)
        return paintUnreachableHost(g, item.unreachable, bounds);

    auto colours = term::getColourBlindSafeHostRowColours(hostColourConfig, item.session.host);

    if (isPressed(item))
        colours = darkenForPress(colours);

    auto area = paintHostBackground(g, colours, bounds);

    if (isPressed(item))
        drawPressedOutline(g, colours, bounds);

    if (item.kind == OverviewItemKind::WaitingCard)
        area = area.inset(0.0f, cardPadding);

    g.setColour(term::toColor(colours.text));
    paintSessionText(g, item, area, term::toColor(colours.text));
}

void OverviewList::paintUnreachableHost(UI::Graphics& g, const UnreachableHost& host, Rect bounds)
{
    const auto colours = term::getColourBlindSafeHostRowColours(hostColourConfig, host.host);
    auto area = paintHostBackground(g, colours, bounds);
    const auto nameLine = area.removeFromTop(area.h / 2.0f);

    g.setColour(term::toColor(colours.text));
    g.setFont(makeFont(nameFontSize));
    g.drawText(describeHost(host.host), nameLine);

    g.setFont(makeFont(detailFontSize));
    g.drawText(host.reason, area);
}

void OverviewList::mouseDown(const UI::MouseEvent& event)
{
    LOG("overview list: finger down at ", event.position.x, ", ", event.position.y);
    pressStoppedACoast = momentum.isMoving();
    showPressAt(event.position);
    momentum.stop();
    stopAnimating();
    isDragging = false;
    lastFingerY = event.position.y;
    fingerVelocity.reset();
    fingerVelocity.addSample({term::FractionalSeconds {event.timestamp}, event.position.y});
}

void OverviewList::mouseDrag(const UI::MouseEvent& event)
{
    if (!isDragging && hasMovedBeyondTapSlop(event.downPosition, event.position))
    {
        isDragging = true;
        clearPress();
    }

    if (!isDragging)
        return;

    scrollByAndCheckIfStoppedAtEdge(event.position.y - lastFingerY);
    lastFingerY = event.position.y;
    fingerVelocity.addSample({term::FractionalSeconds {event.timestamp}, event.position.y});
}

void OverviewList::mouseUp(const UI::MouseEvent& event)
{
    clearPress();

    if (!isDragging)
    {
        if (!pressStoppedACoast)
            openItemAt(event.position);

        return;
    }

    isDragging = false;
    const auto liftTime = term::FractionalSeconds {event.timestamp};
    momentum.startFling(fingerVelocity.calculateVelocityAtLift(liftTime));

    if (momentum.isMoving())
        startAnimating();
}

void OverviewList::mouseCancel(const UI::MouseEvent&)
{
    isDragging = false;
    clearPress();
}

void OverviewList::showPressAt(UI::Point position)
{
    pressedItemIndex.reset();

    if (!pressStoppedACoast)
        pressedItemIndex = findOverviewItemAt(layOutItems(), position);

    repaint();
}

void OverviewList::clearPress()
{
    pressedItemIndex.reset();
    repaint();
}

bool OverviewList::mouseWheelMove(const UI::MouseEvent& event)
{
    const auto rowHeight = getOverviewItemHeight(OverviewItemKind::SessionRow);
    auto pixels = event.wheelDelta.y;

    if (!event.preciseWheel)
        pixels *= rowHeight;

    scrollByAndCheckIfStoppedAtEdge(pixels);
    return true;
}

bool OverviewList::advanceAnimation(double seconds)
{
    const auto pixels = momentum.advanceBy(term::FractionalSeconds {seconds});
    const auto distance = static_cast<float>(pixels);

    if (scrollByAndCheckIfStoppedAtEdge(distance))
        momentum.stop();

    return momentum.isMoving();
}

void OverviewList::openItemAt(UI::Point position)
{
    const auto index = findOverviewItemAt(layOutItems(), position);

    if (!index)
        return;

    const auto& item = items[*index];

    if (item.kind != OverviewItemKind::UnreachableHostRow)
        onOpenSession(item.session);
}

OverviewPanel::OverviewPanel(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    searchField.setFont(makeFont(searchFontSize));
    searchField.setColour(term::toColor(theme.foreground));
    searchField.setAccentColour(term::toColor(theme.ansi[ansiBlue]));
    searchField.setPlaceholder(searchPlaceholder);
    searchField.onTextChange = [this](const std::string& query)
    {
        LOG("overview search: typed, now ", query.size(), " characters");
        showSearchResults(query);
    };
    searchField.onReturnKey = [this](const std::string&) { openBestMatch(); };
    hideKeyboardButton.onClick = [this] { searchField.giveAwayKeyboardFocus(); };
    hideKeyboardButton.setVisible(false);

    addChildren({list, searchField, hideKeyboardButton});
}

void OverviewPanel::setInputs(OverviewInputs inputsToShow)
{
    inputs = std::move(inputsToShow);
    showSearchResults(searchField.getText());
}

void OverviewPanel::showSearchResults(const std::string& query)
{
    inputs.query = query;
    content = arrangeOverview(inputs);
    list.setVisible(!content.banner);
    searchField.setVisible(inputs.hostCount > 0);
    list.setItems(content.itemsFromTheBottom);
    repaint();
}

// Return opens what row 0 shows, as Enter does in the Mac's palette.
void OverviewPanel::openBestMatch()
{
    for (const auto& item: content.itemsFromTheBottom)
    {
        if (item.kind != OverviewItemKind::UnreachableHostRow)
        {
            list.onOpenSession(item.session);
            return;
        }
    }
}

void OverviewPanel::setScreenInsets(eacp::Graphics::Insets safeAreaToUse, float keyboardCoveredHeightToUse)
{
    safeArea = safeAreaToUse;
    keyboardCoveredHeight = keyboardCoveredHeightToUse;
    resized();
}

void OverviewPanel::search(const std::string& query)
{
    searchField.grabKeyboardFocus();
    searchField.setText(query);
    showSearchResults(query);
}

void OverviewPanel::clearSearch()
{
    searchField.giveAwayKeyboardFocus();
    searchField.setText({});
    showSearchResults({});
}

void OverviewPanel::resized()
{
    const auto areas = calculateOverviewAreas(getLocalBounds(), safeArea, keyboardCoveredHeight);
    searchField.setBounds(areas.searchField);
    hideKeyboardButton.setVisible(areas.hideKeyboardButton.has_value());

    if (areas.hideKeyboardButton)
        hideKeyboardButton.setBounds(*areas.hideKeyboardButton);

    list.setBounds(areas.list);
    list.setItems(content.itemsFromTheBottom);
    repaint();
}

void OverviewPanel::paint(UI::Graphics& g)
{
    g.setColour(term::toColor(theme.background));
    g.fillAll();

    if (!content.banner)
        return;

    g.setFont(makeFont(bannerFontSize));
    g.setColour(term::toColor(theme.ansi[ansiRed]));

    const auto lines = eacp::Strings::split(*content.banner, '\n');
    const auto bannerHeight = bannerLineHeight * static_cast<float>(lines.size());
    auto lineBounds = getLocalBounds();
    lineBounds.removeFromTop((lineBounds.h - bannerHeight) / 2.0f);

    for (const auto& line: lines)
        g.drawText(line, lineBounds.removeFromTop(bannerLineHeight), UI::Justification::Centred);
}

OverviewScreen::OverviewScreen()
{
    host.setBackgroundColour(term::toColor(theme.background));
    host.setRootComponent(panel);
    panel.list.onOpenSession = [this](const OverviewRow& session) { onOpenSession(session); };
    addChildren({host});
}

void OverviewScreen::setInputs(OverviewInputs inputs)
{
    panel.setInputs(std::move(inputs));
}

void OverviewScreen::resized()
{
    host.setBounds(getLocalBounds());
    panel.setScreenInsets(getSafeAreaInsets(), keyboardCoveredHeight);
}

void OverviewScreen::search(const std::string& query)
{
    panel.search(query);
}

void OverviewScreen::clearSearch()
{
    panel.clearSearch();
}

void OverviewScreen::safeAreaInsetsChanged()
{
    panel.setScreenInsets(getSafeAreaInsets(), keyboardCoveredHeight);
}

void OverviewScreen::setSoftwareKeyboardCoveredHeight(float coveredHeight)
{
    keyboardCoveredHeight = coveredHeight;
    panel.setScreenInsets(getSafeAreaInsets(), keyboardCoveredHeight);
}
} // namespace cowterm::mobile
