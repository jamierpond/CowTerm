#include "SessionView.h"
#include "DaemonClient.h"

#include <algorithm>

namespace term
{
using namespace eacp;
using Graphics::Rect;

namespace
{
constexpr float gutter = 3.0f;
constexpr int maxTreeDepth = 16;

bool overlaps(float aStart, float aEnd, float bStart, float bEnd)
{
    return std::min(aEnd, bEnd) - std::max(aStart, bStart) > 1.0f;
}
} // namespace

SessionView::SessionView(const AppConfig& configToUse, std::string fallbackDirToUse)
    : config(configToUse)
    , fallbackDir(std::move(fallbackDirToUse))
    , theme(themeByName(configToUse.theme))
{
    setHandlesMouseEvents(true);

    // Added before any pane, so it stays behind them: subviews stack in the
    // order they arrive. The gutter colour is the clear colour, so the gutters
    // cost nothing to draw.
    chromeHost.setBackgroundColour(toColor(theme.paneBorder));
    chromeHost.setRootComponent(chrome);
    addSubview(chromeHost);
}

SessionView::~SessionView()
{
    *alive = false;
}

std::unique_ptr<SessionView::Node> SessionView::makeLeaf(const std::string& dir,
                                                         const std::string& shellId)
{
    auto node = std::make_unique<Node>();
    node->view = std::make_unique<TerminalView>(
        terminalConfig(config), dir.empty() ? fallbackDir : dir, shellId);

    // Tree surgery (split, sibling promotion) moves views between nodes, so
    // the callbacks resolve their node by view at fire time, never by a
    // captured node pointer.
    auto* viewPtr = node->view.get();

    node->view->onFocused = [this, viewPtr]
    {
        if (auto* leaf = leafFor(viewPtr); leaf != nullptr && active != leaf)
            setActive(leaf);
    };

    node->view->onShellExit = [this, viewPtr, guard = std::weak_ptr<bool> {alive}]
    {
        eacp::Threads::callAsync(
            [this, viewPtr, guard]
            {
                if (!guard.expired())
                    removeLeaf(leafFor(viewPtr));
            });
    };

    addSubview(*node->view);
    onPaneCreated(*node->view);
    return node;
}

void SessionView::restore(const std::vector<SavedPane>& saved)
{
    root = saved.empty() ? nullptr : buildFromSaved(saved, 0, 0);

    if (root == nullptr)
        root = makeLeaf(fallbackDir);

    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);
    active = leaves.empty() ? nullptr : leaves.front();
    layout();
}

std::unique_ptr<SessionView::Node> SessionView::buildFromSaved(
    const std::vector<SavedPane>& saved, int index, int depth)
{
    if (index < 0 || index >= (int) saved.size() || depth > maxTreeDepth)
        return nullptr;

    const auto& savedNode = saved[(std::size_t) index];

    if (!savedNode.split)
        return makeLeaf(savedNode.cwd, savedNode.shellId);

    auto first = buildFromSaved(saved, savedNode.first, depth + 1);
    auto second = buildFromSaved(saved, savedNode.second, depth + 1);

    if (first == nullptr || second == nullptr)
        return first != nullptr ? std::move(first) : std::move(second);

    auto node = std::make_unique<Node>();
    node->horizontal = savedNode.horizontal;
    node->ratio = std::clamp(savedNode.ratio, 0.1f, 0.9f);
    first->parent = node.get();
    second->parent = node.get();
    node->first = std::move(first);
    node->second = std::move(second);
    return node;
}

void SessionView::appendSnapshot(const Node& node, std::vector<SavedPane>& out) const
{
    const auto index = out.size();
    out.emplace_back();

    if (node.isLeaf())
    {
        out[index].cwd = node.view->workingDirectory();
        out[index].shellId = node.view->shellId();
        return;
    }

    out[index].split = true;
    out[index].horizontal = node.horizontal;
    out[index].ratio = node.ratio;

    out[index].first = (int) out.size();
    appendSnapshot(*node.first, out);

    out[index].second = (int) out.size();
    appendSnapshot(*node.second, out);
}

std::vector<SavedPane> SessionView::snapshot() const
{
    auto result = std::vector<SavedPane> {};

    if (root != nullptr)
        appendSnapshot(*root, result);

    return result;
}

TerminalView* SessionView::activePane()
{
    return active != nullptr ? active->view.get() : nullptr;
}

const TerminalView* SessionView::activePane() const
{
    return active != nullptr ? active->view.get() : nullptr;
}

int SessionView::paneCount() const
{
    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);
    return (int) leaves.size();
}

bool SessionView::isClaudeAnywhere() const
{
    return !claudePanes().empty();
}

std::vector<TerminalView*> SessionView::claudePanes() const
{
    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);

    auto contains = [](const std::string& text, const char* needle)
    {
        auto lowered = text;
        std::transform(lowered.begin(),
                       lowered.end(),
                       lowered.begin(),
                       [](unsigned char c) { return (char) std::tolower(c); });
        return lowered.find(needle) != std::string::npos;
    };

    auto panes = std::vector<TerminalView*> {};

    for (auto* leaf: leaves)
    {
        const auto process = leaf->view->foregroundProcess();
        const auto& title = leaf->view->currentTitle();

        if (contains(process, "claude") || contains(title, "claude"))
            panes.push_back(leaf->view.get());
    }

    return panes;
}

void SessionView::focusPane(TerminalView* pane)
{
    if (auto* leaf = leafFor(pane))
        setActive(leaf);
}

void SessionView::collectLeaves(Node* node, std::vector<Node*>& out) const
{
    if (node == nullptr)
        return;

    if (node->isLeaf())
    {
        out.push_back(node);
        return;
    }

    collectLeaves(node->first.get(), out);
    collectLeaves(node->second.get(), out);
}

SessionView::Node* SessionView::leafFor(const TerminalView* view) const
{
    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);

    for (auto* leaf: leaves)
        if (leaf->view.get() == view)
            return leaf;

    return nullptr;
}

void SessionView::setActive(Node* leaf)
{
    auto* previous = active;
    active = leaf;

    // Repaint both panes so the cursor swaps solid/hollow immediately
    // instead of on the next blink tick. A split moves the previous node's
    // view into a child, so it can be gone.
    if (previous != nullptr && previous != leaf && previous->view != nullptr)
        previous->view->refreshCursor();

    if (active != nullptr)
    {
        active->view->focus();
        active->view->refreshCursor();
    }

    onActivePaneChanged();

    // The ring follows the active pane, and focus moves without a relayout.
    updateChrome();
}

void SessionView::focusActive()
{
    if (active != nullptr)
        active->view->focus();
}

void SessionView::splitActive(bool horizontal)
{
    if (active == nullptr)
        return;

    zoomed = nullptr;

    auto* splitting = active;
    const auto dir = splitting->view->workingDirectory();

    // The leaf becomes a split: its terminal moves into the first child,
    // the fresh pane becomes the second and takes focus.
    auto firstChild = std::make_unique<Node>();
    firstChild->view = std::move(splitting->view);
    firstChild->parent = splitting;

    auto secondChild = makeLeaf(dir);
    secondChild->parent = splitting;

    splitting->horizontal = horizontal;
    splitting->ratio = 0.5f;
    splitting->first = std::move(firstChild);
    splitting->second = std::move(secondChild);

    layout();
    setActive(splitting->second.get());
}

void SessionView::removeLeaf(Node* leaf)
{
    if (leaf == nullptr || !leaf->isLeaf())
        return;

    zoomed = nullptr;
    removeSubview(*leaf->view);

    if (leaf == root.get())
    {
        root.reset();
        active = nullptr;
        onEmpty();
        return;
    }

    const auto closingActive = leaf == active;

    if (closingActive)
        active = nullptr;

    auto* parent = leaf->parent;
    auto sibling =
        std::move(parent->first.get() == leaf ? parent->second : parent->first);

    // The parent split collapses into the surviving subtree. The old
    // children (including `leaf`) are destroyed by the reassignments.
    parent->view = std::move(sibling->view);
    parent->horizontal = sibling->horizontal;
    parent->ratio = sibling->ratio;
    parent->first = std::move(sibling->first);
    parent->second = std::move(sibling->second);

    if (parent->first != nullptr)
        parent->first->parent = parent;

    if (parent->second != nullptr)
        parent->second->parent = parent;

    layout();

    if (closingActive)
    {
        auto leaves = std::vector<Node*> {};
        collectLeaves(parent, leaves);
        setActive(leaves.empty() ? nullptr : leaves.front());
    }
    else
    {
        onActivePaneChanged();
    }
}

void SessionView::closeActivePane()
{
    if (active != nullptr && active->isLeaf())
        active->view->terminateShell();

    removeLeaf(active);
}

void SessionView::terminateAll()
{
    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);

    for (auto* leaf: leaves)
        leaf->view->terminateShell();
}

void SessionView::focusDirection(char direction)
{
    if (active == nullptr)
        return;

    zoomed = nullptr;

    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);

    const auto& from = active->bounds;
    Node* best = nullptr;
    auto bestDistance = 1e9f;

    for (auto* leaf: leaves)
    {
        if (leaf == active)
            continue;

        const auto& to = leaf->bounds;
        auto distance = 1e9f;

        if (direction == 'h' && to.right() <= from.x + 1.0f
            && overlaps(from.y, from.y + from.h, to.y, to.y + to.h))
            distance = from.x - to.right();
        else if (direction == 'l' && to.x >= from.right() - 1.0f
                 && overlaps(from.y, from.y + from.h, to.y, to.y + to.h))
            distance = to.x - from.right();
        else if (direction == 'k' && to.y + to.h <= from.y + 1.0f
                 && overlaps(from.x, from.x + from.w, to.x, to.x + to.w))
            distance = from.y - (to.y + to.h);
        else if (direction == 'j' && to.y >= from.y + from.h - 1.0f
                 && overlaps(from.x, from.x + from.w, to.x, to.x + to.w))
            distance = to.y - (from.y + from.h);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = leaf;
        }
    }

    if (best != nullptr)
    {
        setActive(best);
        layout();
    }
}

void SessionView::resizeActive(char direction, float cells)
{
    if (active == nullptr)
        return;

    const auto wantHorizontal = direction == 'h' || direction == 'l';
    const auto grow = direction == 'l' || direction == 'j';

    // The nearest enclosing split on the matching axis is the one whose
    // ratio moves; which side the active pane sits on decides the sign.
    auto* child = active;
    auto* split = active->parent;

    while (split != nullptr && split->horizontal != wantHorizontal)
    {
        child = split;
        split = split->parent;
    }

    if (split == nullptr)
        return;

    const auto extent = wantHorizontal ? split->bounds.w : split->bounds.h;

    if (extent <= 0)
        return;

    // A "cell" of resize is ~9pt wide / ~18pt tall at the default font.
    const auto step = cells * (wantHorizontal ? 9.0f : 18.0f) / extent;
    const auto firstSide = split->first.get() == child;
    const auto delta = firstSide == grow ? step : -step;

    split->ratio = std::clamp(split->ratio + delta, 0.1f, 0.9f);
    layout();
}

void SessionView::setFontSize(float size)
{
    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);

    for (auto* leaf: leaves)
        leaf->view->setFontSize(size);
}

void SessionView::toggleZoom()
{
    zoomed = zoomed != nullptr ? nullptr : active;
    layout();
}

void SessionView::cycleFocus()
{
    auto leaves = std::vector<Node*> {};
    collectLeaves(root.get(), leaves);

    if (leaves.size() < 2)
        return;

    zoomed = nullptr;
    const auto current =
        std::find(leaves.begin(), leaves.end(), active) - leaves.begin();
    setActive(leaves[(std::size_t) ((current + 1) % (long) leaves.size())]);
    layout();
}

void SessionView::layoutNode(Node& node, Rect rect)
{
    node.bounds = rect;

    if (node.isLeaf())
    {
        if (zoomed != nullptr)
            rect = zoomed == &node ? getLocalBounds() : Rect {};

        node.view->setBounds(rect);
        return;
    }

    if (node.horizontal)
    {
        const auto firstWidth = std::floor(rect.w * node.ratio - gutter / 2);
        layoutNode(*node.first, rect.withWidth(firstWidth));
        layoutNode(*node.second,
                   Rect {rect.x + firstWidth + gutter,
                         rect.y,
                         rect.w - firstWidth - gutter,
                         rect.h});
    }
    else
    {
        const auto firstHeight = std::floor(rect.h * node.ratio - gutter / 2);
        layoutNode(*node.first, rect.withHeight(firstHeight));
        layoutNode(*node.second,
                   Rect {rect.x,
                         rect.y + firstHeight + gutter,
                         rect.w,
                         rect.h - firstHeight - gutter});
    }
}

void SessionView::layout()
{
    chromeHost.setBounds(getLocalBounds());

    if (root != nullptr)
        layoutNode(*root, getLocalBounds());

    updateChrome();

    repaint();
}

void SessionView::resized()
{
    layout();
}

void SessionView::Chrome::paint(eacp::UI::Graphics& g)
{
    if (!showActive)
        return;

    g.setColour(activeBorder);
    g.fillRect(activeBounds);
}

void SessionView::updateChrome()
{
    // A single pane, or a zoomed one, covers the view whole: there is no gutter
    // to show and no ring to put round the only thing on screen.
    const auto split = root != nullptr && !root->isLeaf() && zoomed == nullptr;

    chrome.showActive = split && active != nullptr;
    chrome.activeBorder = toColor(theme.paneBorderActive);

    if (chrome.showActive)
    {
        const auto b = active->bounds;
        chrome.activeBounds = {b.x - 1.5f, b.y - 1.5f, b.w + 3.0f, b.h + 3.0f};
    }

    chrome.repaint();
}
} // namespace term
