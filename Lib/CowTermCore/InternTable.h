#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace term
{
// Values a screen's cells refer to by a small id (hyperlink URIs, styles,
// grapheme clusters), each stored once. Ids start at 1, so a cell's 0 means
// "none" (or, for styles, the default). When the table is full the screen
// scans its cells and calls keepOnly to drop what nothing uses.
template <typename Value, std::size_t capacity, typename Hash = std::hash<Value>>
class InternTable
{
public:
    using Id = std::uint32_t;
    static constexpr Id none = 0;

    // The id for value, adding it if it is new; none when full.
    Id intern(const Value& value)
    {
        if (const auto found = idsByValue.find(value); found != idsByValue.end())
            return found->second;

        if (isFull())
            return none;

        values.push_back(value);
        const auto id = (Id) values.size();
        idsByValue.emplace(value, id);
        return id;
    }

    bool holds(Id id) const { return id != none && id <= values.size(); }

    // The value behind an id holds() accepts.
    const Value& find(Id id) const { return values[id - 1]; }

    bool isFull() const { return values.size() >= capacity; }
    bool hasRoomFor(std::size_t count) const { return values.size() + count <= capacity; }
    std::size_t size() const { return values.size(); }

    void clear()
    {
        values.clear();
        idsByValue.clear();
    }

    // Drops every value whose id isUsed doesn't mark (indexed by id) and
    // renumbers the rest; returns each old id's new id.
    std::vector<Id> keepOnly(const std::vector<bool>& isUsed)
    {
        auto newIds = std::vector<Id>(values.size() + 1, none);
        auto kept = std::move(values);
        clear();

        for (std::size_t index = 0; index < kept.size(); ++index)
        {
            const auto oldId = index + 1;

            if (oldId < isUsed.size() && isUsed[oldId])
                newIds[oldId] = intern(kept[index]);
        }

        return newIds;
    }

private:
    std::vector<Value> values;
    std::unordered_map<Value, Id, Hash> idsByValue;
};
} // namespace term
