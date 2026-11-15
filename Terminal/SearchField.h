#pragma once

#include <SearchKeys/SearchKeys.h>
#include <eacp/UI/UI.h>

#include <string>

namespace term
{
// A palette's query box: Rudder's one-line vim search box, fed eacp key events.
class SearchField
{
public:
    void reset();

    std::string getText() const { return box.getText(); }
    int getCaret() const { return box.getCaret(); }
    bool isNormalMode() const { return box.isNormalMode(); }

    // Edits the text or names a list action; passThrough means the panel
    // handles the key itself (its control and command chords).
    searchkeys::Action handleKey(const eacp::UI::KeyEvent& event);

private:
    searchkeys::SearchBox box;
};

searchkeys::Key makeSearchKey(const eacp::UI::KeyEvent& event);
} // namespace term
