#include "KittyKeyboardFlags.h"

namespace term
{
void KittyKeyboardStack::push(std::uint8_t flags)
{
    top = (top + 1) % depth;
    entries[top] = flags & KittyKeyboard::allFlags;
}

void KittyKeyboardStack::pop(int count)
{
    if (count >= (int) depth)
    {
        clear();
        return;
    }

    for (auto index = 0; index < count; ++index)
    {
        entries[top] = 0;
        top = (top + depth - 1) % depth;
    }
}

void KittyKeyboardStack::change(std::uint8_t flags, KittyFlagsChange how)
{
    const auto masked = (std::uint8_t) (flags & KittyKeyboard::allFlags);

    switch (how)
    {
        case KittyFlagsChange::Replace:
            entries[top] = masked;
            return;
        case KittyFlagsChange::Add:
            entries[top] |= masked;
            return;
        case KittyFlagsChange::Remove:
            entries[top] &= (std::uint8_t) ~masked;
            return;
    }
}

void KittyKeyboardStack::restore(const Entries& savedEntries, std::size_t savedTop)
{
    for (std::size_t index = 0; index < depth; ++index)
        entries[index] = savedEntries[index] & KittyKeyboard::allFlags;

    top = savedTop < depth ? savedTop : 0;
}

void KittyKeyboardStack::clear()
{
    entries = {};
    top = 0;
}
} // namespace term
