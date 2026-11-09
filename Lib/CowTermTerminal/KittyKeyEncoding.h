#pragma once

#include <eacp/Graphics/Graphics/Keyboard.h>

#include <cstdint>
#include <string>

namespace term
{
enum class KeyAction
{
    Press,
    Repeat,
    Release
};

// A key event as the kitty keyboard protocol encodes it under the program's
// flags (KittyKeyboard::*), after Ghostty's key encoder: CSI code ; mods u for
// keys the legacy encoding can't tell apart (Ctrl+I from Tab, Esc from Alt
// prefixes), releases and repeats when asked for, and plain text otherwise.
// Empty when the key sends nothing under these flags.
std::string encodeKittyKey(const eacp::Graphics::KeyEvent& event,
                           KeyAction action,
                           std::uint8_t flags);
} // namespace term
