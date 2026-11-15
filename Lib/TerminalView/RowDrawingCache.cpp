#include "RowDrawingCache.h"

#include <algorithm>

namespace term
{
void RowDrawingCache::invalidateAllRows()
{
    allRowsStale = true;
}

void RowDrawingCache::invalidateRowsWithBlinkingText()
{
    staleRows.resize(rows.size());

    for (auto row = std::size_t {0}; row < rows.size(); ++row)
    {
        if (rows[row].hasBlinkingText)
            staleRows[row] = true;
    }
}

bool RowDrawingCache::isRowStale(const RowDrawingSource& source, int visualRow) const
{
    const auto index = (std::size_t) visualRow;
    const auto markedStale = index < staleRows.size() && staleRows[index];
    return allRowsStale || markedStale || source.isRowDirty(visualRow);
}

void RowDrawingCache::rebuildRows(const RowDrawingSource& source)
{
    for (auto row = 0; row < source.rowCount; ++row)
    {
        if (isRowStale(source, row))
            rows[(std::size_t) row] = source.buildRow(row);
    }
}

void RowDrawingCache::updateRows(const RowDrawingSource& source)
{
    const auto generationBefore = source.findAtlasGeneration();

    if ((int) rows.size() != source.rowCount || generationBefore != builtAtlasGeneration)
        allRowsStale = true;

    rows.resize((std::size_t) source.rowCount);
    rebuildRows(source);

    // Building a row can fill the atlas and reset it, which strands the rows
    // built before it this frame.
    if (source.findAtlasGeneration() != generationBefore)
    {
        allRowsStale = true;
        rebuildRows(source);
    }

    builtAtlasGeneration = source.findAtlasGeneration();
    allRowsStale = false;
    staleRows.assign(rows.size(), false);
}
} // namespace term
