#pragma once

#include "SessionView.h"

#include <string>
#include <string_view>

namespace term
{
// The leader table's vocabulary, named so it can travel. Every Ctrl+A
// action that changes a session resolves to one of these, and a command
// can then be applied to a local pane tree or shipped to the CowTerm that
// owns one (the gateway's "command" op) — which is what makes a remote
// session behave exactly like a local one.
//
// The split is deliberate and load-bearing:
//
//   * Shape commands (splits, close, resize) mutate the pane TREE, which
//     the owning GUI owns. On a mirror they must travel; applying them
//     locally would desync the two views. isShapeCommand() marks these.
//
//   * View commands (focus, zoom) only decide what THIS viewer looks at,
//     and every viewer is entitled to its own answer — as are copy mode
//     and paste, which read the local screen model. These never travel.
enum class SessionCommand
{
    None,
    SplitBeside, // Ctrl+A %  — side by side
    SplitBelow,  // Ctrl+A "  — stacked
    ClosePane,   // Ctrl+A x
    ResizeLeft,
    ResizeDown,
    ResizeUp,
    ResizeRight,
    ToggleZoom,
    FocusLeft,
    FocusDown,
    FocusUp,
    FocusRight,
    CycleFocus,
};

// Wire names, kebab-case ("split-beside"). Unknown names parse to None,
// so an older CowTerm simply ignores a command it doesn't have.
SessionCommand commandFromName(std::string_view name);
std::string_view nameForCommand(SessionCommand command);

// True when the command mutates the pane tree and therefore belongs to
// whoever owns it.
bool isShapeCommand(SessionCommand command);

// Applies to the tree's active pane. resizeCells scales the resize
// commands (1 cell normally, 5 with Alt held).
void applySessionCommand(SessionView& view,
                         SessionCommand command,
                         float resizeCells = 1.0f);
} // namespace term
