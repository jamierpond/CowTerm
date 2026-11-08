#include "CopyMode.h"

#include "CellText.h"
#include "MacKeyCodes.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace term
{
using eacp::Graphics::KeyEvent;
namespace KeyCode = eacp::Graphics::KeyCode;

namespace
{
constexpr std::uint16_t noKeyCode = 0xffff;

struct CopyModeKeyBinding
{
    std::string_view characters;
    std::uint16_t keyCode = noKeyCode;
    CopyModeCommand command;

    bool matches(const KeyEvent& event) const
    {
        if (!characters.empty() && event.charactersIgnoringModifiers == characters)
            return true;

        return keyCode != noKeyCode && event.keyCode == keyCode;
    }
};

constexpr CopyModeCommand moveBy(CopyModeMotion motion)
{
    return {CopyModeAction::Move, motion};
}

constexpr CopyModeCommand perform(CopyModeAction action)
{
    return {action, CopyModeMotion::Left};
}

constexpr auto leavingBindings = std::array {
    CopyModeKeyBinding {"q", noKeyCode, perform(CopyModeAction::Exit)},
    CopyModeKeyBinding {"y", KeyCode::Return, perform(CopyModeAction::YankAndExit)},
    CopyModeKeyBinding {{}, MacKey::KeypadEnter, perform(CopyModeAction::YankAndExit)}};

constexpr auto controlBindings =
    std::array {CopyModeKeyBinding {"u", noKeyCode, moveBy(CopyModeMotion::HalfPageUp)},
                CopyModeKeyBinding {"d", noKeyCode, moveBy(CopyModeMotion::HalfPageDown)},
                CopyModeKeyBinding {"b", noKeyCode, moveBy(CopyModeMotion::PageUp)},
                CopyModeKeyBinding {"f", noKeyCode, moveBy(CopyModeMotion::PageDown)}};

constexpr auto plainBindings = std::array {
    CopyModeKeyBinding {{}, MacKey::PageUp, moveBy(CopyModeMotion::PageUp)},
    CopyModeKeyBinding {{}, MacKey::PageDown, moveBy(CopyModeMotion::PageDown)},
    CopyModeKeyBinding {"h", KeyCode::LeftArrow, moveBy(CopyModeMotion::Left)},
    CopyModeKeyBinding {"j", KeyCode::DownArrow, moveBy(CopyModeMotion::Down)},
    CopyModeKeyBinding {"k", KeyCode::UpArrow, moveBy(CopyModeMotion::Up)},
    CopyModeKeyBinding {"l", KeyCode::RightArrow, moveBy(CopyModeMotion::Right)},
    CopyModeKeyBinding {"0", MacKey::Home, moveBy(CopyModeMotion::LineStart)},
    CopyModeKeyBinding {"$", MacKey::End, moveBy(CopyModeMotion::LineEnd)},
    CopyModeKeyBinding {"g", noKeyCode, moveBy(CopyModeMotion::Top)},
    CopyModeKeyBinding {"G", noKeyCode, moveBy(CopyModeMotion::Bottom)},
    CopyModeKeyBinding {"w", noKeyCode, moveBy(CopyModeMotion::NextWord)},
    CopyModeKeyBinding {"b", noKeyCode, moveBy(CopyModeMotion::PreviousWord)},
    CopyModeKeyBinding {"v", noKeyCode, perform(CopyModeAction::BeginCharacterSelection)},
    CopyModeKeyBinding {" ", noKeyCode, perform(CopyModeAction::BeginCharacterSelection)},
    CopyModeKeyBinding {"V", noKeyCode, perform(CopyModeAction::BeginLineSelection)}};

template <std::size_t size>
std::optional<CopyModeCommand>
    findBoundCommand(const std::array<CopyModeKeyBinding, size>& bindings,
                     const KeyEvent& event)
{
    for (const auto& binding: bindings)
    {
        if (binding.matches(event))
            return binding.command;
    }

    return std::nullopt;
}

bool isControlC(const KeyEvent& event)
{
    return event.modifiers.control && event.charactersIgnoringModifiers == "c";
}

std::optional<CopyModeCommand> findLeavingCommand(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
        return perform(CopyModeAction::CancelSelectionOrExit);

    if (isControlC(event))
        return perform(CopyModeAction::Exit);

    return findBoundCommand(leavingBindings, event);
}

class BufferWalker
{
public:
    BufferWalker(const TermScreen& screenToWalk, CellRef start)
        : screen(screenToWalk)
        , position(start)
        , lastRow(findLastAbsoluteRow(screenToWalk))
    {
    }

    bool isOnBlank() const { return characterAt(position.col) == U' '; }

    bool isInkBefore() const
    {
        return position.col > 0 && characterAt(position.col - 1) != U' ';
    }

    void moveToWordHead()
    {
        while (isInkBefore())
            --position.col;
    }

    bool stepForward()
    {
        if (position.col < screen.columns() - 1)
        {
            ++position.col;
            return true;
        }

        if (position.row >= lastRow)
            return false;

        ++position.row;
        position.col = 0;
        return true;
    }

    bool stepBackward()
    {
        if (position.col > 0)
        {
            --position.col;
            return true;
        }

        if (position.row <= 0)
            return false;

        --position.row;
        position.col = screen.columns() - 1;
        return true;
    }

    CellRef getPosition() const { return position; }

private:
    char32_t characterAt(int col) const
    {
        const auto& line = findLineAtAbsoluteRow(screen, position.row);
        return col < (int) line.size() ? line[(std::size_t) col].ch : U' ';
    }

    const TermScreen& screen;
    CellRef position;
    long lastRow;
};

using MotionTargetFinder = CellRef (*)(CellRef, const TermScreen&);

struct MotionRule
{
    CopyModeMotion motion;
    MotionTargetFinder findTarget;
};

constexpr auto motionRules = std::array {
    MotionRule {CopyModeMotion::HalfPageUp,
                [](CellRef cursor, const TermScreen& screen) {
                    return CellRef {cursor.row - (long) (screen.rows() / 2), cursor.col};
                }},
    MotionRule {CopyModeMotion::HalfPageDown,
                [](CellRef cursor, const TermScreen& screen) {
                    return CellRef {cursor.row + screen.rows() / 2, cursor.col};
                }},
    MotionRule {CopyModeMotion::PageUp,
                [](CellRef cursor, const TermScreen& screen) {
                    return CellRef {cursor.row - (long) (screen.rows() - 1), cursor.col};
                }},
    MotionRule {CopyModeMotion::PageDown,
                [](CellRef cursor, const TermScreen& screen) {
                    return CellRef {cursor.row + screen.rows() - 1, cursor.col};
                }},
    MotionRule {CopyModeMotion::Left,
                [](CellRef cursor, const TermScreen&) {
                    return CellRef {cursor.row, cursor.col - 1};
                }},
    MotionRule {CopyModeMotion::Down,
                [](CellRef cursor, const TermScreen&) {
                    return CellRef {cursor.row + 1, cursor.col};
                }},
    MotionRule {CopyModeMotion::Up,
                [](CellRef cursor, const TermScreen&) {
                    return CellRef {cursor.row - 1, cursor.col};
                }},
    MotionRule {CopyModeMotion::Right,
                [](CellRef cursor, const TermScreen&) {
                    return CellRef {cursor.row, cursor.col + 1};
                }},
    MotionRule {CopyModeMotion::LineStart,
                [](CellRef cursor, const TermScreen&) { return CellRef {cursor.row, 0}; }},
    MotionRule {CopyModeMotion::LineEnd,
                [](CellRef cursor, const TermScreen& screen)
                {
                    const auto& line = findLineAtAbsoluteRow(screen, cursor.row);
                    return CellRef {cursor.row, std::max(0, measureInkLength(line) - 1)};
                }},
    MotionRule {CopyModeMotion::Top,
                [](CellRef, const TermScreen&) { return CellRef {0, 0}; }},
    MotionRule {CopyModeMotion::Bottom,
                [](CellRef, const TermScreen& screen)
                { return CellRef {findLastAbsoluteRow(screen), 0}; }},
    MotionRule {CopyModeMotion::NextWord, findNextWordStart},
    MotionRule {CopyModeMotion::PreviousWord, findPreviousWordStart}};
} // namespace

std::optional<CopyModeCommand> findCopyModeCommand(const KeyEvent& event)
{
    if (event.modifiers.command)
        return std::nullopt;

    if (const auto leaving = findLeavingCommand(event))
        return leaving;

    if (event.modifiers.control)
        return findBoundCommand(controlBindings, event).value_or(CopyModeCommand {});

    return findBoundCommand(plainBindings, event).value_or(CopyModeCommand {});
}

CellRef findCopyModeMotionTarget(CopyModeMotion motion,
                                 CellRef cursor,
                                 const TermScreen& screen)
{
    for (const auto& rule: motionRules)
    {
        if (rule.motion == motion)
            return rule.findTarget(cursor, screen);
    }

    return cursor;
}

CellRef findNextWordStart(CellRef from, const TermScreen& screen)
{
    auto walker = BufferWalker {screen, from};

    while (!walker.isOnBlank() && walker.stepForward()) {}
    while (walker.isOnBlank() && walker.stepForward()) {}

    return walker.getPosition();
}

CellRef findPreviousWordStart(CellRef from, const TermScreen& screen)
{
    auto walker = BufferWalker {screen, from};

    if (!walker.stepBackward())
        return walker.getPosition();

    while (walker.isOnBlank() && walker.stepBackward()) {}

    walker.moveToWordHead();
    return walker.getPosition();
}
} // namespace term
