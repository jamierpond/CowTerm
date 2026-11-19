#include "OverlayPanel.h"

#include "Multiplexer/TextLayout.h"

#include <algorithm>

namespace term
{
using namespace eacp;
using UI::Color;
using UI::Point;
using UI::Rect;
namespace KeyCode = UI::KeyCode;

namespace
{
constexpr auto windowSideMargin = 60.0f;
constexpr auto minimumPanelTop = 20.0f;
constexpr auto panelBottomPadding = 12.0f;
constexpr auto firstRowOffset = 4.0f;
constexpr auto backdropAlpha = 0.38f;
constexpr auto cardBorderAlpha = 0.8f;
constexpr auto cardBorderThickness = 1.0f;
constexpr auto dividerInset = 12.0f;
constexpr auto dividerRaise = 2.0f;
constexpr auto hostStripeWidth = 4.0f;
constexpr auto highlightOutlineThickness = 2.0f;
constexpr auto blockCaretAlpha = 0.55f;
constexpr auto modeLabelGap = 12.0f;
constexpr auto searchPrompt = "› ";
constexpr auto thinCaret = "▏";
constexpr auto unreadPillSidePadding = 7.0f;
constexpr auto listCardStyle =
    OverlayCardStyle {.backgroundBrightening = 0.04f, .cornerRadius = 12.0f};
} // namespace

int countVisibleListRows(const ListPanelMetrics& metrics, int itemCount)
{
    return std::min(itemCount, metrics.maximumRows);
}

Rect calculateListPanelBounds(Rect area, const ListPanelMetrics& metrics, int itemCount)
{
    const auto width = std::min(metrics.maximumWidth, area.w - windowSideMargin);
    const auto rows = countVisibleListRows(metrics, itemCount);
    const auto height = metrics.headerHeight + (float) std::max(rows, 1) * metrics.rowHeight
                        + metrics.hintHeight + panelBottomPadding;

    return {(area.w - width) / 2.0f,
            std::max(area.h * metrics.topFraction, minimumPanelTop),
            width,
            height};
}

float calculateListRowTop(Rect panel, const ListPanelMetrics& metrics, int row)
{
    return panel.y + metrics.headerHeight + firstRowOffset + (float) row * metrics.rowHeight;
}

int findListRowAt(Rect panel, const ListPanelMetrics& metrics, int itemCount, Point position)
{
    const auto y = position.y - calculateListRowTop(panel, metrics, 0);
    const auto isBesidePanel = position.x < panel.x || position.x > panel.right();

    if (isBesidePanel || y < 0)
        return -1;

    const auto row = (int) (y / metrics.rowHeight);

    if (row < countVisibleListRows(metrics, itemCount))
        return row;

    return -1;
}

int wrapSelection(int selected, int delta, int count)
{
    return (selected + delta + count) % count;
}

bool isSelectPreviousKey(const UI::KeyEvent& event)
{
    return event.keyCode == KeyCode::UpArrow
           || (event.modifiers.control && event.charactersIgnoringModifiers == "p");
}

bool isSelectNextKey(const UI::KeyEvent& event)
{
    return event.keyCode == KeyCode::DownArrow
           || (event.modifiers.control && event.charactersIgnoringModifiers == "n");
}

std::string compactHomePath(std::string path)
{
    const auto home = FilePath::homeDirectory().str();

    if (!home.empty() && path.starts_with(home))
        path = "~" + path.substr(home.size());

    return path;
}

void drawTextInFont(UI::Graphics& g,
                    const std::string& text,
                    Point position,
                    const UI::Font& font)
{
    g.setFont(font);
    g.drawText(text, position);
}

float measureTextInFont(UI::Graphics& g, const std::string& text, const UI::Font& font)
{
    g.setFont(font);
    return g.measureText(text);
}

void paintHostFill(UI::Graphics& g, const HostRowColours& colours, Rect area, float cornerRadius)
{
    g.setColour(toColor(colours.background));
    g.fillRoundedRect(area, cornerRadius);
    g.setColour(toColor(colours.stripe));
    g.fillRect(area.fromLeft(hostStripeWidth).inset(0.0f, cornerRadius));
}

float measureUnreadPillWidth(UI::Graphics& g, const UnreadMark& mark, const UI::Font& font)
{
    return measureTextInFont(g, describeUnreadMark(mark), font) + 2.0f * unreadPillSidePadding;
}

float paintUnreadPill(UI::Graphics& g, const UnreadMark& mark, UnreadPillPlace place, const UI::Font& font)
{
    const auto word = describeUnreadMark(mark);
    const auto width = measureUnreadPillWidth(g, mark, font);
    const auto pill = Rect {place.right - width, place.middle - place.height / 2.0f, width, place.height};

    g.setColour(toColor(getUnreadMarkFillColour()));
    g.fillRoundedRect(pill, place.height / 2.0f);
    g.setColour(toColor(getUnreadMarkTextColour()));
    g.setFont(font);
    g.drawText(word, pill, UI::Justification::Centred);
    return width;
}

void paintHighlightOutline(UI::Graphics& g, const HostRowColours& colours, Rect area, float cornerRadius)
{
    const auto outline = area.inset(highlightOutlineThickness / 2.0f);
    g.setColour(toColor(colours.text));
    g.drawRoundedRect(outline, cornerRadius, highlightOutlineThickness);
}

OverlayPanel::OverlayPanel(const AppConfig& configToUse,
                           const HostRowColours& thisMachineColoursToUse,
                           SessionManager& sessionsToUse,
                           OverlayFontSizes fontSizes)
    : config(configToUse)
    , sessions(sessionsToUse)
    , theme(themeByName(configToUse.theme))
    , headerFont({config.font, fontSizes.header})
    , rowFont({config.font, fontSizes.row})
    , detailFont({config.font, fontSizes.detail})
    , thisMachineColours(thisMachineColoursToUse)
{
    setInterceptsMouseClicks(true);
    setWantsKeyboardFocus(true);
}

void OverlayPanel::paintBackdropAndCard(UI::Graphics& g,
                                        Rect panel,
                                        Color border,
                                        OverlayCardStyle style) const
{
    g.setColour(Color::black(backdropAlpha));
    g.fillRect(getLocalBounds());

    g.setColour(toColor(theme.background).brighter(style.backgroundBrightening));
    g.fillRoundedRect(panel, style.cornerRadius);

    g.setColour(border);
    g.drawRect(panel, cardBorderThickness);
}

void OverlayPanel::paintListBackdropAndCard(UI::Graphics& g, Rect panel) const
{
    paintBackdropAndCard(g, panel, toColor(theme.selection, cardBorderAlpha), listCardStyle);
}

void OverlayPanel::paintHeaderDivider(UI::Graphics& g, Rect panel, float headerHeight) const
{
    const auto y = panel.y + headerHeight - dividerRaise;
    g.setColour(toColor(theme.selection));
    g.drawLine({panel.x + dividerInset, y}, {panel.right() - dividerInset, y});
}

HostRowColours OverlayPanel::getHostRowColoursFor(const std::string& host) const
{
    if (host.empty())
        return thisMachineColours;

    return getColourBlindSafeHostRowColours(config, host);
}

bool SearchablePanel::handleSearchFieldKey(const UI::KeyEvent& event)
{
    using searchkeys::Action;
    const auto textBefore = searchField.getText();
    const auto action = searchField.handleKey(event);

    if (action == Action::passThrough)
        return false;

    if (searchField.getText() != textBefore)
    {
        selected = 0;
        searchTextChanged();
    }

    performSearchAction(action);
    return true;
}

void SearchablePanel::performSearchAction(searchkeys::Action action)
{
    using searchkeys::Action;

    if (action == Action::openHighlighted || action == Action::openHighlightedInNewTab)
        chooseSelected();
    else if (action == Action::closeSearch)
        dismiss();
    else
        moveSelectionForSearchAction(action);

    repaint();
}

void SearchablePanel::moveSelectionForSearchAction(searchkeys::Action action)
{
    using searchkeys::Action;

    if (action == Action::moveHighlightDown)
        moveSelection(1);
    else if (action == Action::moveHighlightUp)
        moveSelection(-1);
    else if (action == Action::moveHighlightToFirst)
        moveSelection(-selected);
    else if (action == Action::moveHighlightToLast)
        moveSelection(countRows() - 1 - selected);
}

namespace
{
// The block covers the character after the caret, or a space at the end of
// the text so it still has a width.
std::string getCharacterUnderCaret(const std::string& text, int caret)
{
    const auto start = (std::size_t) caret;
    const auto end = findNextCodepointEnd(text, start);

    if (end == start)
        return " ";

    return text.substr(start, end - start);
}
} // namespace

void SearchablePanel::paintSearchField(UI::Graphics& g, Point baselineStart)
{
    const auto text = searchField.getText();
    const auto caret = searchField.getCaret();
    const auto beforeCaret = searchPrompt + text.substr(0, (std::size_t) caret);
    g.setColour(toColor(theme.foreground));

    if (!searchField.isNormalMode())
    {
        const auto withCaret = beforeCaret + thinCaret + text.substr((std::size_t) caret);
        drawTextInFont(g, withCaret, baselineStart, headerFont);
        return;
    }

    const auto line = searchPrompt + text;
    drawTextInFont(g, line, baselineStart, headerFont);

    const auto blockLeft = baselineStart.x + measureTextInFont(g, beforeCaret, headerFont);
    const auto blockWidth =
        measureTextInFont(g, getCharacterUnderCaret(text, caret), headerFont);
    g.setColour(toColor(theme.foreground, blockCaretAlpha));
    g.fillRect({blockLeft, baselineStart.y - g.ascent(), blockWidth, g.lineHeight()});

    const auto labelLeft = baselineStart.x + measureTextInFont(g, line + " ", headerFont)
                           + modeLabelGap;
    g.setColour(toColor(theme.ansi[8]));
    drawTextInFont(g, "NORMAL", {labelLeft, baselineStart.y}, detailFont);
}

void OverlayPanel::mouseDown(const UI::MouseEvent& event)
{
    if (const auto row = findRowAt(event.position); row >= 0)
    {
        selected = row;
        chooseSelected();
        return;
    }

    if (!calculatePanelBounds().contains(event.position))
        dismiss();
}
} // namespace term
