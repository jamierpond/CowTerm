#pragma once

#include "GlyphAtlas.h"
#include "GridSelection.h"

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

struct CellColours
{
    Rgb foreground = 0;
    Rgb background = 0;
};

// fg/bg after inverse video and selection are applied.
CellColours findCellColours(const Cell& cell, bool selected, const Theme& theme);

// One visible row as the painter sees it.
struct PaintedRow
{
    const Line& line;
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
                bool blinkingTextVisible);

    // Rasterize ahead of the first draw so the atlas uploads once a frame.
    void rasterizeRowGlyphs(const PaintedRow& row);
    void rasterizeTextGlyphs(const std::string& text);

    void drawRowBackgrounds(const PaintedRow& row);
    void drawRowGlyphs(const PaintedRow& row);

    void drawCursor(eacp::Graphics::Point cellOrigin,
                    CursorStyle style,
                    const Line& line,
                    int column);
    void drawHollowCursor(eacp::Graphics::Point cellOrigin);

    // A filled cell with the glyph under it redrawn in the background colour
    // so it stays legible.
    void drawBlockCursor(eacp::Graphics::Point cellOrigin, const Line& line, int column);

    // tmux's position badge, top right.
    void drawCopyModeBadge(const std::string& indicator, float viewWidth);

private:
    GlyphSlot requestCellGlyph(const Cell& cell);
    void queueGlyph(const GlyphSlot& slot,
                    eacp::Graphics::Point penOnBaseline,
                    const eacp::Graphics::Color& tint,
                    bool colored);
    void drawCellGlyph(const Cell& cell,
                       eacp::Graphics::Point cellOrigin,
                       Rgb foreground);
    void drawCellDecorations(const Cell& cell,
                             eacp::Graphics::Point cellOrigin,
                             Rgb foreground);
    void drawGlyphOverCursor(eacp::Graphics::Point cellOrigin,
                             const Line& line,
                             int column);
    bool isCellInkShown(const Cell& cell) const;

    PaintTools tools;
    const SelectionRange& selection;
    bool blinkVisible;
};
} // namespace term
