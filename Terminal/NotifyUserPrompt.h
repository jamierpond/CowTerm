#pragma once

#include "Config.h"

#include <eacp/UI/UI.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace term
{
// Everything the prompt shows: the pane it is about, the agent's message and
// the ways to answer.
struct NotifyUserPromptText
{
    std::string paneTitle;
    std::string paneSubtitle;
    std::string content;
    std::vector<std::string> options;
    bool allowsTypedResponse = false;
};

// The body of a `ct notify-user` window: a header naming the pane (click it to
// jump there), the message wrapped to the width, one button per option, an
// optional single-line answer field, and Show pane / Dismiss.
class NotifyUserPrompt final : public eacp::UI::Component
{
public:
    NotifyUserPrompt(const AppConfig& configToUse, NotifyUserPromptText textToShow);

    // Needs the component to be in a host already: measuring text asks it.
    float calculateHeightForWidth(float width) const;

    std::function<void(const std::string& answer)> onAnswered =
        [](const std::string&) {};
    eacp::Callback onDismissed = [] {};
    eacp::Callback onShowPane = [] {};

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;
    bool keyDown(const eacp::UI::KeyEvent& event) override;
    void mouseDown(const eacp::UI::MouseEvent& event) override;
    void mouseMove(const eacp::UI::MouseEvent& event) override;
    void mouseExit(const eacp::UI::MouseEvent& event) override;

private:
    enum class ButtonAction
    {
        AnswerWithOption,
        SendTypedResponse,
        ShowPane,
        Dismiss
    };

    struct Button
    {
        ButtonAction action = ButtonAction::Dismiss;
        std::string caption;
        std::string answer;
        eacp::UI::Rect bounds;
    };

    struct Layout
    {
        eacp::UI::Rect header;
        std::vector<std::string> contentLines;
        float contentTop = 0.0f;
        std::vector<Button> buttons;
        std::optional<eacp::UI::Rect> typedResponseField;
        float height = 0.0f;
    };

    Layout calculateLayout(float width) const;
    std::vector<std::string> wrapTextToWidth(const std::string& text, float width) const;
    std::string truncateTextToWidth(const std::string& text,
                                    float width,
                                    const eacp::UI::Font& font) const;
    std::vector<std::string> answerChoices() const;
    std::optional<std::size_t> findButtonAt(eacp::UI::Point position) const;
    void performButtonAction(const Button& button);
    void answerWithOptionNumber(std::size_t number);
    void pasteIntoTypedResponse();
    void deleteLastTypedCharacter();

    void paintHeader(eacp::UI::Graphics& g) const;
    void paintContent(eacp::UI::Graphics& g) const;
    void paintTypedResponseField(eacp::UI::Graphics& g) const;
    void paintButton(eacp::UI::Graphics& g, const Button& button, bool isHovered) const;

    Theme theme;
    NotifyUserPromptText text;
    std::string typedResponse;
    Layout layout;
    std::optional<std::size_t> hoveredButton;
    bool isHeaderHovered = false;

    eacp::UI::Font titleFont;
    eacp::UI::Font subtitleFont;
    eacp::UI::Font contentFont;
    eacp::UI::Font buttonFont;
};
} // namespace term
