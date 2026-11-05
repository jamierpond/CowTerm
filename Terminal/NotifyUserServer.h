#pragma once

#include "Config.h"
#include "NotifyUserProtocol.h"

#include <eacp/Core/Utils/Common.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eacp::IPC
{
class Messenger;
class MessageServer;
} // namespace eacp::IPC

namespace term
{
// How a prompt names its pane: the pane's title and where it is.
struct NotifyUserPaneLabel
{
    std::string title;
    std::string subtitle;
};

// The app's end of `ct notify-user`: serves proto::appServerName() and opens
// one always-on-top prompt window per request, answering the ct that asked
// when the user picks. A ct that goes away (Ctrl-C) takes its window with it.
// Main-thread object.
class NotifyUserServer
{
public:
    explicit NotifyUserServer(const AppConfig& configToUse);
    ~NotifyUserServer();

    std::function<std::optional<NotifyUserPaneLabel>(const std::string& paneId)>
        describePane = [](const std::string&) { return std::nullopt; };

    std::function<void(const std::string& paneId)> onShowPane =
        [](const std::string&) {};

    // A prompt just opened, titled as given; where the app rings back.
    std::function<void(const NotifyUserRequest& request, const std::string& title)>
        onPromptOpened = [](const NotifyUserRequest&, const std::string&) {};

private:
    struct OpenPrompt;

    void adoptClient(eacp::IPC::Messenger& messenger);
    void showPromptWindowForRequest(eacp::IPC::Messenger& messenger,
                                    const NotifyUserRequest& request);
    void answerPrompt(OpenPrompt& prompt, const NotifyUserReply& reply);
    void closePromptLater(const eacp::IPC::Messenger& messenger);
    void closePromptNow(const eacp::IPC::Messenger& messenger);
    NotifyUserPaneLabel labelPaneForRequest(const NotifyUserRequest& request) const;

    const AppConfig& config;
    std::vector<std::unique_ptr<OpenPrompt>> openPrompts;
    int promptsOpenedSinceAllClosed = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    // Last, so its sessions (which the prompts answer through) die first.
    std::unique_ptr<eacp::IPC::MessageServer> server;
};
} // namespace term
