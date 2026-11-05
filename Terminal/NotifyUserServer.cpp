#include "NotifyUserServer.h"

#include "NotifyUserPrompt.h"
#include "Protocol.h"

#include "CowTermCore/Debug.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/Window/Window.h>
#include <eacp/Network/IPC/Messenger.h>

#include <algorithm>
#include <cmath>

namespace term
{
using namespace eacp;

namespace
{
constexpr auto promptWindowWidth = 480;
constexpr auto promptWindowMinimumWidth = 320;
constexpr auto cascadeStep = 28.0f;
constexpr auto cascadeLength = 8;
constexpr auto windowTitlePrefix = "CowTerm · ";
constexpr auto unknownPanePrefix = "pane ";
constexpr auto labelSeparator = " · ";
} // namespace

struct NotifyUserServer::OpenPrompt
{
    OpenPrompt(IPC::Messenger& messengerToUse,
               const AppConfig& config,
               NotifyUserPromptText text)
        : messenger(messengerToUse)
        , prompt(config, std::move(text))
    {
        host.setBackgroundColour(toColor(themeByName(config.theme).background));
        host.setRootComponent(prompt);
    }

    bool isAnswered() const { return answered; }

    IPC::Messenger& messenger;
    NotifyUserPrompt prompt;
    UI::ComponentHost host;
    std::unique_ptr<Graphics::Window> window;
    bool answered = false;
};

NotifyUserServer::NotifyUserServer(const AppConfig& configToUse)
    : config(configToUse)
{
    try
    {
        server = std::make_unique<IPC::MessageServer>(proto::appServerName());
        server->onClient = [this](IPC::Messenger& messenger) { adoptClient(messenger); };
    }
    catch (const std::exception& error)
    {
        debugLog("[notify-user] cannot serve %s: %s\n",
                 proto::appServerName().c_str(),
                 error.what());
    }
}

NotifyUserServer::~NotifyUserServer()
{
    *alive = false;
    server.reset();
    openPrompts.clear();
}

void NotifyUserServer::adoptClient(IPC::Messenger& messenger)
{
    messenger.onMessage = [this, &messenger](const std::string& body)
    {
        if (const auto request = decodeNotifyUserRequest(body))
            showPromptWindowForRequest(messenger, *request);
    };

    messenger.onDisconnected = [this, &messenger] { closePromptLater(messenger); };
}

NotifyUserPaneLabel
    NotifyUserServer::labelPaneForRequest(const NotifyUserRequest& request) const
{
    auto pane = describePane(request.paneId);

    if (!pane)
        pane = NotifyUserPaneLabel {unknownPanePrefix + request.paneId, {}};

    if (request.title.empty())
        return *pane;

    auto subtitle = pane->title;

    if (!pane->subtitle.empty())
        subtitle += labelSeparator + pane->subtitle;

    return {request.title, subtitle};
}

void NotifyUserServer::showPromptWindowForRequest(IPC::Messenger& messenger,
                                                  const NotifyUserRequest& request)
{
    closePromptNow(messenger);

    const auto label = labelPaneForRequest(request);

    auto text = NotifyUserPromptText {};
    text.paneTitle = label.title;
    text.paneSubtitle = label.subtitle;
    text.content = request.content;
    text.options = request.options;
    text.allowsTypedResponse = request.allowsTypedResponse;

    auto& prompt = *openPrompts.emplace_back(
        std::make_unique<OpenPrompt>(messenger, config, std::move(text)));

    const auto paneId = request.paneId;
    prompt.prompt.onShowPane = [this, paneId] { onShowPane(paneId); };
    prompt.prompt.onAnswered = [this, &prompt](const std::string& answer)
    { answerPrompt(prompt, {answer}); };
    prompt.prompt.onDismissed = [this, &prompt] { answerPrompt(prompt, {}); };

    const auto widthAsFloat = static_cast<float>(promptWindowWidth);
    const auto height = prompt.prompt.calculateHeightForWidth(widthAsFloat);

    auto options = Graphics::WindowOptions {};
    options.title = windowTitlePrefix + label.title;
    options.width = promptWindowWidth;
    const auto heightInPoints = static_cast<int>(std::ceil(height));
    options.height = heightInPoints;
    options.minWidth = promptWindowMinimumWidth;
    options.minHeight = heightInPoints;
    options.isPrimary = false;
    options.alwaysOnTop = true;
    options.visibleOnAllWorkspaces = true;

    // Never steal the keyboard from whatever the user is typing into.
    options.showInactive = true;
    options.backgroundColor = toColor(themeByName(config.theme).background);
    options.onQuit = [this, &prompt] { answerPrompt(prompt, {}); };

    prompt.window = std::make_unique<Graphics::Window>(prompt.host, options);

    const auto cascadeIndex = promptsOpenedSinceAllClosed % cascadeLength;
    ++promptsOpenedSinceAllClosed;
    const auto cascadeIndexAsFloat = static_cast<float>(cascadeIndex);
    const auto cascadeOffset = cascadeStep * cascadeIndexAsFloat;
    const auto position = prompt.window->getPosition();
    prompt.window->setPosition({position.x + cascadeOffset, position.y + cascadeOffset});

    prompt.prompt.grabKeyboardFocus();

    onPromptOpened(request, label.title);
}

void NotifyUserServer::answerPrompt(OpenPrompt& prompt, const NotifyUserReply& reply)
{
    if (prompt.isAnswered())
        return;

    prompt.answered = true;
    prompt.messenger.send(encodeNotifyUserReply(reply));
    closePromptLater(prompt.messenger);
}

// Deferred: the request to close arrives inside the prompt's own event handler
// or the window's close, neither of which may destroy itself mid-call.
void NotifyUserServer::closePromptLater(const IPC::Messenger& messenger)
{
    for (auto& prompt: openPrompts)
        if (&prompt->messenger == &messenger && prompt->window != nullptr)
            prompt->window->setVisible(false);

    Threads::callAsync(
        [this, &messenger, guard = std::weak_ptr<bool> {alive}]
        {
            if (!guard.expired())
                closePromptNow(messenger);
        });
}

void NotifyUserServer::closePromptNow(const IPC::Messenger& messenger)
{
    std::erase_if(openPrompts,
                  [&](const std::unique_ptr<OpenPrompt>& prompt)
                  { return &prompt->messenger == &messenger; });

    if (openPrompts.empty())
        promptsOpenedSinceAllClosed = 0;
}
} // namespace term
