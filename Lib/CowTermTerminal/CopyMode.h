#pragma once

#include "GridSelection.h"

#include "CowTermCore/TermScreen.h"

#include <eacp/Graphics/Graphics/Keyboard.h>

#include <optional>

// tmux-style copy mode, as pure decisions: which command a key means and where
// each motion puts the cursor. The view owns the state and applies them.
namespace term
{
enum class CopyModeMotion
{
    HalfPageUp,
    HalfPageDown,
    PageUp,
    PageDown,
    Left,
    Down,
    Up,
    Right,
    LineStart,
    LineEnd,
    Top,
    Bottom,
    NextWord,
    PreviousWord
};

enum class CopyModeAction
{
    Move,

    // First Escape drops the selection, the second leaves the mode.
    CancelSelectionOrExit,
    Exit,
    YankAndExit,
    BeginCharacterSelection,
    BeginLineSelection,

    // Consumed so it never reaches the shell, and otherwise ignored.
    Nothing
};

struct CopyModeCommand
{
    CopyModeAction action = CopyModeAction::Nothing;
    CopyModeMotion motion = CopyModeMotion::Left;
};

// Empty for Cmd chords, which stay live (zoom, copy, paste); every other key
// is a command, if only Nothing.
std::optional<CopyModeCommand> findCopyModeCommand(const eacp::Graphics::KeyEvent& event);

// Where the motion takes the cursor, before clamping to the buffer.
CellRef findCopyModeMotionTarget(CopyModeMotion motion,
                                 CellRef cursor,
                                 const TermScreen& screen);

// vi's w: off the current word, then over the gap to the next one, wrapping
// across rows.
CellRef findNextWordStart(CellRef from, const TermScreen& screen);

// vi's b: over the gap backwards, then to the head of that word.
CellRef findPreviousWordStart(CellRef from, const TermScreen& screen);
} // namespace term
