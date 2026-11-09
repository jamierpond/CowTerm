#pragma once

#include "GridPainter.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace term
{
// Where the cache gets a frame's rows from.
struct RowDrawingSource
{
    int rowCount = 0;
    std::function<bool(int visualRow)> isRowDirty;
    std::function<RowDrawing(int visualRow)> buildRow;
    std::function<std::uint32_t()> findAtlasGeneration;
};

// Each visible row's draws, kept between frames. A frame rebuilds only the
// rows the screen marked dirty, unless something that moves or recolours every
// row changed (scroll offset, selection, size, font): the view then calls
// invalidateAllRows. An atlas reset also rebuilds every row, because the
// cached source rects point at glyphs that are gone.
class RowDrawingCache
{
public:
    void invalidateAllRows();

    // Blinking text shows or hides with the blink phase.
    void invalidateRowsWithBlinkingText();

    void updateRows(const RowDrawingSource& source);

    const std::vector<RowDrawing>& getRows() const { return rows; }

private:
    void rebuildRows(const RowDrawingSource& source);
    bool isRowStale(const RowDrawingSource& source, int visualRow) const;

    std::vector<RowDrawing> rows;
    std::vector<bool> staleRows;
    bool allRowsStale = true;
    std::uint32_t builtAtlasGeneration = 0;
};
} // namespace term
