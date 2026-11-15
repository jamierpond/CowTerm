#include "SearchField.h"

#include "TextLayout.h"

namespace term
{
using namespace eacp;
namespace KeyCode = UI::KeyCode;

namespace
{
searchkeys::SpecialKey getSpecialKey(const UI::KeyEvent& event)
{
    using searchkeys::SpecialKey;

    if (event.keyCode == KeyCode::Escape)
        return SpecialKey::escape;

    if (event.keyCode == KeyCode::Return)
        return SpecialKey::enter;

    if (event.keyCode == KeyCode::UpArrow)
        return SpecialKey::upArrow;

    if (event.keyCode == KeyCode::DownArrow)
        return SpecialKey::downArrow;

    return SpecialKey::none;
}

std::string getTypedCharacters(const UI::KeyEvent& event)
{
    if (event.keyCode == KeyCode::Delete || !isPrintableTypedText(event.characters))
        return {};

    return event.characters;
}
bool isPlainTypedText(const UI::KeyEvent& event)
{
    if (event.modifiers.command || event.modifiers.control)
        return false;

    return isPrintableTypedText(event.characters);
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
    state = searchkeys::getOpeningState();
}

bool SearchField::isNormalMode() const
{
    return searchkeys::isNormalMode(state);
}

searchkeys::Action SearchField::handleKey(const UI::KeyEvent& event)
{
    const auto result = searchkeys::handleKey(state, makeSearchKey(event));

    if (result.action != searchkeys::Action::passThrough)
        state = result.state;

    return result.action;
}

bool SearchField::editText(const UI::KeyEvent& event)
{
    const auto caret = (std::size_t) state.caret;

    if (event.keyCode == KeyCode::Delete)
    {
        if (caret == 0)
            return false;

        const auto start = findPreviousCodepointStart(state.text, caret);
        state.text.erase(start, caret - start);
        state.caret = (int) start;
        return true;
    }

    if (!isPlainTypedText(event))
        return false;

    state.text.insert(caret, event.characters);
    state.caret += (int) event.characters.size();
    return true;
}
} // namespace term
