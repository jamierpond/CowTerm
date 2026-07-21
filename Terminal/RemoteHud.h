#pragma once

#include "Config.h"
#include "Web/GatewayClient.h"
#include "Web/WebGateway.h"

#include <eacp/Graphics/View/View.h>

#include <string>
#include <vector>

namespace term
{
// The remote HUD (Ctrl+A r): every session on every CowTerm the config
// names, live over each machine's gateway websocket. Enter opens the
// selected session as a native mirror — the full pane tree in the session
// roster, first-class alongside local sessions; a activates it on the
// owner's screen instead. The fleet's links live for the app's lifetime
// and redial by themselves; the HUD is a view, never a connection manager.
class RemoteHud final : public eacp::Graphics::View
{
public:
    // The local gateway is only read for the status line: which port this
    // instance serves on, or that it failed to.
    RemoteHud(const AppConfig& configToUse,
              web::RemoteFleet& fleetToUse,
              const web::WebGateway& gatewayToUse);

    void show();
    bool isShown() const { return shown; }

    // The fleet moved (AppShell forwards its onChanged): refresh rows.
    void remoteChanged();

    eacp::Callback onClosed = [] {};

    // Chosen session, ready to open natively.
    std::function<void(web::GatewayClient&, const web::wire::SessionInfo&)>
        onOpenSession =
            [](web::GatewayClient&, const web::wire::SessionInfo&) {};

    void paint(eacp::Graphics::Context& context) override;
    void keyDown(const eacp::Graphics::KeyEvent& event) override;
    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;

private:
    struct Item
    {
        web::GatewayClient* client = nullptr;
        std::string sessionKey; // empty for an offline/idle placeholder row
        std::string name;
        std::string projectDir;
        int paneCount = 0;
        bool activeThere = false;
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
    web::RemoteFleet& fleet;
    const web::WebGateway& gateway;
    Theme theme;

    std::vector<Item> items;
    int selected = 0;
    bool shown = false;

    eacp::Graphics::Font headerFont;
    eacp::Graphics::Font rowFont;
    eacp::Graphics::Font detailFont;
};
} // namespace term
