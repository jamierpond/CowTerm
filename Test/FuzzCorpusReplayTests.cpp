#include "CorpusFiles.h"
#include "ParserFuzzInput.h"

#include <NanoTest/NanoTest.h>

using namespace nano;

namespace
{
const auto corpusDirectory = std::filesystem::path {COWTERM_FUZZ_CORPUS_DIR};
const auto snapshotsFile = std::filesystem::path {COWTERM_FUZZ_SNAPSHOTS_FILE};
} // namespace

auto tCorpusReplaysToRecordedScreens = test("FuzzCorpus/everyRecordingReplaysToItsSnapshot") = []
{
    const auto snapshots = term::fuzz::readSnapshots(snapshotsFile);
    const auto files = term::fuzz::listCorpusFiles(corpusDirectory);

    check(!files.empty(), "the corpus directory has no recordings");

    for (const auto& file: files)
    {
        const auto name = file.filename().string();
        const auto screen = term::fuzz::runParserFuzzInput(term::fuzz::readWholeFile(file));
        const auto snapshot = snapshots.find(name);

        check(snapshot != snapshots.end(), name + " has no snapshot; run Scripts/update-fuzz-snapshots");

        if (snapshot != snapshots.end())
            check(snapshot->second == term::fuzz::hashText(screen), name + " no longer replays to its snapshot");
    }
};
