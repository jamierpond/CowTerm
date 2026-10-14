#pragma once

#include <eacp/Core/Utils/Common.h>

#include <memory>
#include <string>
#include <vector>

namespace term
{
// Calls onChange on the main thread when an entry is added to, removed from
// or renamed in one of the watched directories -- their direct children only,
// which is exactly what the ctrl+a f scan lists. One kqueue vnode source per
// directory on the main dispatch queue: no thread, no timer, nothing at idle.
// macOS only (DirWatcher-macOS.mm).
class DirWatcher
{
public:
    DirWatcher();
    ~DirWatcher();

    DirWatcher(const DirWatcher&) = delete;
    DirWatcher& operator=(const DirWatcher&) = delete;

    // Replaces the watched set. Directories that don't exist are skipped.
    void watch(const std::vector<std::string>& dirs);

    eacp::Callback onChange = [] {};

private:
    struct Native;
    std::unique_ptr<Native> native;
};
} // namespace term
