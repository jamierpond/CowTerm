#include "Typing.h"

#include <TerminalView/KeyEncoding.h>

#include <NanoTest/NanoTest.h>

#include <variant>

// The phone's typing: Ctrl that latches for one key, and what the key row's
// keys send, through the Mac's own key encoding.
// iOS/Typing.cpp, built here on the Mac.

using namespace nano;
using namespace cowterm::mobile;

namespace
{
constexpr auto iPhone17eWidth = 390.0f;
constexpr auto iPhone18ProMaxWidth = 440.0f;
constexpr auto widestKey = 48.0f;
constexpr auto layoutTolerance = 0.01f;
constexpr auto ctrlC = "\x03";
constexpr auto escape = "\x1b";
constexpr auto upArrow = "\x1b[A";
constexpr auto upArrowInApplicationMode = "\x1bOA";

const auto narrowRow = eacp::Graphics::Rect {0.0f, 0.0f, iPhone17eWidth, widestKey};
const auto wideRow = eacp::Graphics::Rect {0.0f, 0.0f, iPhone18ProMaxWidth, widestKey};

std::string encodeKey(const eacp::Graphics::KeyEvent& key, bool applicationCursorKeys = false)
{
    if (const auto special = term::encodeSpecialKey(key, applicationCursorKeys))
    {
        if (const auto* bytes = std::get_if<std::string>(&*special))
            return *bytes;
    }

    return term::encodeTypedKey(key).value_or("");
}

std::string encodeKeyRowKey(KeyRowKey key, bool applicationCursorKeys = false)
{
    return encodeKey(*makeKeyRowKeyEvent(key), applicationCursorKeys);
}
} // namespace

auto tLatch = test("Typing/ctrlLatchesForOneKey") = []
{
    auto latch = ControlLatch {};
    latch.toggle();

    check(latch.isHeld());
    check(encodeKey(latch.applyTo(makeTypedKeyEvent("c"))) == ctrlC);
    check(!latch.isHeld());
    check(encodeKey(latch.applyTo(makeTypedKeyEvent("c"))) == "c");
};

auto tLatchToggle = test("Typing/ctrlTappedTwiceLetsGo") = []
{
    auto latch = ControlLatch {};
    latch.toggle();
    latch.toggle();

    check(!latch.isHeld());
};

auto tControlUpperCase = test("Typing/ctrlWithAnUpperCaseLetterIsTheSameCode") = []
{
    check(encodeKey(makeControlKey(makeTypedKeyEvent("C"))) == ctrlC);
};

auto tKeyRowEncodings = test("Typing/keyRowKeysSendWhatAKeyboardWould") = []
{
    check(encodeKeyRowKey(KeyRowKey::Escape) == escape);
    check(encodeKeyRowKey(KeyRowKey::Tab) == "\t");
    check(encodeKeyRowKey(KeyRowKey::Up) == upArrow);
    check(encodeKeyRowKey(KeyRowKey::Up, true) == upArrowInApplicationMode);
    check(encodeKeyRowKey(KeyRowKey::Pipe) == "|");
    check(encodeKeyRowKey(KeyRowKey::Tilde) == "~");
};

auto tRowControls = test("Typing/rowControlsSendNothing") = []
{
    check(!makeKeyRowKeyEvent(KeyRowKey::Control).has_value());
    check(!makeKeyRowKeyEvent(KeyRowKey::HideKeyboard).has_value());
    check(!makeKeyRowKeyEvent(KeyRowKey::NextPage).has_value());
};

auto tLayoutOrder = test("Typing/escSitsAtTheFarEndFromTheArrows") = []
{
    const auto slots = calculateKeyRowLayout(wideRow, 0);

    check(slots.front().key == KeyRowKey::Escape);
    check(slots[slots.size() - 2].key == KeyRowKey::Right);
    check(slots.back().key == KeyRowKey::NextPage);
};

auto tLayoutWidths = test("Typing/keysAre48PointsWideWhereTheyFitAndNarrowerWhereNot") = []
{
    for (const auto& slot: calculateKeyRowLayout(wideRow, 0))
        check(slot.bounds.w == widestKey);

    const auto narrow = calculateKeyRowLayout(narrowRow, 0);
    check(narrow.front().bounds.w < widestKey);
    check(narrow.front().bounds.x >= 0.0f);
    check(narrow.back().bounds.right() <= iPhone17eWidth + layoutTolerance);
};

auto tLayoutSecondPage = test("Typing/secondPageHoldsTheSymbols") = []
{
    const auto slots = calculateKeyRowLayout(narrowRow, 1);

    check(slots.front().key == KeyRowKey::Pipe);
    check(findKeyRowKeyAt(slots, slots[1].bounds.center()) == KeyRowKey::Tilde);
    check(slots[slots.size() - 2].key == KeyRowKey::HideKeyboard);
    check(slots.back().key == KeyRowKey::NextPage);
};

namespace
{
constexpr auto iPhone12MiniPortraitWidth = 375.0f;
constexpr auto iPhone12MiniLandscapeWidth = 812.0f;
constexpr auto iPhone18ProMaxLandscapeWidth = 956.0f;
} // namespace

auto tKeyRowFitsEveryPhone = test("Typing/everyKeyFitsTheRowOnEveryPhoneBothWays") = []
{
    for (const auto width: {iPhone12MiniPortraitWidth,
                            iPhone18ProMaxWidth,
                            iPhone12MiniLandscapeWidth,
                            iPhone18ProMaxLandscapeWidth})
    {
        const auto row = eacp::Graphics::Rect {0.0f, 0.0f, width, widestKey};

        for (auto page = 0; page < keyRowPageCount; ++page)
        {
            for (const auto& slot: calculateKeyRowLayout(row, page))
            {
                check(slot.bounds.x >= 0.0f);
                check(slot.bounds.right() <= width + layoutTolerance);
            }
        }
    }
};

auto tOnlyArrowsRepeat = test("Typing/onlyTheArrowsRepeatWhileHeld") = []
{
    check(isRepeatingKeyRowKey(KeyRowKey::Up));
    check(isRepeatingKeyRowKey(KeyRowKey::Left));
    check(!isRepeatingKeyRowKey(KeyRowKey::Escape), "a held Esc must not interrupt Claude over and over");
    check(!isRepeatingKeyRowKey(KeyRowKey::Control));
};
