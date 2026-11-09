#include <CowTermTerminal/KeyEncoding.h>
#include <CowTermTerminal/KittyKeyEncoding.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Reads a key event from the fuzzer's bytes (key code, modifiers, action,
// protocol flags, then the typed and unmodified text, split at the first NUL)
// and encodes it under the kitty keyboard protocol; the same text goes
// through the paste encoder, which must never call a paste with a line
// break safe outside bracketed mode.
namespace
{
constexpr auto headerSize = std::size_t {5};
constexpr auto actionCount = 3;
constexpr auto bracketedPasteBit = 16;

std::uint16_t readKeyCode(const std::uint8_t* data)
{
    const auto high = (std::uint16_t) (data[0] << 8);
    return (std::uint16_t) (high | data[1]);
}

eacp::Graphics::ModifierKeys readModifiers(std::uint8_t bits)
{
    auto mods = eacp::Graphics::ModifierKeys {};
    mods.shift = (bits & 1) != 0;
    mods.control = (bits & 2) != 0;
    mods.alt = (bits & 4) != 0;
    mods.command = (bits & 8) != 0;
    return mods;
}

term::KeyAction readAction(std::uint8_t value)
{
    switch (value % actionCount)
    {
        case 0:
            return term::KeyAction::Press;
        case 1:
            return term::KeyAction::Repeat;
        default:
            return term::KeyAction::Release;
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size < headerSize)
        return 0;

    const auto text = std::string {reinterpret_cast<const char*>(data) + headerSize, size - headerSize};
    const auto split = text.find('\0');

    auto event = eacp::Graphics::KeyEvent {};
    event.keyCode = readKeyCode(data);
    event.modifiers = readModifiers(data[2]);
    event.characters = text.substr(0, split);
    event.charactersIgnoringModifiers = split == std::string::npos ? text : text.substr(split + 1);

    term::encodeKittyKey(event, readAction(data[3]), data[4]);

    const auto bracketed = (data[2] & bracketedPasteBit) != 0;
    const auto pastesALineBreak = term::preparePastedText(text, false).find('\r') != std::string::npos;

    if (term::isPasteSafe(text, false) && pastesALineBreak)
        __builtin_trap();

    term::preparePastedText(text, bracketed);
    return 0;
}
