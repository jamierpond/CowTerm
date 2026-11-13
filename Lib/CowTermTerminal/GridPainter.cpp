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

bool hasAttribute(const ResolvedCell& cell, std::uint16_t attribute)
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

bool hasVisibleCharacter(const ResolvedCell& cell)
{
    return cell.ch != U' ' && !hasAttribute(cell, Attr::WideCont);
}

bool isPlainBackground(const ResolvedCell& cell, bool selected, Rgb background, const Theme& theme)
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

CellColours findCellColours(const ResolvedCell& cell, bool selected, const Theme& theme)
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
                         const std::optional<LinkSpan>& hoveredLinkToUnderline,
                         bool blinkingTextVisible)
    : tools(toolsToUse)
    , selection(selectionToShow)
    , hoveredLink(hoveredLinkToUnderline)
    , blinkVisible(blinkingTextVisible)
{
}

void GridPainter::rasterizeTextGlyphs(const std::string& text)
{
    for (const auto c: text)
        tools.atlas.glyph((char32_t) c, false, false, singleCellWidth);
}

RowDrawing GridPainter::buildRowDrawing(const PaintedRow& row)
{
    auto drawing = RowDrawing {};
    addRowBackgrounds(row, drawing);
    addRowInk(row, drawing);
    return drawing;
}

void GridPainter::submitRowDrawings(const std::vector<RowDrawing>& rows)
{
    for (const auto& row: rows)
        fillRects(row.backgrounds);

    for (const auto& row: rows)
        fillRects(row.decorations);

    for (const auto& row: rows)
        addGlyphQuads(row.glyphs);
}

void GridPainter::fillRects(const std::vector<RectFill>& fills)
{
    for (const auto& fill: fills)
        tools.sprites.fillRect(fill.rect, fill.colour);
}

void GridPainter::addGlyphQuads(const std::vector<GlyphQuad>& quads)
{
    for (const auto& quad: quads)
        tools.glyphs.add(quad.destination, quad.source, quad.tint, quad.colored);
}

void GridPainter::addRowBackgrounds(const PaintedRow& row, RowDrawing& drawing)
{
    const auto cellW = tools.atlas.cellWidth();
    const auto cellH = tools.atlas.cellHeight();

    for (const auto& run: findBackgroundRuns(row, selection, tools.theme))
    {
        drawing.backgrounds.push_back({{tools.margins.x + (float) run.firstColumn * cellW,
                                        row.y,
                                        (float) (run.endColumn - run.firstColumn) * cellW,
                                        cellH},
                                       toColor(run.colour)});
    }
}

void GridPainter::addRowInk(const PaintedRow& row, RowDrawing& drawing)
{
    const auto cellW = tools.atlas.cellWidth();

    for (auto col = 0; col < row.columns; ++col)
    {
        const auto& cell = row.line[(std::size_t) col];

        if (hasAttribute(cell, Attr::Blink))
            drawing.hasBlinkingText = true;

        if (!isCellInkShown(cell))
            continue;

        const auto ref = CellRef {row.absoluteRow, col};
        const auto selected = selection.contains(ref);
        const auto foreground = findCellColours(cell, selected, tools.theme).foreground;
        const auto origin = Point {tools.margins.x + (float) col * cellW, row.y};
        const auto isUnderlined = hasAttribute(cell, Attr::Underline) || isInHoveredLink(ref);

        addCellGlyph(cell, origin, foreground, drawing);

        if (isUnderlined)
            addCellUnderline(cell, origin, foreground, drawing);

        addCellStrikethrough(cell, origin, foreground, drawing);
    }
}

void GridPainter::drawCursor(Point cellOrigin,
                             CursorStyle style,
                             const PaintedLine& line,
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

void GridPainter::drawBlockCursor(Point cellOrigin, const PaintedLine& line, int column)
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

// A plain cell is one glyph; only a grapheme cluster pays for shaping.
template <typename Visit>
void GridPainter::forEachCellGlyph(const ResolvedCell& cell, Visit&& visit)
{
    const auto cellsWide =
        hasAttribute(cell, Attr::Wide) ? doubleCellWidth : singleCellWidth;
    const auto bold = hasAttribute(cell, Attr::Bold);
    const auto italic = hasAttribute(cell, Attr::Italic);

    if (cell.text.empty())
    {
        visit(tools.atlas.glyph(cell.ch, bold, italic, cellsWide));
        return;
    }

    for (const auto& slot: tools.atlas.clusterGlyphs(cell.text, bold, italic, cellsWide))
        visit(slot);
}

// Placed by its own bearings from the cell origin and baseline, rather than
// stretched to fill the cell.
GlyphQuad GridPainter::placeGlyph(const GlyphSlot& slot,
                                  Point penOnBaseline,
                                  const Color& tint,
                                  bool colored)
{
    const auto scale = tools.atlas.scale();

    return {{penOnBaseline.x + slot.offset.x,
             penOnBaseline.y + slot.offset.y,
             slot.src.w / scale,
             slot.src.h / scale},
            slot.src,
            tint,
            colored};
}

void GridPainter::queueGlyph(const GlyphSlot& slot,
                             Point penOnBaseline,
                             const Color& tint,
                             bool colored)
{
    const auto quad = placeGlyph(slot, penOnBaseline, tint, colored);
    tools.glyphs.add(quad.destination, quad.source, quad.tint, quad.colored);
}

void GridPainter::addCellGlyph(const ResolvedCell& cell,
                               Point cellOrigin,
                               Rgb foreground,
                               RowDrawing& drawing)
{
    if (cell.ch == U' ')
        return;

    const auto alpha = hasAttribute(cell, Attr::Faint) ? faintGlyphAlpha : opaqueAlpha;
    const auto pen = Point {cellOrigin.x, cellOrigin.y + tools.atlas.baseline()};

    forEachCellGlyph(cell,
                     [&](const GlyphSlot& slot)
                     {
                         if (!isDrawable(slot))
                             return;

                         const auto tint =
                             slot.colored ? Color::white(alpha) : toColor(foreground, alpha);
                         drawing.glyphs.push_back(placeGlyph(slot, pen, tint, slot.colored));
                     });
}

float GridPainter::measureCellWidth(const ResolvedCell& cell) const
{
    const auto cellW = tools.atlas.cellWidth();
    return hasAttribute(cell, Attr::Wide) ? cellW * doubleCellWidth : cellW;
}

void GridPainter::addCellUnderline(const ResolvedCell& cell,
                                   Point cellOrigin,
                                   Rgb foreground,
                                   RowDrawing& drawing)
{
    const auto y = cellOrigin.y + tools.atlas.baseline() + underlineGapBelowBaseline;
    drawing.decorations.push_back(
        {{cellOrigin.x, y, measureCellWidth(cell), decorationThickness}, toColor(foreground)});
}

void GridPainter::addCellStrikethrough(const ResolvedCell& cell,
                                       Point cellOrigin,
                                       Rgb foreground,
                                       RowDrawing& drawing)
{
    if (!hasAttribute(cell, Attr::Strike))
        return;

    const auto y = cellOrigin.y + tools.atlas.cellHeight() * strikethroughHeightFraction;
    drawing.decorations.push_back(
        {{cellOrigin.x, y, measureCellWidth(cell), decorationThickness}, toColor(foreground)});
}

bool GridPainter::isInHoveredLink(CellRef cell) const
{
    return hoveredLink && hoveredLink->contains(cell);
}

void GridPainter::drawGlyphOverCursor(Point cellOrigin, const PaintedLine& line, int column)
{
    if (column >= (int) line.size())
        return;

    const auto& cell = line[(std::size_t) column];

    if (!hasVisibleCharacter(cell))
        return;

    const auto pen = Point {cellOrigin.x, cellOrigin.y + tools.atlas.baseline()};

    forEachCellGlyph(cell,
                     [&](const GlyphSlot& slot)
                     {
                         if (isMonochromeDrawable(slot))
                             queueGlyph(slot, pen, toColor(tools.theme.background), false);
                     });
}

bool GridPainter::isCellInkShown(const ResolvedCell& cell) const
{
    if (hasAttribute(cell, Attr::WideCont) || hasAttribute(cell, Attr::Hidden))
        return false;

    return blinkVisible || !hasAttribute(cell, Attr::Blink);
}
} // namespace term
