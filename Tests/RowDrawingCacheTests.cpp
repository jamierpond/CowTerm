#include <CowTermTerminal/RowDrawingCache.h>

#include <NanoTest/NanoTest.h>

#include <vector>

using namespace nano;
using namespace term;

namespace
{
constexpr auto cachedRows = 4;
constexpr auto firstAtlasGeneration = std::uint32_t {1};

// Counts builds per row; each built row records which frame built it.
struct CountingRowSource
{
    RowDrawingSource makeSource()
    {
        return {cachedRows,
                [this](int row) { return dirtyRows[(std::size_t) row]; },
                [this](int row)
                {
                    ++builds[(std::size_t) row];
                    auto drawing = RowDrawing {};
                    drawing.hasBlinkingText = blinkingRows[(std::size_t) row];

                    if (resetAtlasWhileBuildingRow == row)
                    {
                        ++atlasGeneration;
                        resetAtlasWhileBuildingRow = -1;
                    }

                    return drawing;
                },
                [this] { return atlasGeneration; }};
    }

    void runFrame(RowDrawingCache& cache)
    {
        builds.assign(cachedRows, 0);
        cache.updateRows(makeSource());
        dirtyRows.assign(cachedRows, false);
    }

    std::vector<bool> dirtyRows = std::vector<bool>(cachedRows, true);
    std::vector<bool> blinkingRows = std::vector<bool>(cachedRows, false);
    std::vector<int> builds = std::vector<int>(cachedRows, 0);
    std::uint32_t atlasGeneration = firstAtlasGeneration;
    int resetAtlasWhileBuildingRow = -1;
};

using Builds = std::vector<int>;
} // namespace

auto tFirstFrameBuildsAll = test("RowDrawingCache/theFirstFrameBuildsEveryRow") = []
{
    auto cache = RowDrawingCache {};
    auto source = CountingRowSource {};
    source.runFrame(cache);
    check(source.builds == Builds {1, 1, 1, 1});
    check((int) cache.getRows().size() == cachedRows);
};

auto tCleanRowsReused = test("RowDrawingCache/cleanRowsAreReusedAndDirtyRowsRebuilt") = []
{
    auto cache = RowDrawingCache {};
    auto source = CountingRowSource {};
    source.runFrame(cache);

    source.dirtyRows[2] = true;
    source.runFrame(cache);
    check(source.builds == Builds {0, 0, 1, 0});

    source.runFrame(cache);
    check(source.builds == Builds {0, 0, 0, 0});
};

auto tInvalidateRebuildsAll = test("RowDrawingCache/invalidatingRebuildsEveryRowOnce") = []
{
    auto cache = RowDrawingCache {};
    auto source = CountingRowSource {};
    source.runFrame(cache);

    cache.invalidateAllRows();
    source.runFrame(cache);
    check(source.builds == Builds {1, 1, 1, 1});

    source.runFrame(cache);
    check(source.builds == Builds {0, 0, 0, 0});
};

auto tBlinkRebuildsBlinkingRows = test("RowDrawingCache/aBlinkRebuildsOnlyRowsWithBlinkingText") = []
{
    auto cache = RowDrawingCache {};
    auto source = CountingRowSource {};
    source.blinkingRows[1] = true;
    source.runFrame(cache);

    cache.invalidateRowsWithBlinkingText();
    source.runFrame(cache);
    check(source.builds == Builds {0, 1, 0, 0});
};

auto tAtlasResetBetweenFrames = test("RowDrawingCache/anAtlasResetBetweenFramesRebuildsEveryRow") = []
{
    auto cache = RowDrawingCache {};
    auto source = CountingRowSource {};
    source.runFrame(cache);

    ++source.atlasGeneration;
    source.runFrame(cache);
    check(source.builds == Builds {1, 1, 1, 1});
};

auto tAtlasResetMidFrame = test("RowDrawingCache/anAtlasResetWhileBuildingRebuildsEveryRow") = []
{
    auto cache = RowDrawingCache {};
    auto source = CountingRowSource {};
    source.runFrame(cache);

    source.dirtyRows[3] = true;
    source.resetAtlasWhileBuildingRow = 3;
    source.runFrame(cache);
    check(source.builds == Builds {1, 1, 1, 2});
};
