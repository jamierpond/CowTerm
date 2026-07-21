#pragma once

#include <Miro/Reflect.h>

#include <algorithm>
#include <cstdint>
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

struct SessionInfo
{
    std::string key;
    std::string name;
    std::string projectDir;
    bool active = false;
    bool claude = false;
    std::vector<PaneInfo> panes;

    MIRO_REFLECT(key, name, projectDir, active, claude, panes)
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

    MIRO_REFLECT(name, version, wsUrl)
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
    int cols = 0;
    int rows = 0;
    std::vector<SessionInfo> sessions;

    MIRO_REFLECT(ev, pane, message, cols, rows, sessions)
};

// One struct covers every client op; absent fields stay empty.
struct ClientOp
{
    std::string op;
    std::string pane;
    std::string data;
    std::string key;
    std::string dir;

    MIRO_REFLECT(op, pane, data, key, dir)
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
