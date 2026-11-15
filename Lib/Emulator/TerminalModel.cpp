#include "TerminalModel.h"

namespace term
{
TerminalModel::TerminalModel(int columns, int rows, const Theme& theme)
    : screen(columns, rows, theme)
    , parser(screen, theme)
{
    parser.onTitleChanged = [this](const std::string& newTitle)
    {
        title = newTitle;
        onTitleChanged(title);
    };

    parser.onCwdChanged = [this](const std::string& newCwd)
    {
        workingDirectory = newCwd;
        onCwdChanged(workingDirectory);
    };
}

void TerminalModel::feed(std::string_view bytes)
{
    parser.feed(bytes);
}

TerminalState TerminalModel::captureState() const
{
    auto state = TerminalState {};
    state.screen = screen.saveState();
    state.parser = parser.saveState();
    state.title = title;
    state.workingDirectory = workingDirectory;
    return state;
}

void TerminalModel::restoreState(const TerminalState& state)
{
    screen.restoreState(state.screen);
    parser.restoreState(state.parser);
    title = state.title;
    workingDirectory = state.workingDirectory;
}
} // namespace term
