#include "TextLayout.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

namespace
{
using Lines = std::vector<std::string>;

constexpr auto oneUnitPerByte = [](const std::string& text) { return (float) text.size(); };
} // namespace

auto tSplitKeepsEmptyPieces = test("TextLayout/splitAtSeparatorKeepsEmptyPieces") = []
{
    check(splitAtSeparator("", '\t') == Lines {""});
    check(splitAtSeparator("a", '\t') == Lines {"a"});
    check(splitAtSeparator("a\tb", '\t') == Lines {"a", "b"});
    check(splitAtSeparator("\ta\t\t", '\t') == Lines {"", "a", "", ""});
    check(splitAtSeparator(std::string {"x\0y", 3}, '\0') == Lines {"x", "y"});
};

auto tEraseLastCodepoint = test("TextLayout/eraseLastCodepointRemovesAWholeCharacter") = []
{
    auto text = std::string {"aé"};
    eraseLastCodepoint(text);
    check(text == "a");

    eraseLastCodepoint(text);
    check(text.empty());

    eraseLastCodepoint(text);
    check(text.empty(), "erasing nothing is a no-op");

    auto emoji = std::string {"x✳"};
    eraseLastCodepoint(emoji);
    check(emoji == "x");
};

auto tCodepointSteps = test("TextLayout/codepointBoundaries") = []
{
    const auto text = std::string {"é✳a"};
    check(findNextCodepointEnd(text, 0) == 2);
    check(findNextCodepointEnd(text, 2) == 5);
    check(findNextCodepointEnd(text, 5) == 6);
    check(findPreviousCodepointStart(text, 6) == 5);
    check(findPreviousCodepointStart(text, 5) == 2);
    check(findPreviousCodepointStart(text, 0) == 0);
};

auto tPrintableTypedText = test("TextLayout/isPrintableTypedTextRejectsControlCodes") = []
{
    check(isPrintableTypedText("a"));
    check(isPrintableTypedText(" "));
    check(isPrintableTypedText("é"));
    check(!isPrintableTypedText(""));
    check(!isPrintableTypedText("\x1f"));
    check(!isPrintableTypedText("\x7f"));
    check(!isPrintableTypedText("\t"));
};

auto tTruncateWithEllipsis = test("TextLayout/truncateWithEllipsisCutsByBytes") = []
{
    check(truncateWithEllipsis("abc", 3) == "abc");
    check(truncateWithEllipsis("abcd", 3) == "ab…");
    check(truncateWithEllipsis("", 1).empty());
};

auto tTruncateToWidth = test("TextLayout/truncateTextToWidthFitsTheEllipsisIn") = []
{
    check(truncateTextToWidth("hello", 5.f, oneUnitPerByte) == "hello");

    // "…" is three bytes, so at one unit a byte "he…" is six wide.
    check(truncateTextToWidth("hello world", 6.f, oneUnitPerByte) == "hel…");
    check(truncateTextToWidth("hello", 2.f, oneUnitPerByte) == "…");
};

auto tWrapByWords = test("TextLayout/wrapTextToWidthBreaksBetweenWords") = []
{
    check(wrapTextToWidth("one two three", 7.f, oneUnitPerByte) == Lines {"one two", "three"});
    check(wrapTextToWidth("one two three", 100.f, oneUnitPerByte) == Lines {"one two three"});
    check(wrapTextToWidth("", 10.f, oneUnitPerByte) == Lines {""});
};

auto tWrapKeepsParagraphs = test("TextLayout/wrapTextToWidthKeepsEachLineBreak") = []
{
    check(wrapTextToWidth("a\n\nb", 10.f, oneUnitPerByte) == Lines {"a", "", "b"});
};

auto tWrapBreaksLongWords = test("TextLayout/wrapTextToWidthBreaksAWordWiderThanALine") = []
{
    check(wrapTextToWidth("abcdefgh", 3.f, oneUnitPerByte) == Lines {"abc", "def", "gh"});
    check(wrapTextToWidth("hi abcdefgh", 3.f, oneUnitPerByte)
          == Lines {"hi", "abc", "def", "gh"});

    // A codepoint wider than the line still goes on a line of its own; when
    // the whole word breaks away, an empty line is left after it.
    check(wrapTextToWidth("éé", 1.f, oneUnitPerByte) == Lines {"é", "é", ""});
};
