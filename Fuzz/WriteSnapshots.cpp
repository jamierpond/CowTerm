#include "CorpusFiles.h"
#include "ParserFuzzInput.h"

#include <iostream>

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 3)
    {
        std::cerr << "usage: CowTermWriteFuzzSnapshots CORPUS_DIR SNAPSHOTS_FILE\n";
        return 1;
    }

    auto output = std::ofstream {arguments[2]};

    for (const auto& file: term::fuzz::listCorpusFiles(arguments[1]))
    {
        const auto screen = term::fuzz::runParserFuzzInput(term::fuzz::readWholeFile(file));
        output << file.filename().string() << ' ' << term::fuzz::hashText(screen) << '\n';
    }

    return 0;
}
