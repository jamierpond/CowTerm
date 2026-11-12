#include "KittyKeyEncoding.h"

#include <CowTermCore/KittyKeyboardFlags.h>
#include <CowTermCore/Utf8Decoder.h>

#include <algorithm>
#include <array>
#include <optional>

namespace term
{
using eacp::Graphics::KeyEvent;
using eacp::Graphics::ModifierKeys;
namespace KeyCode = eacp::Graphics::KeyCode;

namespace
{
constexpr auto deleteCode = char32_t {0x7f};
constexpr auto firstPrintableCode = char32_t {0x20};
constexpr auto firstAppleFunctionKeyCode = char32_t {0xF700};
constexpr auto lastAppleFunctionKeyCode = char32_t {0xF8FF};
constexpr auto shiftBit = 1;
constexpr auto altBit = 2;
constexpr auto controlBit = 4;
constexpr auto superBit = 8;
constexpr auto unmodifiedValue = 1;

struct KittyKeyEntry
{
    std::uint16_t keyCode;
    char32_t code;
    char final;
    bool isModifier = false;
};

// Ghostty's kitty.zig table, for the keys eacp names.
constexpr auto kittyKeyEntries = std::array {
    KittyKeyEntry {KeyCode::Escape, 27, 'u'},
    KittyKeyEntry {KeyCode::Return, 13, 'u'},
    KittyKeyEntry {KeyCode::Tab, 9, 'u'},
    KittyKeyEntry {KeyCode::Delete, 127, 'u'},
    KittyKeyEntry {KeyCode::ForwardDelete, 3, '~'},
    KittyKeyEntry {KeyCode::LeftArrow, 1, 'D'},
    KittyKeyEntry {KeyCode::RightArrow, 1, 'C'},
    KittyKeyEntry {KeyCode::UpArrow, 1, 'A'},
    KittyKeyEntry {KeyCode::DownArrow, 1, 'B'},
    KittyKeyEntry {KeyCode::PageUp, 5, '~'},
    KittyKeyEntry {KeyCode::PageDown, 6, '~'},
    KittyKeyEntry {KeyCode::Home, 1, 'H'},
    KittyKeyEntry {KeyCode::End, 1, 'F'},
    KittyKeyEntry {KeyCode::CapsLock, 57358, 'u', true},
    KittyKeyEntry {KeyCode::F1, 1, 'P'},
    KittyKeyEntry {KeyCode::F2, 1, 'Q'},
    KittyKeyEntry {KeyCode::F3, 13, '~'},
    KittyKeyEntry {KeyCode::F4, 1, 'S'},
    KittyKeyEntry {KeyCode::F5, 15, '~'},
    KittyKeyEntry {KeyCode::F6, 17, '~'},
    KittyKeyEntry {KeyCode::F7, 18, '~'},
    KittyKeyEntry {KeyCode::F8, 19, '~'},
    KittyKeyEntry {KeyCode::F9, 20, '~'},
    KittyKeyEntry {KeyCode::F10, 21, '~'},
    KittyKeyEntry {KeyCode::F11, 23, '~'},
    KittyKeyEntry {KeyCode::F12, 24, '~'},
    KittyKeyEntry {KeyCode::Keypad0, 57399, 'u'},
    KittyKeyEntry {KeyCode::Keypad1, 57400, 'u'},
    KittyKeyEntry {KeyCode::Keypad2, 57401, 'u'},
    KittyKeyEntry {KeyCode::Keypad3, 57402, 'u'},
    KittyKeyEntry {KeyCode::Keypad4, 57403, 'u'},
    KittyKeyEntry {KeyCode::Keypad5, 57404, 'u'},
    KittyKeyEntry {KeyCode::Keypad6, 57405, 'u'},
    KittyKeyEntry {KeyCode::Keypad7, 57406, 'u'},
    KittyKeyEntry {KeyCode::Keypad8, 57407, 'u'},
    KittyKeyEntry {KeyCode::Keypad9, 57408, 'u'},
    KittyKeyEntry {KeyCode::KeypadDecimal, 57409, 'u'},
    KittyKeyEntry {KeyCode::KeypadDivide, 57410, 'u'},
    KittyKeyEntry {KeyCode::KeypadMultiply, 57411, 'u'},
    KittyKeyEntry {KeyCode::KeypadMinus, 57412, 'u'},
    KittyKeyEntry {KeyCode::KeypadPlus, 57413, 'u'},
    KittyKeyEntry {KeyCode::KeypadEnter, 57414, 'u'},
    KittyKeyEntry {KeyCode::KeypadEquals, 57415, 'u'},
    KittyKeyEntry {KeyCode::Shift, 57441, 'u', true},
    KittyKeyEntry {KeyCode::RightShift, 57447, 'u', true},
    KittyKeyEntry {KeyCode::Control, 57442, 'u', true},
    KittyKeyEntry {KeyCode::RightControl, 57448, 'u', true},
    KittyKeyEntry {KeyCode::Command, 57444, 'u', true},
    KittyKeyEntry {KeyCode::RightCommand, 57450, 'u', true},
    KittyKeyEntry {KeyCode::Option, 57443, 'u', true},
    KittyKeyEntry {KeyCode::RightOption, 57449, 'u', true},
};

struct BaseLayoutKey
{
    std::uint16_t keyCode;
    char32_t codepoint;
};

// What the US layout's digit and punctuation keys type without Shift: the
// key's code when Shift turned the text into something else ('!' for 1).
constexpr auto baseLayoutKeys = std::array {
    BaseLayoutKey {KeyCode::Num0, '0'},         BaseLayoutKey {KeyCode::Num1, '1'},
    BaseLayoutKey {KeyCode::Num2, '2'},         BaseLayoutKey {KeyCode::Num3, '3'},
    BaseLayoutKey {KeyCode::Num4, '4'},         BaseLayoutKey {KeyCode::Num5, '5'},
    BaseLayoutKey {KeyCode::Num6, '6'},         BaseLayoutKey {KeyCode::Num7, '7'},
    BaseLayoutKey {KeyCode::Num8, '8'},         BaseLayoutKey {KeyCode::Num9, '9'},
    BaseLayoutKey {KeyCode::Minus, '-'},        BaseLayoutKey {KeyCode::Equals, '='},
    BaseLayoutKey {KeyCode::LeftBracket, '['},  BaseLayoutKey {KeyCode::RightBracket, ']'},
    BaseLayoutKey {KeyCode::Backslash, '\\'},   BaseLayoutKey {KeyCode::Semicolon, ';'},
    BaseLayoutKey {KeyCode::Quote, '\''},       BaseLayoutKey {KeyCode::Comma, ','},
    BaseLayoutKey {KeyCode::Period, '.'},       BaseLayoutKey {KeyCode::Slash, '/'},
    BaseLayoutKey {KeyCode::Grave, '`'},        BaseLayoutKey {KeyCode::Space, ' '},
};

struct LegacyKey
{
    std::uint16_t keyCode;
    std::string_view bytes;
};

constexpr auto legacyKeys = std::array {LegacyKey {KeyCode::Return, "\r"},
                                        LegacyKey {KeyCode::Tab, "\t"},
                                        LegacyKey {KeyCode::Delete, "\x7f"}};

template <typename Entry, std::size_t size>
std::optional<Entry> findByKeyCode(const std::array<Entry, size>& table, std::uint16_t keyCode)
{
    const auto found = std::find_if(table.begin(),
                                    table.end(),
                                    [keyCode](const Entry& entry) { return entry.keyCode == keyCode; });

    if (found == table.end())
        return std::nullopt;

    return *found;
}

std::u32string decodeUtf8(std::string_view text)
{
    auto decoder = Utf8Decoder {};
    auto codepoints = std::u32string {};

    for (const auto byte: text)
        decoder.decode((std::uint8_t) byte, [&](char32_t codepoint) { codepoints += codepoint; });

    return codepoints;
}

// macOS gives arrows, F-keys, Home/End and paging Private Use Area characters
// (U+F700 up); they are keys, not text.
bool isAppleFunctionKeyCode(char32_t codepoint)
{
    return codepoint >= firstAppleFunctionKeyCode && codepoint <= lastAppleFunctionKeyCode;
}

bool isNonTextCode(char32_t codepoint)
{
    return codepoint < firstPrintableCode || codepoint == deleteCode
           || isAppleFunctionKeyCode(codepoint);
}

bool hasNonTextCode(const std::u32string& codepoints)
{
    return std::any_of(codepoints.begin(), codepoints.end(), isNonTextCode);
}

char32_t toLowerAscii(char32_t codepoint)
{
    if (codepoint >= 'A' && codepoint <= 'Z')
        return codepoint - 'A' + 'a';

    return codepoint;
}

// The codepoint the key types with no modifiers: the base layout's when Shift
// changed a digit or punctuation key, else the unmodified text lower-cased.
char32_t findUnshiftedCodepoint(const KeyEvent& event)
{
    if (event.modifiers.shift)
    {
        if (const auto base = findByKeyCode(baseLayoutKeys, event.keyCode))
            return base->codepoint;
    }

    const auto unmodified = decodeUtf8(event.charactersIgnoringModifiers);

    if (unmodified.empty() || isNonTextCode(unmodified.front()))
        return 0;

    return toLowerAscii(unmodified.front());
}

int calculateModifierBits(const ModifierKeys& mods)
{
    return (mods.shift ? shiftBit : 0) + (mods.alt ? altBit : 0)
           + (mods.control ? controlBit : 0) + (mods.command ? superBit : 0);
}

bool hasFlag(std::uint8_t flags, std::uint8_t flag)
{
    return (flags & flag) != 0;
}

struct KittyKeyPress
{
    const KeyEvent& event;
    KeyAction action;
    std::uint8_t flags;
    std::u32string text;
    char32_t unshifted;
};

// Shift is spent when it is what made the text ('A' from a): such a press is
// plain typing, not a chord.
bool isShiftSpentOnText(const KittyKeyPress& press)
{
    return press.event.modifiers.shift && press.text.size() == 1
           && !isNonTextCode(press.text.front()) && press.text.front() != press.unshifted;
}

bool isUnchorded(const KittyKeyPress& press)
{
    const auto& mods = press.event.modifiers;
    const auto shiftCounts = mods.shift && !isShiftSpentOnText(press);
    return !mods.alt && !mods.control && !mods.command && !shiftCounts;
}

bool isPlainText(const KittyKeyPress& press)
{
    return !press.text.empty() && !hasNonTextCode(press.text)
           && press.action != KeyAction::Release;
}

// Without report-all, an unchorded Enter, Tab or Backspace and plain typing
// keep their legacy bytes, so a shell still works after a program that set
// the flags crashes.
std::optional<std::string> encodeAsLegacyBytes(const KittyKeyPress& press)
{
    if (hasFlag(press.flags, KittyKeyboard::reportAllAsEscapes) || !isUnchorded(press))
        return std::nullopt;

    if (const auto key = findByKeyCode(legacyKeys, press.event.keyCode))
        return std::string {key->bytes};

    if (isPlainText(press))
        return press.event.characters;

    return std::nullopt;
}

bool isReleaseWithheld(const KittyKeyPress& press)
{
    if (press.action != KeyAction::Release)
        return false;

    if (!hasFlag(press.flags, KittyKeyboard::reportEvents))
        return true;

    const auto isLegacyKey = findByKeyCode(legacyKeys, press.event.keyCode).has_value();
    return !hasFlag(press.flags, KittyKeyboard::reportAllAsEscapes) && isLegacyKey;
}

std::optional<KittyKeyEntry> findKittyEntry(const KittyKeyPress& press)
{
    if (const auto entry = findByKeyCode(kittyKeyEntries, press.event.keyCode))
        return entry;

    if (press.unshifted == 0)
        return std::nullopt;

    return KittyKeyEntry {press.event.keyCode, press.unshifted, 'u'};
}

int findEventNumber(KeyAction action)
{
    constexpr auto repeatEvent = 2;
    constexpr auto releaseEvent = 3;

    if (action == KeyAction::Repeat)
        return repeatEvent;

    if (action == KeyAction::Release)
        return releaseEvent;

    return 0;
}

struct KittySequence
{
    char32_t code = 0;
    char final = 'u';
    int modifierValue = unmodifiedValue;
    int eventNumber = 0;
    char32_t shiftedCode = 0;
    std::u32string associatedText;
};

std::string formatModifiersAndEvent(const KittySequence& sequence)
{
    if (sequence.eventNumber != 0)
        return std::to_string(sequence.modifierValue) + ":" + std::to_string(sequence.eventNumber);

    if (sequence.modifierValue > unmodifiedValue)
        return std::to_string(sequence.modifierValue);

    return {};
}

std::string formatAssociatedText(const std::u32string& text)
{
    auto formatted = std::string {};

    for (const auto codepoint: text)
    {
        if (isNonTextCode(codepoint))
            continue;

        if (!formatted.empty())
            formatted += ':';

        formatted += std::to_string((std::uint32_t) codepoint);
    }

    return formatted;
}

// CSI code[:shifted] ; mods[:event] [; text] u (or ~).
std::string formatNumberedSequence(const KittySequence& sequence)
{
    auto bytes = "\033[" + std::to_string((std::uint32_t) sequence.code);

    if (sequence.shiftedCode != 0)
        bytes += ":" + std::to_string((std::uint32_t) sequence.shiftedCode);

    const auto modifiers = formatModifiersAndEvent(sequence);
    const auto text = formatAssociatedText(sequence.associatedText);

    if (!modifiers.empty() || !text.empty())
        bytes += ";" + modifiers;

    if (!text.empty())
        bytes += ";" + text;

    return bytes + sequence.final;
}

// Arrows, Home/End and F1/F2/F4: CSI 1 ; mods[:event] final, or CSI final.
std::string formatLetterSequence(const KittySequence& sequence)
{
    const auto modifiers = formatModifiersAndEvent(sequence);

    if (modifiers.empty())
        return std::string {"\033["} + sequence.final;

    return "\033[1;" + modifiers + sequence.final;
}

std::string formatSequence(const KittySequence& sequence)
{
    if (sequence.final == 'u' || sequence.final == '~')
        return formatNumberedSequence(sequence);

    return formatLetterSequence(sequence);
}

bool doModifiersPreventText(const ModifierKeys& mods)
{
    return mods.alt || mods.control || mods.command;
}

bool canReportShiftedKey(const KittyKeyPress& press, char32_t code)
{
    if (!hasFlag(press.flags, KittyKeyboard::reportAlternates) || isNonTextCode(code))
        return false;

    return press.event.modifiers.shift && !press.text.empty() && press.text.front() != code;
}

bool canReportAssociatedText(const KittyKeyPress& press)
{
    return hasFlag(press.flags, KittyKeyboard::reportAssociatedText)
           && press.action != KeyAction::Release
           && !doModifiersPreventText(press.event.modifiers);
}

KittySequence buildSequence(const KittyKeyPress& press, const KittyKeyEntry& entry)
{
    auto sequence = KittySequence {};
    sequence.code = entry.code;
    sequence.final = entry.final;
    sequence.modifierValue = unmodifiedValue + calculateModifierBits(press.event.modifiers);

    if (hasFlag(press.flags, KittyKeyboard::reportEvents))
        sequence.eventNumber = findEventNumber(press.action);

    if (canReportShiftedKey(press, sequence.code))
        sequence.shiftedCode = press.text.front();

    if (canReportAssociatedText(press))
        sequence.associatedText = press.text;

    return sequence;
}
} // namespace

std::string encodeKittyKey(const KeyEvent& event, KeyAction action, std::uint8_t flags)
{
    const auto press = KittyKeyPress {
        event, action, flags, decodeUtf8(event.characters), findUnshiftedCodepoint(event)};

    if (isReleaseWithheld(press))
        return {};

    if (auto legacy = encodeAsLegacyBytes(press))
        return std::move(*legacy);

    const auto entry = findKittyEntry(press);

    if (!entry)
        return isPlainText(press) ? event.characters : std::string {};

    if (entry->isModifier && !hasFlag(flags, KittyKeyboard::reportAllAsEscapes))
        return {};

    return formatSequence(buildSequence(press, *entry));
}
} // namespace term
