#pragma once

#include "CsiParameters.h"
#include "TermScreen.h"
#include "Utf8Decoder.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace term
{
// Feeds raw PTY bytes through a VT500-style state machine into a TermScreen:
// UTF-8 decoding, C0 controls, ESC/CSI/OSC dispatch, SGR (16/256/true color),
// DEC private modes and the DEC special-graphics charset. Query sequences
// (DA, DSR, OSC color queries) answer through respond, which writes back to
// the PTY.
class TermParser
{
public:
    TermParser(TermScreen& screenToUse, const Theme& themeToUse);

    void feed(std::string_view bytes);

    std::function<void(std::string_view)> respond = [](std::string_view) {};
    std::function<void(const std::string&)> onTitleChanged =
        [](const std::string&) {};
    std::function<void()> onBell = [] {};

    // OSC 7: the shell reporting its working directory (file:// URL).
    std::function<void(const std::string&)> onCwdChanged = [](const std::string&) {};

    // OSC 9 (or OSC 777;notify;title;body): a program inside the terminal
    // requesting a desktop notification.
    std::function<void(const std::string&)> onNotify = [](const std::string&) {};

    // Mode 2026 was set: the program is drawing a frame. The view holds its
    // repaints until the mode is reset, or for at most a second after this.
    std::function<void()> onSynchronizedOutputStarted = [] {};

    // DECSCUSR shape: 0/1 blinking block, 2 block, 3/4 underline, 5/6 bar.
    int cursorShape() const { return cursorStyle; }

private:
    friend struct TermParserActions;

    enum class State
    {
        Ground,
        Escape,
        Csi,
        Osc,
        StringSequence,
        Charset
    };

    void processByte(std::uint8_t byte);
    void processGroundByte(std::uint8_t byte);
    void processCsiByte(std::uint8_t byte);
    void collectCsiByte(std::uint8_t byte);
    void processOscByte(std::uint8_t byte);
    void processStringSequenceByte(std::uint8_t byte);
    void processCharsetByte(std::uint8_t byte);

    void executeControl(std::uint8_t byte);
    void startSequence(State next);
    void dispatchEscape(std::uint8_t byte);
    void dispatchCsi(std::uint8_t finalByte);
    void runCsi(std::uint8_t finalByte);
    void runPrivateCsi(std::uint8_t finalByte);
    void runCsiAction(std::uint8_t finalByte);
    void reportPrivateMode(int mode);
    void dispatchOsc();
    void setPrivateMode(int mode, bool on);
    void reportDeviceStatus();
    void putCodepoint(char32_t codepoint);

    TermScreen& screen;
    const Theme& theme;

    State state = State::Ground;
    Utf8Decoder utf8;

    CsiParameters params;
    char privateMarker = 0;
    char intermediate = 0;

    std::string oscBuffer;
    bool stringEscPending = false;

    int charsetTarget = 0;
    std::array<bool, 2> charsetSpecial = {false, false};
    int activeCharset = 0;

    int cursorStyle = 0;
};
} // namespace term
