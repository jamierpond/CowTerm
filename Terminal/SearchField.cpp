#include "SearchField.h"

#include "TextLayout.h"

#include <array>

namespace term
{
using namespace eacp;
namespace KeyCode = UI::KeyCode;

namespace
{
struct KeyCodeSpecialKey
{
    std::uint16_t keyCode;
    searchkeys::SpecialKey special;
};

constexpr auto specialKeysByCode = std::array {
    KeyCodeSpecialKey {KeyCode::Escape, searchkeys::SpecialKey::escape},
    KeyCodeSpecialKey {KeyCode::Return, searchkeys::SpecialKey::enter},
    KeyCodeSpecialKey {KeyCode::UpArrow, searchkeys::SpecialKey::upArrow},
    KeyCodeSpecialKey {KeyCode::DownArrow, searchkeys::SpecialKey::downArrow},
    KeyCodeSpecialKey {KeyCode::LeftArrow, searchkeys::SpecialKey::leftArrow},
    KeyCodeSpecialKey {KeyCode::RightArrow, searchkeys::SpecialKey::rightArrow},
    KeyCodeSpecialKey {KeyCode::Delete, searchkeys::SpecialKey::backspace},
    KeyCodeSpecialKey {KeyCode::ForwardDelete, searchkeys::SpecialKey::forwardDelete},
    KeyCodeSpecialKey {KeyCode::Home, searchkeys::SpecialKey::home},
    KeyCodeSpecialKey {KeyCode::End, searchkeys::SpecialKey::end},
};

searchkeys::SpecialKey getSpecialKey(const UI::KeyEvent& event)
{
    for (auto& entry: specialKeysByCode)
        if (entry.keyCode == event.keyCode)
            return entry.special;

    return searchkeys::SpecialKey::none;
}

std::string getTypedCharacters(const UI::KeyEvent& event)
{
    if (getSpecialKey(event) != searchkeys::SpecialKey::none
        || !isPrintableTypedText(event.characters))
        return {};

    return event.characters;
}
} // namespace

searchkeys::Key makeSearchKey(const UI::KeyEvent& event)
{
    auto key = searchkeys::Key {};
    key.special = getSpecialKey(event);
    key.characters = getTypedCharacters(event);
    key.command = event.modifiers.command;
    key.control = event.modifiers.control;
    key.alt = event.modifiers.alt;
    return key;
}

void SearchField::reset()
{
    box = {};
}

searchkeys::Action SearchField::handleKey(const UI::KeyEvent& event)
{
    return box.handleKey(makeSearchKey(event));
}
} // namespace term
