#pragma once

#include <eacp/Graphics/Graphics/Keyboard.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

// What the terminal sends its shell for a key, a paste or a mouse event: pure
// translations from an event to bytes, with no view or screen behind them.
namespace term
{
// Keys that move the viewport through the scrollback instead of reaching the
// shell: Shift with Home, End, Page Up or Page Down.
enum class ViewportJump
{
    ToOldest,
    ToNewest,
    PageTowardOldest,
    PageTowardNewest
};

// Bytes for the shell, or a viewport move that sends nothing.
using SpecialKeyResponse = std::variant<std::string, ViewportJump>;

// Return, Tab, Escape, Backspace, arrows, Home/End, paging, Delete and F1-F12.
// Empty for any other key, which then goes through encodeTypedKey.
std::optional<SpecialKeyResponse>
    encodeSpecialKey(const eacp::Graphics::KeyEvent& event, bool applicationCursorKeys);

// Ctrl+Space as NUL, Alt as an Escape prefix, else the typed characters.
std::optional<std::string> encodeTypedKey(const eacp::Graphics::KeyEvent& event);

enum class EditingShortcut
{
    Copy,
    Paste,
    ClearScreen,

    // Consumed so the shell never sees it, with no action of its own.
    Swallow
};

// Cmd+C, Cmd+V and Cmd+K; any other Cmd chord is swallowed.
std::optional<EditingShortcut>
    findCommandShortcut(const eacp::Graphics::KeyEvent& event);

// Ctrl+Shift+C and Ctrl+Shift+V, the copy and paste chords off macOS.
std::optional<EditingShortcut>
    findControlShiftClipboardShortcut(const eacp::Graphics::KeyEvent& event);

// CRLF folds to CR and LF becomes CR, as Enter would send; bracketed paste
// wraps the result in its start and end markers.
std::string preparePastedText(std::string_view clipboardText, bool bracketedPaste);

struct SgrMouseReport
{
    int button = 0;
    bool pressed = true;
    bool motion = false;

    // 1-based, as the protocol counts them.
    int column = 1;
    int row = 1;
};

std::string encodeSgrMouseReport(const SgrMouseReport& report);

inline constexpr int wheelUpMouseButton = 64;
inline constexpr int wheelDownMouseButton = 65;

// The arrow key a full-screen app gets for each wheel line when it has not
// asked for mouse reports, and how many of them one wheel event may send.
struct WheelArrowKeys
{
    std::string_view key;
    int count = 0;
};

WheelArrowKeys encodeWheelAsArrowKeys(int lines, bool applicationCursorKeys);
} // namespace term
