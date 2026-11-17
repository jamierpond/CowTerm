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
    {KeyRowKey::Composer, "compose", noKeyCode, ""},
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
                   {KeyRowKey::Composer},
                   {KeyRowKey::HideKeyboard},
                   {KeyRowKey::NextPage}};

    if (page == 0)
        return firstPage;

    return secondPage;
}

// The compose switch is two keys wide, so its word fits on the narrowest phone.
int findKeyWidthInKeys(KeyRowKey key)
{
    constexpr auto composeKeyWidthInKeys = 2;

    if (key == KeyRowKey::Composer)
        return composeKeyWidthInKeys;

    return 1;
}

int countKeyWidths(const KeyGroups& groups)
{
    auto count = 0;

    for (const auto& group: groups)
    {
        for (const auto key: group)
            count += findKeyWidthInKeys(key);
    }

    return count;
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

void ComposerHistory::remember(const std::string& message)
{
    if (!message.empty())
        messages.push_back(message);

    recalledIndex.reset();
}

std::optional<std::string> ComposerHistory::recallOlder()
{
    if (messages.empty())
        return std::nullopt;

    if (!recalledIndex)
        recalledIndex = messages.size() - 1;
    else if (*recalledIndex > 0)
        --*recalledIndex;

    return messages[*recalledIndex];
}

std::optional<std::string> ComposerHistory::recallNewer()
{
    if (!recalledIndex)
        return std::nullopt;

    if (*recalledIndex + 1 >= messages.size())
    {
        recalledIndex.reset();
        return std::string {};
    }

    ++*recalledIndex;
    return messages[*recalledIndex];
}

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
    const auto keyWidths = static_cast<float>(countKeyWidths(groups));
    const auto keyWidth = std::min(widestKey, (row.w - gaps) / keyWidths);
    const auto rowWidth = keyWidth * keyWidths + gaps;

    auto x = row.x + (row.w - rowWidth) / 2.0f;
    auto slots = std::vector<KeyRowSlot> {};

    for (const auto& group: groups)
    {
        for (const auto key: group)
        {
            const auto width = keyWidth * static_cast<float>(findKeyWidthInKeys(key));
            slots.push_back({key, {x, row.y, width, row.h}});
            x += width;
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
