#pragma once

#include "TermParser.h"
#include "TermScreen.h"
#include "TermState.h"

#include <functional>
#include <string>
#include <string_view>

namespace term
{
// One terminal's model: the screen, the parser that writes it, and the title
// and working directory its program last reported. The daemon and every
// TerminalView parse output through this one class, so the state the daemon
// saves is exactly the state a view holds.
class TerminalModel
{
public:
    TerminalModel(int columns, int rows, const Theme& theme);

    void feed(std::string_view bytes);

    TerminalState captureState() const;
    void restoreState(const TerminalState& state);

    const std::string& getTitle() const { return title; }
    const std::string& getWorkingDirectory() const { return workingDirectory; }

    // Called on the thread that feeds, after the model has stored the value.
    std::function<void(const std::string&)> onTitleChanged = [](const std::string&) {};
    std::function<void(const std::string&)> onCwdChanged = [](const std::string&) {};

    TermScreen screen;
    TermParser parser;

private:
    std::string title;
    std::string workingDirectory;
};
} // namespace term
