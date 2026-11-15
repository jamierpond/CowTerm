#include "OverlayPanel.h"

#include "TextLayout.h"

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
constexpr auto selectedRowAlpha = 0.85f;
constexpr auto selectedRowCornerRadius = 6.0f;
constexpr auto blockCaretAlpha = 0.55f;
constexpr auto modeLabelGap = 12.0f;
constexpr auto searchPrompt = "› ";
constexpr auto thinCaret = "▏";
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

OverlayPanel::OverlayPanel(const AppConfig& configToUse,
                           SessionManager& sessionsToUse,
                           OverlayFontSizes fontSizes)
    : config(configToUse)
    , sessions(sessionsToUse)
    , theme(themeByName(configToUse.theme))
    , headerFont({config.font, fontSizes.header})
    , rowFont({config.font, fontSizes.row})
    , detailFont({config.font, fontSizes.detail})
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
    paintListBackdropAndCardWithBorder(g, panel, toColor(theme.selection, cardBorderAlpha));
}

void OverlayPanel::paintListBackdropAndCardWithBorder(UI::Graphics& g,
                                                      Rect panel,
                                                      Color border) const
{
    paintBackdropAndCard(g, panel, border, listCardStyle);
}

void OverlayPanel::paintHeaderDivider(UI::Graphics& g, Rect panel, float headerHeight) const
{
    const auto y = panel.y + headerHeight - dividerRaise;
    g.setColour(toColor(theme.selection));
    g.drawLine({panel.x + dividerInset, y}, {panel.right() - dividerInset, y});
}

void OverlayPanel::paintSelectedRowHighlight(UI::Graphics& g, Rect row) const
{
    g.setColour(toColor(theme.selection, selectedRowAlpha));
    g.fillRoundedRect(row, selectedRowCornerRadius);
}
bool OverlayPanel::handleSearchFieldKey(const UI::KeyEvent& event)
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

    switch (action)
    {
        case Action::moveHighlightDown: moveSelection(1); break;
        case Action::moveHighlightUp: moveSelection(-1); break;
        case Action::moveHighlightToFirst: moveSelection(-selected); break;
        case Action::moveHighlightToLast: moveSelection(countRows() - 1 - selected); break;
        case Action::openHighlighted:
        case Action::openHighlightedInNewTab: chooseSelected(); return true;
        case Action::closeHighlighted: closeHighlighted(); break;
        case Action::closeSearch: dismiss(); return true;
        case Action::starHighlighted:
        case Action::passThrough:
        case Action::none: break;
    }

    repaint();
    return true;
}

bool OverlayPanel::editSearchFieldText(const UI::KeyEvent& event)
{
    if (!searchField.editText(event))
        return false;

    selected = 0;
    return true;
}

// The block covers the character after the caret, or a space at the end of
// the text so it still has a width.
static std::string getCharacterUnderCaret(const std::string& text, int caret)
{
    const auto start = (std::size_t) caret;
    const auto end = findNextCodepointEnd(text, start);

    if (end == start)
        return " ";

    return text.substr(start, end - start);
}

void OverlayPanel::paintSearchField(UI::Graphics& g, Point baselineStart)
{
    const auto& text = searchField.getText();
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
