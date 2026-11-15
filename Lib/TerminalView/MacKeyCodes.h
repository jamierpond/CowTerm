#pragma once

#include <cstdint>

// Virtual key codes eacp's KeyCode table does not name, as macOS reports them.
namespace term::MacKey
{
constexpr std::uint16_t Home = 0x73;
constexpr std::uint16_t End = 0x77;
constexpr std::uint16_t PageUp = 0x74;
constexpr std::uint16_t PageDown = 0x79;
constexpr std::uint16_t ForwardDelete = 0x75;
constexpr std::uint16_t KeypadEnter = 0x4c;
} // namespace term::MacKey
