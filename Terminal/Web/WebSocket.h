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
// One live websocket, RFC 6455, either side of the wire: a server-accepted
// browser/peer socket (unmasked sends, masked receives required) or a
// client dial-out via wsConnectClient (masked sends). Frames are read on a
// dedicated thread that holds a shared_ptr to this object, so the connection
// lives exactly as long as either its owner or its own thread needs it.
// Callbacks fire on the reader thread — receivers marshal for themselves,
// alive-guarded, like every other cross-thread hop in the app.
class WsConnection : public std::enable_shared_from_this<WsConnection>
{
public:
    using Ptr = std::shared_ptr<WsConnection>;

    explicit WsConnection(eacp::TCP::Connection socketToUse,
                          bool maskOutboundToUse = false);

    // Spawns the reader thread. Set the callbacks first.
    void start();

    // Thread-safe (serialized by a send lock); a dead socket drops silently
    // and the reader thread notices the break on its side.
    void sendText(std::string_view payload);
    void sendBinary(std::string_view payload);

    void close();
    bool isOpen() const { return open; }

    std::function<void(const std::string&)> onText = [](const std::string&) {};

    // Unset, binary frames fall through to onText — the server treats every
    // inbound frame as a JSON control message. The native client sets this
    // to receive pane output.
    std::function<void(const std::string&)> onBinary;

    std::function<void()> onClosed = [] {};

private:
    void readLoop();
    void sendFrame(std::uint8_t opcode, std::string_view payload);
    std::string readExact(std::size_t count);

    eacp::TCP::Connection socket;
    std::mutex sendLock;
    std::atomic<bool> open {true};
    bool maskOutbound = false;
    std::uint64_t maskState = 0;
};

// Dials a gateway's websocket port and completes the client half of the
// RFC 6455 upgrade, returning a not-yet-started masked connection (wire
// callbacks, then start()). Throws TCP::Error on any network or handshake
// failure. Blocks up to the connect timeout — call it off the main thread.
WsConnection::Ptr wsConnectClient(const std::string& host, std::uint16_t port);

// Accept loop + HTTP upgrade handshake on its own port. When bound to
// loopback it also requires a loopback Host/Origin (the DNS-rebinding
// guard — see DESIGN-WEBUI.md); bound to the network, any Host is let
// through since peers legitimately dial by machine name.
class WsListener
{
public:
    // Throws TCP::Error when the port is taken.
    explicit WsListener(int port,
                        eacp::BindInterface bindTo = eacp::BindInterface::loopback);
    ~WsListener();

    int port() const;

    // Fires on the accept thread with a handshaken, not-yet-started
    // connection; the receiver wires callbacks and calls start().
    std::function<void(WsConnection::Ptr)> onConnection = [](WsConnection::Ptr) {};

private:
    void acceptLoop();

    eacp::TCP::Listener listener;
    bool requireLoopbackHost = true;
    std::atomic<bool> stopping {false};
    std::thread acceptor;
};
} // namespace term::web
