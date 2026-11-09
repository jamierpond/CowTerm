#include "PasteConfirmation.h"

#include "NotifyUserPrompt.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/Window/Window.h>

#include <algorithm>
#include <cmath>

namespace term
{
using namespace eacp;

namespace
{
constexpr auto promptWindowWidth = 520;
constexpr auto previewLineCount = std::size_t {6};
constexpr auto previewLineLength = std::size_t {90};
constexpr auto pasteOption = "Paste";
constexpr auto cancelOption = "Cancel";

std::vector<std::string> splitLines(const std::string& text)
{
    auto lines = std::vector<std::string> {""};

    for (const auto c: text)
    {
        if (c == '\n')
            lines.emplace_back();
        else if (c != '\r')
            lines.back().push_back(c);
    }

    return lines;
}

std::string cutLine(const std::string& line)
{
    if (line.size() <= previewLineLength)
        return line;

    return line.substr(0, previewLineLength) + "...";
}
} // namespace

std::string describePastePreview(const std::string& text)
{
    const auto lines = splitLines(text);
    auto preview = std::string {};

    for (std::size_t index = 0; index < std::min(lines.size(), previewLineCount); ++index)
        preview += cutLine(lines[index]) + "\n";

    if (lines.size() > previewLineCount)
        preview += "(" + std::to_string(lines.size() - previewLineCount) + " more lines)\n";

    return "The shell may run each line as a command.\n\n" + preview;
}

struct PasteConfirmation::OpenPrompt
{
    OpenPrompt(const AppConfig& config, NotifyUserPromptText text)
        : prompt(config, std::move(text))
    {
        host.setBackgroundColour(toColor(themeByName(config.theme).background));
        host.setRootComponent(prompt);
    }

    NotifyUserPrompt prompt;
    UI::ComponentHost host;
    std::unique_ptr<Graphics::Window> window;
};

PasteConfirmation::PasteConfirmation(const AppConfig& configToUse)
    : config(configToUse)
{
}

PasteConfirmation::~PasteConfirmation()
{
    *alive = false;
}

void PasteConfirmation::ask(const std::string& text, const Callback& paste)
{
    auto promptText = NotifyUserPromptText {};
    promptText.paneTitle = "Paste text that may run commands?";
    promptText.content = describePastePreview(text);
    promptText.options = {pasteOption, cancelOption};

    open = std::make_unique<OpenPrompt>(config, std::move(promptText));

    open->prompt.onAnswered = [this, paste](const std::string& answer)
    {
        if (answer == pasteOption)
            paste();

        closeLater();
    };
    open->prompt.onDismissed = [this] { closeLater(); };

    const auto widthAsFloat = static_cast<float>(promptWindowWidth);
    const auto heightInPoints =
        static_cast<int>(std::ceil(open->prompt.calculateHeightForWidth(widthAsFloat)));

    auto options = Graphics::WindowOptions {};
    options.title = "CowTerm · Paste";
    options.width = promptWindowWidth;
    options.height = heightInPoints;
    options.minHeight = heightInPoints;
    options.isPrimary = false;
    options.alwaysOnTop = true;
    options.backgroundColor = toColor(themeByName(config.theme).background);
    options.onQuit = [this] { closeLater(); };

    open->window = std::make_unique<Graphics::Window>(open->host, options);
    open->prompt.grabKeyboardFocus();
}

// Deferred: an answer arrives inside the prompt's own handler, which may not
// destroy its window mid-call.
void PasteConfirmation::closeLater()
{
    if (open == nullptr)
        return;

    if (open->window != nullptr)
        open->window->setVisible(false);

    Threads::callAsync(
        [this, closing = open.get(), guard = std::weak_ptr<bool> {alive}]
        {
            if (!guard.expired() && open.get() == closing)
                open.reset();
        });
}
} // namespace term
