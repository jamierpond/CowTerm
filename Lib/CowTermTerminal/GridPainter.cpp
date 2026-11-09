#include "GridPainter.h"

#include <algorithm>
#include <array>
#include <optional>

namespace term
{
using eacp::Graphics::Color;
using eacp::Graphics::Point;

namespace
{
constexpr auto faintGlyphAlpha = 0.55f;
constexpr auto opaqueAlpha = 1.0f;
constexpr auto decorationThickness = 1.0f;
constexpr auto underlineGapBelowBaseline = 1.5f;
constexpr auto strikethroughHeightFraction = 0.55f;
constexpr auto cursorBarThickness = 2.0f;
constexpr auto hollowCursorLineWidth = 1.0f;
constexpr auto badgePadding = 4.0f;
constexpr auto badgeAnsiColour = std::size_t {3};
constexpr auto singleCellWidth = 1;
constexpr auto doubleCellWidth = 2;

constexpr auto underlineCursorShapes = std::array {3, 4};
constexpr auto barCursorShapes = std::array {5, 6};

bool hasAttribute(const Cell& cell, std::uint16_t attribute)
{
    return (cell.attrs & attribute) != 0;
}

bool isDrawable(const GlyphSlot& slot)
{
    return slot.valid && !slot.empty;
}

bool isMonochromeDrawable(const GlyphSlot& slot)
{
    return isDrawable(slot) && !slot.colored;
}

// A character the frame will draw: not a blank, a wide glyph's right half or
// hidden text.
bool needsGlyphThisFrame(const Cell& cell)
{
    return cell.ch != U' ' && (cell.attrs & (Attr::WideCont | Attr::Hidden)) == 0;
}

bool hasVisibleCharacter(const Cell& cell)
{
    return cell.ch != U' ' && !hasAttribute(cell, Attr::WideCont);
}

bool isPlainBackground(const Cell& cell, bool selected, Rgb background, const Theme& theme)
{
    if (selected || hasAttribute(cell, Attr::Inverse))
        return false;

    return background == theme.background;
}

template <std::size_t size>
bool isShapeIn(const std::array<int, size>& shapes, int cursorShape)
{
    return std::find(shapes.begin(), shapes.end(), cursorShape) != shapes.end();
}
} // namespace

CursorStyle findCursorStyle(int cursorShape)
{
    if (isShapeIn(underlineCursorShapes, cursorShape))
        return CursorStyle::Underline;

    if (isShapeIn(barCursorShapes, cursorShape))
        return CursorStyle::Bar;

    return CursorStyle::Block;
}

bool isBlinkingCursorShape(int cursorShape)
{
    return cursorShape == 0 || cursorShape % 2 == 1;
}

CellColours findCellColours(const Cell& cell, bool selected, const Theme& theme)
{
    auto colours = CellColours {cell.fg, cell.bg};

    if (hasAttribute(cell, Attr::Inverse))
        std::swap(colours.foreground, colours.background);

    if (selected)
        colours.background = theme.selection;

    return colours;
}

std::vector<BackgroundRun> findBackgroundRuns(const PaintedRow& row,
                                              const SelectionRange& selection,
                                              const Theme& theme)
{
    auto runs = std::vector<BackgroundRun> {};
    auto open = std::optional<BackgroundRun> {};

    const auto close = [&](int endColumn)
    {
        if (!open)
            return;

        open->endColumn = endColumn;
        runs.push_back(*open);
        open.reset();
    };

    for (auto col = 0; col < row.columns; ++col)
    {
        const auto& cell = row.line[(std::size_t) col];
        const auto selected = selection.contains({row.absoluteRow, col});
        const auto background = findCellColours(cell, selected, theme).background;

        if (isPlainBackground(cell, selected, background, theme))
        {
            close(col);
            continue;
        }

        if (open && open->colour == background)
            continue;

        close(col);
        open = BackgroundRun {col, col, background};
    }

    close(row.columns);
    return runs;
}

GridPainter::GridPainter(const PaintTools& toolsToUse,
                         const SelectionRange& selectionToShow,
                         bool blinkingTextVisible)
    : tools(toolsToUse)
    , selection(selectionToShow)
    , blinkVisible(blinkingTextVisible)
{
}

void GridPainter::rasterizeRowGlyphs(const PaintedRow& row)
{
    for (auto col = 0; col < row.columns; ++col)
    {
        const auto& cell = row.line[(std::size_t) col];

        if (needsGlyphThisFrame(cell))
            requestCellGlyph(cell);
    }
}

void GridPainter::rasterizeTextGlyphs(const std::string& text)
{
    for (const auto c: text)
        tools.atlas.glyph((char32_t) c, false, false, singleCellWidth);
}

void GridPainter::drawRowBackgrounds(const PaintedRow& row)
{
    const auto cellW = tools.atlas.cellWidth();
    const auto cellH = tools.atlas.cellHeight();

    for (const auto& run: findBackgroundRuns(row, selection, tools.theme))
    {
        tools.sprites.fillRect({tools.margins.x + (float) run.firstColumn * cellW,
                                row.y,
                                (float) (run.endColumn - run.firstColumn) * cellW,
                                cellH},
                               toColor(run.colour));
    }
}

void GridPainter::drawRowGlyphs(const PaintedRow& row)
{
    const auto cellW = tools.atlas.cellWidth();

    for (auto col = 0; col < row.columns; ++col)
    {
        const auto& cell = row.line[(std::size_t) col];

        if (!isCellInkShown(cell))
            continue;

        const auto selected = selection.contains({row.absoluteRow, col});
        const auto foreground = findCellColours(cell, selected, tools.theme).foreground;
        const auto origin = Point {tools.margins.x + (float) col * cellW, row.y};

        drawCellGlyph(cell, origin, foreground);
        drawCellDecorations(cell, origin, foreground);
    }
}

void GridPainter::drawCursor(Point cellOrigin,
                             CursorStyle style,
                             const Line& line,
                             int column)
{
    const auto cellW = tools.atlas.cellWidth();
    const auto cellH = tools.atlas.cellHeight();
    const auto color = toColor(tools.theme.cursor);

    switch (style)
    {
        case CursorStyle::Underline:
            tools.sprites.fillRect({cellOrigin.x,
                                    cellOrigin.y + cellH - cursorBarThickness,
                                    cellW,
                                    cursorBarThickness},
                                   color);
            return;

        case CursorStyle::Bar:
            tools.sprites.fillRect(
                {cellOrigin.x, cellOrigin.y, cursorBarThickness, cellH}, color);
            return;

        case CursorStyle::Block:
            drawBlockCursor(cellOrigin, line, column);
            return;
    }
}

void GridPainter::drawHollowCursor(Point cellOrigin)
{
    tools.sprites.drawRect({cellOrigin.x,
                            cellOrigin.y,
                            tools.atlas.cellWidth(),
                            tools.atlas.cellHeight()},
                           toColor(tools.theme.cursor),
                           hollowCursorLineWidth);
}

void GridPainter::drawBlockCursor(Point cellOrigin, const Line& line, int column)
{
    tools.sprites.fillRect({cellOrigin.x,
                            cellOrigin.y,
                            tools.atlas.cellWidth(),
                            tools.atlas.cellHeight()},
                           toColor(tools.theme.cursor));
    drawGlyphOverCursor(cellOrigin, line, column);
}

void GridPainter::drawCopyModeBadge(const std::string& indicator, float viewWidth)
{
    const auto cellW = tools.atlas.cellWidth();
    const auto width = (float) indicator.size() * cellW + badgePadding * 2;
    const auto left = viewWidth - width - tools.margins.x;

    tools.sprites.fillRect({left, tools.margins.y, width, tools.atlas.cellHeight()},
                           toColor(tools.theme.ansi[badgeAnsiColour]));

    auto pen = Point {left + badgePadding, tools.margins.y + tools.atlas.baseline()};

    for (const auto c: indicator)
    {
        const auto slot = tools.atlas.glyph((char32_t) c, false, false, singleCellWidth);

        if (isDrawable(slot))
            queueGlyph(slot, pen, toColor(tools.theme.background), false);

        pen.x += cellW;
    }
}

GlyphSlot GridPainter::requestCellGlyph(const Cell& cell)
{
    const auto cellsWide =
        hasAttribute(cell, Attr::Wide) ? doubleCellWidth : singleCellWidth;

    return tools.atlas.glyph(cell.ch,
                             hasAttribute(cell, Attr::Bold),
                             hasAttribute(cell, Attr::Italic),
                             cellsWide);
}

// Placed by its own bearings from the cell origin and baseline, rather than
// stretched to fill the cell.
void GridPainter::queueGlyph(const GlyphSlot& slot,
                             Point penOnBaseline,
                             const Color& tint,
                             bool colored)
{
    const auto scale = tools.atlas.scale();

    tools.glyphs.add({penOnBaseline.x + slot.offset.x,
                      penOnBaseline.y + slot.offset.y,
                      slot.src.w / scale,
                      slot.src.h / scale},
                     slot.src,
                     tint,
                     colored);
}

void GridPainter::drawCellGlyph(const Cell& cell, Point cellOrigin, Rgb foreground)
{
    if (cell.ch == U' ')
        return;

    const auto slot = requestCellGlyph(cell);

    if (!isDrawable(slot))
        return;

    const auto alpha = hasAttribute(cell, Attr::Faint) ? faintGlyphAlpha : opaqueAlpha;
    const auto tint = slot.colored ? Color::white(alpha) : toColor(foreground, alpha);
    const auto pen = Point {cellOrigin.x, cellOrigin.y + tools.atlas.baseline()};

    queueGlyph(slot, pen, tint, slot.colored);
}

void GridPainter::drawCellDecorations(const Cell& cell, Point cellOrigin, Rgb foreground)
{
    const auto cellW = tools.atlas.cellWidth();
    const auto width = hasAttribute(cell, Attr::Wide) ? cellW * doubleCellWidth : cellW;

    if (hasAttribute(cell, Attr::Underline))
    {
        const auto y = cellOrigin.y + tools.atlas.baseline() + underlineGapBelowBaseline;
        tools.sprites.fillRect({cellOrigin.x, y, width, decorationThickness},
                               toColor(foreground));
    }

    if (hasAttribute(cell, Attr::Strike))
    {
        const auto y =
            cellOrigin.y + tools.atlas.cellHeight() * strikethroughHeightFraction;
        tools.sprites.fillRect({cellOrigin.x, y, width, decorationThickness},
                               toColor(foreground));
    }
}

void GridPainter::drawGlyphOverCursor(Point cellOrigin, const Line& line, int column)
{
    if (column >= (int) line.size())
        return;

    const auto& cell = line[(std::size_t) column];

    if (!hasVisibleCharacter(cell))
        return;

    const auto slot = requestCellGlyph(cell);

    if (!isMonochromeDrawable(slot))
        return;

    const auto pen = Point {cellOrigin.x, cellOrigin.y + tools.atlas.baseline()};
    queueGlyph(slot, pen, toColor(tools.theme.background), false);
}

bool GridPainter::isCellInkShown(const Cell& cell) const
{
    if (hasAttribute(cell, Attr::WideCont) || hasAttribute(cell, Attr::Hidden))
        return false;

    return blinkVisible || !hasAttribute(cell, Attr::Blink);
}
} // namespace term
