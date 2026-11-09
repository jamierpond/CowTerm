#include "KeyEncoding.h"

#include "MacKeyCodes.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace term
{
using eacp::Graphics::KeyEvent;
using eacp::Graphics::ModifierKeys;
namespace KeyCode = eacp::Graphics::KeyCode;

namespace
{
constexpr int unmodifiedCode = 1;
constexpr int shiftModifierBit = 1;
constexpr int altModifierBit = 2;
constexpr int controlModifierBit = 4;
constexpr int sgrMotionFlag = 32;
constexpr int maximumWheelArrowKeys = 40;
constexpr auto bracketedPasteStart = std::string_view {"\033[200~"};
constexpr auto bracketedPasteEnd = std::string_view {"\033[201~"};

// xterm's modifier parameter: 1 plus a bit for each of Shift, Alt and Ctrl.
int calculateModifierCode(const ModifierKeys& mods)
{
    return unmodifiedCode + (mods.shift ? shiftModifierBit : 0)
           + (mods.alt ? altModifierBit : 0) + (mods.control ? controlModifierBit : 0);
}

std::string csiWithModifiers(char final, const ModifierKeys& mods)
{
    const auto code = calculateModifierCode(mods);

    if (code == unmodifiedCode)
        return std::string {"\033["} + final;

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "\033[1;%d%c", code, final);
    return buffer;
}

std::string tildeKey(int number, const ModifierKeys& mods)
{
    char buffer[16];
    const auto code = calculateModifierCode(mods);

    if (code == unmodifiedCode)
        std::snprintf(buffer, sizeof(buffer), "\033[%d~", number);
    else
        std::snprintf(buffer, sizeof(buffer), "\033[%d;%d~", number, code);

    return buffer;
}

struct FixedKey
{
    std::uint16_t keyCode;
    std::string_view bytes;
};

constexpr auto fixedKeys = std::array {FixedKey {KeyCode::Return, "\r"},
                                       FixedKey {MacKey::KeypadEnter, "\r"},
                                       FixedKey {KeyCode::Escape, "\033"},
                                       FixedKey {KeyCode::F1, "\033OP"},
                                       FixedKey {KeyCode::F2, "\033OQ"},
                                       FixedKey {KeyCode::F3, "\033OR"},
                                       FixedKey {KeyCode::F4, "\033OS"}};

struct TildeKey
{
    std::uint16_t keyCode;
    int number;
};

constexpr auto tildeKeys = std::array {TildeKey {MacKey::ForwardDelete, 3},
                                       TildeKey {MacKey::PageUp, 5},
                                       TildeKey {MacKey::PageDown, 6},
                                       TildeKey {KeyCode::F5, 15},
                                       TildeKey {KeyCode::F6, 17},
                                       TildeKey {KeyCode::F7, 18},
                                       TildeKey {KeyCode::F8, 19},
                                       TildeKey {KeyCode::F9, 20},
                                       TildeKey {KeyCode::F10, 21},
                                       TildeKey {KeyCode::F11, 23},
                                       TildeKey {KeyCode::F12, 24}};

struct FinalByteKey
{
    std::uint16_t keyCode;
    char final;
};

constexpr auto arrowKeys = std::array {FinalByteKey {KeyCode::UpArrow, 'A'},
                                       FinalByteKey {KeyCode::DownArrow, 'B'},
                                       FinalByteKey {KeyCode::RightArrow, 'C'},
                                       FinalByteKey {KeyCode::LeftArrow, 'D'}};

constexpr auto homeEndKeys = std::array {FinalByteKey {MacKey::Home, 'H'},
                                         FinalByteKey {MacKey::End, 'F'}};

struct ScrollKey
{
    std::uint16_t keyCode;
    ViewportJump jump;
};

constexpr auto shiftScrollKeys =
    std::array {ScrollKey {MacKey::Home, ViewportJump::ToOldest},
                ScrollKey {MacKey::End, ViewportJump::ToNewest},
                ScrollKey {MacKey::PageUp, ViewportJump::PageTowardOldest},
                ScrollKey {MacKey::PageDown, ViewportJump::PageTowardNewest}};

template <typename Entry, std::size_t size>
std::optional<Entry> findKeyEntry(const std::array<Entry, size>& table,
                                  std::uint16_t keyCode)
{
    for (const auto& entry: table)
    {
        if (entry.keyCode == keyCode)
            return entry;
    }

    return std::nullopt;
}

std::string ss3Key(char final)
{
    return std::string {"\033O"} + final;
}

using KeyEncoder = std::optional<std::string> (*)(const KeyEvent&, bool);

std::optional<std::string> encodeFixedKey(const KeyEvent& event, bool)
{
    if (const auto key = findKeyEntry(fixedKeys, event.keyCode))
        return std::string {key->bytes};

    return std::nullopt;
}

std::optional<std::string> encodeTabKey(const KeyEvent& event, bool)
{
    if (event.keyCode != KeyCode::Tab)
        return std::nullopt;

    return event.modifiers.shift ? "\033[Z" : "\t";
}

std::optional<std::string> encodeBackspaceKey(const KeyEvent& event, bool)
{
    if (event.keyCode != KeyCode::Delete)
        return std::nullopt;

    return event.modifiers.alt ? "\033\x7f" : "\x7f";
}

std::optional<std::string> encodeTildeKey(const KeyEvent& event, bool)
{
    if (const auto key = findKeyEntry(tildeKeys, event.keyCode))
        return tildeKey(key->number, event.modifiers);

    return std::nullopt;
}

std::optional<std::string> encodeArrowKey(const KeyEvent& event,
                                          bool applicationCursor)
{
    const auto key = findKeyEntry(arrowKeys, event.keyCode);

    if (!key)
        return std::nullopt;

    if (applicationCursor)
        return ss3Key(key->final);

    return csiWithModifiers(key->final, event.modifiers);
}

std::optional<std::string> encodeHomeEndKey(const KeyEvent& event,
                                            bool applicationCursor)
{
    const auto key = findKeyEntry(homeEndKeys, event.keyCode);

    if (!key)
        return std::nullopt;

    if (applicationCursor)
        return ss3Key(key->final);

    return std::string {"\033["} + key->final;
}

constexpr auto keyEncoders = std::array<KeyEncoder, 6> {encodeFixedKey,
                                                        encodeTabKey,
                                                        encodeBackspaceKey,
                                                        encodeTildeKey,
                                                        encodeArrowKey,
                                                        encodeHomeEndKey};

bool hasControlAndShift(const ModifierKeys& mods)
{
    return mods.control && mods.shift;
}

bool hasCommandOrAlt(const ModifierKeys& mods)
{
    return mods.command || mods.alt;
}

std::string toLowerCase(std::string text)
{
    for (auto& character: text)
        character = (char) std::tolower((unsigned char) character);

    return text;
}

std::optional<EditingShortcut> findClipboardShortcutForLetter(const std::string& letter)
{
    if (letter == "c")
        return EditingShortcut::Copy;

    if (letter == "v")
        return EditingShortcut::Paste;

    return std::nullopt;
}

// Ghostty's strip list: bytes the terminal's line discipline or a shell
// reads as commands, not text.
constexpr auto pasteStrippedBytes = std::string_view {
    "\x00\x08\x05\x04\x1b\x7f\x03\x1c\x15\x1a\x11\x13\x17\x16\x12\x0f", 16};

char replaceStrippedPasteByte(char byte)
{
    if (pasteStrippedBytes.find(byte) != std::string_view::npos)
        return ' ';

    if (byte == '\n')
        return '\r';

    return byte;
}

bool isLineFeedAt(std::string_view text, std::size_t index)
{
    return index < text.size() && text[index] == '\n';
}
} // namespace

std::optional<ViewportJump> findViewportJump(const KeyEvent& event)
{
    if (!event.modifiers.shift)
        return std::nullopt;

    if (const auto key = findKeyEntry(shiftScrollKeys, event.keyCode))
        return key->jump;

    return std::nullopt;
}

std::optional<SpecialKeyResponse> encodeSpecialKey(const KeyEvent& event,
                                                   bool applicationCursorKeys)
{
    if (const auto jump = findViewportJump(event))
        return *jump;

    const auto applicationCursor =
        applicationCursorKeys && calculateModifierCode(event.modifiers) == unmodifiedCode;

    for (const auto encoder: keyEncoders)
    {
        if (auto bytes = encoder(event, applicationCursor))
            return std::move(*bytes);
    }

    return std::nullopt;
}

std::optional<std::string> encodeTypedKey(const KeyEvent& event)
{
    const auto& mods = event.modifiers;
    const auto& unmodified = event.charactersIgnoringModifiers;

    if (mods.control && unmodified == " ")
        return std::string {"\0", 1};

    if (mods.alt && !unmodified.empty())
        return "\033" + unmodified;

    if (!event.characters.empty())
        return event.characters;

    return std::nullopt;
}

std::optional<EditingShortcut> findCommandShortcut(const KeyEvent& event)
{
    if (!event.modifiers.command)
        return std::nullopt;

    const auto& chars = event.charactersIgnoringModifiers;

    if (chars == "k")
        return EditingShortcut::ClearScreen;

    if (const auto shortcut = findClipboardShortcutForLetter(chars))
        return shortcut;

    return EditingShortcut::Swallow;
}

std::optional<EditingShortcut> findControlShiftClipboardShortcut(const KeyEvent& event)
{
    if (!hasControlAndShift(event.modifiers) || hasCommandOrAlt(event.modifiers))
        return std::nullopt;

    // Ctrl turns `characters` into a control code, so the unmodified spelling is
    // the one to read; Shift leaves it upper case.
    return findClipboardShortcutForLetter(toLowerCase(event.charactersIgnoringModifiers));
}

std::string preparePastedText(std::string_view clipboardText, bool bracketedPaste)
{
    auto normalized = std::string {};
    normalized.reserve(clipboardText.size());

    for (std::size_t i = 0; i < clipboardText.size(); ++i)
    {
        if (clipboardText[i] == '\r' && isLineFeedAt(clipboardText, i + 1))
            continue;

        normalized.push_back(replaceStrippedPasteByte(clipboardText[i]));
    }

    if (bracketedPaste)
        return std::string {bracketedPasteStart} + normalized
               + std::string {bracketedPasteEnd};

    return normalized;
}

bool isPasteSafe(std::string_view clipboardText, bool bracketedPaste)
{
    if (clipboardText.find(bracketedPasteEnd) != std::string_view::npos)
        return false;

    return bracketedPaste || clipboardText.find_first_of("\r\n") == std::string_view::npos;
}

std::string encodeSgrMouseReport(const SgrMouseReport& report)
{
    char buffer[32];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "\033[<%d;%d;%d%c",
                  report.button + (report.motion ? sgrMotionFlag : 0),
                  report.column,
                  report.row,
                  report.pressed ? 'M' : 'm');
    return buffer;
}

WheelArrowKeys encodeWheelAsArrowKeys(int lines, bool applicationCursorKeys)
{
    const auto count = std::min(std::abs(lines), maximumWheelArrowKeys);

    if (lines > 0)
        return {applicationCursorKeys ? "\033OA" : "\033[A", count};

    return {applicationCursorKeys ? "\033OB" : "\033[B", count};
}
} // namespace term
