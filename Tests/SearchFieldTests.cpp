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
        field.handleKey(makeTypedKey(std::string(1, character)));
}

SearchField makeNormalModeFieldWithText(const std::string& text)
{
    auto field = SearchField {};
    typeText(field, text);
    field.handleKey(makeSpecialKey(UI::KeyCode::Escape));
    return field;
}
} // namespace

auto tOpensInInsert = test("SearchField/opensInInsertModeAndTypingEditsTheText") = []
{
    auto field = SearchField {};

    check(!field.isNormalMode());
    check(field.handleKey(makeTypedKey("j")) == Action::none);
    check(field.getText() == "j");
    check(field.handleKey(makeSpecialKey(UI::KeyCode::Return)) == Action::openHighlighted);
};

auto tEscapeOnLastCharacter = test("SearchField/escapeInInsertGoesToNormalWithTheCaretOnTheLastCharacter") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.isNormalMode());
    check(field.getText() == "cow");
    check(field.getCaret() == 2);
};

auto tEscapeInNormalCloses = test("SearchField/escapeInNormalClosesTheSearch") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.handleKey(makeSpecialKey(UI::KeyCode::Escape)) == Action::closeSearch);
};

auto tNormalListKeys = test("SearchField/normalModeListKeysMapToRowActions") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.handleKey(makeTypedKey("j")) == Action::moveHighlightDown);
    check(field.handleKey(makeTypedKey("k")) == Action::moveHighlightUp);
    check(field.handleKey(makeTypedKey("G")) == Action::moveHighlightToLast);
    check(field.handleKey(makeTypedKey("g")) == Action::none);
    check(field.handleKey(makeTypedKey("g")) == Action::moveHighlightToFirst);
    check(field.handleKey(makeTypedKey("s")) == Action::starHighlighted);
    check(field.getText() == "cow");
};

auto tWordBack = test("SearchField/bFromTheEndOfHelloWorldGoesBackAWord") = []
{
    constexpr auto startOfWorld = 6;
    auto field = makeNormalModeFieldWithText("hello world");

    field.handleKey(makeTypedKey("b"));
    check(field.getCaret() == startOfWorld);
};

auto tVimEdits = test("SearchField/xAndDdEditTheText") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    check(field.handleKey(makeTypedKey("x")) == Action::none);
    check(field.getText() == "co");

    field.handleKey(makeTypedKey("d"));
    field.handleKey(makeTypedKey("d"));
    check(field.getText().empty());
};

auto tBackspaceAtCaret = test("SearchField/backspaceErasesBeforeTheCaret") = []
{
    auto field = makeNormalModeFieldWithText("cow");

    field.handleKey(makeTypedKey("I"));
    typeText(field, "ab");
    field.handleKey(makeSpecialKey(UI::KeyCode::Delete));

    check(field.getText() == "acow");
    check(field.getCaret() == 1);
};

auto tChordsPassThrough = test("SearchField/controlChordsPassThroughInNormal") = []
{
    auto field = makeNormalModeFieldWithText("cow");
    auto event = makeTypedKey("n");
    event.modifiers.control = true;

    check(field.handleKey(event) == Action::passThrough);
};

auto tControlWDeletesWord = test("SearchField/controlWInInsertDeletesTheWordBehindTheCaret") = []
{
    auto field = SearchField {};
    typeText(field, "cow term");

    auto controlW = makeTypedKey("\x17");
    controlW.charactersIgnoringModifiers = "w";
    controlW.modifiers.control = true;

    check(field.handleKey(controlW) == Action::none);
    check(field.getText() == "cow ");
};

auto tControlQPassesThrough = test("SearchField/controlQPassesThroughAndLeavesTheText") = []
{
    auto field = SearchField {};
    typeText(field, "cow");

    auto controlQ = makeTypedKey("\x11");
    controlQ.charactersIgnoringModifiers = "q";
    controlQ.modifiers.control = true;

    check(field.handleKey(controlQ) == Action::passThrough);
    check(field.getText() == "cow");
};
