#include <Multiplexer/PaneSizeOwner.h>
#include <Multiplexer/Protocol.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using term::PaneSizeOwner;
using term::PtySize;

namespace
{
constexpr auto mac = PaneSizeOwner::ClientId {1};
constexpr auto phone = PaneSizeOwner::ClientId {2};
constexpr auto otherMac = PaneSizeOwner::ClientId {3};
constexpr auto desktopSize = PtySize {150, 40};
constexpr auto widerDesktopSize = PtySize {160, 40};
constexpr auto phoneSize = PtySize {49, 25};
constexpr auto otherMacSize = PtySize {120, 30};

bool isSize(const std::optional<PtySize>& size, const PtySize& expected)
{
    return size && size->cols == expected.cols && size->rows == expected.rows;
}

PaneSizeOwner makePaneTheMacOwns()
{
    auto owner = PaneSizeOwner {};
    owner.claimSize(mac, desktopSize);
    return owner;
}
} // namespace

auto tFirstResizeTakesAnUnownedPane = test("PaneSizeOwner/firstResizeTakesAnUnownedPane") = []
{
    auto owner = PaneSizeOwner {};
    check(isSize(owner.requestSize(mac, desktopSize), desktopSize));
    check(owner.isOwnedBy(mac));
};

auto tOwnerResizesFreely = test("PaneSizeOwner/ownerResizesFreely") = []
{
    auto owner = makePaneTheMacOwns();
    check(isSize(owner.requestSize(mac, widerDesktopSize), widerDesktopSize));
};

auto tOthersResizeIsRememberedNotApplied = test("PaneSizeOwner/othersResizeIsRememberedNotApplied") = []
{
    auto owner = makePaneTheMacOwns();
    owner.claimSize(phone, phoneSize);

    check(!owner.requestSize(mac, widerDesktopSize).has_value());
    check(owner.isOwnedBy(phone));
    check(isSize(owner.reclaimSize(mac), widerDesktopSize));
};

auto tClaimTakesThePaneFromTheOwner = test("PaneSizeOwner/claimTakesThePaneFromTheOwner") = []
{
    auto owner = makePaneTheMacOwns();
    const auto applied = owner.claimSize(phone, phoneSize);

    check(applied.cols == phoneSize.cols);
    check(owner.isOwnedBy(phone));
    check(!owner.isOwnedBy(mac));
};

auto tReclaimByTheOwnerChangesNothing = test("PaneSizeOwner/reclaimByTheOwnerChangesNothing") = []
{
    auto owner = makePaneTheMacOwns();
    check(!owner.reclaimSize(mac).has_value());
};

auto tReclaimNeedsAnEarlierSize = test("PaneSizeOwner/reclaimNeedsAnEarlierSize") = []
{
    auto owner = makePaneTheMacOwns();
    check(!owner.reclaimSize(phone).has_value());
    check(owner.isOwnedBy(mac));
};

auto tOwnerLeavingHandsThePaneBack = test("PaneSizeOwner/ownerLeavingHandsThePaneBack") = []
{
    auto owner = makePaneTheMacOwns();
    owner.claimSize(phone, phoneSize);

    check(isSize(owner.forgetClient(phone), desktopSize));
    check(owner.isOwnedBy(mac));
};

auto tOwnerLeavingHandsBackToTheLatestEarlierOwner =
    test("PaneSizeOwner/ownerLeavingHandsBackToTheLatestEarlierOwner") = []
{
    auto owner = makePaneTheMacOwns();
    owner.claimSize(otherMac, otherMacSize);
    owner.claimSize(phone, phoneSize);

    check(isSize(owner.forgetClient(phone), otherMacSize));
    check(owner.isOwnedBy(otherMac));
};

auto tWatcherLeavingChangesNothing = test("PaneSizeOwner/watcherLeavingChangesNothing") = []
{
    auto owner = makePaneTheMacOwns();
    owner.requestSize(phone, phoneSize);

    check(!owner.forgetClient(phone).has_value());
    check(owner.isOwnedBy(mac));
};

auto tLastOwnerLeavingLeavesNoOwner = test("PaneSizeOwner/lastOwnerLeavingLeavesNoOwner") = []
{
    auto owner = makePaneTheMacOwns();

    check(!owner.forgetClient(mac).has_value());
    check(!owner.getOwner().has_value());
};

auto tNeighbouringVersionsTalk = test("Protocol/neighbouringVersionsTalk") = []
{
    constexpr auto ours = 5;
    constexpr auto older = 4;
    constexpr auto newer = 6;
    constexpr auto tooOld = 3;
    constexpr auto tooNew = 7;

    check(term::proto::isNeighbouringProtocolVersion(ours, ours));
    check(term::proto::isNeighbouringProtocolVersion(ours, older));
    check(term::proto::isNeighbouringProtocolVersion(ours, newer));
    check(!term::proto::isNeighbouringProtocolVersion(ours, tooOld));
    check(!term::proto::isNeighbouringProtocolVersion(ours, tooNew));
};
