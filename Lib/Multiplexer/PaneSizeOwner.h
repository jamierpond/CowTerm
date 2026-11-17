#pragma once

#include "Shell/Pty.h"

#include <map>
#include <optional>
#include <vector>

namespace term
{
// Who sets a pane's size when several clients show it (the Mac, a phone,
// another Mac's bridge). The client that claimed it last owns it; the others'
// sizes are remembered and come back when they claim again or the owner goes.
// Each call that changes the pane's size returns the size to apply.
class PaneSizeOwner
{
public:
    using ClientId = int;

    // A client's plain resize: applied when it owns the pane or nobody does.
    std::optional<PtySize> requestSize(ClientId client, const PtySize& size);

    // The client takes the pane at this size, whoever owned it.
    PtySize claimSize(ClientId client, const PtySize& size);

    // The client takes the pane back at the size it last asked for. Empty
    // when it already owns the pane or never asked for a size.
    std::optional<PtySize> reclaimSize(ClientId client);

    // The client stopped showing the pane. When it owned it, the client that
    // owned it before (and still shows it) takes it back at its size; when no
    // other client owned it before, one that still shows it takes it.
    std::optional<PtySize> forgetClient(ClientId client);

    std::optional<ClientId> getOwner() const;
    bool isOwnedBy(ClientId client) const;

private:
    PtySize takeOwnership(ClientId client);

    std::map<ClientId, PtySize> requestedSizes;

    // Every client that has owned the pane and still shows it, latest last.
    std::vector<ClientId> owners;
};
} // namespace term
