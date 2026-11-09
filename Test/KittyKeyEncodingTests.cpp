#include <CowTermCore/KittyKeyboardFlags.h>
#include <CowTermTerminal/KeyEncoding.h>
#include <CowTermTerminal/KittyKeyEncoding.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <string>

using namespace nano;
using namespace term;
namespace KeyCode = eacp::Graphics::KeyCode;

namespace
{
constexpr auto disambiguate = KittyKeyboard::disambiguate;
constexpr auto withEvents = (std::uint8_t) (KittyKeyboard::disambiguate | KittyKeyboard::reportEvents);
constexpr auto everything = KittyKeyboard::allFlags;

eacp::Graphics::KeyEvent makeKey(std::uint16_t keyCode,
                                 std::string characters,
                                 std::string unmodified,
                                 eacp::Graphics::ModifierKeys mods = {})
{
    auto event = eacp::Graphics::KeyEvent {};
    event.keyCode = keyCode;
    event.characters = std::move(characters);
    event.charactersIgnoringModifiers = std::move(unmodified);
    event.modifiers = mods;
    return event;
}

eacp::Graphics::ModifierKeys control()
{
    auto mods = eacp::Graphics::ModifierKeys {};
    mods.control = true;
    return mods;
}

eacp::Graphics::ModifierKeys shift()
{
    auto mods = eacp::Graphics::ModifierKeys {};
    mods.shift = true;
    return mods;
}

eacp::Graphics::ModifierKeys alt()
{
    auto mods = eacp::Graphics::ModifierKeys {};
    mods.alt = true;
    return mods;
}

std::string pressed(const eacp::Graphics::KeyEvent& event, std::uint8_t flags = disambiguate)
{
    return encodeKittyKey(event, KeyAction::Press, flags);
}
} // namespace

auto tControlITellsItselfFromTab = test("KittyKeys/controlIIsNotTab") = []
{
    check(pressed(makeKey(KeyCode::I, "\t", "i", control())) == "\033[105;5u");
    check(pressed(makeKey(KeyCode::Tab, "\t", "\t")) == "\t");
    check(pressed(makeKey(KeyCode::Tab, "\t", "\t", shift())) == "\033[9;2u");
};

auto tEscapeIsDisambiguated = test("KittyKeys/escapeIsACsiSequence") = []
{
    check(pressed(makeKey(KeyCode::Escape, "\033", "\033")) == "\033[27u");
};

auto tPlainTextStaysText = test("KittyKeys/plainAndShiftedTextIsSentAsText") = []
{
    check(pressed(makeKey(KeyCode::A, "a", "a")) == "a");
    check(pressed(makeKey(KeyCode::A, "A", "A", shift())) == "A");
    check(pressed(makeKey(KeyCode::Num1, "!", "!", shift())) == "!");
};

auto tEnterAndBackspaceStayLegacy = test("KittyKeys/enterAndBackspaceStayLegacyBytes") = []
{
    check(pressed(makeKey(KeyCode::Return, "\r", "\r")) == "\r");
    check(pressed(makeKey(KeyCode::Delete, "\x7f", "\x7f")) == "\x7f");
    check(pressed(makeKey(KeyCode::Return, "\r", "\r"), everything) == "\033[13u");
};

auto tAltLetters = test("KittyKeys/altLetterIsAChord") = []
{
    check(pressed(makeKey(KeyCode::A, "\xc3\xa5", "a", alt())) == "\033[97;3u");
};

auto tArrowsAndFunctionKeys = test("KittyKeys/arrowsAndFunctionKeys") = []
{
    check(pressed(makeKey(KeyCode::UpArrow, "", "")) == "\033[A");
    check(pressed(makeKey(KeyCode::UpArrow, "", "", control())) == "\033[1;5A");
    check(pressed(makeKey(KeyCode::F5, "", "")) == "\033[15~");
    check(pressed(makeKey(KeyCode::ForwardDelete, "", "", shift())) == "\033[3;2~");
};

auto tReleasesNeedTheFlag = test("KittyKeys/releasesOnlyWithReportEvents") = []
{
    const auto key = makeKey(KeyCode::A, "a", "a");

    check(encodeKittyKey(key, KeyAction::Release, disambiguate).empty());
    check(encodeKittyKey(key, KeyAction::Release, withEvents) == "\033[97;1:3u");
    check(encodeKittyKey(key, KeyAction::Repeat, withEvents) == "a", "a held key types");

    const auto up = makeKey(KeyCode::UpArrow, "", "");
    check(encodeKittyKey(up, KeyAction::Repeat, withEvents) == "\033[1;1:2A");
};

auto tEnterReleaseNeedsReportAll = test("KittyKeys/enterReleaseNeedsReportAll") = []
{
    const auto enter = makeKey(KeyCode::Return, "\r", "\r");

    check(encodeKittyKey(enter, KeyAction::Release, withEvents).empty());
    check(encodeKittyKey(enter, KeyAction::Release, everything) == "\033[13;1:3u");
};

auto tReportAllEncodesText = test("KittyKeys/reportAllEncodesTextWithAssociatedText") = []
{
    check(pressed(makeKey(KeyCode::A, "a", "a"), everything) == "\033[97;;97u");
    check(pressed(makeKey(KeyCode::A, "A", "A", shift()), everything) == "\033[97:65;2;65u");
};

auto tModifierKeysNeedReportAll = test("KittyKeys/modifierKeysNeedReportAll") = []
{
    const auto shiftKey = makeKey(KeyCode::Shift, "", "", shift());

    check(pressed(shiftKey).empty());
    check(pressed(shiftKey, everything) == "\033[57441;2u");
};

namespace
{
constexpr auto disambiguateAndAlternates =
    (std::uint8_t) (KittyKeyboard::disambiguate | KittyKeyboard::reportAlternates);

// What macOS puts in a key event's characters for each non-text key.
struct MacFunctionKey
{
    std::uint16_t keyCode;
    std::string characters;
};

const auto macFunctionKeys = std::array {
    MacFunctionKey {KeyCode::UpArrow, "\xef\x9c\x80"},
    MacFunctionKey {KeyCode::DownArrow, "\xef\x9c\x81"},
    MacFunctionKey {KeyCode::LeftArrow, "\xef\x9c\x82"},
    MacFunctionKey {KeyCode::RightArrow, "\xef\x9c\x83"},
    MacFunctionKey {KeyCode::F1, "\xef\x9c\x84"},
    MacFunctionKey {KeyCode::F2, "\xef\x9c\x85"},
    MacFunctionKey {KeyCode::F3, "\xef\x9c\x86"},
    MacFunctionKey {KeyCode::F4, "\xef\x9c\x87"},
    MacFunctionKey {KeyCode::F5, "\xef\x9c\x88"},
    MacFunctionKey {KeyCode::F6, "\xef\x9c\x89"},
    MacFunctionKey {KeyCode::F7, "\xef\x9c\x8a"},
    MacFunctionKey {KeyCode::F8, "\xef\x9c\x8b"},
    MacFunctionKey {KeyCode::F9, "\xef\x9c\x8c"},
    MacFunctionKey {KeyCode::F10, "\xef\x9c\x8d"},
    MacFunctionKey {KeyCode::F11, "\xef\x9c\x8e"},
    MacFunctionKey {KeyCode::F12, "\xef\x9c\x8f"},
    MacFunctionKey {KeyCode::ForwardDelete, "\xef\x9c\xa8"},
    MacFunctionKey {KeyCode::Home, "\xef\x9c\xa9"},
    MacFunctionKey {KeyCode::End, "\xef\x9c\xab"},
    MacFunctionKey {KeyCode::PageUp, "\xef\x9c\xac"},
    MacFunctionKey {KeyCode::PageDown, "\xef\x9c\xad"},
    MacFunctionKey {KeyCode::Escape, "\x1b"},
    MacFunctionKey {KeyCode::Return, "\r"},
    MacFunctionKey {KeyCode::Tab, "\t"},
    MacFunctionKey {KeyCode::Delete, "\x7f"},
};

// Kitty's spec: the number and final byte each key is reported with.
struct KittyKeyExpectation
{
    std::uint16_t keyCode;
    std::string code;
    char final;
};

const auto kittySpecKeys = std::array {
    KittyKeyExpectation {KeyCode::UpArrow, "1", 'A'},
    KittyKeyExpectation {KeyCode::DownArrow, "1", 'B'},
    KittyKeyExpectation {KeyCode::LeftArrow, "1", 'D'},
    KittyKeyExpectation {KeyCode::RightArrow, "1", 'C'},
    KittyKeyExpectation {KeyCode::F1, "1", 'P'},
    KittyKeyExpectation {KeyCode::F2, "1", 'Q'},
    KittyKeyExpectation {KeyCode::F3, "13", '~'},
    KittyKeyExpectation {KeyCode::F4, "1", 'S'},
    KittyKeyExpectation {KeyCode::F5, "15", '~'},
    KittyKeyExpectation {KeyCode::F6, "17", '~'},
    KittyKeyExpectation {KeyCode::F7, "18", '~'},
    KittyKeyExpectation {KeyCode::F8, "19", '~'},
    KittyKeyExpectation {KeyCode::F9, "20", '~'},
    KittyKeyExpectation {KeyCode::F10, "21", '~'},
    KittyKeyExpectation {KeyCode::F11, "23", '~'},
    KittyKeyExpectation {KeyCode::F12, "24", '~'},
    KittyKeyExpectation {KeyCode::ForwardDelete, "3", '~'},
    KittyKeyExpectation {KeyCode::Home, "1", 'H'},
    KittyKeyExpectation {KeyCode::End, "1", 'F'},
    KittyKeyExpectation {KeyCode::PageUp, "5", '~'},
    KittyKeyExpectation {KeyCode::PageDown, "6", '~'},
    KittyKeyExpectation {KeyCode::Escape, "27", 'u'},
    KittyKeyExpectation {KeyCode::Return, "13", 'u'},
    KittyKeyExpectation {KeyCode::Tab, "9", 'u'},
    KittyKeyExpectation {KeyCode::Delete, "127", 'u'},
};

struct NamedModifiers
{
    eacp::Graphics::ModifierKeys keys;
    int kittyValue;
};

eacp::Graphics::ModifierKeys controlShift()
{
    auto mods = shift();
    mods.control = true;
    return mods;
}

const auto modifierCombinations = std::array {NamedModifiers {{}, 1},
                                              NamedModifiers {shift(), 2},
                                              NamedModifiers {alt(), 3},
                                              NamedModifiers {control(), 5},
                                              NamedModifiers {controlShift(), 6}};

eacp::Graphics::KeyEvent makeMacFunctionKey(const MacFunctionKey& key,
                                            eacp::Graphics::ModifierKeys mods)
{
    return makeKey(key.keyCode, key.characters, key.characters, mods);
}

std::string findCode(std::uint16_t keyCode)
{
    for (const auto& expectation: kittySpecKeys)
    {
        if (expectation.keyCode == keyCode)
            return expectation.code;
    }

    return {};
}

char findFinal(std::uint16_t keyCode)
{
    for (const auto& expectation: kittySpecKeys)
    {
        if (expectation.keyCode == keyCode)
            return expectation.final;
    }

    return 0;
}

bool isLegacyTextKey(std::uint16_t keyCode)
{
    return keyCode == KeyCode::Return || keyCode == KeyCode::Tab || keyCode == KeyCode::Delete;
}

// Kitty's spec for a press under the given flags: legacy bytes for an
// unmodified Enter/Tab/Backspace without report-all, CSI final for an
// unmodified letter-final key, else CSI code ; mods final.
std::string expectKittyPress(std::uint16_t keyCode, int kittyModifiers, std::uint8_t flags)
{
    const auto isUnmodified = kittyModifiers == 1;
    const auto reportsAll = (flags & KittyKeyboard::reportAllAsEscapes) != 0;
    const auto code = findCode(keyCode);
    const auto final = findFinal(keyCode);

    if (isUnmodified && !reportsAll && isLegacyTextKey(keyCode))
    {
        for (const auto& key: macFunctionKeys)
        {
            if (key.keyCode == keyCode)
                return key.characters;
        }
    }

    const auto isLetterFinal = final != 'u' && final != '~';

    if (isUnmodified && isLetterFinal)
        return std::string {"\033["} + final;

    if (isUnmodified)
        return "\033[" + code + final;

    return "\033[" + code + ";" + std::to_string(kittyModifiers) + final;
}

void checkEveryFunctionKeyUnder(std::uint8_t flags)
{
    for (const auto& key: macFunctionKeys)
    {
        for (const auto& mods: modifierCombinations)
        {
            const auto sent = pressed(makeMacFunctionKey(key, mods.keys), flags);
            check(sent == expectKittyPress(key.keyCode, mods.kittyValue, flags));
        }
    }
}
} // namespace

auto tFunctionKeysUnderDisambiguate =
    test("KittyKeys/macFunctionKeysUnderFlags1") = [] { checkEveryFunctionKeyUnder(disambiguate); };

auto tFunctionKeysUnderAlternates = test("KittyKeys/macFunctionKeysUnderFlags5") = []
{ checkEveryFunctionKeyUnder(disambiguateAndAlternates); };

auto tFunctionKeysUnderEverything =
    test("KittyKeys/macFunctionKeysUnderFlags31") = [] { checkEveryFunctionKeyUnder(everything); };

auto tFunctionKeyNeverSendsItsGlyph = test("KittyKeys/unknownMacFunctionKeySendsNothing") = []
{
    constexpr auto macHelpKeyCode = std::uint16_t {0x72};
    const auto insertKey = makeKey(macHelpKeyCode, "\xef\x9c\xa7", "\xef\x9c\xa7");
    check(pressed(insertKey, disambiguate).empty());
};

namespace
{
bool sendsAppleFunctionKeyGlyph(const std::optional<SpecialKeyResponse>& response)
{
    if (!response || !std::holds_alternative<std::string>(*response))
        return false;

    constexpr auto appleFunctionKeyUtf8Prefix = std::string_view {"\xef\x9c"};
    return std::get<std::string>(*response).find(appleFunctionKeyUtf8Prefix) != std::string::npos;
}
} // namespace

auto tFunctionKeysWithoutKittyFlags = test("KittyKeys/macFunctionKeysUnderFlags0") = []
{
    for (const auto& key: macFunctionKeys)
    {
        for (const auto& mods: modifierCombinations)
        {
            const auto response = encodeSpecialKey(makeMacFunctionKey(key, mods.keys), false);
            check(!sendsAppleFunctionKeyGlyph(response));
        }
    }

    const auto up = encodeSpecialKey(makeMacFunctionKey(macFunctionKeys[0], {}), false);
    check(up && std::get<std::string>(*up) == "\033[A");
};
