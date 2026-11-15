#pragma once

#include "Multiplexer/Config.h"
#include "Multiplexer/NotifyUserProtocol.h"

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

    // Whoever asked, identified by an address that stays unique while the
    // request is open (a local ct's Messenger, a remote request's token),
    // and how to send it the encoded reply.
    struct Asker
    {
        const void* identity = nullptr;
        std::function<void(const std::string& encodedReply)> sendReply;
    };

    // A request that came another way than this machine's IPC: a `ct
    // notify-user` in a remote pane, relayed by that host's ct bridge.
    void showPromptForRequest(const Asker& asker, const NotifyUserRequest& request);

    // The asker went away (Ctrl-C, link lost): its window goes too.
    void closePromptFor(const void* identity);

private:
    struct OpenPrompt;

    void adoptClient(eacp::IPC::Messenger& messenger);
    void answerPrompt(OpenPrompt& prompt, const NotifyUserReply& reply);
    void closePromptLater(const void* identity);
    void closePromptNow(const void* identity);
    NotifyUserPaneLabel labelPaneForRequest(const NotifyUserRequest& request) const;

    const AppConfig& config;
    std::vector<std::unique_ptr<OpenPrompt>> openPrompts;
    int promptsOpenedSinceAllClosed = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    // Last, so its sessions (which the prompts answer through) die first.
    std::unique_ptr<eacp::IPC::MessageServer> server;
};
} // namespace term
