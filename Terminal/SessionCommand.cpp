#include "SessionCommand.h"

#include <array>

namespace term
{
namespace
{
struct Naming
{
    SessionCommand command;
    std::string_view name;
};

constexpr auto namings = std::array {
    Naming {SessionCommand::SplitBeside, "split-beside"},
    Naming {SessionCommand::SplitBelow, "split-below"},
    Naming {SessionCommand::ClosePane, "close-pane"},
    Naming {SessionCommand::ResizeLeft, "resize-left"},
    Naming {SessionCommand::ResizeDown, "resize-down"},
    Naming {SessionCommand::ResizeUp, "resize-up"},
    Naming {SessionCommand::ResizeRight, "resize-right"},
    Naming {SessionCommand::ToggleZoom, "toggle-zoom"},
    Naming {SessionCommand::FocusLeft, "focus-left"},
    Naming {SessionCommand::FocusDown, "focus-down"},
    Naming {SessionCommand::FocusUp, "focus-up"},
    Naming {SessionCommand::FocusRight, "focus-right"},
    Naming {SessionCommand::CycleFocus, "cycle-focus"},
};
} // namespace

SessionCommand commandFromName(std::string_view name)
{
    for (const auto& naming: namings)
        if (naming.name == name)
            return naming.command;

    return SessionCommand::None;
}

std::string_view nameForCommand(SessionCommand command)
{
    for (const auto& naming: namings)
        if (naming.command == command)
            return naming.name;

    return {};
}

bool isShapeCommand(SessionCommand command)
{
    switch (command)
    {
        case SessionCommand::SplitBeside:
        case SessionCommand::SplitBelow:
        case SessionCommand::ClosePane:
        case SessionCommand::ResizeLeft:
        case SessionCommand::ResizeDown:
        case SessionCommand::ResizeUp:
        case SessionCommand::ResizeRight:
            return true;

        default:
            return false;
    }
}

void applySessionCommand(SessionView& view,
                         SessionCommand command,
                         float resizeCells)
{
    switch (command)
    {
        case SessionCommand::SplitBeside:
            view.splitActive(true);
            break;

        case SessionCommand::SplitBelow:
            view.splitActive(false);
            break;

        case SessionCommand::ClosePane:
            view.closeActivePane();
            break;

        case SessionCommand::ResizeLeft:
            view.resizeActive('h', resizeCells);
            break;

        case SessionCommand::ResizeDown:
            view.resizeActive('j', resizeCells);
            break;

        case SessionCommand::ResizeUp:
            view.resizeActive('k', resizeCells);
            break;

        case SessionCommand::ResizeRight:
            view.resizeActive('l', resizeCells);
            break;

        case SessionCommand::ToggleZoom:
            view.toggleZoom();
            break;

        case SessionCommand::FocusLeft:
            view.focusDirection('h');
            break;

        case SessionCommand::FocusDown:
            view.focusDirection('j');
            break;

        case SessionCommand::FocusUp:
            view.focusDirection('k');
            break;

        case SessionCommand::FocusRight:
            view.focusDirection('l');
            break;

        case SessionCommand::CycleFocus:
            view.cycleFocus();
            break;

        case SessionCommand::None:
            break;
    }
}
} // namespace term
