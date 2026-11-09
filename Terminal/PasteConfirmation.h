#pragma once

#include "Config.h"

#include <eacp/Core/Utils/Common.h>

#include <memory>
#include <string>

namespace term
{
// The first lines of a paste for the confirmation prompt: at most a few
// lines, each cut short, with a count of what was left out.
std::string describePastePreview(const std::string& text);

// Asks before a paste that could run commands (TerminalView::confirmUnsafePaste):
// a prompt window in front of CowTerm showing the paste's first lines, with
// Paste and Cancel. One at a time; a new paste replaces an open prompt.
// Main-thread object.
class PasteConfirmation
{
public:
    explicit PasteConfirmation(const AppConfig& configToUse);
    ~PasteConfirmation();

    void ask(const std::string& text, const eacp::Callback& paste);

private:
    struct OpenPrompt;

    void closeLater();

    const AppConfig& config;
    std::unique_ptr<OpenPrompt> open;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term
