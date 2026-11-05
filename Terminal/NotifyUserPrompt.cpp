#include "NotifyUserPrompt.h"

#include <eacp/Core/App/Clipboard.h>

#include <algorithm>
#include <cmath>

namespace term
{
using namespace eacp;
using UI::KeyEvent;
using UI::MouseEvent;
using UI::Point;
using UI::Rect;
namespace KeyCode = UI::KeyCode;

namespace
{
constexpr auto titleFontSize = 14.0f;
constexpr auto subtitleFontSize = 12.0f;
constexpr auto contentFontSize = 13.0f;
constexpr auto buttonFontSize = 13.0f;

constexpr auto outerPadding = 16.0f;
constexpr auto headerToContentGap = 14.0f;
constexpr auto contentToButtonsGap = 16.0f;
constexpr auto rowGap = 8.0f;
constexpr auto buttonGap = 8.0f;
constexpr auto buttonHeight = 30.0f;
constexpr auto buttonCaptionPadding = 14.0f;
constexpr auto minimumButtonWidth = 64.0f;
constexpr auto buttonCornerRadius = 6.0f;
constexpr auto typedResponseFieldPadding = 10.0f;
constexpr auto dividerThickness = 1.0f;
constexpr auto contentLineSpacing = 1.25f;
constexpr auto maximumPromptHeight = 640.0f;
constexpr auto backgroundBrightening = 0.04f;
constexpr auto hoverBrightening = 0.15f;
constexpr auto firstOptionShortcut = '1';
constexpr auto lastOptionShortcut = '9';
constexpr auto firstPrintableCharacter = 0x20;
constexpr auto deleteCharacter = 0x7f;

constexpr auto okCaption = "OK";
constexpr auto sendCaption = "Send";
constexpr auto showPaneCaption = "Show pane";
constexpr auto dismissCaption = "Dismiss";
constexpr auto showPaneHint = "show pane ↗";
constexpr auto ellipsis = "…";
constexpr auto typingCursor = "▏";

bool isUtf8ContinuationByte(char byte)
{
    const auto value = (unsigned char) byte;
    return (value & 0xc0) == 0x80;
}

std::size_t findPreviousCodepointStart(const std::string& text, std::size_t end)
{
    auto start = end;

    while (start > 0)
    {
        --start;

        if (!isUtf8ContinuationByte(text[start]))
            return start;
    }

    return 0;
}

std::size_t findNextCodepointEnd(const std::string& text, std::size_t start)
{
    auto end = start + 1;

    while (end < text.size() && isUtf8ContinuationByte(text[end]))
        ++end;

    return end;
}

std::vector<std::string> splitIntoParagraphs(const std::string& text)
{
    auto paragraphs = std::vector<std::string> {};
    auto start = std::size_t {0};

    while (true)
    {
        const auto end = text.find('\n', start);

        if (end == std::string::npos)
        {
            paragraphs.push_back(text.substr(start));
            return paragraphs;
        }

        paragraphs.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::string firstLineOf(const std::string& text)
{
    return text.substr(0, text.find_first_of("\r\n"));
}
} // namespace

NotifyUserPrompt::NotifyUserPrompt(const AppConfig& configToUse,
                                   NotifyUserPromptText textToShow)
    : theme(themeByName(configToUse.theme))
    , text(std::move(textToShow))
    , titleFont({configToUse.font, titleFontSize, UI::FontStyle::Bold})
    , subtitleFont({configToUse.font, subtitleFontSize})
    , contentFont({configToUse.font, contentFontSize})
    , buttonFont({configToUse.font, buttonFontSize})
{
    setInterceptsMouseClicks(true);
    setWantsKeyboardFocus(true);
}

std::vector<std::string> NotifyUserPrompt::answerChoices() const
{
    if (text.options.empty() && !text.allowsTypedResponse)
        return {okCaption};

    return text.options;
}

std::vector<std::string> NotifyUserPrompt::wrapTextToWidth(const std::string& source,
                                                           float width) const
{
    auto lines = std::vector<std::string> {};

    const auto fits = [&](const std::string& candidate)
    { return measureText(candidate, contentFont) <= width; };

    for (const auto& paragraph: splitIntoParagraphs(source))
    {
        auto line = std::string {};
        auto start = std::size_t {0};

        while (start <= paragraph.size())
        {
            const auto end = std::min(paragraph.find(' ', start), paragraph.size());
            auto word = paragraph.substr(start, end - start);
            start = end + 1;

            const auto joined = line.empty() ? word : line + " " + word;

            if (fits(joined))
            {
                line = joined;
                continue;
            }

            if (!line.empty())
                lines.push_back(line);

            line.clear();

            // A word wider than the whole line breaks between characters.
            while (!fits(word))
            {
                auto piece = std::string {};
                auto cut = std::size_t {0};

                while (cut < word.size())
                {
                    const auto next = findNextCodepointEnd(word, cut);

                    if (!piece.empty() && !fits(word.substr(0, next)))
                        break;

                    piece = word.substr(0, next);
                    cut = next;
                }

                lines.push_back(piece);
                word.erase(0, cut);
            }

            line = word;
        }

        lines.push_back(line);
    }

    return lines;
}

std::string NotifyUserPrompt::truncateTextToWidth(const std::string& source,
                                                  float width,
                                                  const UI::Font& font) const
{
    if (measureText(source, font) <= width)
        return source;

    auto truncated = source;

    while (!truncated.empty() && measureText(truncated + ellipsis, font) > width)
        truncated.erase(findPreviousCodepointStart(truncated, truncated.size()));

    return truncated + ellipsis;
}

NotifyUserPrompt::Layout NotifyUserPrompt::calculateLayout(float width) const
{
    auto result = Layout {};
    const auto innerWidth = std::max(0.0f, width - 2.0f * outerPadding);
    const auto left = outerPadding;

    auto* host = getHost();
    const auto lineHeightOf = [&](const UI::Font& font)
    {
        if (host == nullptr)
            return font.pointSize * contentLineSpacing;

        return host->getLineHeight(font);
    };

    auto y = outerPadding;

    const auto headerHeight = lineHeightOf(titleFont) + lineHeightOf(subtitleFont);
    result.header = {left, y, innerWidth, headerHeight};
    y += headerHeight + headerToContentGap;

    result.contentTop = y;
    const auto contentLineHeight = lineHeightOf(contentFont) * contentLineSpacing;
    result.contentLines = wrapTextToWidth(text.content, innerWidth);

    // Buttons flow left to right, wrapping onto new rows.
    auto buttonsHeight = 0.0f;
    auto rowX = left;
    auto rowCount = 0.0f;
    auto flowButtons = std::vector<Button> {};

    for (const auto& choice: answerChoices())
    {
        const auto caption = truncateTextToWidth(
            choice, innerWidth - 2.0f * buttonCaptionPadding, buttonFont);
        const auto buttonWidth = std::max(
            minimumButtonWidth, measureText(caption, buttonFont) + 2.0f * buttonCaptionPadding);

        if (rowCount == 0.0f || rowX + buttonWidth > left + innerWidth)
        {
            rowX = left;
            rowCount += 1.0f;
        }

        const auto rowTop = (rowCount - 1.0f) * (buttonHeight + rowGap);
        flowButtons.push_back({ButtonAction::AnswerWithOption,
                               caption,
                               choice,
                               {rowX, rowTop, buttonWidth, buttonHeight}});
        rowX += buttonWidth + buttonGap;
    }

    buttonsHeight = rowCount * (buttonHeight + rowGap);

    if (text.allowsTypedResponse)
        buttonsHeight += buttonHeight + rowGap;

    const auto footerHeight = buttonHeight;
    const auto fixedHeight = y + contentToButtonsGap + buttonsHeight + footerHeight
                             + outerPadding;

    const auto availableContentHeight = maximumPromptHeight - fixedHeight;
    const auto fittingLinesAsFloat = std::floor(availableContentHeight / contentLineHeight);
    const auto fittingLineCount =
        std::max(std::size_t {1}, static_cast<std::size_t>(std::max(0.0f, fittingLinesAsFloat)));

    if (result.contentLines.size() > fittingLineCount)
    {
        result.contentLines.resize(fittingLineCount);
        auto& last = result.contentLines.back();
        last = truncateTextToWidth(last + ellipsis, innerWidth, contentFont);
    }

    const auto contentLineCount = static_cast<float>(result.contentLines.size());
    y += contentLineCount * contentLineHeight + contentToButtonsGap;

    for (auto& button: flowButtons)
    {
        button.bounds.y += y;
        result.buttons.push_back(button);
    }

    y += rowCount * (buttonHeight + rowGap);

    if (text.allowsTypedResponse)
    {
        const auto sendWidth =
            std::max(minimumButtonWidth,
                     measureText(sendCaption, buttonFont) + 2.0f * buttonCaptionPadding);

        result.typedResponseField =
            Rect {left, y, innerWidth - sendWidth - buttonGap, buttonHeight};
        result.buttons.push_back({ButtonAction::SendTypedResponse,
                                  sendCaption,
                                  {},
                                  {left + innerWidth - sendWidth, y, sendWidth, buttonHeight}});
        y += buttonHeight + rowGap;
    }

    const auto footerButtonWidthFor = [&](const std::string& caption)
    {
        return std::max(minimumButtonWidth,
                        measureText(caption, buttonFont) + 2.0f * buttonCaptionPadding);
    };

    const auto showPaneWidth = footerButtonWidthFor(showPaneCaption);
    const auto dismissWidth = footerButtonWidthFor(dismissCaption);

    result.buttons.push_back(
        {ButtonAction::ShowPane, showPaneCaption, {}, {left, y, showPaneWidth, buttonHeight}});
    result.buttons.push_back({ButtonAction::Dismiss,
                              dismissCaption,
                              {},
                              {left + innerWidth - dismissWidth, y, dismissWidth, buttonHeight}});
    y += footerHeight + outerPadding;

    result.height = y;
    return result;
}

float NotifyUserPrompt::calculateHeightForWidth(float width) const
{
    return calculateLayout(width).height;
}

void NotifyUserPrompt::resized()
{
    layout = calculateLayout(getLocalBounds().w);
    hoveredButton.reset();
    repaint();
}

std::optional<std::size_t> NotifyUserPrompt::findButtonAt(Point position) const
{
    for (auto index = std::size_t {0}; index < layout.buttons.size(); ++index)
        if (layout.buttons[index].bounds.contains(position))
            return index;

    return std::nullopt;
}

void NotifyUserPrompt::performButtonAction(const Button& button)
{
    switch (button.action)
    {
        case ButtonAction::AnswerWithOption:
            onAnswered(button.answer);
            return;
        case ButtonAction::SendTypedResponse:
            onAnswered(typedResponse);
            return;
        case ButtonAction::ShowPane:
            onShowPane();
            return;
        case ButtonAction::Dismiss:
            onDismissed();
            return;
    }
}

void NotifyUserPrompt::answerWithOptionNumber(std::size_t number)
{
    const auto choices = answerChoices();

    if (number >= 1 && number <= choices.size())
        onAnswered(choices[number - 1]);
}

void NotifyUserPrompt::pasteIntoTypedResponse()
{
    typedResponse += firstLineOf(Clipboard::getText());
    repaint();
}

void NotifyUserPrompt::deleteLastTypedCharacter()
{
    if (typedResponse.empty())
        return;

    typedResponse.erase(findPreviousCodepointStart(typedResponse, typedResponse.size()));
    repaint();
}

bool NotifyUserPrompt::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        onDismissed();
        return true;
    }

    if (event.keyCode == KeyCode::Return)
    {
        if (text.allowsTypedResponse)
            onAnswered(typedResponse);
        else if (answerChoices().size() == 1)
            onAnswered(answerChoices().front());

        return true;
    }

    if (!text.allowsTypedResponse)
    {
        const auto& typed = event.charactersIgnoringModifiers;

        if (typed.size() == 1 && typed[0] >= firstOptionShortcut
            && typed[0] <= lastOptionShortcut)
        {
            const auto shortcutOffset = typed[0] - firstOptionShortcut;
            const auto number = static_cast<std::size_t>(shortcutOffset) + 1;
            answerWithOptionNumber(number);
        }

        return true;
    }

    if (event.modifiers.command && event.charactersIgnoringModifiers == "v")
    {
        pasteIntoTypedResponse();
        return true;
    }

    if (event.keyCode == KeyCode::Delete)
    {
        deleteLastTypedCharacter();
        return true;
    }

    if (event.modifiers.command || event.modifiers.control)
        return true;

    const auto& characters = event.characters;
    if (characters.empty())
        return true;

    const auto firstByte = static_cast<unsigned char>(characters[0]);

    if (firstByte >= firstPrintableCharacter && firstByte != deleteCharacter)
    {
        typedResponse += characters;
        repaint();
    }

    return true;
}

void NotifyUserPrompt::mouseDown(const MouseEvent& event)
{
    if (layout.header.contains(event.position))
    {
        onShowPane();
        return;
    }

    if (const auto index = findButtonAt(event.position))
        performButtonAction(layout.buttons[*index]);
}

void NotifyUserPrompt::mouseMove(const MouseEvent& event)
{
    const auto button = findButtonAt(event.position);
    const auto overHeader = layout.header.contains(event.position);

    if (button == hoveredButton && overHeader == isHeaderHovered)
        return;

    hoveredButton = button;
    isHeaderHovered = overHeader;
    repaint();
}

void NotifyUserPrompt::mouseExit(const MouseEvent&)
{
    hoveredButton.reset();
    isHeaderHovered = false;
    repaint();
}

void NotifyUserPrompt::paintHeader(UI::Graphics& g) const
{
    const auto& header = layout.header;
    const auto hintWidth = measureText(showPaneHint, subtitleFont);

    g.setFont(subtitleFont);
    g.setColour(toColor(isHeaderHovered ? theme.ansi[4] : theme.ansi[8]));
    const auto titleLineHeight = header.h - g.lineHeight();
    g.drawText(showPaneHint,
               {header.x, header.y, header.w, titleLineHeight},
               UI::Justification::Right);

    g.setColour(toColor(theme.ansi[8]));
    g.drawText(truncateTextToWidth(text.paneSubtitle, header.w, subtitleFont),
               {header.x, header.y + titleLineHeight, header.w, g.lineHeight()});

    g.setFont(titleFont);
    g.setColour(toColor(theme.foreground));
    g.drawText(truncateTextToWidth(text.paneTitle, header.w - hintWidth - buttonGap, titleFont),
               {header.x, header.y, header.w, titleLineHeight});

    g.setColour(toColor(theme.selection));
    const auto dividerY = header.y + header.h + headerToContentGap / 2.0f;
    g.drawLine({header.x, dividerY}, {header.x + header.w, dividerY}, dividerThickness);
}

void NotifyUserPrompt::paintContent(UI::Graphics& g) const
{
    g.setFont(contentFont);
    g.setColour(toColor(theme.foreground));

    const auto lineHeight = g.lineHeight() * contentLineSpacing;
    auto baseline = layout.contentTop + g.ascent();

    for (const auto& line: layout.contentLines)
    {
        g.drawText(line, Point {outerPadding, baseline});
        baseline += lineHeight;
    }
}

void NotifyUserPrompt::paintTypedResponseField(UI::Graphics& g) const
{
    if (!layout.typedResponseField)
        return;

    const auto& field = *layout.typedResponseField;

    g.setColour(toColor(theme.background));
    g.fillRoundedRect(field, buttonCornerRadius);
    g.setColour(toColor(theme.selection));
    g.drawRoundedRect(field, buttonCornerRadius, dividerThickness);

    const auto textArea = field.inset(typedResponseFieldPadding, 0.0f);
    const auto cursorWidth = measureText(typingCursor, contentFont);
    auto shown = typedResponse;

    // Keep the end of a long answer, where the typing is, in view.
    while (!shown.empty() && measureText(shown, contentFont) + cursorWidth > textArea.w)
        shown.erase(0, findNextCodepointEnd(shown, 0));

    g.setFont(contentFont);
    g.setColour(toColor(theme.foreground));
    g.drawText(shown + typingCursor, textArea);
}

void NotifyUserPrompt::paintButton(UI::Graphics& g,
                                   const Button& button,
                                   bool isHovered) const
{
    const auto fill = [&]
    {
        if (button.action == ButtonAction::AnswerWithOption
            || button.action == ButtonAction::SendTypedResponse)
            return toColor(theme.selection);

        return toColor(theme.background);
    }();

    g.setColour(isHovered ? fill.brighter(hoverBrightening) : fill);
    g.fillRoundedRect(button.bounds, buttonCornerRadius);
    g.setColour(toColor(theme.paneBorderActive));
    g.drawRoundedRect(button.bounds, buttonCornerRadius, dividerThickness);

    g.setFont(buttonFont);
    g.setColour(toColor(theme.foreground));
    g.drawText(button.caption, button.bounds, UI::Justification::Centred);
}

void NotifyUserPrompt::paint(UI::Graphics& g)
{
    g.fillAll(toColor(theme.background).brighter(backgroundBrightening));

    paintHeader(g);
    paintContent(g);
    paintTypedResponseField(g);

    for (auto index = std::size_t {0}; index < layout.buttons.size(); ++index)
        paintButton(g, layout.buttons[index], hoveredButton == index);
}
} // namespace term
