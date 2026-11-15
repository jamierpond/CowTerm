#pragma once

#include <eacp/Core/Utils/Common.h>
#include <eacp/Network/IPC/Messenger.h>

#include <functional>
#include <memory>
#include <string>

namespace term
{
// One conversation with a session daemon: the local one over IPC, or one on
// another machine through `ssh <host> ct bridge`. Main-thread object.
class DaemonLink
{
public:
    virtual ~DaemonLink() = default;

    virtual bool isConnected() const = 0;
    virtual void send(const std::string& message) = 0;

    std::function<void(std::string)> onMessage = [](std::string) {};
    eacp::Callback onDisconnected = [] {};
};

class MessengerDaemonLink final : public DaemonLink
{
public:
    explicit MessengerDaemonLink(std::unique_ptr<eacp::IPC::Messenger> messengerToUse)
        : messenger(std::move(messengerToUse))
    {
        messenger->onMessage = [this](std::string body) { onMessage(std::move(body)); };
        messenger->onDisconnected = [this] { onDisconnected(); };
    }

    bool isConnected() const override { return messenger->isConnected(); }
    void send(const std::string& message) override { messenger->send(message); }

private:
    std::unique_ptr<eacp::IPC::Messenger> messenger;
};
} // namespace term
