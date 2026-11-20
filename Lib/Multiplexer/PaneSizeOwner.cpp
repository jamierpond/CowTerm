#include "PaneSizeOwner.h"

#include <algorithm>

namespace term
{
std::optional<PtySize> PaneSizeOwner::requestSize(ClientId client, const PtySize& size)
{
    requestedSizes[client] = size;
    silentClients.erase(client);

    if (owners.empty() || isOwnedBy(client))
        return takeOwnership(client);

    return std::nullopt;
}

PtySize PaneSizeOwner::claimSize(ClientId client, const PtySize& size)
{
    requestedSizes[client] = size;
    silentClients.erase(client);
    return takeOwnership(client);
}

std::optional<PtySize> PaneSizeOwner::reclaimSize(ClientId client)
{
    if (isOwnedBy(client) || !requestedSizes.contains(client))
        return std::nullopt;

    return takeOwnership(client);
}

std::optional<PtySize> PaneSizeOwner::forgetClient(ClientId client)
{
    const auto wasOwner = isOwnedBy(client);
    requestedSizes.erase(client);
    silentClients.erase(client);
    std::erase(owners, client);

    if (!wasOwner)
        return std::nullopt;

    return handOverToTheNextClient();
}

std::optional<PtySize> PaneSizeOwner::yieldSizeWhileSilent(ClientId client)
{
    if (!requestedSizes.contains(client))
        return std::nullopt;

    const auto wasOwner = isOwnedBy(client);
    silentClients.insert(client);
    std::erase(owners, client);

    if (!wasOwner)
        return std::nullopt;

    return handOverToTheNextClient();
}

std::optional<PtySize> PaneSizeOwner::markClientHeardAgain(ClientId client)
{
    if (silentClients.erase(client) == 0 || !owners.empty())
        return std::nullopt;

    return takeOwnership(client);
}

std::optional<PtySize> PaneSizeOwner::handOverToTheNextClient()
{
    if (!owners.empty())
        return requestedSizes.at(owners.back());

    // A client that opened the pane while another held it never owned it;
    // it is the one still showing the pane, so it takes it.
    for (const auto& [client, size]: requestedSizes)
    {
        if (!silentClients.contains(client))
            return takeOwnership(client);
    }

    return std::nullopt;
}

std::optional<PaneSizeOwner::ClientId> PaneSizeOwner::getOwner() const
{
    if (owners.empty())
        return std::nullopt;

    return owners.back();
}

bool PaneSizeOwner::isOwnedBy(ClientId client) const
{
    return !owners.empty() && owners.back() == client;
}

PtySize PaneSizeOwner::takeOwnership(ClientId client)
{
    std::erase(owners, client);
    owners.push_back(client);
    return requestedSizes.at(client);
}
} // namespace term
