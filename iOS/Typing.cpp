#include "Typing.h"

#include <algorithm>
#include <array>

namespace cowterm::mobile
{
using eacp::Graphics::KeyEvent;
using eacp::Graphics::Point;
using eacp::Graphics::Rect;
namespace KeyCode = eacp::Graphics::KeyCode;

namespace
{
constexpr auto widestKey = 48.0f;
constexpr auto groupGap = 10.0f;
constexpr auto controlCodeMask = 0x1f;

struct KeyRowKeyInfo
{
    KeyRowKey key;
    std::string_view label;
    std::uint16_t keyCode;
    std::string_view characters;
};

constexpr auto noKeyCode = KeyCode::Unknown;

constexpr auto keyRowKeys = std::to_array<KeyRowKeyInfo>({
    {KeyRowKey::Escape, "Esc", KeyCode::Escape, "\x1b"},
    {KeyRowKey::Tab, "Tab", KeyCode::Tab, "\t"},
    {KeyRowKey::Control, "Ctrl", noKeyCode, ""},
    {KeyRowKey::Up, "\xe2\x86\x91", KeyCode::UpArrow, ""},
    {KeyRowKey::Down, "\xe2\x86\x93", KeyCode::DownArrow, ""},
    {KeyRowKey::Left, "\xe2\x86\x90", KeyCode::LeftArrow, ""},
    {KeyRowKey::Right, "\xe2\x86\x92", KeyCode::RightArrow, ""},
    {KeyRowKey::Pipe, "|", noKeyCode, "|"},
    {KeyRowKey::Tilde, "~", noKeyCode, "~"},
    {KeyRowKey::Slash, "/", noKeyCode, "/"},
    {KeyRowKey::Minus, "-", noKeyCode, "-"},
    {KeyRowKey::HideKeyboard, "\xe2\x8c\x84", noKeyCode, ""},
    {KeyRowKey::NextPage, "\xe2\x8b\xaf", noKeyCode, ""},
});

const KeyRowKeyInfo& findKeyRowKeyInfo(KeyRowKey key)
{
    return *std::ranges::find(keyRowKeys, key, &KeyRowKeyInfo::key);
}

using KeyGroups = std::vector<std::vector<KeyRowKey>>;

// Esc alone at the left end, the arrows at the right, the page key last.
const KeyGroups& findKeyGroups(int page)
{
    static const auto firstPage = KeyGroups {{KeyRowKey::Escape},
                                             {KeyRowKey::Tab, KeyRowKey::Control},
                                             {KeyRowKey::Up,
                                              KeyRowKey::Down,
                                              KeyRowKey::Left,
                                              KeyRowKey::Right},
                                             {KeyRowKey::NextPage}};

    static const auto secondPage =
        KeyGroups {{KeyRowKey::Pipe, KeyRowKey::Tilde, KeyRowKey::Slash, KeyRowKey::Minus},
                   {KeyRowKey::HideKeyboard},
                   {KeyRowKey::NextPage}};

    if (page == 0)
        return firstPage;

    return secondPage;
}

int countKeys(const KeyGroups& groups)
{
    auto count = std::size_t {0};

    for (const auto& group: groups)
        count += group.size();

    return static_cast<int>(count);
}

// A letter or one of @ [ \ ] ^ _ has a control code; anything else keeps its
// characters and only gains the modifier.
bool hasControlCode(char character)
{
    const auto isLetter = (character >= 'a' && character <= 'z')
                          || (character >= 'A' && character <= 'Z');
    const auto isControlPunctuation = std::string_view {"@[\\]^_"}.find(character)
                                      != std::string_view::npos;
    return isLetter || isControlPunctuation;
}
} // namespace

bool isRepeatingKeyRowKey(KeyRowKey key)
{
    return key == KeyRowKey::Up || key == KeyRowKey::Down || key == KeyRowKey::Left
           || key == KeyRowKey::Right;
}

std::string_view getKeyRowLabel(KeyRowKey key)
{
    return findKeyRowKeyInfo(key).label;
}

std::optional<KeyEvent> makeKeyRowKeyEvent(KeyRowKey key)
{
    const auto& info = findKeyRowKeyInfo(key);

    if (info.keyCode == noKeyCode && info.characters.empty())
        return std::nullopt;

    auto event = KeyEvent {};
    event.keyCode = info.keyCode;
    event.characters = std::string {info.characters};
    event.charactersIgnoringModifiers = event.characters;
    return event;
}

std::vector<KeyRowSlot> calculateKeyRowLayout(Rect row, int page)
{
    const auto& groups = findKeyGroups(page);
    const auto gaps = (float) (groups.size() - 1) * groupGap;
    const auto keyCount = static_cast<float>(countKeys(groups));
    const auto keyWidth = std::min(widestKey, (row.w - gaps) / keyCount);
    const auto rowWidth = keyWidth * keyCount + gaps;

    auto x = row.x + (row.w - rowWidth) / 2.0f;
    auto slots = std::vector<KeyRowSlot> {};

    for (const auto& group: groups)
    {
        for (const auto key: group)
        {
            slots.push_back({key, {x, row.y, keyWidth, row.h}});
            x += keyWidth;
        }

        x += groupGap;
    }

    return slots;
}

std::optional<KeyRowKey> findKeyRowKeyAt(const std::vector<KeyRowSlot>& slots, Point position)
{
    for (const auto& slot: slots)
    {
        if (slot.bounds.contains(position))
            return slot.key;
    }

    return std::nullopt;
}

KeyEvent ControlLatch::applyTo(const KeyEvent& key)
{
    if (!isLatched)
        return key;

    isLatched = false;
    return makeControlKey(key);
}

KeyEvent makeControlKey(const KeyEvent& key)
{
    auto withControl = key;
    withControl.modifiers.control = true;

    if (key.characters.size() != 1 || !hasControlCode(key.characters.front()))
        return withControl;

    const auto code = (char) (key.characters.front() & controlCodeMask);
    withControl.characters = std::string(1, code);
    return withControl;
}

KeyEvent makeTypedKeyEvent(const std::string& characters)
{
    auto event = KeyEvent {};
    event.keyCode = KeyCode::Unknown;
    event.characters = characters;
    event.charactersIgnoringModifiers = characters;
    return event;
}
} // namespace cowterm::mobile
