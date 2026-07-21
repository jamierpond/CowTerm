#pragma once

#include <eacp/Network/TCP/Listener.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace term::web
{
// One accepted browser socket, RFC 6455 server side. Frames are read on a
// dedicated thread that holds a shared_ptr to this object, so the connection
// lives exactly as long as either the gateway or its own thread needs it.
// Callbacks fire on the reader thread — the gateway marshals to the main
// loop itself, alive-guarded, like every other cross-thread hop in the app.
class WsConnection : public std::enable_shared_from_this<WsConnection>
{
public:
    using Ptr = std::shared_ptr<WsConnection>;

    explicit WsConnection(eacp::TCP::Connection socketToUse);

    // Spawns the reader thread. Set the callbacks first.
    void start();

    // Thread-safe (serialized by a send lock); a dead socket drops silently
    // and the reader thread notices the break on its side.
    void sendText(std::string_view payload);
    void sendBinary(std::string_view payload);

    void close();
    bool isOpen() const { return open; }

    std::function<void(const std::string&)> onText = [](const std::string&) {};
    std::function<void()> onClosed = [] {};

private:
    void readLoop();
    void sendFrame(std::uint8_t opcode, std::string_view payload);
    std::string readExact(std::size_t count);

    eacp::TCP::Connection socket;
    std::mutex sendLock;
    std::atomic<bool> open {true};
};

// Accept loop + HTTP upgrade handshake on its own loopback port. Rejects
// anything that isn't a well-formed loopback websocket upgrade (the Host /
// Origin check is the DNS-rebinding guard — see DESIGN-WEBUI.md).
class WsListener
{
public:
    // Throws TCP::Error when the port is taken.
    explicit WsListener(int port);
    ~WsListener();

    int port() const;

    // Fires on the accept thread with a handshaken, not-yet-started
    // connection; the receiver wires callbacks and calls start().
    std::function<void(WsConnection::Ptr)> onConnection = [](WsConnection::Ptr) {};

private:
    void acceptLoop();

    eacp::TCP::Listener listener;
    std::atomic<bool> stopping {false};
    std::thread acceptor;
};
} // namespace term::web
