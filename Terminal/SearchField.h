#pragma once

#include <SearchKeys/SearchKeys.h>
#include <eacp/UI/UI.h>

#include <string>

namespace term
{
// A palette's query box with Rudder's vim modes: SearchKeys picks the mode and
// the action for each key, this keeps the text and caret it edits.
class SearchField
{
public:
    void reset();

    const std::string& getText() const { return state.text; }
    int getCaret() const { return state.caret; }
    bool isNormalMode() const;

    // Applies the key's mode and text change; passThrough means the panel
    // handles the key itself (typing, Enter in insert mode, chords).
    searchkeys::Action handleKey(const eacp::UI::KeyEvent& event);

    // Insert-mode typing and backspace at the caret; true when the text changed.
    bool editText(const eacp::UI::KeyEvent& event);

private:
    searchkeys::State state = searchkeys::getOpeningState();
};

searchkeys::Key makeSearchKey(const eacp::UI::KeyEvent& event);
} // namespace term
