#pragma once

#include <array>
#include <cstdint>

namespace term
{
// The kitty keyboard protocol's progressive-enhancement bits, as a program
// sends them in CSI > flags u.
namespace KittyKeyboard
{
constexpr std::uint8_t disambiguate = 1 << 0;
constexpr std::uint8_t reportEvents = 1 << 1;
constexpr std::uint8_t reportAlternates = 1 << 2;
constexpr std::uint8_t reportAllAsEscapes = 1 << 3;
constexpr std::uint8_t reportAssociatedText = 1 << 4;
constexpr std::uint8_t allFlags = (1 << 5) - 1;
} // namespace KittyKeyboard

// CSI = flags ; how u: 1 replaces the flags, 2 adds them, 3 removes them.
enum class KittyFlagsChange
{
    Replace,
    Add,
    Remove
};

// The flags stack one screen keeps (the primary and alternate screens each
// have their own). Eight entries, wrapping like Ghostty's: a push past the
// top overwrites the oldest.
class KittyKeyboardStack
{
public:
    std::uint8_t current() const { return entries[top]; }

    void push(std::uint8_t flags);
    void pop(int count);
    void change(std::uint8_t flags, KittyFlagsChange how);
    void clear();

    static constexpr std::size_t depth = 8;
    using Entries = std::array<std::uint8_t, depth>;

    const Entries& getEntries() const { return entries; }
    std::size_t getTop() const { return top; }

    // Takes a saved stack back; flags outside the protocol and a top past
    // the end are dropped.
    void restore(const Entries& savedEntries, std::size_t savedTop);

private:
    Entries entries {};
    std::size_t top = 0;
};
} // namespace term
