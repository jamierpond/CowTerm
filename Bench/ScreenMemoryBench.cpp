#include <CowTermCore/TermParser.h>
#include <CowTermCore/TermScreen.h>

#include <cstdio>
#include <memory>
#include <string>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

// Fills one pane's screen with 10,000 lines of coloured text (scrollback
// full) and reports how much memory that took: the process's resident size
// before and after, on macOS, and sizeof(Cell).
//
//   CowTermScreenMemoryBench [COLUMNS]
namespace
{
constexpr auto defaultColumns = 200;
constexpr auto rows = 50;
constexpr auto lineCount = 10000;
constexpr auto bytesPerMegabyte = 1024.0 * 1024.0;

double findResidentMegabytes()
{
#if defined(__APPLE__)
    auto info = mach_task_basic_info_data_t {};
    auto count = mach_msg_type_number_t {MACH_TASK_BASIC_INFO_COUNT};
    task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count);
    return (double) info.resident_size / bytesPerMegabyte;
#else
    return 0;
#endif
}

std::string makeColouredLine(int line, int columns)
{
    auto text = "\033[3" + std::to_string(line % 8) + "m" + std::to_string(line) + " ";

    while ((int) text.size() < columns)
        text += "lorem ipsum \033[1mdolor\033[22m sit amet ";

    return text.substr(0, (std::size_t) columns) + "\033[0m\r\n";
}
} // namespace

int main(int argc, char** argv)
{
    const auto columns = argc > 1 ? std::stoi(argv[1]) : defaultColumns;
    const auto before = findResidentMegabytes();

    const auto theme = term::Theme {};
    auto screen = std::make_unique<term::TermScreen>(columns, rows, theme);
    auto parser = term::TermParser {*screen, theme};

    for (auto line = 0; line < lineCount + rows; ++line)
        parser.feed(makeColouredLine(line, columns));

    const auto after = findResidentMegabytes();

    std::printf("sizeof(Cell) %zu bytes; %d columns, %d lines of scrollback: %.1f MB resident for the pane\n",
                sizeof(term::Cell),
                columns,
                screen->scrollbackSize(),
                after - before);
    return 0;
}
