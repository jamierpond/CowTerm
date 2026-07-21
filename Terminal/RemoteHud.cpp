#include "RemoteHud.h"

#include <eacp/Graphics/Primitives/TextMetrics.h>

#include <algorithm>

namespace term
{
using namespace eacp;
using Graphics::Color;
using Graphics::Context;
using Graphics::KeyEvent;
using Graphics::MouseEvent;
using Graphics::Point;
using Graphics::Rect;
namespace KeyCode = Graphics::KeyCode;

namespace
{
constexpr float panelWidth = 760.0f;
constexpr float rowHeight = 48.0f;
constexpr float headerHeight = 46.0f;
constexpr float hintHeight = 22.0f;
constexpr int maxRows = 12;

std::string truncated(const std::string& text, std::size_t max)
{
    if (text.size() <= max)
        return text;

    return text.substr(0, max - 1) + "…";
}
} // namespace

RemoteHud::RemoteHud(const AppConfig& configToUse,
                     web::RemoteFleet& fleetToUse,
                     const web::WebGateway& gatewayToUse)
    : config(configToUse)
    , fleet(fleetToUse)
    , gateway(gatewayToUse)
    , theme(themeByName(configToUse.theme))
    , headerFont({config.font, 16.0f})
    , rowFont({config.font, 14.0f})
    , detailFont({config.font, 12.0f})
{
    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);
}

void RemoteHud::show()
{
    selected = 0;
    shown = true;
    rebuild();
    repaint();
}

void RemoteHud::remoteChanged()
{
    if (shown)
    {
        rebuild();
        repaint();
    }
}

void RemoteHud::rebuild()
{
    const auto previous = selected < (int) items.size()
                              ? items[(std::size_t) selected].client->address()
                                    + items[(std::size_t) selected].sessionKey
                              : std::string {};
    items.clear();

    for (const auto& remote: fleet.all())
    {
        if (!remote->isConnected() || remote->sessions().empty())
        {
            auto item = Item {};
            item.client = remote.get();
            items.push_back(std::move(item));
            continue;
        }

        for (const auto& session: remote->sessions())
        {
            auto item = Item {};
            item.client = remote.get();
            item.sessionKey = session.key;
            item.name = session.name;
            item.projectDir = session.projectDir;
            item.paneCount = (int) session.panes.size();
            item.activeThere = session.active;
            item.claude = session.claude;
            items.push_back(std::move(item));
        }
    }

    if (!previous.empty())
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].client->address() + items[i].sessionKey == previous)
            {
                selected = (int) i;
                break;
            }

    selected = std::clamp(selected, 0, std::max((int) items.size() - 1, 0));
}

void RemoteHud::choose()
{
    if (selected >= (int) items.size())
    {
        cancel();
        return;
    }

    const auto item = items[(std::size_t) selected];

    if (item.sessionKey.empty())
        return; // an offline placeholder row

    // The roster entry may have moved under the HUD; open from live data.
    for (const auto& session: item.client->sessions())
    {
        if (session.key == item.sessionKey)
        {
            const auto info = session; // copy: onOpenSession may mutate rosters
            shown = false;
            onClosed();
            onOpenSession(*item.client, info);
            return;
        }
    }
}

void RemoteHud::activateSelected()
{
    if (selected >= (int) items.size())
        return;

    const auto& item = items[(std::size_t) selected];

    if (!item.sessionKey.empty())
        item.client->activate(item.sessionKey);
}

void RemoteHud::cancel()
{
    shown = false;
    onClosed();
}

void RemoteHud::moveSelection(int delta)
{
    if (!items.empty())
    {
        selected = (selected + delta + (int) items.size()) % (int) items.size();
        repaint();
    }
}

void RemoteHud::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        cancel();
        return;
    }

    if (event.keyCode == KeyCode::Return)
    {
        choose();
        return;
    }

    const auto& chars = event.charactersIgnoringModifiers;

    if (chars == "a")
    {
        activateSelected();
        return;
    }

    if (event.keyCode == KeyCode::UpArrow || chars == "k"
        || (event.modifiers.control && chars == "p"))
    {
        moveSelection(-1);
        return;
    }

    if (event.keyCode == KeyCode::DownArrow || chars == "j"
        || (event.modifiers.control && chars == "n"))
    {
        moveSelection(1);
        return;
    }
}

Rect RemoteHud::panelBounds() const
{
    const auto bounds = getLocalBounds();
    const auto width = std::min(panelWidth, bounds.w - 60.0f);
    const auto rows = std::min((int) items.size(), maxRows);
    const auto height = headerHeight + (float) std::max(rows, 1) * rowHeight
                        + hintHeight + 12.0f;

    return {
        (bounds.w - width) / 2.0f, std::max(bounds.h * 0.14f, 20.0f), width, height};
}

int RemoteHud::rowAt(Point pos) const
{
    const auto panel = panelBounds();
    const auto y = pos.y - (panel.y + headerHeight + 4.0f);

    if (pos.x < panel.x || pos.x > panel.right() || y < 0)
        return -1;

    const auto row = (int) (y / rowHeight);
    return row < std::min((int) items.size(), maxRows) ? row : -1;
}

void RemoteHud::mouseMoved(const MouseEvent& event)
{
    if (const auto row = rowAt(event.pos); row >= 0 && row != selected)
    {
        selected = row;
        repaint();
    }
}

void RemoteHud::mouseDown(const MouseEvent& event)
{
    const auto row = rowAt(event.pos);

    if (row >= 0)
    {
        selected = row;
        choose();
        return;
    }

    if (!panelBounds().contains(event.pos))
        cancel();
}

void RemoteHud::paint(Context& context)
{
    const auto panel = panelBounds();

    context.setColor(Color::black(0.38f));
    context.fillRect(getLocalBounds());

    context.setColor(toColor(theme.background).brighter(0.04f));
    context.fillRoundedRect(panel, 12.0f);

    context.setColor(toColor(theme.selection, 0.8f));
    context.setLineWidth(1.0f);
    context.strokeRect(panel);

    context.setColor(toColor(theme.ansi[6]));
    context.drawText("⇄ remote sessions",
                     {panel.x + 18.0f, panel.y + 30.0f},
                     headerFont);

    const auto online = (int) std::count_if(fleet.all().begin(),
                                            fleet.all().end(),
                                            [](const auto& remote)
                                            { return remote->isConnected(); });
    const auto summary = std::to_string(online) + "/"
                         + std::to_string(fleet.all().size()) + " online";
    const auto summaryWidth =
        Graphics::TextMetrics::measureWidth(summary, detailFont);
    context.setColor(online > 0 ? toColor(theme.ansi[2]) : toColor(theme.ansi[8]));
    context.drawText(
        summary, {panel.right() - summaryWidth - 16.0f, panel.y + 30.0f}, detailFont);

    context.setColor(toColor(theme.selection));
    context.drawLine({panel.x + 12.0f, panel.y + headerHeight - 2.0f},
                     {panel.right() - 12.0f, panel.y + headerHeight - 2.0f});

    const auto rows = std::min((int) items.size(), maxRows);

    for (auto i = 0; i < rows; ++i)
    {
        const auto& item = items[(std::size_t) i];
        const auto y = panel.y + headerHeight + 4.0f + (float) i * rowHeight;
        const auto rowRect =
            Rect {panel.x + 8.0f, y, panel.w - 16.0f, rowHeight - 4.0f};

        if (i == selected)
        {
            context.setColor(toColor(theme.selection, 0.85f));
            context.fillRoundedRect(rowRect, 6.0f);
        }

        const auto line1 = y + rowHeight * 0.40f;
        const auto line2 = y + rowHeight * 0.78f;

        if (item.sessionKey.empty())
        {
            context.setColor(toColor(theme.ansi[8]));
            context.drawText(item.client->address()
                                 + (item.client->isConnected()
                                        ? " — no sessions"
                                        : " — offline, retrying…"),
                             {rowRect.x + 12.0f, line1},
                             rowFont);
            continue;
        }

        context.setColor(item.claude ? toColor(theme.ansi[5])
                                     : toColor(theme.ansi[6]));
        context.drawText(item.claude ? "✳" : "⇄", {rowRect.x + 12.0f, line1}, rowFont);

        context.setColor(toColor(theme.foreground));
        context.drawText(
            truncated(item.name, 26), {rowRect.x + 36.0f, line1}, rowFont);

        const auto meta =
            item.client->hostName() + " · " + std::to_string(item.paneCount)
            + (item.paneCount == 1 ? " pane" : " panes")
            + (item.activeThere ? " · on their screen" : "");
        const auto metaWidth = Graphics::TextMetrics::measureWidth(meta, detailFont);
        context.setColor(toColor(theme.ansi[8]));
        context.drawText(
            meta, {rowRect.right() - metaWidth - 12.0f, line1}, detailFont);

        context.setColor(toColor(theme.ansi[8]));
        context.drawText(truncated(item.projectDir, 84),
                         {rowRect.x + 36.0f, line2},
                         detailFont);
    }

    if (items.empty())
    {
        context.setColor(toColor(theme.ansi[8]));
        context.drawText("no remotes configured — add \"remotes\": "
                         "[\"host:2697\"] to ~/.config/cowterm.json",
                         {panel.x + 18.0f, panel.y + headerHeight + rowHeight * 0.55f},
                         detailFont);
    }

    const auto local = gateway.isRunning()
                           ? "serving :" + std::to_string(gateway.port())
                                 + (gateway.servesNetwork() ? " (network)"
                                                            : " (local only)")
                           : std::string {config.webPort > 0
                                              ? "gateway OFF — port busy?"
                                              : "gateway disabled"};

    context.setColor(gateway.isRunning() ? toColor(theme.ansi[2])
                                         : toColor(theme.ansi[1]));
    context.drawText(local, {panel.x + 18.0f, panel.bottom() - 10.0f}, detailFont);

    const auto hints =
        std::string {"enter open here · a focus on their screen · esc close"};
    const auto hintsWidth = Graphics::TextMetrics::measureWidth(hints, detailFont);
    context.setColor(toColor(theme.ansi[8]));
    context.drawText(
        hints, {panel.right() - hintsWidth - 16.0f, panel.bottom() - 10.0f}, detailFont);
}
} // namespace term
