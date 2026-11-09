#include "CorpusFiles.h"
#include "ParserFuzzInput.h"

#include <CowTermCore/TerminalModel.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

namespace
{
const auto stateCorpusDirectory = std::filesystem::path {COWTERM_FUZZ_CORPUS_DIR};

struct FuzzTerminal
{
    FuzzTerminal(int columns, int rows)
        : model(columns, rows, theme)
    {
    }

    std::string serialize() const { return serializeTerminalState(model.captureState()); }

    Theme theme;
    TerminalModel model;
};
} // namespace

// Each recording is cut in half: a terminal restored from a snapshot taken at
// the cut must end up exactly where the uninterrupted one does.
auto tCorpusSurvivesSnapshotMidway = test("FuzzCorpus/everyRecordingSurvivesASnapshotMidway") = []
{
    const auto files = fuzz::listCorpusFiles(stateCorpusDirectory);
    check(!files.empty(), "the corpus directory has no recordings");

    for (const auto& file: files)
    {
        const auto bytes = fuzz::readWholeFile(file);
        const auto input = fuzz::splitParserFuzzInput(bytes);

        auto uninterrupted = FuzzTerminal {input.columns, input.rows};
        uninterrupted.model.feed(input.firstHalf);

        const auto parsed = parseTerminalState(uninterrupted.serialize());
        check(parsed.has_value(), file.filename().string() + " did not parse back");

        if (!parsed)
            continue;

        auto restored = FuzzTerminal {input.columns, input.rows};
        restored.model.restoreState(*parsed);

        for (auto* terminal: {&uninterrupted, &restored})
        {
            terminal->model.screen.resize(input.resizedColumns, input.resizedRows);
            terminal->model.feed(input.secondHalf);
        }

        check(restored.serialize() == uninterrupted.serialize(),
              file.filename().string() + " diverged after restoring");
    }
};
