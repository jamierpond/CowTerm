#pragma once

#include "GlyphAtlas.h"
#include "GridSelection.h"
#include "LinkDetection.h"

#include "CowTermCore/TermScreen.h"

#include <eacp/Sprites/Sprites.h>

#include <string>
#include <vector>

namespace term
{
// DECSCUSR's three shapes; 0 and the odd numbers blink.
enum class CursorStyle
{
    Block,
    Underline,
    Bar
};

CursorStyle findCursorStyle(int cursorShape);
bool isBlinkingCursorShape(int cursorShape);

// One row's cells with styles and clusters looked up, for one frame.
using PaintedLine = std::vector<ResolvedCell>;

struct CellColours
{
    Rgb foreground = 0;
    Rgb background = 0;
};

// fg/bg after inverse video and selection are applied.
CellColours findCellColours(const ResolvedCell& cell, bool selected, const Theme& theme);

// One visible row as the painter sees it.
struct PaintedRow
{
    PaintedLine line;
    long absoluteRow = 0;
    float y = 0;
    int columns = 0;
};

// A stretch of cells sharing one non-default background, [firstColumn,
// endColumn).
struct BackgroundRun
{
    int firstColumn = 0;
    int endColumn = 0;
    Rgb colour = 0;
};

std::vector<BackgroundRun> findBackgroundRuns(const PaintedRow& row,
                                              const SelectionRange& selection,
                                              const Theme& theme);

struct RectFill
{
    eacp::Graphics::Rect rect;
    eacp::Graphics::Color colour;
};

struct GlyphQuad
{
    eacp::Graphics::Rect destination;
    eacp::Graphics::Rect source;
    eacp::Graphics::Color tint;
    bool colored = false;
};

// One row's draws, kept between frames so an unchanged row is not rebuilt.
// Atlas source rects stay valid until the atlas generation changes.
struct RowDrawing
{
    std::vector<RectFill> backgrounds;
    std::vector<RectFill> decorations;
    std::vector<GlyphQuad> glyphs;
    bool hasBlinkingText = false;
};

// What the painter draws with, borrowed for one frame.
struct PaintTools
{
    GlyphAtlas& atlas;
    eacp::Sprites::SpriteRenderer& sprites;
    eacp::Text::GlyphRenderer& glyphs;
    const Theme& theme;
    eacp::Graphics::Point margins;
};

// Draws the cell grid's layers: backgrounds as sprite runs, glyphs through
// the instanced glyph renderer, decorations, cursors and the copy-mode badge.
// Queues only; the caller begins and flushes the renderers in layer order.
class GridPainter
{
public:
    GridPainter(const PaintTools& toolsToUse,
                const SelectionRange& selectionToShow,
                const std::optional<LinkSpan>& hoveredLinkToUnderline,
                bool blinkingTextVisible);

    // Rasterize ahead of the first draw so the atlas uploads once a frame.
    void rasterizeTextGlyphs(const std::string& text);

    // Rasterizes the row's glyphs as a side effect; commit the atlas before
    // submitting.
    RowDrawing buildRowDrawing(const PaintedRow& row);

    // Every row's backgrounds, then every row's decorations, then the glyphs.
    void submitRowDrawings(const std::vector<RowDrawing>& rows);

    void drawCursor(eacp::Graphics::Point cellOrigin,
                    CursorStyle style,
                    const PaintedLine& line,
                    int column);
    void drawHollowCursor(eacp::Graphics::Point cellOrigin);

    // A filled cell with the glyph under it redrawn in the background colour
    // so it stays legible.
    void drawBlockCursor(eacp::Graphics::Point cellOrigin, const PaintedLine& line, int column);

    // tmux's position badge, top right.
    void drawCopyModeBadge(const std::string& indicator, float viewWidth);

private:
    template <typename Visit>
    void forEachCellGlyph(const ResolvedCell& cell, Visit&& visit);
    GlyphQuad placeGlyph(const GlyphSlot& slot,
                         eacp::Graphics::Point penOnBaseline,
                         const eacp::Graphics::Color& tint,
                         bool colored);
    void queueGlyph(const GlyphSlot& slot,
                    eacp::Graphics::Point penOnBaseline,
                    const eacp::Graphics::Color& tint,
                    bool colored);
    void fillRects(const std::vector<RectFill>& fills);
    void addGlyphQuads(const std::vector<GlyphQuad>& quads);
    void addRowBackgrounds(const PaintedRow& row, RowDrawing& drawing);
    void addRowInk(const PaintedRow& row, RowDrawing& drawing);
    void addCellGlyph(const ResolvedCell& cell,
                      eacp::Graphics::Point cellOrigin,
                      Rgb foreground,
                      RowDrawing& drawing);
    float measureCellWidth(const ResolvedCell& cell) const;
    void addCellUnderline(const ResolvedCell& cell,
                          eacp::Graphics::Point cellOrigin,
                          Rgb foreground,
                          RowDrawing& drawing);
    void addCellStrikethrough(const ResolvedCell& cell,
                              eacp::Graphics::Point cellOrigin,
                              Rgb foreground,
                              RowDrawing& drawing);
    bool isInHoveredLink(CellRef cell) const;
    void drawGlyphOverCursor(eacp::Graphics::Point cellOrigin,
                             const PaintedLine& line,
                             int column);
    bool isCellInkShown(const ResolvedCell& cell) const;

    PaintTools tools;
    const SelectionRange& selection;
    const std::optional<LinkSpan>& hoveredLink;
    bool blinkVisible;
};
} // namespace term
