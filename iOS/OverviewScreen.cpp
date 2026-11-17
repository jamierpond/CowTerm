#include "OverviewScreen.h"

#include "PaneScreenLayout.h"

#include "Multiplexer/Config.h"
#include "Multiplexer/HostColours.h"
#include "Multiplexer/RemoteLocation.h"

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
constexpr auto searchFieldHeight = 44.0f;
constexpr auto searchFieldGap = 8.0f;

constexpr auto mutedTextAlpha = 0.55f;
constexpr auto cardBrightening = 0.06f;
constexpr auto rowBrightening = 0.03f;

constexpr auto ansiRed = 1;
constexpr auto ansiYellow = 3;
constexpr auto ansiBlue = 4;

constexpr auto claudeMark = "\xe2\x9c\xb3 ";
constexpr auto searchPlaceholder = "Search sessions";

UI::Font makeFont(float size)
{
    return UI::Font {fontFamily, size};
}

Color getHostColour(const std::string& host)
{
    return term::toColor(term::getHostColour(term::AppConfig {}, host));
}

std::string describeSessionName(const OverviewRow& session)
{
    if (session.isClaude)
        return claudeMark + session.sessionName;

    return session.sessionName;
}

// The machine's colour down the left edge of a card or row.
Rect paintHostStripe(UI::Graphics& g, const std::string& host, Rect bounds)
{
    g.setColour(getHostColour(host));
    g.fillRect(bounds.removeFromLeft(stripeWidth));
    bounds.removeFromLeft(stripeGap);
    return bounds;
}

// Shown only where a session name alone could be another machine's.
std::string describeHost(const std::string& host)
{
    return term::remote::getHostLabel(host);
}
} // namespace

OverviewList::OverviewList(const term::Theme& themeToUse)
    : theme(themeToUse)
{
    setInterceptsMouseClicks(true);
}

void OverviewList::setItems(std::vector<OverviewItem> itemsToShow)
{
    items = std::move(itemsToShow);
    const auto maximum = calculateMaximumOverviewScroll(listOverviewItemHeights(items), getHeight());
    scrollOffset = std::clamp(scrollOffset, 0.0f, maximum);
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

void OverviewList::paintItem(UI::Graphics& g, const OverviewItem& item, Rect bounds)
{
    switch (item.kind)
    {
        case OverviewItemKind::WaitingCard:
            paintWaitingCard(g, item.session, bounds);
            return;
        case OverviewItemKind::SessionRow:
            paintSessionRow(g, item.session, bounds);
            return;
        case OverviewItemKind::UnreachableHostRow:
            paintUnreachableHost(g, item.unreachable, bounds);
            return;
    }
}

void OverviewList::paintWaitingCard(UI::Graphics& g, const OverviewRow& session, Rect bounds)
{
    const auto foreground = term::toColor(theme.foreground);
    g.setColour(term::toColor(theme.background).brighter(cardBrightening));
    g.fillRoundedRect(bounds, cornerRadius);

    auto area = paintHostStripe(g, session.host, bounds.inset(0.0f, cardPadding));
    area.removeFromRight(cardPadding);
    const auto nameLine = area.removeFromTop(area.h / 2.0f);

    g.setFont(makeFont(detailFontSize));
    g.setColour(foreground.withAlpha(mutedTextAlpha));
    const auto host = describeHost(session.host);
    g.drawText(host, nameLine.fromRight(g.measureText(host)), UI::Justification::Right);

    g.setFont(makeFont(nameFontSize));
    g.setColour(foreground);
    g.drawText(describeSessionName(session), nameLine);

    g.setFont(makeFont(detailFontSize));
    g.setColour(term::toColor(theme.ansi[ansiYellow]));
    g.drawText(session.stateLine, area);
}

void OverviewList::paintSessionRow(UI::Graphics& g, const OverviewRow& session, Rect bounds)
{
    const auto foreground = term::toColor(theme.foreground);
    g.setColour(term::toColor(theme.background).brighter(rowBrightening));
    g.fillRect(bounds);

    auto area = paintHostStripe(g, session.host, bounds);
    area.removeFromRight(sidePadding);
    const auto nameLine = area.removeFromTop(area.h / 2.0f);

    g.setFont(makeFont(detailFontSize));
    g.setColour(foreground.withAlpha(mutedTextAlpha));
    const auto host = describeHost(session.host);
    g.drawText(host, nameLine.fromRight(g.measureText(host)), UI::Justification::Right);
    g.drawText(session.stateLine, area);

    g.setFont(makeFont(nameFontSize));
    g.setColour(foreground);
    g.drawText(describeSessionName(session), nameLine);
}

void OverviewList::paintUnreachableHost(UI::Graphics& g, const UnreachableHost& host, Rect bounds)
{
    g.setColour(term::toColor(theme.background).brighter(rowBrightening));
    g.fillRect(bounds);

    auto area = paintHostStripe(g, host.host, bounds);
    const auto nameLine = area.removeFromTop(area.h / 2.0f);

    g.setFont(makeFont(nameFontSize));
    g.setColour(term::toColor(theme.foreground).withAlpha(mutedTextAlpha));
    g.drawText(describeHost(host.host), nameLine);

    g.setFont(makeFont(detailFontSize));
    g.setColour(term::toColor(theme.ansi[ansiRed]));
    g.drawText(host.reason, area);
}

void OverviewList::mouseDown(const UI::MouseEvent& event)
{
    LOG("overview list: finger down at ", event.position.x, ", ", event.position.y);
    pressStoppedACoast = momentum.isMoving();
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
        isDragging = true;

    if (!isDragging)
        return;

    scrollByAndCheckIfStoppedAtEdge(event.position.y - lastFingerY);
    lastFingerY = event.position.y;
    fingerVelocity.addSample({term::FractionalSeconds {event.timestamp}, event.position.y});
}

void OverviewList::mouseUp(const UI::MouseEvent& event)
{
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
    : list(themeToUse)
    , theme(themeToUse)
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

    addChildren({list, searchField});
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

void OverviewPanel::resized()
{
    auto area = getLocalBounds().inset(sidePadding, 0.0f);
    searchField.setBounds(area.removeFromBottom(searchFieldHeight));
    area.removeFromBottom(searchFieldGap);
    list.setBounds(area);
    list.setItems(content.itemsFromTheBottom);
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
}
} // namespace cowterm::mobile
