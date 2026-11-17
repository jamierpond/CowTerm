#include "PaneSizeOwner.h"

#include <algorithm>

namespace term
{
std::optional<PtySize> PaneSizeOwner::requestSize(ClientId client, const PtySize& size)
{
    requestedSizes[client] = size;

    if (owners.empty() || isOwnedBy(client))
        return takeOwnership(client);

    return std::nullopt;
}

PtySize PaneSizeOwner::claimSize(ClientId client, const PtySize& size)
{
    requestedSizes[client] = size;
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
    std::erase(owners, client);

    if (!wasOwner || owners.empty())
        return std::nullopt;

    return requestedSizes.at(owners.back());
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
