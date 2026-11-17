#include "PairingScreen.h"

#include "Multiplexer/TextLayout.h"
#include "Pairing/SshKeyEncoding.h"

#include <eacp/Core/App/Clipboard.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Threads/ThreadPool.h>
#include <eacp/Core/Utils/Logging.h>

#include <algorithm>

namespace cowterm::mobile
{
using namespace eacp;
using eacp::Graphics::Rect;
using term::pairing::HostPairingStep;

namespace
{
constexpr auto fontFamily = "JetBrains Mono";

// JetBrains Mono's advance is 600 units of its 1000-unit em.
constexpr auto advanceShareOfFontSize = 0.6f;
constexpr auto lineHeightShareOfFontSize = 1.35f;

constexpr auto titleFontSize = 20.0f;
constexpr auto bodyFontSize = 14.0f;
constexpr auto keyFontSize = 11.0f;
constexpr auto fingerprintFontSize = 15.0f;
constexpr auto blockGap = 12.0f;
constexpr auto controlHeight = 44.0f;
constexpr auto controlGap = 8.0f;
constexpr auto connectButtonWidth = 104.0f;
constexpr auto qrCodeQuietModules = 2;
constexpr auto qrCodeCornerRadius = 6.0f;
constexpr auto mutedTextAlpha = 0.7f;
constexpr auto ansiBlue = 4;
constexpr auto ansiYellow = 3;

constexpr auto title = "Add a computer";
constexpr auto keyStep = "1. On the computer, add this phone's key to ~/.ssh/authorized_keys. Scan the code, or copy the key:";
constexpr auto hostStep = "2. Then type the computer as user@computer:";
constexpr auto hostPlaceholder = "you@computer";
constexpr auto needsDaemon = "The computer needs CowTerm's ct and its session daemon installed.";
constexpr auto couldNotConnect = "Could not connect: ";
constexpr auto connectingTo = "Connecting to ";
constexpr auto ellipsis = "\xe2\x80\xa6";
constexpr auto firstConnection = "This is the first connection to ";
constexpr auto offersKey = ". It shows this host key:";
constexpr auto checkItMatches = "Trust it only if this command, run on that computer, prints the same:";
constexpr auto keyChangedStart = "Not connecting: ";
constexpr auto keyChangedEnd = " shows a different host key from the one this phone trusted. It now shows:";
constexpr auto startAgainLabel = "Start again";
constexpr auto cancelLabel = "Cancel";

UI::Font makeFont(float size)
{
    return UI::Font {fontFamily, size};
}

float measureMonospaceWidth(const std::string& text, float fontSize)
{
    const auto codepoints = std::ranges::count_if(text, [](char byte) { return !term::isUtf8ContinuationByte(byte); });
    const auto codepointCount = static_cast<float>(codepoints);
    return codepointCount * fontSize * advanceShareOfFontSize;
}

struct PlacedText
{
    std::string text;
    float fontSize = bodyFontSize;
    bool isMuted = false;
    bool isWarning = false;
    Rect bounds;
};

// What the column shows for a step, top to bottom, and where the controls go.
struct ColumnPlan
{
    std::vector<PlacedText> lines;
    std::optional<Rect> copyKeyButton;
    std::optional<Rect> hostField;
    std::optional<Rect> connectButton;
    std::optional<Rect> trustButton;
    std::optional<Rect> cancelButton;
};

class ColumnPlanner
{
public:
    explicit ColumnPlanner(Rect columnToFill)
        : column(columnToFill)
    {
    }

    void addText(const std::string& text, float fontSize, bool isMuted = false, bool isWarning = false)
    {
        if (!isWideEnoughForText(fontSize))
            return;

        const auto lineHeight = fontSize * lineHeightShareOfFontSize;
        const auto measure = [fontSize](const std::string& line) { return measureMonospaceWidth(line, fontSize); };

        for (auto& line: term::wrapTextToWidth(text, column.w, measure))
            plan.lines.push_back({std::move(line), fontSize, isMuted, isWarning, column.removeFromTop(lineHeight)});

        column.removeFromTop(blockGap);
    }

    Rect addControlRow()
    {
        auto row = column.removeFromTop(controlHeight);
        column.removeFromTop(blockGap);
        return row;
    }

    ColumnPlan plan;

private:
    // wrapTextToWidth needs room for one character, or it never finishes a line.
    bool isWideEnoughForText(float fontSize) const { return column.w >= fontSize * advanceShareOfFontSize; }

    Rect column;
};

std::string describeDestination(const term::SshDestination& destination)
{
    return destination.user + "@" + destination.host;
}

struct RowHalves
{
    Rect left;
    Rect right;
};

RowHalves splitRowInTwo(Rect row)
{
    const auto halfWidth = (row.w - controlGap) / 2.0f;
    auto halves = RowHalves {};
    halves.left = row.removeFromLeft(halfWidth);
    row.removeFromLeft(controlGap);
    halves.right = row;
    return halves;
}

std::string describeHostEntryNote(const term::pairing::HostPairing& pairing)
{
    if (pairing.getStep() == HostPairingStep::Failed)
        return couldNotConnect + pairing.getMessage();

    return pairing.getMessage();
}

void planHostEntry(ColumnPlanner& planner,
                   const term::pairing::HostPairing& pairing,
                   const std::string& authorizedKeysLine,
                   bool isKeyboardUp)
{
    if (!isKeyboardUp)
    {
        planner.addText(keyStep, bodyFontSize, true);
        planner.addText(authorizedKeysLine, keyFontSize);
        planner.plan.copyKeyButton = planner.addControlRow();
    }

    planner.addText(hostStep, bodyFontSize, true);
    auto row = planner.addControlRow();
    planner.plan.connectButton = row.removeFromRight(connectButtonWidth);
    row.removeFromRight(controlGap);
    planner.plan.hostField = row;

    const auto note = describeHostEntryNote(pairing);

    if (note.empty())
        planner.addText(needsDaemon, bodyFontSize, true);
    else
        planner.addText(note, bodyFontSize, false, true);
}

void planFingerprintConfirmation(ColumnPlanner& planner, const term::pairing::HostPairing& pairing)
{
    planner.addText(firstConnection + pairing.getDestination()->host + offersKey, bodyFontSize);
    planner.addText(pairing.getFingerprint(), fingerprintFontSize);
    planner.addText(checkItMatches, bodyFontSize, true);
    planner.addText(term::pairing::makeHostKeyCheckCommand(pairing.getOfferedKeyBlob()), keyFontSize);

    const auto halves = splitRowInTwo(planner.addControlRow());
    planner.plan.cancelButton = halves.left;
    planner.plan.trustButton = halves.right;
}

void planChangedHostKey(ColumnPlanner& planner, const term::pairing::HostPairing& pairing)
{
    planner.addText(keyChangedStart + pairing.getDestination()->host + keyChangedEnd, bodyFontSize, false, true);
    planner.addText(pairing.getFingerprint(), fingerprintFontSize);
    planner.plan.cancelButton = planner.addControlRow();
}

// What the column shows for the current step; paint draws its text and
// placeControls puts the controls where it says.
ColumnPlan planColumn(const term::pairing::HostPairing& pairing,
                      const std::string& authorizedKeysLine,
                      Rect column,
                      bool isKeyboardUp)
{
    auto planner = ColumnPlanner {column};

    switch (pairing.getStep())
    {
        case HostPairingStep::EnteringHost:
        case HostPairingStep::Failed:
            planHostEntry(planner, pairing, authorizedKeysLine, isKeyboardUp);
            break;
        case HostPairingStep::FetchingHostKey:
            planner.addText(connectingTo + describeDestination(*pairing.getDestination()) + ellipsis, bodyFontSize);
            planner.plan.cancelButton = planner.addControlRow();
            break;
        case HostPairingStep::ConfirmingFingerprint:
            planFingerprintConfirmation(planner, pairing);
            break;
        case HostPairingStep::HostKeyChanged:
            planChangedHostKey(planner, pairing);
            break;
        case HostPairingStep::Paired:
            break;
    }

    return planner.plan;
}
} // namespace

PairingPanel::PairingPanel(const term::Theme& themeToUse,
                           const term::pairing::PairedHosts& pairedHosts,
                           std::string authorizedKeysLineToShow,
                           HostKeyFetcher fetchHostKeyToUse)
    : theme(themeToUse)
    , pairing(pairedHosts)
    , authorizedKeysLine(std::move(authorizedKeysLineToShow))
    , qrCode(makeQrCode(authorizedKeysLine))
    , fetchHostKey(std::move(fetchHostKeyToUse))
{
    const auto accent = term::toColor(theme.ansi[ansiBlue]);
    hostField.setFont(makeFont(bodyFontSize));
    hostField.setColour(term::toColor(theme.foreground));
    hostField.setAccentColour(accent);
    hostField.setPlaceholder(hostPlaceholder);
    hostField.onReturnKey = [this](const std::string&) { submitTypedHost(); };

    for (auto* button: {&copyKeyButton, &connectButton, &trustButton, &cancelButton})
        button->setAccentColour(accent);

    copyKeyButton.onClick = [this]
    {
        Clipboard::copyText(authorizedKeysLine);
        copyKeyButton.setText("Copied");
    };
    connectButton.onClick = [this] { submitTypedHost(); };
    trustButton.onClick = [this] { trustOfferedHostKey(); };
    cancelButton.onClick = [this] { startAgain(); };

    addChildren({hostField, copyKeyButton, connectButton, trustButton, cancelButton});
    showStep();
}

void PairingPanel::setScreenInsets(eacp::Graphics::Insets safeAreaToUse, float keyboardCoveredHeightToUse)
{
    safeArea = safeAreaToUse;
    keyboardCoveredHeight = keyboardCoveredHeightToUse;
    resized();
}

void PairingPanel::connectTo(const std::string& typedHost)
{
    hostField.setText(typedHost);
    submitTypedHost();
}

void PairingPanel::trustOfferedHostKey()
{
    pairing.confirm();
    showStep();
}

void PairingPanel::trustOfferedHostKeyOnceShown()
{
    trustsNextOfferedHostKey = true;
}

void PairingPanel::submitTypedHost()
{
    if (!pairing.submitHost(hostField.getText()))
        return showStep();

    hostField.giveAwayKeyboardFocus();
    const auto destination = *pairing.getDestination();
    LOG("pairing: reading the host key of ", describeDestination(destination));

    auto guard = std::weak_ptr<bool> {alive};
    fetchHostKey(destination,
                 [this, guard](const HostKeyAnswer& answer)
                 {
                     if (!guard.expired())
                         receiveHostKeyAnswer(answer);
                 });
    showStep();
}

void PairingPanel::receiveHostKeyAnswer(const HostKeyAnswer& answer)
{
    if (const auto* key = std::get_if<term::SshHostKey>(&answer))
        pairing.receiveHostKey(key->keyBlob);
    else
        pairing.receiveFailure(std::get<std::string>(answer));

    LOG("pairing: host key answer, fingerprint ", pairing.getFingerprint(), " ", pairing.getMessage());

    if (trustsNextOfferedHostKey && pairing.getStep() == HostPairingStep::ConfirmingFingerprint)
        return trustOfferedHostKey();

    showStep();
}

void PairingPanel::startAgain()
{
    pairing.startAgain();
    showStep();
}

bool PairingPanel::isShowingKey() const
{
    const auto step = pairing.getStep();
    return step == HostPairingStep::EnteringHost || step == HostPairingStep::Failed;
}

void PairingPanel::showStep()
{
    const auto step = pairing.getStep();

    if (step == HostPairingStep::Paired)
    {
        LOG("pairing: paired ", describeDestination(*pairing.getDestination()));
        return onPaired();
    }

    if (step == HostPairingStep::HostKeyChanged)
        cancelButton.setText(startAgainLabel);
    else
        cancelButton.setText(cancelLabel);

    resized();
}

void PairingPanel::resized()
{
    const auto areas = calculatePairingAreas(getLocalBounds(), safeArea, keyboardCoveredHeight, isShowingKey());
    placeControls(areas.column);
    repaint();
}

void PairingPanel::placeControls(Rect column)
{
    const auto plan = planColumn(pairing, authorizedKeysLine, column, keyboardCoveredHeight > 0.0f);

    const auto place = [](UI::Component& control, const std::optional<Rect>& bounds)
    {
        control.setVisible(bounds.has_value());

        if (bounds)
            control.setBounds(*bounds);
    };

    place(copyKeyButton, plan.copyKeyButton);
    place(hostField, plan.hostField);
    place(connectButton, plan.connectButton);
    place(trustButton, plan.trustButton);
    place(cancelButton, plan.cancelButton);
}

void PairingPanel::paint(UI::Graphics& g)
{
    g.setColour(term::toColor(theme.background));
    g.fillAll();

    const auto areas = calculatePairingAreas(getLocalBounds(), safeArea, keyboardCoveredHeight, isShowingKey());
    g.setFont(makeFont(titleFontSize));
    g.setColour(term::toColor(theme.foreground));
    g.drawText(title, areas.title, UI::Justification::Left);

    if (areas.qrCode)
        paintQrCode(g, *areas.qrCode);

    paintColumn(g, areas.column);
}

// Dark modules on a white square with a quiet zone: scanners want dark on light.
void PairingPanel::paintQrCode(UI::Graphics& g, Rect area) const
{
    if (!qrCode)
        return;

    const auto modulesAcross = static_cast<float>(qrCode->size + 2 * qrCodeQuietModules);
    const auto moduleSize = std::floor(area.w / modulesAcross);
    const auto drawnSide = moduleSize * modulesAcross;
    const auto origin = Graphics::Point {area.x + (area.w - drawnSide) / 2.0f, area.y + (area.h - drawnSide) / 2.0f};

    g.setColour(UI::Color {1.0f, 1.0f, 1.0f, 1.0f});
    g.fillRoundedRect({origin.x, origin.y, drawnSide, drawnSide}, qrCodeCornerRadius);
    g.setColour(UI::Color {0.0f, 0.0f, 0.0f, 1.0f});

    const auto quietZone = moduleSize * static_cast<float>(qrCodeQuietModules);

    for (auto row = 0; row < qrCode->size; ++row)
    {
        for (auto column = 0; column < qrCode->size; ++column)
        {
            if (!qrCode->isDark(column, row))
                continue;

            const auto x = origin.x + quietZone + moduleSize * static_cast<float>(column);
            const auto y = origin.y + quietZone + moduleSize * static_cast<float>(row);
            g.fillRect({x, y, moduleSize, moduleSize});
        }
    }
}

void PairingPanel::paintColumn(UI::Graphics& g, Rect column) const
{
    const auto plan = planColumn(pairing, authorizedKeysLine, column, keyboardCoveredHeight > 0.0f);
    const auto foreground = term::toColor(theme.foreground);

    for (const auto& line: plan.lines)
    {
        g.setFont(makeFont(line.fontSize));

        if (line.isWarning)
            g.setColour(term::toColor(theme.ansi[ansiYellow]));
        else if (line.isMuted)
            g.setColour(foreground.withAlpha(mutedTextAlpha));
        else
            g.setColour(foreground);

        g.drawText(line.text, line.bounds, UI::Justification::Left);
    }
}

PairingScreen::PairingScreen(const term::pairing::PairedHosts& pairedHosts,
                             std::string authorizedKeysLine,
                             HostKeyFetcher fetchHostKey)
    : panel(theme, pairedHosts, std::move(authorizedKeysLine), std::move(fetchHostKey))
{
    host.setBackgroundColour(term::toColor(theme.background));
    // The panel takes the safe area and the keyboard from calculatePairingAreas.
    host.setRespectsSafeArea(false);
    host.setRootComponent(panel);
    addChildren({host});
}

void PairingScreen::resized()
{
    host.setBounds(getLocalBounds());
    panel.setScreenInsets(getSafeAreaInsets(), keyboardCoveredHeight);
}

void PairingScreen::safeAreaInsetsChanged()
{
    panel.setScreenInsets(getSafeAreaInsets(), keyboardCoveredHeight);
}

void PairingScreen::setSoftwareKeyboardCoveredHeight(float coveredHeight)
{
    keyboardCoveredHeight = coveredHeight;
    panel.setScreenInsets(getSafeAreaInsets(), keyboardCoveredHeight);
}

void fetchHostKeyInBackground(const term::SshDestination& destination, std::function<void(HostKeyAnswer)> onAnswer)
{
    Threads::ThreadPool::shared().submit(
        [destination, onAnswer = std::move(onAnswer)]
        {
            auto answer = term::readSshHostKey(destination);
            Threads::callAsync([onAnswer, answer = std::move(answer)] { onAnswer(answer); });
        });
}
} // namespace cowterm::mobile
