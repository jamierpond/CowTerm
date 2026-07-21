#include "WebSocket.h"
#include "Sha1.h"

#include <eacp/Network/HTTP/HttpProtocol.h>

#include <cctype>
#include <random>

namespace term::web
{
using namespace eacp;

namespace
{
constexpr std::size_t maxFramePayload = 16 * 1024 * 1024;

namespace Op
{
constexpr std::uint8_t Continuation = 0x0;
constexpr std::uint8_t Text = 0x1;
constexpr std::uint8_t Binary = 0x2;
constexpr std::uint8_t Close = 0x8;
constexpr std::uint8_t Ping = 0x9;
constexpr std::uint8_t Pong = 0xa;
} // namespace Op

bool isLoopbackHost(const std::string& value)
{
    for (const auto* allowed: {"127.0.0.1", "localhost", "[::1]"})
        if (value.rfind(allowed, 0) == 0)
            return true;

    // A browser Origin arrives with its scheme attached.
    for (const auto* allowed:
         {"http://127.0.0.1", "http://localhost", "http://[::1]"})
        if (value.rfind(allowed, 0) == 0)
            return true;

    return false;
}
} // namespace

WsConnection::WsConnection(TCP::Connection socketToUse, bool maskOutboundToUse)
    : socket(std::move(socketToUse))
    , maskOutbound(maskOutboundToUse)
    , maskState(std::random_device {}())
{
}

void WsConnection::start()
{
    // The thread keeps the connection alive by itself; the gateway's list
    // is a second owner. Detached because the thread cannot join itself
    // and teardown is "close the socket, the read throws, the thread ends".
    std::thread([self = shared_from_this()] { self->readLoop(); }).detach();
}

void WsConnection::sendText(std::string_view payload)
{
    sendFrame(Op::Text, payload);
}

void WsConnection::sendBinary(std::string_view payload)
{
    sendFrame(Op::Binary, payload);
}

void WsConnection::sendFrame(std::uint8_t opcode, std::string_view payload)
{
    if (!open)
        return;

    auto frame = std::string {};
    frame.reserve(payload.size() + 14);
    frame.push_back((char) (0x80 | opcode));

    // Server frames go bare; client frames carry the RFC-mandated mask.
    const auto maskBit = maskOutbound ? 0x80 : 0x00;

    if (payload.size() < 126)
    {
        frame.push_back((char) (maskBit | payload.size()));
    }
    else if (payload.size() <= 0xffff)
    {
        frame.push_back((char) (maskBit | 126));
        frame.push_back((char) ((payload.size() >> 8) & 0xff));
        frame.push_back((char) (payload.size() & 0xff));
    }
    else
    {
        frame.push_back((char) (maskBit | 127));

        for (auto shift = 56; shift >= 0; shift -= 8)
            frame.push_back((char) ((payload.size() >> shift) & 0xff));
    }

    try
    {
        auto lock = std::scoped_lock {sendLock};

        if (maskOutbound)
        {
            // xorshift64 walks the mask state under the send lock; the mask
            // hides payload bytes from broken proxies, not from anyone.
            maskState ^= maskState << 13;
            maskState ^= maskState >> 7;
            maskState ^= maskState << 17;

            char mask[4];

            for (auto i = 0; i < 4; ++i)
                mask[i] = (char) ((maskState >> (i * 8)) & 0xff);

            frame.append(mask, 4);

            for (std::size_t i = 0; i < payload.size(); ++i)
                frame.push_back((char) (payload[i] ^ mask[i % 4]));
        }
        else
        {
            frame += payload;
        }

        socket.send(frame);
    }
    catch (const TCP::Error&)
    {
        open = false;
    }
}

void WsConnection::close()
{
    if (!open.exchange(false))
        return;

    try
    {
        auto lock = std::scoped_lock {sendLock};
        socket.close();
    }
    catch (const TCP::Error&)
    {
    }
}

std::string WsConnection::readExact(std::size_t count)
{
    auto out = std::string {};

    while (out.size() < count)
    {
        auto chunk = socket.receive(count - out.size());

        if (chunk.empty())
            throw TCP::Error {"peer closed"};

        out += chunk;
    }

    return out;
}

void WsConnection::readLoop()
{
    auto message = std::string {};
    auto messageOpcode = std::uint8_t {0};

    try
    {
        while (open)
        {
            const auto head = readExact(2);
            const auto b0 = (std::uint8_t) head[0];
            const auto b1 = (std::uint8_t) head[1];
            const auto fin = (b0 & 0x80) != 0;
            const auto opcode = (std::uint8_t) (b0 & 0x0f);
            const auto masked = (b1 & 0x80) != 0;
            auto length = (std::uint64_t) (b1 & 0x7f);

            if (length == 126)
            {
                const auto ext = readExact(2);
                length = ((std::uint64_t) (std::uint8_t) ext[0] << 8)
                         | (std::uint8_t) ext[1];
            }
            else if (length == 127)
            {
                const auto ext = readExact(8);
                length = 0;

                for (auto i = 0; i < 8; ++i)
                    length = (length << 8) | (std::uint8_t) ext[(std::size_t) i];
            }

            if (length > maxFramePayload)
                break;

            // On the server side clients must mask (RFC 6455 §5.1); a bare
            // frame is a broken peer, not a soft error. On the client side
            // the server's frames arrive bare.
            if (!masked && !maskOutbound)
                break;

            auto payload = std::string {};

            if (masked)
            {
                const auto mask = readExact(4);
                payload = readExact((std::size_t) length);

                for (std::size_t i = 0; i < payload.size(); ++i)
                    payload[i] = (char) (payload[i] ^ mask[i % 4]);
            }
            else
            {
                payload = readExact((std::size_t) length);
            }

            if (opcode == Op::Close)
            {
                sendFrame(Op::Close, {});
                break;
            }

            if (opcode == Op::Ping)
            {
                sendFrame(Op::Pong, payload);
                continue;
            }

            if (opcode == Op::Pong)
                continue;

            if (opcode == Op::Text || opcode == Op::Binary)
            {
                message = std::move(payload);
                messageOpcode = opcode;
            }
            else if (opcode == Op::Continuation)
            {
                message += payload;
            }
            else
            {
                break;
            }

            if (fin && messageOpcode != 0)
            {
                if (messageOpcode == Op::Binary && onBinary)
                    onBinary(message);
                else
                    onText(message);

                message.clear();
                messageOpcode = 0;
            }
        }
    }
    catch (const TCP::Error&)
    {
    }

    open = false;
    onClosed();
}

WsListener::WsListener(int port, BindInterface bindTo)
    : listener(TCP::Listener::bind(
          (std::uint16_t) port,
          // No accept deadline, and accepted sockets read without an io
          // timeout — an idle browser tab is not a dead peer.
          {.connect = Time::MS {0}, .io = Time::MS {0}},
          bindTo))
    , requireLoopbackHost(bindTo == BindInterface::loopback)
    , acceptor([this] { acceptLoop(); })
{
}

WsListener::~WsListener()
{
    stopping = true;

    // accept() sits in a deadline-free select that closing the fd from
    // another thread does not reliably interrupt (BSD semantics) — the
    // same shape as the PTY quit-hang this codebase already fought once.
    // Dial ourselves to hand the accept loop a connection; it then sees
    // `stopping` and returns.
    try
    {
        auto wake = TCP::Connection::connect(
            {"127.0.0.1", listener.port()},
            {.connect = Time::MS {500}, .io = Time::MS {500}});
    }
    catch (const TCP::Error&)
    {
    }

    listener.close();

    if (acceptor.joinable())
        acceptor.join();
}

int WsListener::port() const
{
    return listener.port();
}

namespace
{
bool handshake(TCP::Connection& socket, bool requireLoopbackHost);
} // namespace

void WsListener::acceptLoop()
{
    while (!stopping)
    {
        try
        {
            auto socket = listener.accept();

            // The handshake reads from the client, so it runs on the
            // connection's own thread — a client that connects and goes
            // silent must never block the accept loop (or teardown).
            // onConnection is copied because the thread may outlive this
            // listener; it only fires for handshakes that complete.
            std::thread(
                [socket = std::move(socket),
                 deliver = onConnection,
                 loopbackOnly = requireLoopbackHost]() mutable
                {
                    if (!handshake(socket, loopbackOnly))
                        return;

                    deliver(std::make_shared<WsConnection>(std::move(socket)));
                })
                .detach();
        }
        catch (const TCP::Error&)
        {
            // accept() throws when the listener closes underneath it
            // (shutdown) or on a transient socket error; only the former
            // ends the loop.
            if (stopping || !listener.isListening())
                return;
        }
    }
}

namespace
{
bool handshake(TCP::Connection& socket, bool requireLoopbackHost)
{
    try
    {
        auto parser = HTTP::RequestParser {};
        auto state = HTTP::RequestParser::State::NeedMore;

        while (state == HTTP::RequestParser::State::NeedMore)
        {
            const auto chunk = socket.receive();

            if (chunk.empty())
                return false;

            state = parser.feed(chunk.data(), chunk.size());
        }

        if (state != HTTP::RequestParser::State::Ready)
            return false;

        auto& request = parser.request();
        const auto upgrade =
            HTTP::findHeaderIgnoringCase(request.headers, "Upgrade");
        const auto key =
            HTTP::findHeaderIgnoringCase(request.headers, "Sec-WebSocket-Key");
        const auto host = HTTP::findHeaderIgnoringCase(request.headers, "Host");
        const auto origin =
            HTTP::findHeaderIgnoringCase(request.headers, "Origin");

        auto lowered = upgrade;

        for (auto& c: lowered)
            c = (char) std::tolower((unsigned char) c);

        const auto hostAllowed =
            !requireLoopbackHost
            || (isLoopbackHost(host) && (origin.empty() || isLoopbackHost(origin)));

        const auto valid = lowered.find("websocket") != std::string::npos
                           && !key.empty() && hostAllowed;

        if (!valid)
        {
            socket.send("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n"
                        "Content-Length: 0\r\n\r\n");
            return false;
        }

        socket.send("HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: "
                    + websocketAcceptKey(key) + "\r\n\r\n");
        return true;
    }
    catch (const TCP::Error&)
    {
        return false;
    }
}
} // namespace

WsConnection::Ptr wsConnectClient(const std::string& host, std::uint16_t port)
{
    // io stays unbounded: after the upgrade this socket idles between
    // frames, and a quiet remote is not a dead one.
    auto socket = TCP::Connection::connect(
        {host, port}, {.connect = Time::MS {5000}, .io = Time::MS {0}});

    std::uint8_t nonce[16];
    auto engine = std::mt19937 {std::random_device {}()};

    for (auto& byte: nonce)
        byte = (std::uint8_t) (engine() & 0xff);

    const auto key = base64(nonce, sizeof(nonce));

    socket.send("GET / HTTP/1.1\r\n"
                "Host: "
                + host + ":" + std::to_string(port)
                + "\r\n"
                  "Upgrade: websocket\r\n"
                  "Connection: Upgrade\r\n"
                  "Sec-WebSocket-Key: "
                + key
                + "\r\n"
                  "Sec-WebSocket-Version: 13\r\n\r\n");

    const auto statusLine = socket.receiveLine();

    if (statusLine.find("101") == std::string::npos)
        throw TCP::Error {"websocket upgrade refused: " + statusLine};

    auto acceptValue = std::string {};

    while (true)
    {
        const auto line = socket.receiveLine();

        if (line.empty())
            break;

        const auto colon = line.find(':');

        if (colon == std::string::npos)
            continue;

        auto name = line.substr(0, colon);

        for (auto& c: name)
            c = (char) std::tolower((unsigned char) c);

        if (name == "sec-websocket-accept")
        {
            acceptValue = line.substr(colon + 1);

            const auto start = acceptValue.find_first_not_of(' ');
            acceptValue = start == std::string::npos
                              ? std::string {}
                              : acceptValue.substr(start);
        }
    }

    if (acceptValue != websocketAcceptKey(key))
        throw TCP::Error {"websocket accept key mismatch"};

    return std::make_shared<WsConnection>(std::move(socket),
                                          /*maskOutbound*/ true);
}
} // namespace term::web
