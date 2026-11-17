#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace term::phone
{
// How often and how recently each session was opened on the phone, kept in
// the app's container. The Mac keeps the same record in PaletteUsageStore,
// which needs emberstore, and emberstore does not build for iOS; this is the
// phone's own copy of that record, with the same arithmetic: an opening adds
// one to the usage decayed to now (frecencysearch::calculateDecayedUsage).
class SessionOpenings
{
public:
    explicit SessionOpenings(std::filesystem::path pathToUse);

    // Adds one opening of rowKey ("<host>/<session>") and saves.
    void recordOpening(const std::string& rowKey, std::int64_t nowMilliseconds);

    // rowKey's usage decayed to now; 0 for a session never opened here.
    double getUsage(const std::string& rowKey, std::int64_t nowMilliseconds) const;

    // When rowKey was last opened here; empty for a session never opened here.
    std::optional<std::int64_t> getLastOpeningMilliseconds(const std::string& rowKey) const;

private:
    struct Usage
    {
        double count = 0.0;
        std::int64_t atMilliseconds = 0;
    };

    void save() const;

    std::filesystem::path path;
    std::map<std::string, Usage> usageByRowKey;
};
} // namespace term::phone
