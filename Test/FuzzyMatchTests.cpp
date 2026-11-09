#include "FuzzyMatch.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto subsequenceScores = test("FuzzyMatch/subsequenceScores") = []
{
    check(fuzzyScore("abc", "abc") == std::optional<int> {21});
    check(fuzzyScore("ac", "abc") == std::optional<int> {5});
    check(fuzzyScore("", "anything") == std::optional<int> {0});
    check(fuzzyScore("ABC", "abc") == std::optional<int> {21});
};

auto missingCharacterFails = test("FuzzyMatch/missingCharacterFails") = []
{
    check(!fuzzyScore("abd", "abc"));
    check(!fuzzyScore("ba", "ab"));
    check(!fuzzyScore("a", ""));
};

auto boundariesAndSpaces = test("FuzzyMatch/boundariesAndSpaces") = []
{
    check(fuzzyScore("eacp term", "eacp — Terminal") == fuzzyScore("eacpterm", "eacp — Terminal"));
    check(fuzzyScore("t", "eacp — Terminal") == std::optional<int> {2});
    check(fuzzyScore("ft", "foo-test") == std::optional<int> {7});
    check(fuzzyScore("x", "abcdefgx") == std::optional<int> {-2});
};
