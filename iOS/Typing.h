#pragma once

#include <eacp/Graphics/Graphics/Keyboard.h>
#include <eacp/Graphics/Primitives/Primitives.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The phone's typing (docs/iOSDesign.md, "Typing"): the composer's history, the
// key row's keys and layout, and Ctrl that latches for one key. Plain logic,
// so the Mac's tests can check it.
namespace cowterm::mobile
{
// Earlier messages, for the up arrow in an empty composer: older on each up,
// back toward the newest on each down, and an empty field past the newest.
class ComposerHistory
{
public:
    void remember(const std::string& message);

    std::optional<std::string> recallOlder();
    std::optional<std::string> recallNewer();

    bool isRecalling() const { return recalledIndex.has_value(); }
    void stopRecalling() { recalledIndex.reset(); }

private:
    std::vector<std::string> messages;
    std::optional<std::size_t> recalledIndex;
};

enum class KeyRowKey
{
    Escape,
    Tab,
    Control,
    Up,
    Down,
    Left,
    Right,
    Pipe,
    Tilde,
    Slash,
    Minus,
    Composer,
    HideKeyboard,
    NextPage
};

std::string_view getKeyRowLabel(KeyRowKey key);

// The arrows repeat while held, as a hardware keyboard's do; the others press once.
bool isRepeatingKeyRowKey(KeyRowKey key);

// The key a key row key presses, as a hardware keyboard would report it.
// Empty for the row's own controls (Ctrl, the composer switch, hide, next page).
std::optional<eacp::Graphics::KeyEvent> makeKeyRowKeyEvent(KeyRowKey key);

struct KeyRowSlot
{
    KeyRowKey key = KeyRowKey::Escape;
    eacp::Graphics::Rect bounds;
};

inline constexpr auto keyRowPageCount = 2;

// The row's keys in groups, with a gap between groups: Esc alone at the left,
// far from the arrows at the right, so a thumb reaching for an arrow cannot
// interrupt Claude. Keys are 48 points wide where the row has room, narrower
// where it does not. The second page holds the symbols.
std::vector<KeyRowSlot> calculateKeyRowLayout(eacp::Graphics::Rect row, int page);

// The key under a finger; empty between keys.
std::optional<KeyRowKey> findKeyRowKeyAt(const std::vector<KeyRowSlot>& slots,
                                         eacp::Graphics::Point position);

// Ctrl latched for one key: the next key goes out with Ctrl held, then Ctrl
// lets go.
class ControlLatch
{
public:
    void toggle() { isLatched = !isLatched; }
    bool isHeld() const { return isLatched; }

    // The key with Ctrl applied when latched (and the latch released), or the
    // key as it was.
    eacp::Graphics::KeyEvent applyTo(const eacp::Graphics::KeyEvent& key);

private:
    bool isLatched = false;
};

// The key a typed character makes with Ctrl held: "c" becomes ETX (Ctrl+C),
// as a hardware keyboard reports it.
eacp::Graphics::KeyEvent makeControlKey(const eacp::Graphics::KeyEvent& key);

// A key event for text the software keyboard typed.
eacp::Graphics::KeyEvent makeTypedKeyEvent(const std::string& characters);
} // namespace cowterm::mobile
