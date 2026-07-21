#pragma once

#include <Miro/Reflect.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace term::web::wire
{
// The gateway protocol's message shapes, shared verbatim by the server
// (WebGateway) and the native client (GatewayClient) so the two ends can
// never drift. All Miro-reflected: the same tolerant JSON machinery as the
// config file, so additive protocol changes cost nothing on either side.
// Documented in DESIGN-WEBUI.md.

// Names this process for the lifetime of the run, so a gateway link can
// tell a genuine peer from this very instance answering its own dial.
// Address comparison cannot: "127.0.0.1", "localhost", the machine's LAN
// name and its ::1 all reach the same gateway, while two CowTerms sharing
// a machine on different ports are real peers that must keep working.
// Identity travels in the discovery document instead, where it survives
// every alias. Inline, so all translation units share the one static.
inline const std::string& localInstanceId()
{
    static const auto id = []
    {
        auto device = std::random_device {};
        auto engine = std::mt19937_64 {device()};
        char buffer[24];
        std::snprintf(
            buffer, sizeof(buffer), "%016llx", (unsigned long long) engine());
        return std::string {buffer};
    }();

    return id;
}

struct PaneInfo
{
    std::string id;
    std::string title;
    std::string cwd;
    int cols = 0;
    int rows = 0;
    bool active = false;

    MIRO_REFLECT(id, title, cwd, cols, rows, active)
};

// One node of a session's pane tree, mirroring the GUI's split geometry so
// a client can lay panes out exactly as the owning CowTerm does. `first`/
// `second` index into the same vector, node 0 is the root; leaves carry the
// pane id. horizontal means side by side; ratio is `first`'s share.
struct LayoutNode
{
    bool split = false;
    bool horizontal = false;
    float ratio = 0.5f;
    int first = -1;
    int second = -1;
    std::string pane;

    MIRO_REFLECT(split, horizontal, ratio, first, second, pane)
};

struct SessionInfo
{
    std::string key;
    std::string name;
    std::string projectDir;
    bool active = false;
    bool claude = false;
    std::vector<PaneInfo> panes;
    std::vector<LayoutNode> layout;

    MIRO_REFLECT(key, name, projectDir, active, claude, panes, layout)
};

struct SessionsEvent
{
    std::string ev = "sessions";
    std::vector<SessionInfo> sessions;

    MIRO_REFLECT(ev, sessions)
};

struct ServerInfo
{
    std::string name = "cowterm";
    std::string version = "0.1.0";
    std::string wsUrl;

    // Who is answering — see localInstanceId. Deliberately left empty here
    // rather than defaulted to our own id: this struct is also the shape a
    // *remote's* reply is parsed into, and a peer too old to send the field
    // would otherwise inherit our id and read as a self-link.
    std::string instanceId;

    // This instance's configured peers, so a browser hitting one CowTerm
    // can fan out to the whole fleet (the web UI's remotes come from here).
    std::vector<std::string> remotes;

    MIRO_REFLECT(name, version, wsUrl, instanceId, remotes)
};

struct AttachedEvent
{
    std::string ev = "attached";
    std::string pane;
    int cols = 0;
    int rows = 0;

    MIRO_REFLECT(ev, pane, cols, rows)
};

struct ExitEvent
{
    std::string ev = "exit";
    std::string pane;

    MIRO_REFLECT(ev, pane)
};

struct ErrorEvent
{
    std::string ev = "error";
    std::string message;

    MIRO_REFLECT(ev, message)
};

// Any server→client text frame, decoded just far enough to dispatch on ev.
struct AnyEvent
{
    std::string ev;
    std::string pane;
    std::string message;
    std::string key;
    int cols = 0;
    int rows = 0;
    std::vector<SessionInfo> sessions;

    MIRO_REFLECT(ev, pane, message, key, cols, rows, sessions)
};

// Answers an "open" op, naming the session that was created, so the
// caller can mirror the session it just asked for.
struct OpenedEvent
{
    std::string ev = "opened";
    std::string key;

    MIRO_REFLECT(ev, key)
};

// Ephemeral panes (popups) are the one case where the CLIENT owns the
// grid: nobody else is displaying them, so they size to the viewer.
struct ResizeOp
{
    std::string op = "resize";
    std::string pane;
    int cols = 0;
    int rows = 0;

    MIRO_REFLECT(op, pane, cols, rows)
};

// One struct covers every client op; absent fields stay empty.
//
// "command" carries a SessionCommand name in `command`, targeted at the
// session owning `pane` (which is focused first, so commands act on the
// pane the user is actually in — tmux semantics). `cells` scales resizes.
//
// "popup" runs `data` as a command in a fresh ephemeral pane on the
// serving machine, in the cwd of `pane`, sized `cols`x`rows`. The gateway
// answers with a "popup" event naming the new pane, which the client
// attaches like any other — that is how lazygit runs against the REMOTE
// repo while being displayed locally.
struct ClientOp
{
    std::string op;
    std::string pane;
    std::string data;
    std::string key;
    std::string dir;
    std::string command;
    float cells = 1.0f;
    int cols = 0;
    int rows = 0;

    MIRO_REFLECT(op, pane, data, key, dir, command, cells, cols, rows)
};

struct PopupEvent
{
    std::string ev = "popup";
    std::string pane;

    MIRO_REFLECT(ev, pane)
};

// Pane output rides binary frames as [u8 idLen][id][raw bytes].
inline std::string binaryFrame(const std::string& id, std::string_view data)
{
    auto frame = std::string {};
    frame.reserve(1 + id.size() + data.size());
    frame.push_back((char) (std::uint8_t) std::min<std::size_t>(id.size(), 255));
    frame += id.substr(0, 255);
    frame += data;
    return frame;
}

struct BinaryFrame
{
    std::string pane;
    std::string_view payload;
};

inline BinaryFrame parseBinaryFrame(std::string_view frame)
{
    if (frame.empty())
        return {};

    const auto idLength = (std::size_t) (std::uint8_t) frame[0];

    if (frame.size() < 1 + idLength)
        return {};

    return {std::string {frame.substr(1, idLength)}, frame.substr(1 + idLength)};
}
} // namespace term::web::wire
