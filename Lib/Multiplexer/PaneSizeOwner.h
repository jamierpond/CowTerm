#pragma once

#include "Shell/Pty.h"

#include <map>
#include <optional>
#include <set>
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

    // The client stopped showing the pane. When it owned it, the client that
    // owned it before (and still shows it) takes it back at its size; when no
    // other client owned it before, one that still shows it takes it.
    std::optional<PtySize> forgetClient(ClientId client);

    // The client stopped answering (a Mac asleep, a phone iOS suspended). It
    // holds the pane no longer, as if it had left, but keeps the size it asked
    // for, and nothing is handed to it until it is heard again.
    std::optional<PtySize> yieldSizeWhileSilent(ClientId client);

    // The silent client spoke again. It takes the pane back at its size when
    // nobody else holds it.
    std::optional<PtySize> markClientHeardAgain(ClientId client);

    std::optional<ClientId> getOwner() const;
    bool isOwnedBy(ClientId client) const;

private:
    PtySize takeOwnership(ClientId client);
    std::optional<PtySize> handOverToTheNextClient();

    std::map<ClientId, PtySize> requestedSizes;

    // Every client that has owned the pane, still shows it and still answers, latest last.
    std::vector<ClientId> owners;

    std::set<ClientId> silentClients;
};
} // namespace term
