#pragma once

#include "PairingLayout.h"
#include "QrCode.h"

#include "Emulator/TermTypes.h"
#include "Pairing/HostPairing.h"

#include <eacp/Graphics/View/View.h>
#include <eacp/UI/UI.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

// The phone adding a computer itself, for a build that `just ios-pair` cannot
// reach (TestFlight): its key to put in authorized_keys, as text and a QR
// code, a field for user@computer, and the host key's fingerprint to confirm
// on first connection.
namespace cowterm::mobile
{
using HostKeyAnswer = std::variant<term::SshHostKey, std::string>;

// Reads a host's key off the main thread and calls back on it.
using HostKeyFetcher =
    std::function<void(const term::SshDestination& destination, std::function<void(HostKeyAnswer)> onAnswer)>;

class PairingPanel final : public eacp::UI::Component
{
public:
    PairingPanel(const term::Theme& themeToUse,
                 const term::pairing::PairedHosts& pairedHosts,
                 std::string authorizedKeysLineToShow,
                 HostKeyFetcher fetchHostKeyToUse);

    void setScreenInsets(eacp::Graphics::Insets safeAreaToUse, float keyboardCoveredHeightToUse);

    // Types a host and presses Connect, as a finger would.
    void connectTo(const std::string& typedHost);

    // Presses Trust, as a finger would.
    void trustOfferedHostKey();

    // Presses Trust as soon as a host key waits for it, for a launch that cannot tap.
    void trustOfferedHostKeyOnceShown();

    void paint(eacp::UI::Graphics& g) override;
    void resized() override;

    std::function<void()> onPaired = [] {};

private:
    void submitTypedHost();
    void receiveHostKeyAnswer(const HostKeyAnswer& answer);
    void startAgain();
    void showStep();
    bool isShowingKey() const;
    void placeControls(eacp::Graphics::Rect column);
    void paintQrCode(eacp::UI::Graphics& g, eacp::Graphics::Rect area) const;
    void paintColumn(eacp::UI::Graphics& g, eacp::Graphics::Rect column) const;

    term::Theme theme;
    term::pairing::HostPairing pairing;
    std::string authorizedKeysLine;
    std::optional<QrCode> qrCode;
    HostKeyFetcher fetchHostKey;
    eacp::Graphics::Insets safeArea;
    float keyboardCoveredHeight = 0.0f;
    bool trustsNextOfferedHostKey = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    eacp::UI::TextEditor hostField;
    eacp::UI::Button copyKeyButton {"Copy key"};
    eacp::UI::Button connectButton {"Connect"};
    eacp::UI::Button trustButton {"Trust this computer"};
    eacp::UI::Button cancelButton {"Cancel"};
};

class PairingScreen final : public eacp::Graphics::View
{
public:
    PairingScreen(const term::pairing::PairedHosts& pairedHosts,
                  std::string authorizedKeysLine,
                  HostKeyFetcher fetchHostKey);

    void setSoftwareKeyboardCoveredHeight(float coveredHeight);
    PairingPanel& getPanel() { return panel; }

    void resized() override;
    void safeAreaInsetsChanged() override;

private:
    term::Theme theme;
    eacp::UI::ComponentHost host;
    PairingPanel panel;
    float keyboardCoveredHeight = 0.0f;
};

// readSshHostKey on a worker thread, answered on the main thread.
void fetchHostKeyInBackground(const term::SshDestination& destination, std::function<void(HostKeyAnswer)> onAnswer);
} // namespace cowterm::mobile
