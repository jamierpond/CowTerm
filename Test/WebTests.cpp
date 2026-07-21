// Coverage for the web gateway's transport pieces, none of which need the
// GUI: the RFC 6455 handshake digest, a real client conversation against
// WsListener over loopback TCP, and the attach snapshot — serialized from
// one screen, parsed into a fresh one, and compared cell by cell.

#include "Web/ScreenSerializer.h"
#include "Web/Sha1.h"
#include "Web/WebSocket.h"
#include "Web/Wire.h"

#include "TermParser.h"

#include <NanoTest/NanoTest.h>

#include <eacp/Network/TCP/Connection.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace nano;
using namespace term;
using namespace std::chrono_literals;

namespace
{
std::string readExact(eacp::TCP::Connection& socket, std::size_t count)
{
    auto out = std::string {};

    while (out.size() < count)
    {
        auto chunk = socket.receive(count - out.size());
        check(!chunk.empty(), "peer closed early");
        out += chunk;
    }

    return out;
}

std::string maskedTextFrame(const std::string& payload)
{
    const char mask[4] = {0x12, 0x34, 0x56, 0x78};

    auto frame = std::string {};
    frame.push_back((char) 0x81);
    frame.push_back((char) (0x80 | (std::uint8_t) payload.size()));
    frame.append(mask, 4);

    for (std::size_t i = 0; i < payload.size(); ++i)
        frame.push_back((char) (payload[i] ^ mask[i % 4]));

    return frame;
}

std::string upgradeRequest(const std::string& host)
{
    return "GET / HTTP/1.1\r\n"
           "Host: "
           + host
           + "\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n";
}

std::string readHttpResponse(eacp::TCP::Connection& socket)
{
    auto response = std::string {};

    while (response.find("\r\n\r\n") == std::string::npos)
        response += socket.receiveUntil('\n') + "\n";

    return response;
}
} // namespace

auto sha1KnownAnswer = test("sha1 + base64 match the RFC 6455 example") = []
{
    // RFC 3174 test vector.
    const auto digest = web::sha1("abc");
    const auto expected = std::array<std::uint8_t, 20> {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    check(digest == expected, "sha1(abc)");

    // RFC 6455 §1.3 handshake example.
    check(web::websocketAcceptKey("dGhlIHNhbXBsZSBub25jZQ==")
              == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "accept key");
};

auto websocketConversation = test("WsListener talks RFC 6455 with a client") = []
{
    auto listener = web::WsListener {0};

    auto serverSide = std::atomic<web::WsConnection*> {nullptr};

    listener.onConnection = [&](web::WsConnection::Ptr connection)
    {
        connection->onText = [connection = connection.get()](const std::string& text)
        { connection->sendText("echo:" + text); };

        serverSide = connection.get();
        connection->start();
    };

    auto client = eacp::TCP::Connection::connect(
        {"127.0.0.1", (std::uint16_t) listener.port()});

    client.send(upgradeRequest("127.0.0.1"));

    const auto response = readHttpResponse(client);
    check(response.find("101") != std::string::npos, "101 response");
    check(response.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos,
          "accept header");

    client.send(maskedTextFrame("moo"));

    const auto header = readExact(client, 2);
    check((std::uint8_t) header[0] == 0x81, "fin+text frame");

    const auto length = (std::size_t) (std::uint8_t) header[1];
    check(readExact(client, length) == "echo:moo", "echo payload");

    // Close: the server answers the close frame and the reader thread ends.
    client.send("\x88\x80\x12\x34\x56\x78");
    const auto closeHeader = readExact(client, 2);
    check((std::uint8_t) closeHeader[0] == 0x88, "close reply");
};

auto websocketRejectsForeignHost = test("WsListener rejects a non-loopback Host") = []
{
    auto listener = web::WsListener {0};

    auto accepted = std::atomic<bool> {false};
    listener.onConnection = [&](web::WsConnection::Ptr) { accepted = true; };

    auto client = eacp::TCP::Connection::connect(
        {"127.0.0.1", (std::uint16_t) listener.port()});

    client.send(upgradeRequest("evil.example.com"));

    const auto response = readHttpResponse(client);
    check(response.find("400") != std::string::npos, "400 response");

    std::this_thread::sleep_for(50ms);
    check(!accepted, "no connection surfaced");
};

auto clientConversation = test("wsConnectClient talks to WsListener end to end") = []
{
    auto listener = web::WsListener {0};

    listener.onConnection = [](web::WsConnection::Ptr connection)
    {
        auto* raw = connection.get();

        // Echo text back as text; announce binary via a binary frame. The
        // reader thread keeps the connection alive on its own.
        connection->onText = [raw](const std::string& text)
        { raw->sendText("pong:" + text); };
        connection->onBinary = [raw](const std::string& bytes)
        { raw->sendBinary("bin:" + bytes); };
        connection->start();
    };

    auto client = web::wsConnectClient("127.0.0.1", listener.port());

    auto text = std::promise<std::string> {};
    auto binary = std::promise<std::string> {};
    client->onText = [&](const std::string& reply) { text.set_value(reply); };
    client->onBinary = [&](const std::string& reply) { binary.set_value(reply); };
    client->start();

    // The client's frames are masked (the server enforces RFC 6455 §5.1,
    // so an unmasked send would kill the conversation, not echo).
    client->sendText("moo");

    auto textFuture = text.get_future();
    check(textFuture.wait_for(3s) == std::future_status::ready, "text reply");
    check(textFuture.get() == "pong:moo", "text payload");

    client->sendBinary(std::string {"\x00\xff\x1b", 3});

    auto binaryFuture = binary.get_future();
    check(binaryFuture.wait_for(3s) == std::future_status::ready, "binary reply");
    check(binaryFuture.get() == "bin:" + std::string {"\x00\xff\x1b", 3},
          "binary payload");

    client->close();
};

auto wireShapes = test("wire structs and binary frames round-trip") = []
{
    // Control bytes (arrow keys are ESC sequences) must survive the JSON
    // hop — this is the exact payload shape the input op carries.
    auto op = term::web::wire::ClientOp {.op = "input",
                                         .pane = "abc123",
                                         .data = "\x1b[A\ttab\"quote"};
    auto parsed = term::web::wire::ClientOp {};
    Miro::fromJSONString(parsed, Miro::toJSONString(op));
    check(parsed.op == "input" && parsed.pane == "abc123", "op fields");
    check(parsed.data == "\x1b[A\ttab\"quote", "escaped payload");

    const auto frame = term::web::wire::binaryFrame(
        "pane1", std::string_view {"raw\x00里bytes", 12});
    const auto back = term::web::wire::parseBinaryFrame(frame);
    check(back.pane == "pane1", "frame pane id");
    check(back.payload == std::string_view {"raw\x00里bytes", 12}, "frame payload");
};

auto layoutRoundTrip = test("session layout survives the wire as a tree") = []
{
    // The shape a client must be able to rebuild: root splits horizontally
    // 70/30, the left half splits vertically. Three leaves, one per pane.
    auto session = term::web::wire::SessionInfo {};
    session.key = "/work/repo";
    session.name = "repo";
    session.layout = {
        {.split = true, .horizontal = true, .ratio = 0.7f, .first = 1, .second = 4},
        {.split = true, .horizontal = false, .ratio = 0.4f, .first = 2, .second = 3},
        {.pane = "aaa"},
        {.pane = "bbb"},
        {.pane = "ccc"},
    };
    session.panes = {{.id = "aaa", .cols = 80, .rows = 10},
                     {.id = "bbb", .cols = 80, .rows = 14},
                     {.id = "ccc", .cols = 40, .rows = 24}};

    auto parsed = term::web::wire::SessionInfo {};
    Miro::fromJSONString(parsed, Miro::toJSONString(session));

    check(parsed.layout.size() == 5, "node count");
    check(parsed.layout[0].split && parsed.layout[0].horizontal, "root split");
    check(parsed.layout[0].ratio > 0.69f && parsed.layout[0].ratio < 0.71f,
          "root ratio");
    check(parsed.layout[1].split && !parsed.layout[1].horizontal, "nested split");
    check(!parsed.layout[2].split && parsed.layout[2].pane == "aaa", "leaf pane id");

    // Walk it the way a client does; every leaf must resolve to a pane.
    auto leaves = std::vector<std::string> {};
    auto walk = [&](auto&& self, int index) -> void
    {
        const auto& node = parsed.layout[(std::size_t) index];

        if (!node.split)
        {
            leaves.push_back(node.pane);
            return;
        }

        self(self, node.first);
        self(self, node.second);
    };
    walk(walk, 0);

    check(leaves.size() == parsed.panes.size(), "one leaf per pane");

    for (const auto& id: leaves)
        check(std::any_of(parsed.panes.begin(),
                          parsed.panes.end(),
                          [&](const auto& p) { return p.id == id; }),
              "leaf resolves to a pane");
};

auto snapshotRoundTrip = test("serializeScreen reproduces the screen exactly") = []
{
    const auto theme = Theme {};

    auto source = TermScreen {40, 6, theme};
    auto sourceParser = TermParser {source, theme};

    // Colors, attributes, scrolled-off history, cursor placement and an
    // input-relevant mode all in one feed.
    sourceParser.feed("history line\r\n"
                      "\x1b[31mred\x1b[0m plain \x1b[1;44mboldblue\x1b[0m\r\n"
                      "\x1b[38;2;10;20;30mtruecolor\x1b[0m\r\n"
                      "\x1b[?2004h"
                      "\x1b[2;5H");

    auto replica = TermScreen {40, 6, theme};
    auto replicaParser = TermParser {replica, theme};
    replicaParser.feed(web::serializeScreen(source));

    for (auto row = 0; row < source.rows(); ++row)
    {
        const auto& sourceLine = source.lineAt(row, 0);
        const auto& replicaLine = replica.lineAt(row, 0);

        for (std::size_t col = 0; col < sourceLine.size(); ++col)
        {
            const auto& a = sourceLine[col];
            const auto& b = replicaLine[col];

            check(a.ch == b.ch, "cell char");

            if (a.ch == U' ' && (a.attrs & Attr::DefaultBg))
                continue; // trailing blanks may drop their stale fg

            check(a.fg == b.fg && a.bg == b.bg, "cell colors");
            check(a.attrs == b.attrs, "cell attrs");
        }
    }

    check(source.cursor.x == replica.cursor.x
              && source.cursor.y == replica.cursor.y,
          "cursor position");
    check(replica.modes.bracketedPaste, "bracketed paste replayed");
    check(!replica.modes.altScreen, "primary screen");
};

auto snapshotAltScreen = test("serializeScreen replays the alt screen modes") = []
{
    const auto theme = Theme {};

    auto source = TermScreen {20, 4, theme};
    auto sourceParser = TermParser {source, theme};

    sourceParser.feed("\x1b[?1049h\x1b[2J\x1b[Hfullscreen\x1b[?1000h\x1b[?1006h");

    auto replica = TermScreen {20, 4, theme};
    auto replicaParser = TermParser {replica, theme};
    replicaParser.feed(web::serializeScreen(source));

    check(replica.modes.altScreen, "alt screen");
    check(replica.modes.mouseButtons && replica.modes.mouseSgr, "mouse modes");
    check(replica.lineAt(0, 0)[0].ch == U'f', "alt content");
};
