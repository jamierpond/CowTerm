#include "TerminalViewTestPane.h"

#include <TerminalView/CellText.h>
#include <TerminalView/LinkDetection.h>

#include <Emulator/TermParser.h>

#include <array>

// Links in a pane: what a click or a tap opens, what a copy of a link's text
// gives, and which cells are underlined, for OSC 8 links, URLs in the text and
// URLs a program broke over rows at the pane's edge.

namespace
{
Graphics::MouseEvent linkClickAtCell(int col, int row)
{
    auto event = mouseAtCell(col, row);

    if constexpr (Platform::isMac())
        event.modifiers.command = true;
    else
        event.modifiers.control = true;

    return event;
}

struct LinkPane
{
    LinkPane()
    {
        pane.view->onOpenHyperlink = [this](const std::string& uri) { opened = uri; };
    }

    TestPane pane;
    std::string opened;
};
} // namespace

auto tLinkClickOpensTheHyperlink = test("View/linkClickOpensTheHyperlink") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("\033]8;;https://example.com\033\\link\033]8;;\033\\ plain");

    linked.pane.view->mouseDown(linkClickAtCell(1, 0));
    check(linked.opened == "https://example.com");

    linked.opened.clear();
    linked.pane.view->mouseDown(linkClickAtCell(6, 0));
    check(linked.opened.empty(), "text after the link's end opens nothing");
};

auto tLinkClickOpensAPlainTextUrl = test("View/linkClickOpensAUrlWrittenAsText") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("see https://example.com/a. done");

    linked.pane.view->mouseDown(linkClickAtCell(6, 0));
    check(linked.opened == "https://example.com/a");
};

namespace
{
constexpr auto underlineSearchRows = 4;

// Whether any pixel in the strip under a cell's baseline, where an underline
// sits and lowercase letters leave background, has the colour.
bool isUnderlinePainted(const Graphics::Image& image, int col, int row, Rgb expected)
{
    const auto& atlas = referenceAtlas();
    const auto x = marginX + ((float) col + 0.5f) * atlas.cellWidth();
    const auto baselineY = marginY + (float) row * atlas.cellHeight() + atlas.baseline();
    const auto firstY = (int) std::floor(baselineY) + 1;

    for (auto y = firstY; y < firstY + underlineSearchRows; ++y)
    {
        if (matches(image.at((int) x, y), expected))
            return true;
    }

    return false;
}
} // namespace

auto tHoverUnderlinesTheLink = test("View/linkModifierHoverUnderlinesTheWholeLink") = []
{
    auto pane = TestPane {};
    pane.showOutput("x https://example.com/a y");
    constexpr auto hoveredColumn = 5;
    constexpr auto otherLinkColumn = 15;
    constexpr auto plainColumn = 24;

    pane.view->mouseMoved(linkClickAtCell(hoveredColumn, 0));
    const auto image = pane.view->renderToImage(1.f);

    if (image.width() == 0)
        return;

    check(isUnderlinePainted(image, otherLinkColumn, 0, defaultTheme.foreground));
    check(!isUnderlinePainted(image, plainColumn, 0, defaultTheme.foreground));

    pane.view->mouseMoved(mouseAtCell(hoveredColumn, 0));
    const auto released = pane.view->renderToImage(1.f);
    check(!isUnderlinePainted(released, otherLinkColumn, 0, defaultTheme.foreground),
          "without the modifier the underline goes");
};

auto tPlainClickOnALinkSelects = test("View/aPlainClickOnALinkOpensNothing") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("\033]8;;https://example.com\033\\link\033]8;;\033\\");

    linked.pane.view->mouseDown(mouseAtCell(1, 0));

    check(linked.opened.empty());
};

auto tCustomSchemesAreNotOpened = test("View/linkWithACustomSchemeIsNotOpened") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("\033]8;;x-app://run\033\\link\033]8;;\033\\");

    linked.pane.view->mouseDown(linkClickAtCell(1, 0));

    check(linked.opened.empty());
};

namespace
{
// Claude Code 2.1.326's /login link: shape and length as Claude Code prints
// it, with made-up ids, long enough for four rows at 150 columns.
const auto claudeLoginUrl = std::string {
    "https://claude.com/cai/oauth/authorize?code=true&client_id=9d1c250a-e61b-44d9-88ed-"
    "5944d1962f5e&response_type=code&redirect_uri=https%3A%2F%2Fplatform.claude.com%2F"
    "oauth%2Fcode%2Fcallback&scope=org%3Acreate_api_key+user%3Aprofile+user%3Ainference"
    "+user%3Asessions%3Aclaude_code+user%3Amcp_servers+user%3Afile_upload&code_challenge="
    "Xq3vR8kLmN2pT5wY7zA1bC4dE6fG9hJ0kL3mN5pQ8rS&code_challenge_method=S256&state="
    "aB3cD5eF7gH9iJ1kL3mN5oP7qR9sT1uV3wX5yZ7aB9c&orgUUID=3f1e9c2a-7b4d-4e8a-9c61-"
    "2d5f8a0b7e43"};

constexpr auto paneWidths = std::array {150, 80, 45};
constexpr auto loginScreenRows = 16;
constexpr auto firstUrlRow = 2;
constexpr auto pastePromptRowsBelowTheUrl = 2;
const auto loginLead = std::string {" Use the URL below to sign in"};
const auto pastePrompt = std::string {" Paste code here if prompted >"};
const auto fullScreenWithMouseReports = std::string {"\033[?1049h\033[?1000h\033[?1006h"};

int countUrlRows(int columns)
{
    const auto length = static_cast<int>(claudeLoginUrl.size());
    return (length + columns - 1) / columns;
}

int findLastUrlRow(int columns)
{
    return firstUrlRow + countUrlRows(columns) - 1;
}

std::string moveCursorToRow(int row)
{
    return "\033[" + std::to_string(row + 1) + ";1H";
}

// Claude Code's /login screen in a full-screen tmux pane: the URL starts at
// the first column and fills each row to the edge. tmux draws every row at its
// own position, so no row is marked wrapped, and it drops the OSC 8 link.
std::string drawClaudeCodeLoginScreen(int columns)
{
    auto output = fullScreenWithMouseReports + moveCursorToRow(0) + loginLead;
    auto row = firstUrlRow;

    for (auto start = std::size_t {0}; start < claudeLoginUrl.size(); start += (std::size_t) columns)
    {
        output += moveCursorToRow(row) + "\033[2m" + claudeLoginUrl.substr(start, (std::size_t) columns)
                  + "\033[22m";
        ++row;
    }

    return output + moveCursorToRow(findLastUrlRow(columns) + pastePromptRowsBelowTheUrl) + pastePrompt
           + " ";
}

struct ScreenOfWidth
{
    ScreenOfWidth(int columns, const std::string& output)
        : screen(columns, loginScreenRows, theme)
    {
        parser.feed(output);
    }

    Theme theme;
    TermScreen screen;
    TermParser parser {screen, theme};
};

struct LoginScreen : ScreenOfWidth
{
    explicit LoginScreen(int columns)
        : ScreenOfWidth(columns, drawClaudeCodeLoginScreen(columns))
    {
    }
};
} // namespace

auto tLoginUrlOpensWhole = test("Links/claudeCodesLoginUrlBrokenOverFullRowsOpensWholeFromEveryRow") = []
{
    constexpr auto nearTheRowStart = 3;

    for (const auto columns: paneWidths)
    {
        const auto login = LoginScreen {columns};
        const auto lastUrlRow = findLastUrlRow(columns);

        for (auto row = firstUrlRow; row <= lastUrlRow; ++row)
        {
            const auto link = findLinkAt(login.screen, {row, nearTheRowStart});
            check(link && link->uri == claudeLoginUrl, "a click on any row of the URL opens all of it");
        }

        const auto link = findLinkAt(login.screen, {firstUrlRow, 0});
        check(link->span.start == CellRef {firstUrlRow, 0});
        check(link->span.indentedRowStarts.empty(), "every row is underlined from its first column");
        check(!findLinkAt(login.screen, {lastUrlRow + pastePromptRowsBelowTheUrl, 2}));
    }
};

auto tLoginUrlCopiesWhole = test("Copy/claudeCodesLoginUrlBrokenOverFullRowsCopiesOnOneLine") = []
{
    for (const auto columns: paneWidths)
    {
        const auto login = LoginScreen {columns};
        const auto lastUrlRow = findLastUrlRow(columns);
        const auto lastColumn = columns - 1;
        const auto pastePromptRow = lastUrlRow + pastePromptRowsBelowTheUrl;

        check(extractTextBetween(login.screen, {firstUrlRow, 0}, {lastUrlRow, lastColumn}) == claudeLoginUrl);
        check(extractTextBetween(login.screen, {0, 0}, {pastePromptRow, lastColumn})
                  == loginLead + "\n\n" + claudeLoginUrl + "\n\n" + pastePrompt,
              "the lines around the URL keep their line ends");

        constexpr auto partWayIn = 10;
        constexpr auto intoTheSecondRow = 5;
        const auto partOfTheUrl = claudeLoginUrl.substr(partWayIn, (std::size_t) (columns - partWayIn + intoTheSecondRow));
        check(extractTextBetween(login.screen, {firstUrlRow, partWayIn}, {firstUrlRow + 1, intoTheSecondRow - 1})
              == partOfTheUrl);
    }
};

auto tOrdinaryLinesKeepTheirEnds = test("Copy/rowsThatReallyEndKeepTheirLineEnds") = []
{
    constexpr auto narrowColumns = 20;
    const auto copyFirstTwoRows = [](const std::string& output)
    {
        const auto parsed = ScreenOfWidth {narrowColumns, output};
        return extractTextBetween(parsed.screen, {0, 0}, {1, narrowColumns - 1});
    };

    check(copyFirstTwoRows("words fill this row.\r\nnext") == "words fill this row.\nnext");
    check(copyFirstTwoRows("====================\r\nnext") == "====================\nnext",
          "one word to the edge that is not a URL");
    check(copyFirstTwoRows("see https://a.test/x\r\nThen") == "see https://a.test/x\nThen",
          "a URL after other words that happens to end at the edge");
    check(copyFirstTwoRows("https://a.test/abcde\r\n\xe2\x94\x80\xe2\x94\x80")
              == "https://a.test/abcde\n\xe2\x94\x80\xe2\x94\x80",
          "a box line under a URL");
    check(copyFirstTwoRows("aa\r\nbb") == "aa\nbb");
};

auto tOneWordRowJoins = test("Links/aUrlFillingItsRowFromTheFirstColumnRunsOnIntoTheNextRow") = []
{
    constexpr auto narrowColumns = 20;
    constexpr auto onTheUrl = CellRef {0, 10};

    const auto broken = ScreenOfWidth {narrowColumns, "https://a.test/abcde\r\nfgh rest"};
    check(findLinkAt(broken.screen, onTheUrl)->uri == "https://a.test/abcdefgh");
    check(extractTextBetween(broken.screen, {0, 0}, {1, narrowColumns - 1}) == "https://a.test/abcdefgh rest");

    const auto brokenAfterADot = ScreenOfWidth {narrowColumns, "https://platform.cl.\r\ncom/x"};
    check(findLinkAt(brokenAfterADot.screen, onTheUrl)->uri == "https://platform.cl.com/x");

    const auto afterWords = ScreenOfWidth {narrowColumns, "see https://a.test/x\r\nThen"};
    check(findLinkAt(afterWords.screen, onTheUrl)->uri == "https://a.test/x");

    const auto boxLine = ScreenOfWidth {narrowColumns, "https://a.test/abcde\r\n\xe2\x94\x80\xe2\x94\x80"};
    check(findLinkAt(boxLine.screen, onTheUrl)->uri == "https://a.test/abcde");
};

auto tCellsInLinks = test("Links/everyCellOfEveryLinkOnARowIsFound") = []
{
    constexpr auto narrowColumns = 20;

    const auto text = ScreenOfWidth {narrowColumns, "x https://a.test/ y"};
    const auto textCells = findCellsInLinksOnRow(text.screen, 0);
    check(!textCells[1] && textCells[2] && textCells[16] && !textCells[17] && !textCells[18]);

    const auto osc8 = ScreenOfWidth {narrowColumns, "\033]8;;https://x.test\033\\label\033]8;;\033\\ z"};
    const auto osc8Cells = findCellsInLinksOnRow(osc8.screen, 0);
    check(osc8Cells[0] && osc8Cells[4] && !osc8Cells[5] && !osc8Cells[6]);

    const auto customScheme = ScreenOfWidth {narrowColumns, "\033]8;;x-app://run\033\\label\033]8;;\033\\"};
    check(!findCellsInLinksOnRow(customScheme.screen, 0)[0], "a link CowTerm will not open");

    const auto softWrapped = ScreenOfWidth {narrowColumns, "go https://a.test/long/path ok"};
    const auto secondRowCells = findCellsInLinksOnRow(softWrapped.screen, 1);
    check(secondRowCells[0] && secondRowCells[6] && !secondRowCells[8], "the part the terminal wrapped");

    for (const auto columns: paneWidths)
    {
        const auto login = LoginScreen {columns};
        const auto middleCells = findCellsInLinksOnRow(login.screen, firstUrlRow + 1);
        check(std::all_of(middleCells.begin(), middleCells.begin() + columns, [](bool inLink) { return inLink; }));
        check(!findCellsInLinksOnRow(login.screen, 0)[2], "the lead above the URL");
    }
};

namespace
{
// A view whose grid is the given size, with the view no bigger than the grid
// so the grid's first row is the view's first row.
void showOutputInGridOfWidth(TestPane& pane, int columns, const std::string& output)
{
    constexpr auto lessThanACell = 1.f;
    const auto& atlas = referenceAtlas();
    const auto width = 2.f * marginX + (float) columns * atlas.cellWidth() + lessThanACell;
    const auto height = 2.f * marginY + (float) loginScreenRows * atlas.cellHeight() + lessThanACell;
    pane.view->setBounds({0.f, 0.f, width, height});
    pane.view->resized();
    pane.view->setFixedGridSize(PtySize {columns, loginScreenRows});
    pane.showOutput(output);
    pane.takeWritten();
}

void showLoginScreenInGridOfWidth(TestPane& pane, int columns)
{
    showOutputInGridOfWidth(pane, columns, drawClaudeCodeLoginScreen(columns));
}

Graphics::MouseEvent makeTapAtCell(int col, int row, Graphics::MouseEventType type)
{
    auto tap = mouseAtCell(col, row);
    tap.downPos = tap.pos;
    tap.button = Graphics::MouseButton::Left;
    tap.fromTouch = true;
    tap.clickCount = 1;
    tap.type = type;
    return tap;
}

void tapAtCell(TestPane& pane, int col, int row)
{
    pane.view->dispatchMouseEvent(makeTapAtCell(col, row, Graphics::MouseEventType::Down));
    pane.view->dispatchMouseEvent(makeTapAtCell(col, row, Graphics::MouseEventType::Up));
}
} // namespace

auto tLinkClickOnLoginUrl = test("View/linkClickOnClaudeCodesLoginUrlOpensAllOfItAndSendsNothing") = []
{
    for (const auto columns: paneWidths)
    {
        auto linked = LinkPane {};
        showLoginScreenInGridOfWidth(linked.pane, columns);
        const auto lastUrlRow = findLastUrlRow(columns);

        linked.pane.view->mouseDown(linkClickAtCell(columns - 1, lastUrlRow - 1));
        linked.pane.view->mouseUp(linkClickAtCell(columns - 1, lastUrlRow - 1));

        check(linked.opened == claudeLoginUrl);
        check(linked.pane.takeWritten().empty(), "neither the press nor its release reaches Claude Code");
    }
};

auto tSelectionOfLoginUrl = test("View/selectingClaudeCodesLoginUrlCopiesItOnOneLine") = []
{
    for (const auto columns: paneWidths)
    {
        auto pane = TestPane {};
        showLoginScreenInGridOfWidth(pane, columns);
        const auto lastUrlRow = findLastUrlRow(columns);
        const auto lastUrlColumn = (int) ((claudeLoginUrl.size() - 1) % (std::size_t) columns);

        pane.view->mouseDown(shiftMouseAtCell(0, firstUrlRow));
        pane.view->mouseDragged(shiftMouseAtCell(lastUrlColumn, lastUrlRow));
        pane.view->mouseUp(shiftMouseAtCell(lastUrlColumn, lastUrlRow));

        check(pane.view->getSelectedText() == claudeLoginUrl, "a mouse selection");
    }
};

auto tCopyModeOnLoginUrl = test("View/copyModeLinesOverClaudeCodesLoginUrlYankItOnOneLine") = []
{
    for (const auto columns: paneWidths)
    {
        auto pane = TestPane {};
        showLoginScreenInGridOfWidth(pane, columns);
        pane.view->enterCopyMode();

        for (auto step = 0; step < pastePromptRowsBelowTheUrl; ++step)
            pane.view->keyDown(typedKey("k"));

        pane.view->keyDown(typedKey("V"));

        for (auto step = 1; step < countUrlRows(columns); ++step)
            pane.view->keyDown(typedKey("k"));

        check(pane.view->getSelectedText() == claudeLoginUrl, "what y would copy");
        pane.view->keyDown(typedKey("q"));
    }
};

auto tTapOpensALinkUnderMouseReporting = test("View/aTapOnALinkOpensItEvenWhenTheProgramReportsTheMouse") = []
{
    constexpr auto columns = 80;
    auto linked = LinkPane {};
    showLoginScreenInGridOfWidth(linked.pane, columns);

    tapAtCell(linked.pane, 3, firstUrlRow + 1);

    check(linked.opened == claudeLoginUrl);
    check(linked.pane.takeWritten().empty(), "the program gets no click");
};

auto tTapElsewhereIsAClick = test("View/aTapOffALinkStillReachesTheProgramAsAClick") = []
{
    constexpr auto columns = 80;
    constexpr auto onTheLead = 3;
    auto linked = LinkPane {};
    showLoginScreenInGridOfWidth(linked.pane, columns);

    tapAtCell(linked.pane, onTheLead, 0);

    check(linked.opened.empty());
    check(linked.pane.takeWritten() == "\033[<0;4;1M\033[<0;4;1m");
};

auto tTapOpensALinkInAShell = test("View/aTapOnALinkOpensItWithoutMouseReporting") = []
{
    auto linked = LinkPane {};
    linked.pane.showOutput("see https://example.com/a now");

    tapAtCell(linked.pane, 1, 0);
    check(linked.opened.empty(), "a tap on plain text opens nothing");

    tapAtCell(linked.pane, 8, 0);
    check(linked.opened == "https://example.com/a");
};

auto tEveryLinkUnderlined = test("View/underliningEveryLinkMarksLinksWithoutAHover") = []
{
    constexpr auto linkColumn = 5;
    constexpr auto plainColumn = 24;
    auto pane = TestPane {};
    pane.showOutput("x https://example.com/a y");

    const auto plain = pane.view->renderToImage(1.f);

    if (plain.width() == 0)
        return;

    check(!isUnderlinePainted(plain, linkColumn, 0, defaultTheme.foreground), "off by default, as on the Mac");

    pane.view->setUnderlinesEveryLink(true);
    const auto underlined = pane.view->renderToImage(1.f);
    check(isUnderlinePainted(underlined, linkColumn, 0, defaultTheme.foreground));
    check(!isUnderlinePainted(underlined, plainColumn, 0, defaultTheme.foreground));
};

namespace
{
constexpr auto claudeReplyMargin = 2;
const auto replyUrlStart = std::string {"https://github.com/eyalamirmusic/eacp/pulls?q=is%3Apr"};
const auto replyUrlPartBeforeTheBreak = std::string {"&utf8=%E2"};
const auto replyUrlPartAfterTheBreak = std::string {"%9C%93&cowterm_long_link_test=click_me_copies_as_one_line"};
const auto replyUrlFiller = std::string {"+takes+no+clicks+or+touches"};

// A URL whose second row ends between two percent escapes of one character.
std::string makeReplyUrlBreakingInsideAnEscape(int columns)
{
    const auto twoRowsOfText = (std::size_t) (2 * (columns - claudeReplyMargin));
    auto url = replyUrlStart;

    while (url.size() + replyUrlPartBeforeTheBreak.size() < twoRowsOfText)
        url += replyUrlFiller[(url.size() - replyUrlStart.size()) % replyUrlFiller.size()];

    return url + replyUrlPartBeforeTheBreak + replyUrlPartAfterTheBreak;
}

// Claude Code's reply text in a full-screen tmux pane: every row keeps the
// reply's left margin, and tmux draws each row at its own position.
std::string drawClaudeCodeReplyWithUrl(const std::string& url, int columns)
{
    const auto rowText = (std::size_t) (columns - claudeReplyMargin);
    const auto margin = std::string((std::size_t) claudeReplyMargin, ' ');
    auto output = fullScreenWithMouseReports;
    auto row = 0;

    for (auto start = std::size_t {0}; start < url.size(); start += rowText)
        output += moveCursorToRow(row++) + margin + url.substr(start, rowText);

    return output;
}

int countReplyUrlRows(const std::string& url, int columns)
{
    const auto rowText = columns - claudeReplyMargin;
    return ((int) url.size() + rowText - 1) / rowText;
}
} // namespace

auto tReplyUrlWithMargin = test("View/aClaudeReplyUrlBrokenInsideAnEscapeOpensAndCopiesWhole") = []
{
    for (const auto columns: paneWidths)
    {
        const auto url = makeReplyUrlBreakingInsideAnEscape(columns);
        const auto output = drawClaudeCodeReplyWithUrl(url, columns);
        const auto lastRow = countReplyUrlRows(url, columns) - 1;

        for (auto row = 0; row <= lastRow; ++row)
        {
            auto clicked = LinkPane {};
            showOutputInGridOfWidth(clicked.pane, columns, output);
            clicked.pane.view->mouseDown(linkClickAtCell(claudeReplyMargin + 1, row));
            check(clicked.opened == url, "Cmd+click");

            auto tapped = LinkPane {};
            showOutputInGridOfWidth(tapped.pane, columns, output);
            tapAtCell(tapped.pane, claudeReplyMargin + 1, row);
            check(tapped.opened == url, "a tap");
        }

        auto yanked = TestPane {};
        showOutputInGridOfWidth(yanked, columns, output);
        yanked.view->enterCopyMode();
        yanked.view->keyDown(typedKey("V"));

        for (auto step = 0; step < lastRow; ++step)
            yanked.view->keyDown(typedKey("k"));

        check(yanked.view->getSelectedText().find(url) != std::string::npos, "what y would copy");
    }
};
