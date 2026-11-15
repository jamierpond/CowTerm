#include "SearchField.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace eacp;
using namespace term;
using searchkeys::Action;

namespace
{
constexpr std::uint16_t noKeyCode = 0xFFFF;

UI::KeyEvent makeTypedKey(const std::string& characters)
{
    auto event = UI::KeyEvent {};
    event.keyCode = noKeyCode;
    event.characters = characters;
    event.charactersIgnoringModifiers = characters;
    return event;
}

UI::KeyEvent makeSpecialKey(std::uint16_t keyCode)
{
    auto event = UI::KeyEvent {};
    event.keyCode = keyCode;
    return event;
}

void typeText(SearchField& field, const std::string& text)
{
    for (auto character: text)
        field.editText(makeTypedKey(std::string(1, character)));
}

SearchField makeNormalModeFieldWithText(const std::string& text)
{
    auto field = SearchField {};
    typeText(field, text);
    field.handleKey(makeSpecialKey(UI::KeyCode::Escape));
    return field;
}
} // namespace

auto tOpensInInsert = test("SearchField/opensInInsertModeAndTypingPassesThrough") = []
{
    auto field = SearchField {};

    check(!field.isNormalMode());
    check(field.handleKey(makeTypedKey("j")) == Action::passThrough);
    check(field.handleKey(makeSpecialKey(UI::KeyCode::Return)) == Action::passThrough);
};

auto tEscapeKeepsText = test("SearchField/escapeInInsertGoesToNormalAndKeepsText") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.isNormalMode());
    check(field.getText() == "cow");
};

auto tEscapeInNormalCloses = test("SearchField/escapeInNormalClosesTheSearch") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.handleKey(makeSpecialKey(UI::KeyCode::Escape)) == Action::closeSearch);
};

auto tNormalKeys = test("SearchField/normalModeKeysMapToRowActions") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.handleKey(makeTypedKey("j")) == Action::moveHighlightDown);
    check(field.handleKey(makeTypedKey("k")) == Action::moveHighlightUp);
    check(field.handleKey(makeTypedKey("G")) == Action::moveHighlightToLast);
    check(field.handleKey(makeTypedKey("g")) == Action::none);
    check(field.handleKey(makeTypedKey("g")) == Action::moveHighlightToFirst);
    check(field.handleKey(makeTypedKey("x")) == Action::closeHighlighted);
    check(field.handleKey(makeTypedKey("d")) == Action::none);
    check(field.handleKey(makeTypedKey("d")) == Action::closeHighlighted);
    check(field.handleKey(makeTypedKey("s")) == Action::starHighlighted);
    check(field.handleKey(makeSpecialKey(UI::KeyCode::Return)) == Action::openHighlighted);
    check(field.handleKey(makeTypedKey("q")) == Action::none);
    check(field.getText() == "cow");
};

auto tBackspaceInNormal = test("SearchField/backspaceInNormalLeavesTextAlone") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.handleKey(makeSpecialKey(UI::KeyCode::Delete)) == Action::none);
    check(field.getText() == "cow");
};

auto tInsertAtStart = test("SearchField/capitalIThenTypingInsertsAtTheStart") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    field.handleKey(makeTypedKey("I"));
    typeText(field, "my");

    check(!field.isNormalMode());
    check(field.getText() == "mycow");
    check(field.getCaret() == 2);
};

auto tBackspaceAtCaret = test("SearchField/backspaceErasesBeforeTheCaret") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    field.handleKey(makeTypedKey("I"));
    typeText(field, "ab");
    field.editText(makeSpecialKey(UI::KeyCode::Delete));

    check(field.getText() == "acow");
    check(field.getCaret() == 1);
};

auto tClearAndInsert = test("SearchField/ccAndCapitalSClearTheTextIntoInsert") = []
{
    auto field = makeNormalModeFieldWithText("cow");
    field.handleKey(makeTypedKey("S"));

    check(field.getText().empty());
    check(!field.isNormalMode());

    typeText(field, "moo");
    field.handleKey(makeSpecialKey(UI::KeyCode::Escape));
    field.handleKey(makeTypedKey("c"));
    field.handleKey(makeTypedKey("c"));

    check(field.getText().empty());
    check(!field.isNormalMode());
};

auto tChordsPassThrough = test("SearchField/controlChordsPassThroughInNormal") = []
{
    auto field = makeNormalModeFieldWithText("cow");
    auto event = makeTypedKey("n");
    event.modifiers.control = true;

    check(field.handleKey(event) == Action::passThrough);
};
