#pragma once

#include "Config.h"
#include "Web/GatewayClient.h"
#include "Web/WebGateway.h"

#include <eacp/Graphics/View/View.h>

#include <memory>
#include <string>
#include <vector>

namespace term
{
// The remote HUD (Ctrl+A r): every CowTerm named in the config's "remotes",
// each with its live sessions and panes streamed over that machine's
// gateway websocket — the exact conversation the browser client holds, so
// the two piloting surfaces can never diverge. Enter attaches the selected
// pane full-window (a RemoteShell popup); a activates its session on the
// remote GUI. The gateway links live for the app's lifetime and redial by
// themselves, so the HUD is a view, never a connection manager.
class RemoteHud final : public eacp::Graphics::View
{
public:
    // The local gateway is only read for the status line: which port this
    // instance serves on, or that it failed to.
    RemoteHud(const AppConfig& configToUse, const web::WebGateway& gatewayToUse);

    void show();
    bool isShown() const { return shown; }

    eacp::Callback onClosed = [] {};

    // Hands the app shell a ready-to-host remote pane; the shell drops it
    // into the popup.
    std::function<void(std::unique_ptr<Shell>)> onAttachPane =
        [](std::unique_ptr<Shell>) {};

    void paint(eacp::Graphics::Context& context) override;
    void keyDown(const eacp::Graphics::KeyEvent& event) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;

private:
    struct Item
    {
        web::GatewayClient* client = nullptr;
        std::string sessionKey;
        std::string sessionName;
        std::string paneId; // empty for an offline-remote placeholder row
        std::string title;
        std::string cwd;
        int cols = 0;
        int rows = 0;
        bool sessionActive = false;
        bool claude = false;
    };

    void rebuild();
    void choose();
    void activateSelected();
    void cancel();
    void moveSelection(int delta);
    int rowAt(eacp::Graphics::Point pos) const;
    eacp::Graphics::Rect panelBounds() const;

    const AppConfig& config;
    const web::WebGateway& gateway;
    Theme theme;

    std::vector<std::unique_ptr<web::GatewayClient>> remotes;
    std::vector<Item> items;
    int selected = 0;
    bool shown = false;

    eacp::Graphics::Font headerFont;
    eacp::Graphics::Font rowFont;
    eacp::Graphics::Font detailFont;
};
} // namespace term
