#include "MruStore.h"
#include "StorePruning.h"

#include <chrono>

namespace term
{
MruStore::MruStore(const emberstore::Database& db)
    : stamps(db.collection<Stamp>("mru"))
{
    prune();
}

void MruStore::touch(const std::string& key, std::int64_t atMs)
{
    stamps.doc(key).set({atMs});
}

std::int64_t MruStore::lastUsed(const std::string& key)
{
    if (auto stamp = stamps.get(key))
        return stamp->at;

    return 0;
}

void MruStore::prune()
{
    removeLowestRankedEntriesOverLimit(stamps,
                                       maxEntries,
                                       [this](const std::string& key) { return lastUsed(key); });
}

std::int64_t MruStore::nowMs()
{
    using namespace std::chrono;
    return (std::int64_t) duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}
} // namespace term
