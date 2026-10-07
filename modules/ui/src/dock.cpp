//
// koral-ui: docking, in the manner of the JetBrains IDEs' tool windows. One render object holds every
// panel as a child that stays put in the tree — so a panel keeps its state wherever it is shown — and
// lays each out where the layout says: in one of the areas round the edge of the space (down its left,
// down its right, along its bottom), in the middle, in a float over it, or — pulled out of the window —
// floating over the desktop, in one see-through window that covers it and lets the pointer through
// wherever no panel is.
//

#include <kui/dock.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

#include <app.h>
#include <commandBuffer.h>
#include <frameGraph.h>
#include <log.h>
#include <scene.h>
#include <window.h>

#include <kui/render.h>

#include "glass.h"

namespace kui
{
    namespace {
        /// What the layout itself goes by, which knows no style: the least a float is made, until the space
        /// that shows it says otherwise.
        constexpr float MinFloat = DockStyle {}.minFloatSize;
        /// Where the desktop's part of the space starts, in the space's own coordinates: far from the
        /// space itself, so one point is in one of them at most.
        constexpr float WindowSpacing = 100000.f;
        constexpr glm::vec2 OverlayOrigin { WindowSpacing, 0.f };
        /** Whether a point of the space's coordinates is one of the desktop's. */
        constexpr bool InOverlay(const glm::vec2 position) { return position.x >= WindowSpacing * 0.5f; }

        /**
         * What the panels that are open are laid out by: a tree of splits over leaves, made again from
         * the layout whenever that changes. A leaf is one area's open panel (the middle's: all of its
         * panels, as tabs), a float's panel, or the hole — the middle while nothing is in it.
         */
        struct Node {
            bool split = false;
            Axis axis = Axis::eHorizontal;
            float ratio = 0.5f;                 ///< The first child's share — unless extent says how big one of them is.
            float extent = -1.f;                ///< Not negative: how long one child is, whatever the other is left with.
            bool extentSecond = false;          ///< Which: the second child, rather than the first.
            std::function<void(float)> onResize;///< Told the ratio, or the extent, when the line between the two is dragged.
            std::unique_ptr<Node> a, b;
            std::vector<std::string> tabs;
            std::size_t active = 0;
            Node* parent = nullptr;
            bool tool = false;                  ///< A leaf that is an area round the edge: it can be hidden.
            bool center = false;                ///< The leaf that is the middle: its tabs are all of its panels.
            /// The middle while nothing is docked in it: a leaf with no panels, which shows what is
            /// behind the space and lets the pointer through.
            bool hole = false;

            // What the last layout made of it.
            bool visible = false;
            bool bare = false;                  ///< A float of one panel with no title bar: only its content.
            bool fit = false;                   ///< A float as big as what it shows: its panel was laid out to say how big.
            Rect rect {}, bar {}, body {}, line {};
            Rect custom {};                     ///< In the bar, between the titles and the buttons: the panel's own title bar widget.
            std::vector<std::size_t> shown;     ///< The tabs whose panels are there, by index into tabs.
            std::vector<Rect> tabRects;         ///< One per shown.
        };

        struct Floating {
            int id = 0;
            std::unique_ptr<Node> root;         ///< One leaf, of one panel.
            Rect rect {};
            bool window = false;                ///< Out of the window: over the desktop, in the overlay.
            bool fit = false;                   ///< As big as what it shows, rather than as big as its rect says.
        };

        /** The panels of one area — or, down a side, of one part of it — and which of them is open. */
        struct Group {
            std::vector<std::string> panels;    ///< In the order of their buttons.
            std::string shown;                  ///< The one that is open; none: the area is folded away.
            float weight = 1.f;                 ///< A side's part: its share of the side's height.
        };

        std::unique_ptr<Node> TabsOf(std::string panel)
        {
            auto node = std::make_unique<Node>();
            node->tabs.push_back(std::move(panel));
            return node;
        }

        bool Holds(const Group& group, const std::string& panel) { return std::ranges::find(group.panels, panel) != group.panels.end(); }
    }

    struct DockLayout::Impl {
        // Where each docked panel is. Down a side there may be several parts, one over the other; the
        // bottom has two, side by side; the middle one.
        std::vector<Group> left, right;
        Group bottomLeft, bottomRight, center;
        /// How wide the sides are and how tall the bottom is, in the space's units — or, negative, that
        /// share of the space, until the first layout says how big the space is.
        float leftWidth = -0.22f, rightWidth = -0.22f, bottomHeight = -0.3f;
        float bottomSplit = 0.5f;               ///< The bottom's left part's share of its width.

        std::unique_ptr<Node> root;             ///< Made from the above: see Rebuild.
        bool dirty = true;                      ///< The above changed since root was made.
        unsigned revision = 0;                  ///< Counts the times root was made: what points into it is stale after.

        std::vector<Floating> floats;           ///< Bottom to top.
        std::set<std::string> closed;
        struct Default {
            DockArea area = DockArea::eCenter;
            int part = 0;
            std::string beside;             ///< In the same group as this panel, when it is in that area.
            float share = 0.f;              ///< Not zero: how much of the space the area takes, when this is the first panel in it.
            std::optional<Rect> floating;
            bool fit = false;               ///< Floating, as big as what it shows: only where is said.
        };
        std::map<std::string, Default> defaults;
        int nextFloat = 1;
        /// How big each panel was when it last floated: what it goes back to when it floats again.
        std::map<std::string, glm::vec2> floatSize;
        /// And where: a panel that never docks, told to, stays where it floated.
        std::map<std::string, glm::vec2> floatAt;
        /// Where each panel was last docked: where the button that docks a float back puts it.
        std::map<std::string, DockArea> lastArea;
        std::function<void()> changed;          ///< The dock space showing it.

        void Changed() const { if (changed) changed(); }

        // ---- where a panel is ------------------------------------------------------------------------

        template <typename F> void EachGroup(F&& visit)
        {
            for (std::size_t i = 0; i < left.size(); ++i) visit(left[i], DockArea::eLeft, static_cast<int>(i));
            for (std::size_t i = 0; i < right.size(); ++i) visit(right[i], DockArea::eRight, static_cast<int>(i));
            visit(bottomLeft, DockArea::eBottomLeft, 0);
            visit(bottomRight, DockArea::eBottomRight, 0);
            visit(center, DockArea::eCenter, 0);
        }

        /** The group @p panel is docked in, with which area it is; null when it floats, or is nowhere. */
        Group* GroupOf(const std::string& panel, DockArea* area = nullptr, int* part = nullptr)
        {
            Group* found = nullptr;
            EachGroup([&](Group& group, const DockArea a, const int p) {
                if (found || !Holds(group, panel)) return;
                found = &group;
                if (area) *area = a;
                if (part) *part = p;
            });
            return found;
        }

        Floating* FloatOf(const std::string& panel)
        {
            for (auto& f : floats) if (f.root && !f.root->tabs.empty() && f.root->tabs.front() == panel) return &f;
            return nullptr;
        }

        bool Known(const std::string& panel) { return GroupOf(panel) != nullptr || FloatOf(panel) != nullptr; }

        Group& GroupAt(const DockArea area)
        {
            return area == DockArea::eBottomLeft ? bottomLeft : area == DockArea::eBottomRight ? bottomRight : center;
        }

        static Node* FindIn(Node* node, const std::string& panel)
        {
            if (!node) return nullptr;
            if (!node->split) return std::ranges::find(node->tabs, panel) != node->tabs.end() ? node : nullptr;
            if (Node* found = FindIn(node->a.get(), panel)) return found;
            return FindIn(node->b.get(), panel);
        }

        // ---- moving panels ---------------------------------------------------------------------------

        /** Takes @p panel out of wherever it is. */
        void Remove(const std::string& panel)
        {
            EachGroup([&](Group& group, const DockArea area, int) {
                if (std::erase(group.panels, panel) == 0) return;
                lastArea[panel] = area;
                // The middle always shows one of its panels; an area round the edge whose open panel went is folded away.
                if (group.shown == panel) group.shown = area == DockArea::eCenter && !group.panels.empty() ? group.panels.front() : std::string();
            });
            // A part of a side with nothing left in it is no part.
            std::erase_if(left, [](const Group& g) { return g.panels.empty(); });
            std::erase_if(right, [](const Group& g) { return g.panels.empty(); });
            if (const Floating* f = FloatOf(panel)) {
                // Its size is kept for when it floats again.
                floatSize[panel] = f->rect.Size();
                floatAt[panel] = f->rect.TopLeft();
                const int id = f->id;
                std::erase_if(floats, [id](const Floating& other) { return other.id == id; });
            }
            dirty = true;
        }

        /**
         * Docks @p panel (which is nowhere) in @p area, and opens it. Among the area's buttons it goes
         * before @p before, or after @p after, or last. Down a side those also say which part: theirs
         * — or, with @p newPart, a part of its own, under the one @p after is in (the first, with none).
         */
        void DockAt(const std::string& panel, const DockArea area, const std::string& before = {}, const std::string& after = {},
                    const bool newPart = false, const float share = 0.f)
        {
            const auto insert = [&](Group& group) {
                auto at = group.panels.end();
                if (!before.empty()) at = std::ranges::find(group.panels, before);
                else if (!after.empty()) { at = std::ranges::find(group.panels, after); if (at != group.panels.end()) ++at; }
                group.panels.insert(at, panel);
                group.shown = panel;
            };
            if (area == DockArea::eLeft || area == DockArea::eRight) {
                auto& parts = area == DockArea::eLeft ? left : right;
                const bool first = parts.empty();
                const auto partOf = [&](const std::string& other) {
                    for (std::size_t i = 0; i < parts.size(); ++i) if (Holds(parts[i], other)) return static_cast<int>(i);
                    return -1;
                };
                if (newPart || parts.empty()) {
                    const int at = parts.empty() || after.empty() ? 0 : partOf(after) + 1;
                    insert(*parts.insert(parts.begin() + at, Group {}));
                } else {
                    const int at = !before.empty() ? partOf(before) : !after.empty() ? partOf(after) : 0;
                    insert(parts[static_cast<std::size_t>(std::max(at, 0))]);
                }
                if (first && share > 0.f) (area == DockArea::eLeft ? leftWidth : rightWidth) = -share;
            } else {
                const bool first = bottomLeft.panels.empty() && bottomRight.panels.empty();
                insert(GroupAt(area));
                if (area != DockArea::eCenter && first && share > 0.f) bottomHeight = -share;
            }
            lastArea[panel] = area;
            dirty = true;
        }

        /** Docks @p panel in the @p part 'th part of @p area (a side's; the others have the one), last among its buttons. */
        void DockInPart(const std::string& panel, const DockArea area, const int part, const float share = 0.f)
        {
            if (area != DockArea::eLeft && area != DockArea::eRight) { DockAt(panel, area, {}, {}, false, share); return; }
            auto& parts = area == DockArea::eLeft ? left : right;
            const bool first = parts.empty();
            const auto at = static_cast<std::size_t>(std::max(part, 0));
            while (parts.size() <= at) parts.emplace_back();
            parts[at].panels.push_back(panel);
            parts[at].shown = panel;
            if (first && share > 0.f) (area == DockArea::eLeft ? leftWidth : rightWidth) = -share;
            lastArea[panel] = area;
            dirty = true;
        }

        Floating& FloatPanel(const std::string& panel, const Rect rect, const bool window)
        {
            Floating f;
            f.id = nextFloat++;
            f.root = TabsOf(panel);
            f.rect = rect;
            f.window = window;
            // Declared as big as what it shows: it is that wherever it floats, however it came to.
            if (const auto declared = defaults.find(panel); declared != defaults.end()) f.fit = declared->second.fit;
            floats.push_back(std::move(f));
            return floats.back();
        }

        /** A panel the layout has not placed: where its Dock or Float said, or the middle. */
        void Place(const std::string& panel)
        {
            const auto it = defaults.find(panel);
            if (it == defaults.end()) { DockAt(panel, DockArea::eCenter); return; }
            const Default& d = it->second;
            if (d.floating) { FloatPanel(panel, *d.floating, false); return; }
            DockArea beside = DockArea::eCenter;
            if (!d.beside.empty() && GroupOf(d.beside, &beside) && beside == d.area) DockAt(panel, d.area, {}, d.beside, false, d.share);
            else DockInPart(panel, d.area, d.part, d.share);
        }

        /** Opens @p panel in its area, in front of whatever was open there. */
        void Show(const std::string& panel)
        {
            if (Group* group = GroupOf(panel); group && group->shown != panel) { group->shown = panel; dirty = true; }
        }

        // ---- the tree the open panels are laid out by ---------------------------------------------------

        static std::unique_ptr<Node> Join(const Axis axis, std::unique_ptr<Node> a, std::unique_ptr<Node> b)
        {
            auto node = std::make_unique<Node>();
            node->split = true;
            node->axis = axis;
            a->parent = b->parent = node.get();
            node->a = std::move(a);
            node->b = std::move(b);
            return node;
        }

        /** A side's open parts, one over the other, each with its share of the side's height. */
        std::unique_ptr<Node> Column(std::vector<Group>& parts)
        {
            std::vector<std::size_t> open;
            for (std::size_t i = 0; i < parts.size(); ++i) if (!parts[i].shown.empty()) open.push_back(i);
            std::unique_ptr<Node> rest;
            for (std::size_t k = open.size(); k-- > 0;) {
                auto leaf = TabsOf(parts[open[k]].shown);
                leaf->tool = true;
                if (!rest) { rest = std::move(leaf); continue; }
                float under = 0.f;
                for (std::size_t j = k + 1; j < open.size(); ++j) under += parts[open[j]].weight;
                const float own = parts[open[k]].weight;
                auto split = Join(Axis::eVertical, std::move(leaf), std::move(rest));
                split->ratio = own + under > 0.f ? own / (own + under) : 0.5f;
                // Dragged: this part's share of itself and what is under it, which keep theirs of each other.
                split->onResize = [&parts, open, k](const float ratio) {
                    float below = 0.f;
                    for (std::size_t j = k + 1; j < open.size(); ++j) below += parts[open[j]].weight;
                    const float total = parts[open[k]].weight + below;
                    parts[open[k]].weight = ratio * total;
                    const float scale = below > 0.f ? (1.f - ratio) * total / below : 1.f;
                    for (std::size_t j = k + 1; j < open.size(); ++j) parts[open[j]].weight *= scale;
                };
                rest = std::move(split);
            }
            return rest;
        }

        /**
         * Makes root from where the panels are: the sides over the bottom, which runs from one stripe to
         * the other; between the sides the middle — its panels, or nothing, a hole the scene shows through.
         */
        void Rebuild()
        {
            dirty = false;
            ++revision;
            auto leftColumn = Column(left), rightColumn = Column(right);
            const auto tool = [](const Group& group) {
                auto leaf = TabsOf(group.shown);
                leaf->tool = true;
                return leaf;
            };
            std::unique_ptr<Node> top = std::make_unique<Node>();
            if (center.panels.empty()) {
                top->hole = true;
            } else {
                top->center = true;
                top->tabs = center.panels;
                const auto at = std::ranges::find(center.panels, center.shown);
                top->active = at != center.panels.end() ? static_cast<std::size_t>(at - center.panels.begin()) : 0;
            }
            const auto fixed = [](std::unique_ptr<Node> split, float& extent, const bool second) {
                split->extent = extent >= 0.f ? extent : 200.f;
                split->extentSecond = second;
                split->onResize = [&extent](const float value) { extent = value; };
                return split;
            };
            if (rightColumn) top = fixed(Join(Axis::eHorizontal, std::move(top), std::move(rightColumn)), rightWidth, true);
            if (leftColumn) top = fixed(Join(Axis::eHorizontal, std::move(leftColumn), std::move(top)), leftWidth, false);
            std::unique_ptr<Node> bottom;
            if (!bottomLeft.shown.empty() && !bottomRight.shown.empty()) {
                bottom = Join(Axis::eHorizontal, tool(bottomLeft), tool(bottomRight));
                bottom->ratio = bottomSplit;
                bottom->onResize = [this](const float ratio) { bottomSplit = ratio; };
            } else if (!bottomLeft.shown.empty()) {
                bottom = tool(bottomLeft);
            } else if (!bottomRight.shown.empty()) {
                bottom = tool(bottomRight);
            }
            if (bottom) top = fixed(Join(Axis::eVertical, std::move(top), std::move(bottom)), bottomHeight, true);
            // Nothing open anywhere: no tree at all.
            root = top->hole ? nullptr : std::move(top);
        }

        // ---- as text ---------------------------------------------------------------------------------

        /** A number as text whatever the process's locale says a decimal point is: what Load reads back. */
        static std::string Number(const float value)
        {
            char buffer[32];
            const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return error == std::errc() ? std::string(buffer, end) : std::string("0");
        }

        static void Quote(std::string& out, const std::string& s)
        {
            out += '"';
            for (const char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
            out += '"';
        }

        static void Write(std::string& out, const char* where, const Group& group)
        {
            if (group.panels.empty()) return;
            out += std::string("group ") + where + ' ' + Number(group.weight) + ' ';
            Quote(out, group.shown);
            for (const auto& panel : group.panels) { out += ' '; Quote(out, panel); }
            out += '\n';
        }

        struct Reader {
            std::string_view text;
            std::size_t at = 0;
            bool ok = true;

            void Space() { while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at; }
            std::string Word()
            {
                Space();
                const std::size_t start = at;
                while (at < text.size() && text[at] != ' ' && text[at] != '\n' && text[at] != '\r' && text[at] != '\t') ++at;
                return std::string(text.substr(start, at - start));
            }
            float Number()
            {
                const std::string word = Word();
                float value = 0.f;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
                const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
                if (error != std::errc() || word.empty()) ok = false;
#else
                // Apple's libc++ has floating-point from_chars only from macOS 26. As Koral's own parseNumber.h does.
                std::istringstream in(word);
                in.imbue(std::locale::classic());
                if (word.empty() || !(in >> value)) ok = false;
#endif
                return value;
            }
            std::optional<std::string> String()
            {
                Space();
                if (at >= text.size() || text[at] != '"') return std::nullopt;
                ++at;
                std::string out;
                while (at < text.size() && text[at] != '"') {
                    if (text[at] == '\\' && at + 1 < text.size()) ++at;
                    out += text[at++];
                }
                if (at >= text.size()) { ok = false; return std::nullopt; }
                ++at;
                return out;
            }
        };
    };

    DockLayout::DockLayout() : _impl(std::make_unique<Impl>()) {}
    DockLayout::~DockLayout() = default;

    DockLayout& DockLayout::Dock(std::string panel, const DockArea area, const int part)
    {
        _impl->defaults[panel] = { area, part, {}, 0.f, std::nullopt };
        if (_impl->Known(panel)) {
            _impl->Remove(panel);
            _impl->DockInPart(panel, area, part);
            _impl->Changed();
        }
        return *this;
    }

    DockLayout& DockLayout::Dock(std::string panel, const DockSide side, std::string relativeTo, const float fraction)
    {
        // Where the panel it is put beside is — or will be, when it has only been told where to go.
        DockArea beside = DockArea::eCenter;
        bool known = !relativeTo.empty() && _impl->GroupOf(relativeTo, &beside) != nullptr;
        if (!known && !relativeTo.empty())
            if (const auto told = _impl->defaults.find(relativeTo); told != _impl->defaults.end() && !told->second.floating) { beside = told->second.area; known = true; }

        Impl::Default where;
        where.share = fraction;
        switch (side) {
        case DockSide::eCenter:
            // A tab beside that panel: in its group, wherever that is.
            where.area = known ? beside : DockArea::eCenter;
            where.beside = relativeTo;
            break;
        case DockSide::eLeft:
        case DockSide::eTop:
            where.area = DockArea::eLeft;
            break;
        case DockSide::eRight:
            where.area = DockArea::eRight;
            break;
        case DockSide::eBottom:
            // Under what is on the right, the bottom's right part; otherwise its left.
            where.area = known && (beside == DockArea::eRight || beside == DockArea::eBottomRight) ? DockArea::eBottomRight : DockArea::eBottomLeft;
            break;
        }
        _impl->defaults[panel] = where;
        if (_impl->Known(panel)) {
            _impl->Remove(panel);
            _impl->Place(panel);
            _impl->Changed();
        }
        return *this;
    }

    DockLayout& DockLayout::Float(std::string panel, const Rect rect)
    {
        Impl::Default where;
        where.floating = rect;
        _impl->defaults[panel] = where;
        if (_impl->Known(panel)) {
            _impl->Remove(panel);
            _impl->FloatPanel(panel, rect, false);
            _impl->Changed();
        }
        return *this;
    }

    DockLayout& DockLayout::Float(std::string panel, const glm::vec2 at)
    {
        const Rect rect = Rect::XYWH(at.x, at.y, 0.f, 0.f);
        Impl::Default where;
        where.floating = rect;
        where.fit = true;
        _impl->defaults[panel] = where;
        if (_impl->Known(panel)) {
            _impl->Remove(panel);
            _impl->FloatPanel(panel, rect, false);
            _impl->Changed();
        }
        return *this;
    }

    DockLayout& DockLayout::PopOut(std::string panel, const glm::vec2 size)
    {
        if (_impl->Known(panel)) _impl->Remove(panel);
        _impl->defaults.erase(panel);
        _impl->FloatPanel(panel, Rect::XYWH(WindowSpacing + 80.f, 80.f, size.x, size.y), true);
        _impl->Changed();
        return *this;
    }

    void DockLayout::Close(const std::string& panel) { if (_impl->closed.insert(panel).second) _impl->Changed(); }
    void DockLayout::Open(const std::string& panel) { if (_impl->closed.erase(panel) > 0) { Activate(panel); _impl->Changed(); } }
    bool DockLayout::IsOpen(const std::string& panel) const { return !_impl->closed.contains(panel); }

    void DockLayout::Activate(const std::string& panel)
    {
        _impl->Show(panel);
        _impl->Changed();
    }

    void DockLayout::Hide(const std::string& panel)
    {
        DockArea area = DockArea::eCenter;
        Group* group = _impl->GroupOf(panel, &area);
        // The middle always shows one of its panels: there is nothing to fold it away to.
        if (!group || area == DockArea::eCenter || group->shown != panel) return;
        group->shown.clear();
        _impl->dirty = true;
        _impl->Changed();
    }

    bool DockLayout::IsShown(const std::string& panel) const
    {
        if (_impl->closed.contains(panel)) return false;
        if (_impl->FloatOf(panel)) return true;
        const Group* group = _impl->GroupOf(panel);
        return group && group->shown == panel;
    }

    bool DockLayout::IsFloating(const std::string& panel) const { return _impl->FloatOf(panel) != nullptr; }

    std::string DockLayout::Save() const
    {
        std::string out = "dock 2\n";
        out += "sizes " + Impl::Number(_impl->leftWidth) + ' ' + Impl::Number(_impl->rightWidth) + ' ' + Impl::Number(_impl->bottomHeight) + ' '
             + Impl::Number(_impl->bottomSplit) + '\n';
        for (const auto& group : _impl->left) Impl::Write(out, "left", group);
        for (const auto& group : _impl->right) Impl::Write(out, "right", group);
        Impl::Write(out, "bottom-left", _impl->bottomLeft);
        Impl::Write(out, "bottom-right", _impl->bottomRight);
        Impl::Write(out, "center", _impl->center);
        for (const auto& f : _impl->floats) {
            if (!f.root || f.root->tabs.empty()) continue;
            // No size is one as big as what it shows.
            out += "float " + Impl::Number(f.rect.left) + ' ' + Impl::Number(f.rect.top) + ' ' + Impl::Number(f.fit ? 0.f : f.rect.Width()) + ' '
                 + Impl::Number(f.fit ? 0.f : f.rect.Height()) + (f.window ? " 1 " : " 0 ");
            Impl::Quote(out, f.root->tabs.front());
            out += '\n';
        }
        if (!_impl->closed.empty()) {
            out += "closed";
            for (const auto& panel : _impl->closed) { out += ' '; Impl::Quote(out, panel); }
            out += '\n';
        }
        for (const auto& [panel, size] : _impl->floatSize) {
            out += "size ";
            Impl::Quote(out, panel);
            out += ' ' + Impl::Number(size.x) + ' ' + Impl::Number(size.y) + '\n';
        }
        return out;
    }

    bool DockLayout::Load(const std::string_view text)
    {
        Impl::Reader reader { text };
        if (reader.Word() != "dock" || reader.Number() != 2.f) return false;
        std::vector<Group> left, right;
        Group bottomLeft, bottomRight, center;
        float leftWidth = _impl->leftWidth, rightWidth = _impl->rightWidth, bottomHeight = _impl->bottomHeight, bottomSplit = _impl->bottomSplit;
        std::vector<Floating> floats;
        std::set<std::string> closed;
        std::map<std::string, glm::vec2> floatSize;
        int next = 1;
        while (reader.ok) {
            const std::string word = reader.Word();
            if (word.empty()) break;
            if (word == "sizes") {
                leftWidth = reader.Number();
                rightWidth = reader.Number();
                bottomHeight = reader.Number();
                bottomSplit = std::clamp(reader.Number(), 0.05f, 0.95f);
            } else if (word == "group") {
                const std::string where = reader.Word();
                Group group;
                group.weight = std::max(reader.Number(), 0.01f);
                auto shown = reader.String();
                if (!shown) { reader.ok = false; break; }
                group.shown = std::move(*shown);
                while (auto panel = reader.String()) group.panels.push_back(std::move(*panel));
                if (group.panels.empty()) { reader.ok = false; break; }
                if (!group.shown.empty() && !Holds(group, group.shown)) group.shown.clear();
                if (where == "left") left.push_back(std::move(group));
                else if (where == "right") right.push_back(std::move(group));
                else if (where == "bottom-left") bottomLeft = std::move(group);
                else if (where == "bottom-right") bottomRight = std::move(group);
                else if (where == "center") { if (group.shown.empty()) group.shown = group.panels.front(); center = std::move(group); }
                else reader.ok = false;
            } else if (word == "float") {
                Floating f;
                const float x = reader.Number(), y = reader.Number(), w = reader.Number(), h = reader.Number();
                f.fit = w <= 0.f || h <= 0.f;
                f.rect = f.fit ? Rect::XYWH(x, y, 0.f, 0.f) : Rect::XYWH(x, y, std::max(w, MinFloat), std::max(h, MinFloat));
                f.window = reader.Number() != 0.f;
                auto panel = reader.String();
                if (!panel) { reader.ok = false; break; }
                f.root = TabsOf(std::move(*panel));
                f.id = next++;
                floats.push_back(std::move(f));
            } else if (word == "closed") {
                while (auto panel = reader.String()) closed.insert(std::move(*panel));
            } else if (word == "size") {
                auto panel = reader.String();
                const float w = reader.Number(), h = reader.Number();
                if (panel) floatSize[std::move(*panel)] = { std::max(w, MinFloat), std::max(h, MinFloat) };
                else reader.ok = false;
            } else {
                reader.ok = false;
            }
        }
        if (!reader.ok) return false;
        _impl->left = std::move(left);
        _impl->right = std::move(right);
        _impl->bottomLeft = std::move(bottomLeft);
        _impl->bottomRight = std::move(bottomRight);
        _impl->center = std::move(center);
        _impl->leftWidth = leftWidth;
        _impl->rightWidth = rightWidth;
        _impl->bottomHeight = bottomHeight;
        _impl->bottomSplit = bottomSplit;
        _impl->floats = std::move(floats);
        _impl->closed = std::move(closed);
        _impl->floatSize = std::move(floatSize);
        _impl->nextFloat = next;
        _impl->dirty = true;
        _impl->Changed();
        return true;
    }

    // ---- the window over the desktop that panels are pulled out into -----------------------------------

    namespace {
        /** @brief Clears the screen to nothing at all: what the overlay shows where no panel is. */
        class ClearToNothing final : public kor::RenderPass {
        public:
            ClearToNothing() : RenderPass("Clear") {}
            void Setup(kor::PassBuilder& builder) override { builder.Write(kor::FrameGraph::Screen, kor::Image::Usage::eTransferDst); }
            void Initialize(const kor::PassResources& resources) override { _screen = resources.ImageNamed(kor::FrameGraph::Screen); }
            void Record(kor::CommandBuffer& commands) const override { commands.ClearColorImage(_screen, glm::vec4(0.f)); }

        private:
            kor::ResourceRef<const kor::Image> _screen;
        };

        /** @brief A scene that shows one layer over nothing: the panels out of a dock space, recorded by it. */
        class DockWindow final : public kor::Scene {
        public:
            void Initialize() override
            {
                Graph().Add<ClearToNothing>();
                Graph().Add<UiPass>(renderer);
            }
            void Update() override
            {
                const glm::uvec2 extent = Window::Extent();
                if (extent != _last) { _last = extent; if (onResized) onResized(); }
                if (onFrame) onFrame();
            }
            void Shutdown() override { if (onGone) onGone(); }

            Renderer renderer;
            std::function<void()> onResized, onFrame, onGone;

        private:
            glm::uvec2 _last {};
        };

        class RenderDock final : public RenderContainer {
        public:
            /// titleBar: whether it has a widget of its own in its title bar — the children after the panels' contents.
            struct Meta { std::string id, title; bool closable = true, dockable = true, showTitleBar = true; std::shared_ptr<const VectorImage> icon; bool titleBar = false; };
            struct Config { std::shared_ptr<DockLayout> layout; std::vector<Meta> panels; DockOptions options; };

            ~RenderDock() override
            {
                if (_config.layout) _config.layout->Internal().changed = nullptr;
                CloseOverlay();
                if (_registered) std::erase_if(_registered->viewports, [this](const PointerViewport& v) { return v.root == this; });
            }

            void Set(Config config)
            {
                if (_config.layout && _config.layout != config.layout) _config.layout->Internal().changed = nullptr;
                _config = std::move(config);
                _index.clear();
                _barIndex.clear();
                for (std::size_t i = 0; i < _config.panels.size(); ++i) _index[_config.panels[i].id] = i;
                // The title bars' widgets follow the contents, in the panels' order.
                std::size_t next = _config.panels.size();
                for (const auto& meta : _config.panels) if (meta.titleBar) _barIndex[meta.id] = next++;
                _titles.clear();
                _icons.clear();
                if (_config.layout) _config.layout->Internal().changed = [this] { MarkNeedsLayout(); MarkNeedsPaint(); };
                MarkNeedsLayout();
                MarkNeedsPaint();
            }

            [[nodiscard]] bool IsRepaintBoundary() const override { return true; }

            bool HitTest(HitTestResult& result, const glm::vec2 position) override
            {
                // Everywhere it shows something — the space, and each panel over the desktop — it is hit;
                // under that, the panel the point is in.
                const Floating* surface = nullptr;
                if (!OnSurface(position, surface)) return false;
                // The space itself is only what is docked in it and what floats over it: where there
                // is neither it shows what is behind, and lets the pointer through to it. A tab in
                // hand is the exception — it can be dropped anywhere.
                if (!surface && !_dragging && Probe(position) == Hit{}) return false;
                // Under that, the panel the point is in — or the panel's own widget in its title bar.
                const Hit hit = Probe(position);
                if (hit.node && !hit.node->split && !hit.node->tabs.empty()) {
                    const std::string& panel = hit.node->tabs[hit.node->active];
                    RenderObject* child = hit.kind == Hit::Kind::eContent ? ChildOf(panel)
                                        : hit.kind == Hit::Kind::eBar && hit.node->custom.Contains(position) ? BarChildOf(panel) : nullptr;
                    if (child) child->HitTest(result, position - child->Offset());
                }
                result.Add(this, position);
                return true;
            }

            bool HandleEvent(const PointerEvent& event) override;
            void Paint(Canvas& canvas, glm::vec2 offset) override;

        protected:
            void PerformLayout() override;
            [[nodiscard]] bool SizedByParent() const override { return true; }

        private:
            /**
             * The window over the desktop that panels pulled out of the space float in: see-through,
             * with no frame, above everything, over every monitor, and letting the pointer through
             * except where a panel is. One for all of them: opened when the first float is moved or
             * goes out, and closed a while after the last one has come back in.
             */
            struct Overlay {
                DockWindow* scene = nullptr;
                std::shared_ptr<Layer> layer;
                glm::ivec2 at {};               ///< Where on the desktop its window is.
                /// Where on the desktop its coordinates start: the desktop's corner. The window is there
                /// too unless it is fitted — then it is only as big as the panels in it, and goes where they do.
                glm::ivec2 origin {};
                glm::ivec2 desktop {};          ///< How big the desktop is.
                bool fitted = false;
                glm::ivec2 wantedAt {}, wantedSize {};
            };

            /** One of a stripe's buttons: a docked panel's, where it is drawn, and which group it is of. */
            struct Button {
                Rect rect {};
                std::string panel;
                DockArea area = DockArea::eLeft;
                int part = 0;
            };

            /**
             * One of the two stripes down the sides of the space. From its top, the buttons of that
             * side's panels, a line between one part's and the next's; at its foot, those of the panels
             * of that end of the bottom. There only while it has buttons.
             */
            struct Stripe {
                Rect rect {};
                std::vector<Button> buttons;
                std::vector<Rect> separators;
                bool shown = false;
            };

            struct Hit {
                /// eCard: the content of a float with no title bar, where nothing in it took the press.
                /// eButton: one of a stripe's; eStripe: a stripe, where it has none.
                enum class Kind : std::uint8_t { eNothing, eContent, eTab, eClose, eBar, eSplitter, eResize, eFrame, eRedock, eCard, eHide, eButton, eStripe };
                Kind kind = Kind::eNothing;
                Node* node = nullptr;
                std::size_t tab = 0;        ///< Index into the node's tabs.
                int floatId = 0;
                int stripe = -1;            ///< Which stripe, for eButton and eStripe.
                int button = -1;            ///< Which of its buttons, for eButton.
                bool operator==(const Hit&) const = default;
            };

            /** Where a panel in hand would be docked if it were let go. */
            struct Zone {
                bool valid = false;
                DockArea area = DockArea::eLeft;
                std::string before, after;  ///< Among that area's buttons: see DockLayout::Impl::DockAt.
                bool newPart = false;
                Rect preview {};            ///< Where it would be shown.
                Rect marker {};             ///< Where its button would go, on a stripe.
            };

            [[nodiscard]] DockLayout::Impl& L() const { return _config.layout->Internal(); }
            [[nodiscard]] bool Present(const std::string& panel) const { return _index.contains(panel) && !L().closed.contains(panel); }
            [[nodiscard]] RenderObject* ChildOf(const std::string& panel) const
            {
                const auto it = _index.find(panel);
                return it != _index.end() && it->second < Children().size() ? Children()[it->second] : nullptr;
            }
            /** @p panel 's own widget for its title bar, when it has one. */
            [[nodiscard]] RenderObject* BarChildOf(const std::string& panel) const
            {
                const auto it = _barIndex.find(panel);
                return it != _barIndex.end() && it->second < Children().size() ? Children()[it->second] : nullptr;
            }
            [[nodiscard]] const Meta* MetaOf(const std::string& panel) const
            {
                const auto it = _index.find(panel);
                return it != _index.end() ? &_config.panels[it->second] : nullptr;
            }
            [[nodiscard]] Floating* FloatById(const int id) const
            {
                for (auto& f : L().floats) if (f.id == id) return &f;
                return nullptr;
            }
            [[nodiscard]] Floating* FloatOf(const Node* node) const
            {
                while (node->parent) node = node->parent;
                for (auto& f : L().floats) if (f.root.get() == node) return &f;
                return nullptr;
            }

            [[nodiscard]] bool Dockable(const std::string& panel) const
            {
                const Meta* meta = MetaOf(panel);
                return !meta || meta->dockable;
            }
            /** Whether @p f 's panel has no title bar: then the float is only that panel's content. */
            [[nodiscard]] bool Bare(const Floating& f) const
            {
                if (!f.root || f.root->tabs.empty()) return false;
                const Meta* meta = MetaOf(f.root->tabs.front());
                return meta && !meta->showTitleBar;
            }

            const Paragraph& Title(const std::string& panel);
            const Paragraph& Icon(const std::string& panel);
            void Arrange(Node& node, Rect rect, bool bare = false);
            /** The buttons at the right of a title bar, counted from its end. */
            [[nodiscard]] static Rect BarButton(const Node& node, const int slot)
            {
                return Rect::XYWH(node.bar.right - 26.f - 24.f * static_cast<float>(slot), std::round(node.bar.Center().y - 10.f), 20.f, 20.f);
            }
            [[nodiscard]] bool Closable(const Node& node) const
            {
                const Meta* meta = node.active < node.tabs.size() ? MetaOf(node.tabs[node.active]) : nullptr;
                return meta && meta->closable;
            }
            /** How many buttons the right end of @p node 's title bar has: the titles stop short of them. */
            [[nodiscard]] int BarButtons(const Node& node) const
            {
                int count = Closable(node) ? 1 : 0;
                if (node.tool) return count + 1;
                const Floating* f = FloatOf(&node);
                return count + (f && f->window ? 1 : 0);
            }
            /** Where the part of a side that @p member is in is shown, when it is open: what a panel dropped into it would take. */
            [[nodiscard]] std::optional<Rect> PartRect(const std::string& member) const
            {
                const Group* group = L().GroupOf(member);
                if (!group || group->shown.empty()) return std::nullopt;
                const Node* node = DockLayout::Impl::FindIn(L().root.get(), group->shown);
                return node && node->visible ? std::optional<Rect>(node->rect) : std::nullopt;
            }
            /** The space between two areas next to each other: the line that is dragged to resize them. */
            [[nodiscard]] float Gap() const { return std::max(_config.options.gap, 1.f); }
            /** The sizes it is drawn and handled with. */
            [[nodiscard]] const DockStyle& S() const { return _config.options.style; }
            void LayoutStripes(glm::vec2 size);
            /**
             * How narrow @p node can be with nothing its panels show cut off: the widest least width of its
             * panels' contents (it keeps it whichever is shown); two side by side and the gap between them;
             * the wider of two one over the other. @p shown says whether anything of it is shown at all.
             */
            [[nodiscard]] float MinWidth(const Node& node, bool& shown) const;
            [[nodiscard]] float MinWidth(const Node& node) const { bool shown = false; return MinWidth(node, shown); }
            /** Where the line between @p node 's two sides goes, @p first along its @p length: no closer to either edge than that side's MinWidth allows. */
            [[nodiscard]] float KeepWidths(const Node& node, float length, float first) const;
            void PlaceChildren(const Node& node, std::vector<bool>& placed);
            void SyncOverlay();
            void OverlayFrame();
            void PublishViewports();
            void CloseOverlay();
            static void BringIn(Floating& f);
            void MoveFloat(Floating& f, glm::vec2 position);
            void Settle(Floating& f);
            [[nodiscard]] bool Fits(const Rect& rect) const;
            [[nodiscard]] glm::vec2 SpaceFromOverlay(glm::vec2 position) const;
            [[nodiscard]] glm::vec2 OverlayFromSpace(glm::vec2 position) const;
            [[nodiscard]] Rect OverlayArea() const;
            [[nodiscard]] glm::ivec2 OverlayAt() const;
            bool OverlayFit(glm::ivec2& at, glm::ivec2& size) const;
            [[nodiscard]] std::optional<glm::vec2> Desktop(glm::vec2 position) const;
            [[nodiscard]] std::optional<glm::vec2> SpacePoint(glm::vec2 position) const;
            [[nodiscard]] std::optional<glm::vec2> OverlayPoint(glm::vec2 position) const;
            [[nodiscard]] bool CanOpenWindows() const;
            bool OnSurface(glm::vec2 position, const Floating*& surface) const;
            [[nodiscard]] Hit Probe(glm::vec2 position) const;
            static bool ProbeNode(Node& node, glm::vec2 position, Hit& hit);
            [[nodiscard]] std::optional<glm::vec2> Elsewhere(glm::vec2 position) const;
            [[nodiscard]] Zone ZoneAt(glm::vec2 position, const std::string& panel) const;
            [[nodiscard]] Zone StripeZone(int stripe, glm::vec2 position, const std::string& panel) const;
            [[nodiscard]] Rect AreaPreview(DockArea area) const;
            void PaintSurface(Canvas& canvas, glm::vec2 offset, bool overlay);
            void PaintStripe(Canvas& canvas, glm::vec2 offset, int stripe);
            void PaintFloat(Canvas& canvas, glm::vec2 offset, Floating& f);
            void PaintNode(Canvas& canvas, glm::vec2 offset, Node& node);
            void Drop(glm::vec2 position);
            void Redock(Floating& floating);
            void Changed();

            Config _config;
            std::unordered_map<std::string, std::size_t> _index;
            std::unordered_map<std::string, std::size_t> _barIndex;    ///< A panel's title bar widget, by index into the children.
            std::unordered_map<std::string, Paragraph> _titles;
            std::unordered_map<std::string, Paragraph> _icons;
            Stripe _stripes[2];                         ///< Left, right.
            Rect _ground {};                            ///< The space between them, to its edges: what is filled behind the islands.
            Rect _docked {};                            ///< The space between them: where what is docked is laid out.
            Overlay _overlay;
            bool _overlayFailed = false;                ///< The display cannot show a see-through window: panels stay in the space.
            int _overlayIdle = 0;                       ///< Frames the overlay has shown nothing for.
            Owner* _registered = nullptr;
            std::vector<bool> _placed;                  ///< Per child: whether the last layout showed it.
            bool _pressInOverlay = false;               ///< Whether the gesture in hand began over the desktop: where its positions are from.

            // The gesture in hand.
            Hit _pressed {}, _hover {};
            unsigned _pressRevision = 0;    ///< The layout's tree when the press was made: _pressed.node is of that one.
            std::string _pressedPanel;      ///< The panel the press was on: a button's, a title's.
            glm::vec2 _pressAt {}, _grab {};
            Rect _floatAtPress {};
            bool _dragging = false;         ///< A panel in hand — by its button, or its title — past the slop.
            std::string _dragged;
            glm::vec2 _dragAt {};
            Zone _zone {};
        };

        const Paragraph& RenderDock::Title(const std::string& panel)
        {
            auto it = _titles.find(panel);
            if (it == _titles.end()) {
                const Meta* meta = MetaOf(panel);
                TextStyle style = Theme::Current().textStyle;
                style.size = 13.f;
                it = _titles.emplace(panel, Paragraph(meta ? meta->title : panel, style)).first;
            }
            return it->second;
        }

        /** What the button of a panel with no icon shows: the first letter of its title. */
        const Paragraph& RenderDock::Icon(const std::string& panel)
        {
            auto it = _icons.find(panel);
            if (it == _icons.end()) {
                const Meta* meta = MetaOf(panel);
                // One character of the title: all the bytes of its first, as UTF-8 has them.
                const std::string& title = meta && !meta->title.empty() ? meta->title : panel;
                std::size_t length = title.empty() ? 0 : 1;
                while (length < title.size() && (static_cast<unsigned char>(title[length]) & 0xC0) == 0x80) ++length;
                std::string glyph = title.substr(0, length);
                if (glyph.size() == 1 && glyph[0] >= 'a' && glyph[0] <= 'z') glyph[0] = static_cast<char>(glyph[0] - 'a' + 'A');
                TextStyle style = Theme::Current().textStyle;
                style.size = 14.f;
                it = _icons.emplace(panel, Paragraph(glyph, style)).first;
            }
            return it->second;
        }

        bool RenderDock::CanOpenWindows() const
        {
            // The overlay has to be put over the monitor, and to be seen through: without either, panels stay in the space.
            return _config.options.multiViewport && !_overlayFailed && kor::Window::CanBePositioned()
                && GetOwner() && GetOwner()->window && !GetOwner()->window->IsOffscreen() && kor::App::Exists();
        }

        // ---- layout ------------------------------------------------------------------------------------

        float RenderDock::MinWidth(const Node& node, bool& shown) const
        {
            shown = false;
            if (node.split) {
                bool a = false, b = false;
                const float wa = MinWidth(*node.a, a), wb = MinWidth(*node.b, b);
                shown = a || b;
                if (!(a && b)) return a ? wa : wb;
                return node.axis == Axis::eHorizontal ? wa + Gap() + wb : std::max(wa, wb);
            }
            if (node.hole) return 0.f;
            float width = 0.f;
            for (const auto& panel : node.tabs) {
                if (!Present(panel)) continue;
                shown = true;
                if (const RenderObject* child = ChildOf(panel)) width = std::max(width, child->MinIntrinsicWidth());
            }
            return std::ceil(width);
        }

        float RenderDock::KeepWidths(const Node& node, const float length, const float first) const
        {
            if (!node.split || node.axis != Axis::eHorizontal) return first;
            const float a = MinWidth(*node.a), b = MinWidth(*node.b);
            // Room for both: each side keeps its least. Not: they share what there is in proportion to their leasts.
            if (a + b <= length) return std::clamp(first, a, length - b);
            return a + b > 0.f ? std::round(length * a / (a + b)) : first;
        }

        void RenderDock::Arrange(Node& node, const Rect rect, const bool bare)
        {
            node.rect = rect;
            node.fit = false;
            node.bare = bare && !node.split;
            if (node.split) {
                // Laid out as if both were there, then the one that is takes it all.
                Arrange(*node.a, rect);
                Arrange(*node.b, rect);
                node.visible = node.a->visible || node.b->visible;
                node.line = {};
                if (!(node.a->visible && node.b->visible)) return;
                const bool h = node.axis == Axis::eHorizontal;
                const float gap = Gap();
                const float length = std::max(0.f, (h ? rect.Width() : rect.Height()) - gap);
                float first = length * node.ratio;
                if (node.extent >= 0.f) {
                    // One of the two is as long as it was made, as far as there is room; the other has the rest.
                    const float extent = std::clamp(node.extent, std::min(S().minAreaSize, length * 0.5f), length * 0.9f);
                    first = node.extentSecond ? length - extent : extent;
                }
                first = std::round(first);
                // And neither side narrower than what is in it needs.
                if (h) first = KeepWidths(node, length, first);
                if (h) {
                    Arrange(*node.a, Rect::LTRB(rect.left, rect.top, rect.left + first, rect.bottom));
                    node.line = Rect::LTRB(rect.left + first, rect.top, rect.left + first + gap, rect.bottom);
                    Arrange(*node.b, Rect::LTRB(node.line.right, rect.top, rect.right, rect.bottom));
                } else {
                    Arrange(*node.a, Rect::LTRB(rect.left, rect.top, rect.right, rect.top + first));
                    node.line = Rect::LTRB(rect.left, rect.top + first, rect.right, rect.top + first + gap);
                    Arrange(*node.b, Rect::LTRB(rect.left, node.line.bottom, rect.right, rect.bottom));
                }
                return;
            }
            node.shown.clear();
            node.tabRects.clear();
            node.custom = {};
            // Where nothing is docked: there, as big as it is, with nothing in it.
            if (node.hole) {
                node.visible = true;
                node.bar = node.body = {};
                return;
            }
            for (std::size_t i = 0; i < node.tabs.size(); ++i) if (Present(node.tabs[i])) node.shown.push_back(i);
            node.visible = !node.shown.empty();
            if (!node.visible) return;
            if (std::ranges::find(node.shown, node.active) == node.shown.end()) node.active = node.shown.front();
            // With no title bar the content has it all.
            if (node.bare) {
                node.bar = Rect::LTRB(rect.left, rect.top, rect.left, rect.top);
                node.body = rect;
                return;
            }
            // A title bar along its top: each panel's title from the left, as far as its buttons at the
            // right — which a title too long for the room left is cut short of, not drawn under — and
            // between the two, the shown panel's own widget, when it has one: the bar is as tall as that is.
            const float end = std::max(rect.left + 2.f, rect.right - 6.f - 24.f * static_cast<float>(BarButtons(node)));
            float x = rect.left + 2.f;
            std::vector<std::pair<float, float>> spans;
            for (const std::size_t i : node.shown) {
                const float width = Title(node.tabs[i]).MaxIntrinsicWidth() + 2.f * S().tabPadding;
                spans.emplace_back(std::min(x, end), std::min(x + width, end));
                x += width;
            }
            const float from = std::min(std::ceil(x), end);     // on a whole pixel, for what is drawn there to be crisp
            float height = S().titleBarHeight;
            if (RenderObject* custom = BarChildOf(node.tabs[node.active]); custom && end > from) {
                custom->Layout(BoxConstraints { end - from, end - from, 0.f, std::numeric_limits<float>::infinity() }, true);
                height = std::max(height, std::ceil(custom->Size().y));
            }
            node.bar = Rect::LTRB(rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + height));
            node.body = Rect::LTRB(rect.left, node.bar.bottom, rect.right, rect.bottom);
            node.custom = Rect::LTRB(from, node.bar.top, end, node.bar.bottom);
            for (const auto& [l, r] : spans) node.tabRects.push_back(Rect::LTRB(l, node.bar.top, r, node.bar.bottom));
        }

        /** Where the two stripes are, and each of their buttons. */
        void RenderDock::LayoutStripes(const glm::vec2 size)
        {
            auto& layout = L();
            const float inset = (S().stripeWidth - S().buttonSize) * 0.5f;
            // A stripe is as wide as its buttons need and the space kept round them: the buttons are in the
            // middle of it, as far from the edge of the window as from the panels.
            const float strip = S().stripeWidth + std::max(_config.options.stripeGap, 0.f);
            for (int s = 0; s < 2; ++s) {
                Stripe& stripe = _stripes[s];
                stripe.buttons.clear();
                stripe.separators.clear();
                const float x = s == 0 ? 0.f : size.x - strip;
                stripe.rect = Rect::XYWH(x, 0.f, strip, size.y);

                const auto& parts = s == 0 ? layout.left : layout.right;
                const Group& bottom = s == 0 ? layout.bottomLeft : layout.bottomRight;
                std::vector<const std::string*> low;
                for (const auto& panel : bottom.panels) if (Present(panel)) low.push_back(&panel);

                // How big a button can be: its full size where the stripe is tall enough for all of them,
                // the lines between the side's parts and a gap before those of the bottom — and smaller
                // where it is not, so that the ones from the top never run into the ones at the foot.
                int count = static_cast<int>(low.size()), lines = -1;
                for (const auto& part : parts) {
                    const auto here = static_cast<int>(std::ranges::count_if(part.panels, [this](const std::string& panel) { return Present(panel); }));
                    count += here;
                    if (here > 0) ++lines;
                }
                const float fixed = 2.f * inset + S().separatorGap * static_cast<float>(std::max(lines, 0)) + (low.empty() ? 0.f : 8.f);
                const float box = count > 0 ? std::clamp(std::floor((size.y - fixed) / static_cast<float>(count) - S().buttonGap), 10.f, S().buttonSize) : S().buttonSize;
                const float left = x + (strip - box) * 0.5f;

                // From the top: the side's panels, part by part, a line between one part's and the next's.
                float y = inset;
                bool any = false;
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    bool first = true;
                    for (const auto& panel : parts[i].panels) {
                        if (!Present(panel)) continue;
                        if (first && any) {
                            stripe.separators.push_back(Rect::XYWH(x + (strip - S().stripeWidth) * 0.5f + 9.f, y + 2.f, S().stripeWidth - 18.f, 1.f));
                            y += S().separatorGap;
                        }
                        first = false;
                        any = true;
                        stripe.buttons.push_back({ Rect::XYWH(left, y, box, box), panel, s == 0 ? DockArea::eLeft : DockArea::eRight, static_cast<int>(i) });
                        y += box + S().buttonGap;
                    }
                }
                // At the foot: those of that end of the bottom.
                float by = size.y - inset - static_cast<float>(low.size()) * (box + S().buttonGap) + S().buttonGap;
                for (const auto* panel : low) {
                    stripe.buttons.push_back({ Rect::XYWH(left, by, box, box), *panel, s == 0 ? DockArea::eBottomLeft : DockArea::eBottomRight, 0 });
                    by += box + S().buttonGap;
                }
                stripe.shown = !stripe.buttons.empty();
            }
            const float left = _stripes[0].shown ? strip : 0.f;
            _ground = Rect::LTRB(left, 0.f, std::max(size.x - (_stripes[1].shown ? strip : 0.f), left), size.y);
            // Half the gap between two islands is kept from the top and the foot of the space as well,
            // and from a side that has no stripe: a stripe keeps the islands from the edge by itself.
            const float edge = Gap() * 0.5f;
            const float l = _ground.left + (_stripes[0].shown ? 0.f : edge);
            const float top = std::min(edge, size.y * 0.5f);
            _docked = Rect::LTRB(l, top, std::max(_ground.right - (_stripes[1].shown ? 0.f : edge), l), std::max(size.y - edge, top));
        }

        void RenderDock::PlaceChildren(const Node& node, std::vector<bool>& placed)
        {
            if (!node.visible) return;
            if (node.split) {
                PlaceChildren(*node.a, placed);
                PlaceChildren(*node.b, placed);
                return;
            }
            if (node.hole) return;
            const auto it = _index.find(node.tabs[node.active]);
            if (it == _index.end() || it->second >= Children().size()) return;
            RenderObject* child = Children()[it->second];
            // Where the float is as big as its panel, the panel was laid out already — to say how big.
            if (!node.fit) child->Layout(BoxConstraints::Tight(glm::max(node.body.Size(), glm::vec2(0.f))));
            child->SetOffset(node.body.TopLeft());
            placed[it->second] = true;
            // Its title bar's widget, laid out by Arrange: in the middle of the bar, top to bottom.
            if (const auto bar = _barIndex.find(node.tabs[node.active]); bar != _barIndex.end() && bar->second < Children().size() && !node.custom.Empty()) {
                RenderObject* custom = Children()[bar->second];
                custom->SetOffset({ node.custom.left, std::round(node.custom.top + (node.custom.Height() - custom->Size().y) * 0.5f) });
                placed[bar->second] = true;
            }
        }

        void RenderDock::CloseOverlay()
        {
            if (!_overlay.scene) return;
            DockWindow* scene = _overlay.scene;
            scene->onGone = nullptr;
            scene->onResized = nullptr;
            scene->onFrame = nullptr;
            _overlay = {};
            if (kor::App::Exists() && kor::App::Current().IsOpen(scene)) kor::App::Current().Close(*scene);
        }

        /** A panel over the desktop comes back to float in the space. */
        void RenderDock::BringIn(Floating& f)
        {
            f.window = false;
            f.rect = Rect::XYWH(40.f, 40.f, f.rect.Width(), f.rect.Height());
        }

        Rect RenderDock::OverlayArea() const
        {
            const Owner* owner = GetOwner();
            if (!_overlay.scene || !owner) return Rect::XYWH(OverlayOrigin.x, OverlayOrigin.y, 0.f, 0.f);
            // Fitted, as big as the desktop says, in screen coordinates; otherwise as its window, in pixels.
            const glm::vec2 extent = _overlay.fitted ? glm::vec2(_overlay.desktop) / owner->desktopScale
                                                     : glm::vec2(_overlay.scene->SceneWindow().Extent()) / owner->scale;
            return Rect::XYWH(OverlayOrigin.x, OverlayOrigin.y, extent.x, extent.y);
        }

        /** Where on the desktop the overlay starts — or would, before there is one: the desktop's own corner. */
        glm::ivec2 RenderDock::OverlayAt() const
        {
            return _overlay.scene ? _overlay.origin : kor::Window::Desktop().position;
        }

        /**
         * Where a fitted overlay's window should be, and how big, in pixels: round the panels out over
         * the desktop with room for their shadows — and for a panel's title, while one is in hand out
         * there. In steps, so that a panel resized does not make a new window of it every frame.
         */
        bool RenderDock::OverlayFit(glm::ivec2& at, glm::ivec2& size) const
        {
            const Owner* owner = GetOwner();
            if (!owner || !_config.layout) return false;
            constexpr float Room = 48.f;
            bool any = false;
            Rect all {};
            const auto add = [&](const Rect& r) { all = any ? Rect::LTRB(std::min(all.left, r.left), std::min(all.top, r.top), std::max(all.right, r.right), std::max(all.bottom, r.bottom)) : r; any = true; };
            for (const Floating& f : L().floats) if (f.window && f.root) add(f.rect.Inflate(Room));
            if (_dragging) if (const auto there = OverlayPoint(_dragAt)) add(Rect::LTRB(there->x - 80.f, there->y - 60.f, there->x + 420.f, there->y + 60.f));
            if (!any) return false;
            constexpr int Step = 128;
            const glm::vec2 low = (glm::vec2(all.left, all.top) - OverlayOrigin) * owner->desktopScale;
            const glm::vec2 high = (glm::vec2(all.right, all.bottom) - OverlayOrigin) * owner->desktopScale;
            at = _overlay.origin + glm::ivec2(glm::floor(low));
            const glm::ivec2 need = glm::ivec2(glm::ceil(high - glm::floor(low)));
            size = glm::max((need + Step - 1) / Step * Step, glm::ivec2(2 * Step));
            return true;
        }

        /** How long the overlay stays once nothing is in it: a float picked up again soon finds it there. */
        constexpr int OverlayLinger = 300;

        void RenderDock::SyncOverlay()
        {
            Owner* owner = GetOwner();
            if (owner && _registered != owner) _registered = owner;
            auto& layout = L();
            const auto out = [](const Floating& f) { return f.window && f.root; };

            // Nothing out there, or nowhere for it to be: no overlay, and what was out comes in.
            if (!CanOpenWindows()) for (auto& f : layout.floats) if (f.window) BringIn(f);
            if (std::ranges::none_of(layout.floats, out)) {
                // Kept a while with nothing in it: opening a window over the whole desktop is not
                // something to do each time a float is picked up and put down again.
                if (!CanOpenWindows() || _overlayIdle > OverlayLinger) CloseOverlay();
                PublishViewports();
                return;
            }
            _overlayIdle = 0;

            if (!_overlay.scene) {
                const auto desktop = kor::Window::Desktop();
                kor::WindowSettings settings;
                settings.title = "Panels";
                settings.position = desktop.position;
                // One row short of the desktop: a window that covers a monitor exactly is taken for a
                // fullscreen one, and shown without being blended with what is behind it.
                settings.extent = glm::uvec2(glm::max(desktop.size - glm::ivec2(0, 1), glm::ivec2(64)));
                // Where a window is the dearer to show the bigger it is (X11 — under XWayland every frame
                // of one is copied), and where the pointer can be asked of the desktop while a window moves
                // under it, the overlay is fitted: as big as the panels in it, and where they are.
                _overlay.origin = desktop.position;
                _overlay.desktop = desktop.size;
                _overlay.fitted = kor::Window::DesktopCursor().has_value();
                if (glm::ivec2 at, size; _overlay.fitted && OverlayFit(at, size)) {
                    settings.position = at;
                    settings.extent = glm::uvec2(size);
                    _overlay.wantedAt = at;
                    _overlay.wantedSize = size;
                }
                settings.resizable = false;
                settings.decorated = false;
                settings.transparentFramebuffer = true;
                settings.alwaysOnTop = true;
                settings.mousePassthrough = true;
                settings.focusOnOpen = false;
                settings.taskbar = false;
                // Never the one that waits for the display: the space's own window does, and a second
                // wait in the same frame is a second refresh gone — half the frames, while it is open.
                settings.vsync = false;
                auto made = std::make_unique<DockWindow>();
                DockWindow* scene = made.get();
                const bool opened = kor::App::Current().Open(settings.title, std::move(made), settings) != nullptr;
                if (!opened || !scene->SceneWindow().CompositesWithDesktop()) {
                    // A window over the whole desktop that cannot be seen through would hide it: not that.
                    if (opened) kor::App::Current().Close(*scene);
                    kor::log::Warn("[koral.ui] panels cannot float outside the window here: the display shows no see-through window");
                    _overlayFailed = true;
                    for (auto& f : layout.floats) if (f.window) BringIn(f);
                    PublishViewports();
                    return;
                }
                _overlay.scene = scene;
                _overlay.layer = Layer::Create();
                scene->renderer.SetRoot(_overlay.layer);
                scene->onResized = [this] { MarkNeedsLayout(); MarkNeedsPaint(); };
                scene->onFrame = [this] { OverlayFrame(); };
                scene->onGone = [this] {
                    // Closed from outside: what floated in it goes back into the space.
                    _overlay = {};
                    std::vector<int> ids;
                    for (const auto& f : L().floats) if (f.window) ids.push_back(f.id);
                    for (const int id : ids) if (Floating* gone = FloatById(id)) Redock(*gone);
                    PublishViewports();   // its input and window are going: nothing may ask them anything
                    Changed();
                };
            }
            _overlay.scene->renderer.SetScale(owner->scale);
            _overlay.at = _overlay.scene->SceneWindow().Position();
            if (!_overlay.fitted) _overlay.origin = _overlay.at;
            PublishViewports();
        }

        /**
         * Every frame of the overlay: the pointer is let through it, to whatever is behind, unless it is
         * over one of the panels there or a gesture that began on one is still in hand. A window that
         * lets the pointer through hears nothing of it, so where the pointer is has to be asked.
         */
        void RenderDock::OverlayFrame()
        {
            const Owner* owner = GetOwner();
            if (!_overlay.scene || !owner || !_config.layout) return;
            kor::Window& window = _overlay.scene->SceneWindow();
            const glm::ivec2 was = _overlay.at;
            _overlay.at = window.Position();
            if (!_overlay.fitted) _overlay.origin = _overlay.at;
            // Fitted, the window goes where the panels have gone, and is as big as they have become;
            // what is drawn in it is drawn from where the window really is.
            if (glm::ivec2 to, size; _overlay.fitted && OverlayFit(to, size)) {
                if (to != _overlay.wantedAt) { _overlay.wantedAt = to; window.SetPosition(to); }
                if (size != _overlay.wantedSize) { _overlay.wantedSize = size; window.Resize(glm::uvec2(size)); }
            }
            if (_overlay.at != was) { MarkNeedsPaint(); PublishViewports(); }
            const glm::vec2 shift = glm::vec2(_overlay.at - _overlay.origin) / owner->desktopScale;
            const glm::vec2 at = OverlayOrigin + shift + window.CursorPosition() / owner->scale;
            const bool held = _pressInOverlay && _pressed.kind != Hit::Kind::eNothing;
            const bool over = std::ranges::any_of(L().floats, [&](const Floating& f) { return f.window && f.root && f.rect.Inflate(2.f).Contains(at); });
            // Where a window can say which parts of it take the pointer, the panels are those parts and
            // the pointer finds them by itself. (Asking where it is has no answer there while it is
            // over another program's window: under XWayland, X11 hears nothing of it then.)
            std::vector<glm::ivec4> panels;
            for (const Floating& f : L().floats) {
                if (!f.window || !f.root) continue;
                const Rect r = f.rect.Inflate(2.f);
                const glm::vec2 low = glm::floor((glm::vec2(r.left, r.top) - OverlayOrigin - shift) * owner->scale);
                const glm::vec2 high = glm::ceil((glm::vec2(r.right, r.bottom) - OverlayOrigin - shift) * owner->scale);
                panels.emplace_back(glm::ivec4(low, high - low));
            }
            if (!window.SetInputRegion(panels)) window.SetMousePassthrough(!(over || held));

            // With nothing in it for a while, it goes — at the next layout, not from inside its own frame.
            const bool empty = std::ranges::none_of(L().floats, [](const Floating& f) { return f.window && f.root; });
            if (!empty || _pressed.kind != Hit::Kind::eNothing) _overlayIdle = 0;
            else if (++_overlayIdle == OverlayLinger + 1) MarkNeedsLayout();
        }

        /** Where the overlay's pointer and keys land in the space: told to the view. */
        void RenderDock::PublishViewports()
        {
            if (!_registered) return;
            std::erase_if(_registered->viewports, [this](const PointerViewport& v) { return v.root == this; });
            if (_overlay.scene)
                _registered->viewports.push_back({ &_overlay.scene->SceneInput(), &_overlay.scene->SceneWindow(), this, OverlayOrigin,
                                                   _overlay.fitted ? std::optional(_overlay.origin) : std::nullopt });
        }

        void RenderDock::PerformLayout()
        {
            const auto& c = Constraints();
            SetSize({ c.HasBoundedWidth() ? c.maxWidth : c.minWidth, c.HasBoundedHeight() ? c.maxHeight : c.minHeight });
            if (!_config.layout) return;
            auto& layout = L();

            // Panels the layout has not seen yet go where they were told to, or into the middle. One
            // told to go beside another waits for that one, whatever order they were given in.
            std::vector<const Meta*> waiting;
            for (const auto& meta : _config.panels) if (!layout.Known(meta.id)) waiting.push_back(&meta);
            while (!waiting.empty()) {
                const auto ready = std::ranges::find_if(waiting, [&](const Meta* meta) {
                    const auto d = layout.defaults.find(meta->id);
                    if (d == layout.defaults.end() || d->second.beside.empty() || layout.Known(d->second.beside)) return true;
                    return std::ranges::none_of(waiting, [&](const Meta* other) { return other->id == d->second.beside; });
                });
                const auto next = ready != waiting.end() ? ready : waiting.begin();
                layout.Place((*next)->id);
                waiting.erase(next);
            }

            // A panel that never docks floats, whatever the layout — loaded from a file, or told to
            // dock it — says.
            for (const auto& meta : _config.panels) {
                if (meta.dockable || !layout.GroupOf(meta.id)) continue;
                const auto kept = layout.floatSize.find(meta.id);
                const glm::vec2 size = kept != layout.floatSize.end() ? kept->second : glm::vec2(S().minFloatSize * 2.f, S().minFloatSize * 1.5f);
                const auto where = layout.floatAt.find(meta.id);
                const glm::vec2 at = where != layout.floatAt.end() ? where->second : glm::vec2(40.f);
                layout.Remove(meta.id);
                layout.FloatPanel(meta.id, Rect::XYWH(at.x, at.y, size.x, size.y), false);
            }

            SyncOverlay();

            const glm::vec2 size = Size();
            // The areas' sizes, where they were given as shares of a space whose size was not known yet.
            const auto resolve = [&layout](float& extent, const float of) {
                if (extent >= 0.f || of <= 0.f) return;
                extent = std::round(-extent * of);
                layout.dirty = true;
            };
            resolve(layout.leftWidth, size.x);
            resolve(layout.rightWidth, size.x);
            resolve(layout.bottomHeight, size.y);

            LayoutStripes(size);
            if (layout.dirty) layout.Rebuild();
            if (layout.root) Arrange(*layout.root, _docked);

            const Rect desktop = OverlayArea();
            for (auto& f : layout.floats) {
                if (!f.root) continue;
                // Kept where it can be reached: its bar inside the space, or inside the overlay.
                const Rect area = f.window ? desktop : Rect::FromSize(size);
                const bool bare = Bare(f);
                // As big as what it shows, where it was declared so: its panel is asked how big it
                // wants to be, with as much room as there is, and the float is that and its own frame.
                bool fit = false;
                if (f.fit && !f.root->tabs.empty() && Present(f.root->tabs.front())) {
                    if (RenderObject* child = ChildOf(f.root->tabs.front())) {
                        // Its bar as tall as its own widget there wants, with the room there is.
                        float barHeight = S().titleBarHeight;
                        if (RenderObject* custom = BarChildOf(f.root->tabs.front()); custom && !bare) {
                            custom->Layout(BoxConstraints { 0.f, std::max(area.Width() - 2.f, 0.f), 0.f, std::numeric_limits<float>::infinity() }, true);
                            barHeight = std::max(barHeight, std::ceil(custom->Size().y));
                        }
                        const glm::vec2 frame = bare ? glm::vec2(0.f) : glm::vec2(2.f, barHeight + 2.f);
                        const glm::vec2 room = glm::max(area.Size() - frame, glm::vec2(0.f));
                        child->Layout(BoxConstraints { 0.f, room.x, 0.f, room.y }, true);
                        glm::vec2 wanted = child->Size() + frame;
                        // And no narrower than its title bar needs, for its title and its buttons.
                        if (!bare) wanted.x = std::max(wanted.x, Title(f.root->tabs.front()).MaxIntrinsicWidth() + 2.f * S().tabPadding + 24.f * static_cast<float>(BarButtons(*f.root)) + 12.f);
                        f.rect = Rect::XYWH(f.rect.left, f.rect.top, wanted.x, wanted.y);
                        fit = true;
                    }
                }
                const float least = fit ? 1.f : S().minFloatSize;
                // No narrower than what it shows needs, and its frame — unless the space itself is narrower.
                const float leastWidth = fit ? least : std::max(least, MinWidth(*f.root) + (bare ? 0.f : 2.f));
                const float w = std::clamp(f.rect.Width(), leastWidth, std::max(leastWidth, area.Width()));
                const float h = std::clamp(f.rect.Height(), least, std::max(least, area.Height()));
                const float x = std::clamp(f.rect.left, area.left + std::min(0.f, 40.f - w), area.left + std::max(0.f, area.Width() - 40.f));
                const float y = std::clamp(f.rect.top, area.top, area.top + std::max(0.f, area.Height() - S().titleBarHeight));
                f.rect = Rect::XYWH(x, y, w, h);
                // A frame of one around it — unless it has no title bar, and is only its content.
                Arrange(*f.root, bare ? f.rect : f.rect.Deflate(1.f), bare);
                f.root->fit = fit;
            }

            std::vector<bool> placed(Children().size(), false);
            if (layout.root) PlaceChildren(*layout.root, placed);
            for (const auto& f : layout.floats) if (f.root) PlaceChildren(*f.root, placed);
            _placed = std::move(placed);
        }

        // ---- what is where -------------------------------------------------------------------------------

        bool RenderDock::OnSurface(const glm::vec2 position, const Floating*& surface) const
        {
            surface = nullptr;
            if (!_config.layout) return false;
            for (const auto& f : L().floats)
                if (f.window && f.rect.Contains(position)) { surface = &f; return true; }
            return Rect::FromSize(Size()).Contains(position);
        }

        bool RenderDock::ProbeNode(Node& node, const glm::vec2 position, Hit& hit)
        {
            if (!node.visible || !node.rect.Contains(position)) return false;
            if (node.split) {
                if (!node.line.Empty() && node.line.Inflate(2.f).Contains(position)) { hit.kind = Hit::Kind::eSplitter; hit.node = &node; return true; }
                return ProbeNode(*node.a, position, hit) || ProbeNode(*node.b, position, hit);
            }
            if (node.hole) return false;    // nothing there: the pointer is whatever is behind's
            hit.node = &node;
            if (!node.bar.Contains(position)) { hit.kind = Hit::Kind::eContent; return true; }
            hit.kind = Hit::Kind::eBar;
            for (std::size_t i = 0; i < node.shown.size(); ++i) {
                if (!node.tabRects[i].Contains(position)) continue;
                hit.tab = node.shown[i];
                hit.kind = Hit::Kind::eTab;
                break;
            }
            return true;
        }

        RenderDock::Hit RenderDock::Probe(const glm::vec2 position) const
        {
            Hit hit;
            if (!_config.layout) return hit;
            auto& layout = L();
            for (auto it = layout.floats.rbegin(); it != layout.floats.rend(); ++it) {
                Floating& f = *it;
                if (!f.root || !f.rect.Contains(position)) continue;
                hit.floatId = f.id;
                // No corner to resize by where its size is not its own: its content's, or (with no title bar) what it was given.
                if (!f.root->bare && !f.fit && Rect::LTRB(f.rect.right - S().resizeGrip, f.rect.bottom - S().resizeGrip, f.rect.right, f.rect.bottom).Contains(position)) {
                    hit.kind = Hit::Kind::eResize;
                    return hit;
                }
                if (!ProbeNode(*f.root, position, hit)) hit.kind = Hit::Kind::eFrame;
                break;
            }
            if (hit.kind == Hit::Kind::eNothing && hit.floatId == 0 && Rect::FromSize(Size()).Contains(position)) {
                // A stripe: one of its buttons, or the stripe itself.
                for (int s = 0; s < 2; ++s) {
                    const Stripe& stripe = _stripes[s];
                    if (!stripe.shown || !stripe.rect.Contains(position)) continue;
                    hit.kind = Hit::Kind::eStripe;
                    hit.stripe = s;
                    for (std::size_t i = 0; i < stripe.buttons.size(); ++i) {
                        if (!stripe.buttons[i].rect.Contains(position)) continue;
                        hit.kind = Hit::Kind::eButton;
                        hit.button = static_cast<int>(i);
                        break;
                    }
                    return hit;
                }
                if (layout.root) ProbeNode(*layout.root, position, hit);
            }

            // A title bar has its buttons at its right end. Last, the one that closes the panel it shows;
            // before it, the one that folds a docked area away — or, on a float out over the desktop,
            // the one that docks it back.
            if (hit.node && (hit.kind == Hit::Kind::eTab || hit.kind == Hit::Kind::eBar)) {
                const Node& node = *hit.node;
                const bool closable = Closable(node);
                const Floating* f = FloatById(hit.floatId);
                if (closable && BarButton(node, 0).Contains(position)) {
                    hit.kind = Hit::Kind::eClose;
                    hit.tab = node.active;
                } else if (BarButton(node, closable ? 1 : 0).Contains(position)) {
                    if (node.tool) hit.kind = Hit::Kind::eHide;
                    else if (f && f->window) hit.kind = Hit::Kind::eRedock;
                }
            }
            return hit;
        }

        /**
         * Where on the desktop a point of the gesture in hand is. Its positions are the space's when it
         * began in the window, and the overlay's when it began on a panel out there — and stay so until
         * the button is let go, wherever the pointer goes. Nothing where windows cannot say where they are.
         */
        std::optional<glm::vec2> RenderDock::Desktop(const glm::vec2 position) const
        {
            const Owner* owner = GetOwner();
            if (!owner || !owner->window || owner->window->IsOffscreen() || !kor::Window::CanBePositioned()) return std::nullopt;
            if (_pressInOverlay) return glm::vec2(OverlayAt()) + (position - OverlayOrigin) * owner->desktopScale;
            return glm::vec2(owner->window->Position()) + (ToGlobal({ 0.f, 0.f }) + position) * owner->desktopScale;
        }

        /** That point in the space's own coordinates: inside it or not. */
        std::optional<glm::vec2> RenderDock::SpacePoint(const glm::vec2 position) const
        {
            if (!_pressInOverlay) return position;
            const auto desktop = Desktop(position);
            if (!desktop) return std::nullopt;
            const Owner* owner = GetOwner();
            return (*desktop - glm::vec2(owner->window->Position())) / owner->desktopScale - ToGlobal({ 0.f, 0.f });
        }

        /** That point in the overlay's part of the coordinates: over the desktop. */
        std::optional<glm::vec2> RenderDock::OverlayPoint(const glm::vec2 position) const
        {
            if (_pressInOverlay) return position;
            const auto desktop = Desktop(position);
            if (!desktop) return std::nullopt;
            return OverlayOrigin + (*desktop - glm::vec2(OverlayAt())) / GetOwner()->desktopScale;
        }

        /**
         * Where a point of the gesture in hand is on whatever it is over: a panel out over the desktop
         * (in front of the window, so asked first), or the space. Nothing when it is over neither.
         */
        std::optional<glm::vec2> RenderDock::Elsewhere(const glm::vec2 position) const
        {
            if (!_config.layout) return std::nullopt;
            if (const auto there = OverlayPoint(position))
                for (const auto& f : L().floats)
                    if (f.window && f.root && f.rect.Contains(*there)) return *there;
            if (const auto there = SpacePoint(position); there && Rect::FromSize(Size()).Contains(*there)) return *there;
            return std::nullopt;
        }

        /** A point of the overlay's part of the coordinates, in the space's own — and the other way. Where windows say where they are. */
        glm::vec2 RenderDock::SpaceFromOverlay(const glm::vec2 position) const
        {
            const Owner* owner = GetOwner();
            const glm::vec2 desktop = glm::vec2(OverlayAt()) + (position - OverlayOrigin) * owner->desktopScale;
            return (desktop - glm::vec2(owner->window->Position())) / owner->desktopScale - ToGlobal({ 0.f, 0.f });
        }

        glm::vec2 RenderDock::OverlayFromSpace(const glm::vec2 position) const
        {
            const Owner* owner = GetOwner();
            const glm::vec2 desktop = glm::vec2(owner->window->Position()) + (ToGlobal({ 0.f, 0.f }) + position) * owner->desktopScale;
            return OverlayOrigin + (desktop - glm::vec2(OverlayAt())) / owner->desktopScale;
        }

        /** Whether @p rect, in the space's coordinates, is wholly inside the space. */
        bool RenderDock::Fits(const Rect& rect) const
        {
            const glm::vec2 size = Size();
            return rect.left >= 0.f && rect.top >= 0.f && rect.right <= size.x && rect.bottom <= size.y;
        }

        /**
         * A float in hand goes where the pointer does, and while it is in hand it is over the desktop —
         * in front of the window and everything else — wherever there is a desktop to be over. Where it
         * belongs is settled when it is put down. @see Settle
         */
        void RenderDock::MoveFloat(Floating& f, const glm::vec2 position)
        {
            const glm::vec2 grab = _pressAt - _floatAtPress.TopLeft();
            const glm::vec2 size = f.rect.Size();
            if (const auto out = CanOpenWindows() ? OverlayPoint(position) : std::nullopt) {
                f.window = true;
                f.rect = Rect::XYWH(out->x - grab.x, out->y - grab.y, size.x, size.y);
            } else if (const auto inSpace = SpacePoint(position)) {
                f.window = false;
                f.rect = Rect::XYWH(inSpace->x - grab.x, inSpace->y - grab.y, size.x, size.y);
            }
            Changed();
        }

        /**
         * A float put down: in the space when all of it is inside the window, and over the desktop
         * otherwise — a float half out of the window is not cut off at its edge.
         */
        void RenderDock::Settle(Floating& f)
        {
            if (!f.window || !CanOpenWindows()) return;
            const glm::vec2 corner = SpaceFromOverlay(f.rect.TopLeft());
            const Rect inSpace = Rect::XYWH(corner.x, corner.y, f.rect.Width(), f.rect.Height());
            if (!Fits(inSpace)) return;
            f.window = false;
            f.rect = inSpace;
            Changed();
        }

        /** Where @p area is — or would be, with nothing open in it yet: what is shown while a panel is held over it. */
        Rect RenderDock::AreaPreview(const DockArea area) const
        {
            auto& layout = L();
            const glm::vec2 size = Size();
            const auto extent = [](const float value, const float of) { return value >= 0.f ? value : -value * of; };
            const float bottom = std::min(extent(layout.bottomHeight, size.y), size.y * 0.9f);
            const bool low = !layout.bottomLeft.shown.empty() || !layout.bottomRight.shown.empty();
            const float foot = std::max(_docked.bottom - bottom, _docked.top);
            const float sides = low ? foot : _docked.bottom;
            switch (area) {
            case DockArea::eLeft: return Rect::LTRB(_docked.left, _docked.top, _docked.left + std::min(extent(layout.leftWidth, size.x), _docked.Width() * 0.9f), sides);
            case DockArea::eRight: return Rect::LTRB(_docked.right - std::min(extent(layout.rightWidth, size.x), _docked.Width() * 0.9f), _docked.top, _docked.right, sides);
            case DockArea::eBottomLeft: return Rect::LTRB(_docked.left, foot, _docked.left + _docked.Width() * 0.5f, _docked.bottom);
            case DockArea::eBottomRight: return Rect::LTRB(_docked.left + _docked.Width() * 0.5f, foot, _docked.right, _docked.bottom);
            default: return _docked;
            }
        }

        /**
         * Where on a stripe a panel in hand would go: among the buttons of one part of the side, in a
         * part of its own between two of them or under the last, or — at the stripe's foot — among those
         * of that end of the bottom.
         */
        RenderDock::Zone RenderDock::StripeZone(const int s, const glm::vec2 position, const std::string& panel) const
        {
            const Stripe& stripe = _stripes[s];
            const DockArea side = s == 0 ? DockArea::eLeft : DockArea::eRight;
            const DockArea low = s == 0 ? DockArea::eBottomLeft : DockArea::eBottomRight;
            Zone zone;
            zone.valid = true;
            // The buttons there are, but for the one in hand.
            std::vector<const Button*> top, bottom;
            for (const auto& button : stripe.buttons) {
                if (button.panel == panel) continue;
                (button.area == side ? top : bottom).push_back(&button);
            }
            const auto marker = [&stripe](const float y) { return Rect::LTRB(stripe.rect.left + 5.f, y - 1.f, stripe.rect.right - 5.f, y + 1.f); };

            const float foot = bottom.empty() ? stripe.rect.bottom - S().stripeWidth : bottom.front()->rect.top;
            if (position.y >= foot - 10.f) {
                zone.area = low;
                zone.preview = AreaPreview(low);
                // The end it goes to has all of the bottom while the other end is not open.
                const Group& other = s == 0 ? L().bottomRight : L().bottomLeft;
                if (other.shown.empty() || other.shown == panel || !Present(other.shown))
                    zone.preview = Rect::LTRB(_docked.left, zone.preview.top, _docked.right, zone.preview.bottom);
                for (const auto* button : bottom) {
                    if (button->rect.Center().y <= position.y) continue;
                    zone.before = button->panel;
                    zone.marker = marker(button->rect.top - 2.f);
                    return zone;
                }
                zone.marker = marker(bottom.empty() ? stripe.rect.bottom - 5.f : bottom.back()->rect.bottom + 2.f);
                return zone;
            }

            zone.area = side;
            zone.preview = AreaPreview(side);
            // What is shown as where it would land: the part it would join, when that is open; for a part
            // of its own, the lower half of the one it goes under. The whole side where neither can be said.
            const auto joining = [&](const std::string& member) { return PartRect(member).value_or(zone.preview); };
            const auto under = [&](const std::string& member) {
                const auto part = PartRect(member);
                return part ? Rect::LTRB(part->left, part->top + part->Height() * 0.5f, part->right, part->bottom) : zone.preview;
            };
            if (top.empty()) {
                zone.newPart = true;
                zone.marker = marker(stripe.rect.top + 5.f);
                return zone;
            }
            for (std::size_t i = 0; i < top.size();) {
                // One part's buttons: from i to j.
                std::size_t j = i;
                while (j + 1 < top.size() && top[j + 1]->part == top[i]->part) ++j;
                if (position.y < top[i]->rect.top - 1.f) {
                    // Above the first of all: the first of its part. Between two parts: a part of its own, there.
                    if (i == 0) {
                        zone.before = top[0]->panel;
                        zone.marker = marker(top[0]->rect.top - 2.f);
                        zone.preview = joining(top[0]->panel);
                    } else {
                        zone.newPart = true;
                        zone.after = top[i - 1]->panel;
                        zone.marker = marker((top[i - 1]->rect.bottom + top[i]->rect.top) * 0.5f);
                        zone.preview = under(top[i - 1]->panel);
                    }
                    return zone;
                }
                if (position.y <= top[j]->rect.bottom + 1.f) {
                    zone.preview = joining(top[i]->panel);
                    for (std::size_t k = i; k <= j; ++k) {
                        if (top[k]->rect.Center().y <= position.y) continue;
                        zone.before = top[k]->panel;
                        zone.marker = marker(top[k]->rect.top - 2.f);
                        return zone;
                    }
                    zone.after = top[j]->panel;
                    zone.marker = marker(top[j]->rect.bottom + 2.f);
                    return zone;
                }
                i = j + 1;
            }
            // Under the last of them: a part of its own, at the end — unless it is just under, and joins it.
            if (position.y <= top.back()->rect.bottom + S().buttonGap + 4.f) {
                zone.after = top.back()->panel;
                zone.marker = marker(top.back()->rect.bottom + 2.f);
                zone.preview = joining(top.back()->panel);
                return zone;
            }
            zone.newPart = true;
            zone.after = top.back()->panel;
            zone.marker = marker(top.back()->rect.bottom + S().separatorGap);
            zone.preview = under(top.back()->panel);
            return zone;
        }

        /**
         * Where a panel held at @p position, in the space, would be docked if it were let go: on a stripe,
         * among its buttons; or in a margin of the space — within Margin of its left, its right or its
         * bottom — the area on that side. Anywhere else, nowhere: it floats.
         */
        RenderDock::Zone RenderDock::ZoneAt(const glm::vec2 position, const std::string& panel) const
        {
            Zone zone;
            // A panel that never docks has no target anywhere: let go, it floats where it is.
            if (!Dockable(panel) || !_config.layout) return zone;
            const glm::vec2 size = Size();
            if (!Rect::FromSize(size).Contains(position)) return zone;
            for (int s = 0; s < 2; ++s)
                if (_stripes[s].shown && _stripes[s].rect.Contains(position)) return StripeZone(s, position, panel);

            // On a panel that is docked round the edge (whatever floats over it: a float is no target, and
            // the one in hand may well be under the pointer): it joins that panel's group, in front of it —
            // or, down a side and over the lower part of the panel, goes under it, a part of its own.
            auto& layout = L();
            Hit over;
            if (layout.root && _docked.Contains(position)) ProbeNode(*layout.root, position, over);
            if (over.node && over.node->tool && !over.node->tabs.empty() && over.node->tabs.front() != panel) {
                const std::string& member = over.node->tabs.front();
                DockArea area = DockArea::eCenter;
                // (Not a side's panel where it reaches into the bottom margin of the space: that is the bottom's.)
                const bool low = position.y >= size.y * 0.85f;
                if (layout.GroupOf(member, &area) && !(low && (area == DockArea::eLeft || area == DockArea::eRight))) {
                    const Rect shown = over.node->rect;
                    zone.valid = true;
                    zone.area = area;
                    zone.after = member;
                    zone.preview = shown;
                    const float Under = S().underDropStart;   // from here down, of the panel's height
                    if ((area == DockArea::eLeft || area == DockArea::eRight) && position.y > shown.top + shown.Height() * Under) {
                        zone.newPart = true;
                        zone.preview = Rect::LTRB(shown.left, shown.top + shown.Height() * 0.5f, shown.right, shown.bottom);
                    }
                    return zone;
                }
            }

            // The middle: what the sides and the bottom leave, whether panels are docked in it or it is
            // empty. On its title bar, or within the Middle about its centre, the panel joins it — a tab
            // among whatever is there. (The rest of it is no target, so that a panel can be let go over
            // the middle to float there.)
            {
                const float Middle = S().centerDropSize;
                const std::function<const Node*(const Node*)> find = [&find](const Node* node) -> const Node* {
                    if (!node) return nullptr;
                    if (!node->split) return node->hole || node->center ? node : nullptr;
                    if (const Node* found = find(node->a.get())) return found;
                    return find(node->b.get());
                };
                const Node* middle = find(layout.root.get());
                const Rect where = middle ? middle->rect : layout.root ? Rect {} : _docked;
                const bool own = Holds(layout.center, panel) && layout.center.panels.size() == 1;
                if (!where.Empty() && where.Contains(position) && !own) {
                    const glm::vec2 at = (position - where.TopLeft()) / glm::max(where.Size(), glm::vec2(1.f));
                    const bool onBar = middle && middle->center && middle->bar.Contains(position);
                    if (onBar || (std::abs(at.x - 0.5f) <= Middle * 0.5f && std::abs(at.y - 0.5f) <= Middle * 0.5f)) {
                        zone.valid = true;
                        zone.area = DockArea::eCenter;
                        zone.preview = where;
                        return zone;
                    }
                }
            }

            // The margins of the space, between its stripes: within Margin of its left, its right or its
            // bottom, the area on that side.
            const float Margin = S().edgeDropMargin;
            const glm::vec2 t = (position - _docked.TopLeft()) / glm::max(_docked.Size(), glm::vec2(1.f));
            const float left = t.x, right = 1.f - t.x, bottom = 1.f - t.y;
            const float nearest = std::min({ left, right, bottom });
            if (nearest > Margin) return zone;
            zone.valid = true;
            // The bottom runs under both sides: in a corner, where two margins meet, it is the bottom's.
            zone.area = bottom <= Margin ? (t.x < 0.5f ? DockArea::eBottomLeft : DockArea::eBottomRight)
                      : nearest == left ? DockArea::eLeft : DockArea::eRight;
            zone.preview = AreaPreview(zone.area);
            if (zone.area == DockArea::eLeft || zone.area == DockArea::eRight) {
                // A side has levels, one over the other. Its margin is as many bands as it has levels,
                // and one more: over a level's band the panel joins that level; over the last, it is a
                // level of its own, under them all. (A level that holds only the panel in hand does not
                // count: it is leaving it.)
                const auto& parts = zone.area == DockArea::eLeft ? layout.left : layout.right;
                std::vector<const std::string*> levels;     // one member of each: the last of its panels
                for (const auto& part : parts) {
                    const std::string* member = nullptr;
                    for (const auto& other : part.panels) if (other != panel && Present(other)) member = &other;
                    if (member) levels.push_back(member);
                }
                const Rect side = zone.preview;
                const auto bands = static_cast<float>(levels.size() + 1);
                const auto band = static_cast<std::size_t>(std::clamp((position.y - side.top) / std::max(side.Height(), 1.f) * bands, 0.f, bands - 1.f));
                const Rect slice = Rect::LTRB(side.left, side.top + side.Height() * static_cast<float>(band) / bands, side.right,
                                              side.top + side.Height() * static_cast<float>(band + 1) / bands);
                if (band < levels.size()) {
                    zone.after = *levels[band];
                    zone.preview = PartRect(*levels[band]).value_or(slice);
                } else {
                    zone.newPart = true;
                    if (!levels.empty()) zone.after = *levels.back();
                    zone.preview = slice;
                }
            } else {
                const Group& other = zone.area == DockArea::eBottomLeft ? layout.bottomRight : layout.bottomLeft;
                if (other.shown.empty() || other.shown == panel || !Present(other.shown))
                    zone.preview = Rect::LTRB(_docked.left, zone.preview.top, _docked.right, zone.preview.bottom);
            }
            return zone;
        }

        // ---- the pointer -----------------------------------------------------------------------------------

        void RenderDock::Changed()
        {
            MarkNeedsLayout();
            MarkNeedsPaint();
            if (_config.options.onChanged) _config.options.onChanged();
        }

        /** A float goes back into the space: where its panel was docked last, or down the left side. */
        void RenderDock::Redock(Floating& floating)
        {
            auto& layout = L();
            if (!floating.root || floating.root->tabs.empty()) return;
            const std::string panel = floating.root->tabs.front();
            const auto last = layout.lastArea.find(panel);
            const DockArea area = last != layout.lastArea.end() ? last->second : DockArea::eLeft;
            layout.Remove(panel);
            layout.DockAt(panel, area);
        }

        void RenderDock::Drop(const glm::vec2 position)
        {
            auto& layout = L();
            const std::string panel = _dragged;
            if (!layout.Known(panel)) return;
            Floating* in = layout.FloatOf(panel);
            // How big it floats: as it is when it floats already; as it last floated, when it has;
            // otherwise what it shows now, and no less than a window is worth.
            glm::vec2 size { S().minFloatSize * 2.f, S().minFloatSize * 1.5f };
            if (in) size = in->rect.Size();
            else if (const auto kept = layout.floatSize.find(panel); kept != layout.floatSize.end()) size = kept->second;
            else if (const Node* shown = DockLayout::Impl::FindIn(layout.root.get(), panel); shown && shown->visible) size = glm::max(shown->body.Size(), size);

            if (_zone.valid) {
                layout.Remove(panel);
                layout.DockAt(panel, _zone.area, _zone.before, _zone.after, _zone.newPart);
                Changed();
                return;
            }

            const auto there = Elsewhere(position);
            const Floating* surface = nullptr;
            const bool inSpace = there && OnSurface(*there, surface) && surface == nullptr;
            if (inSpace) {
                // Let go over the space, on no target: it floats there — in the space when all of it
                // is inside the window, and over the desktop when some of it would stick out.
                Rect rect = Rect::XYWH(there->x - _grab.x, there->y - S().titleBarHeight * 0.5f, size.x, size.y);
                const bool out = !Fits(rect) && CanOpenWindows();
                if (out) {
                    const glm::vec2 corner = OverlayFromSpace(rect.TopLeft());
                    rect = Rect::XYWH(corner.x, corner.y, size.x, size.y);
                }
                if (in) { in->window = out; in->rect = rect; Changed(); return; }
                layout.Remove(panel);
                layout.FloatPanel(panel, rect, out);
                Changed();
                return;
            }
            if (there || !CanOpenWindows()) return;   // over a panel out there with nowhere to take it, or nowhere a panel can be

            // Let go outside the window: it floats over the desktop, there.
            const auto out = OverlayPoint(position);
            if (!out) return;
            const Rect rect = Rect::XYWH(out->x - _grab.x, out->y - S().titleBarHeight * 0.5f, size.x, size.y);
            if (in) { in->window = true; in->rect = rect; Changed(); return; }
            layout.Remove(panel);
            layout.FloatPanel(panel, rect, true);
            Changed();
        }

        bool RenderDock::HandleEvent(const PointerEvent& event)
        {
            if (!_config.layout) return false;
            auto& layout = L();
            const glm::vec2 p = event.local;
            using Kind = Hit::Kind;

            switch (event.type) {
            case PointerEvent::Type::eHover: {
                const Hit hit = Probe(p);
                if (hit != _hover) { _hover = hit; MarkNeedsPaint(); }
                // Over the line between two areas the pointer shows which way it is dragged; over a
                // float's corner, that it resizes. While one of them is in hand it stays so, wherever
                // the pointer has got to.
                if (Owner* owner = GetOwner()) {
                    const bool held = _pressed.kind == Kind::eResize || (_pressed.kind == Kind::eSplitter && _pressRevision == layout.revision);
                    const Hit& over = held ? _pressed : hit;
                    if (over.kind == Kind::eSplitter && over.node)
                        owner->cursor = over.node->axis == Axis::eHorizontal ? PointerCursor::eResizeHorizontal : PointerCursor::eResizeVertical;
                    else if (over.kind == Kind::eResize)
                        owner->cursor = PointerCursor::eResizeDiagonal;
                }
                return false;
            }
            case PointerEvent::Type::eExit:
                if (_hover.kind != Kind::eNothing) { _hover = {}; MarkNeedsPaint(); }
                return false;

            case PointerEvent::Type::eDown: {
                if (event.button != kor::MouseButton::eLeft) return false;
                const Hit hit = Probe(p);
                _pressed = hit;
                _pressRevision = layout.revision;
                _pressAt = p;
                _pressInOverlay = InOverlay(p);
                _dragging = false;
                _pressedPanel.clear();
                // A float pressed anywhere comes to the front.
                if (hit.floatId != 0 && !layout.floats.empty() && layout.floats.back().id != hit.floatId) {
                    const auto it = std::ranges::find_if(layout.floats, [&](const Floating& f) { return f.id == hit.floatId; });
                    if (it != layout.floats.end()) { std::rotate(it, it + 1, layout.floats.end()); MarkNeedsLayout(); MarkNeedsPaint(); }
                }
                // (Rotating moved the floats, not their nodes: hit.node still stands.)
                if (const Floating* f = FloatById(hit.floatId)) _floatAtPress = f->rect;
                if (hit.kind == Kind::eButton) {
                    const Button& button = _stripes[hit.stripe].buttons[static_cast<std::size_t>(hit.button)];
                    _pressedPanel = button.panel;
                    _grab = p - button.rect.TopLeft();
                } else if (hit.node && !hit.node->split && !hit.node->tabs.empty()) {
                    // The panel a title, or a title bar's button, is of: the one shown — or the one whose title it is.
                    const Node& node = *hit.node;
                    _pressedPanel = node.tabs[hit.kind == Kind::eTab ? hit.tab : node.active];
                    // By its bar, beside its title, a docked panel is picked up as by its title: held near its corner.
                    if (hit.kind == Kind::eBar) _grab = { 24.f, S().titleBarHeight * 0.5f };
                    if (hit.kind == Kind::eTab) {
                        const auto shown = static_cast<std::size_t>(std::ranges::find(node.shown, hit.tab) - node.shown.begin());
                        _grab = shown < node.tabRects.size() ? p - node.tabRects[shown].TopLeft() : glm::vec2(20.f, 10.f);
                        // One of several, in the middle: pressed, it is the one shown.
                        if (node.active != hit.tab) { layout.Show(_pressedPanel); Changed(); }
                    }
                }
                // A float with no title bar is moved by its content: its padding, and whatever else
                // of it takes no press. A button in it, or a slider, took this one first.
                if (hit.kind == Kind::eContent && hit.node->bare && !event.taken) _pressed.kind = Kind::eCard;
                // The panel's own widget in its title bar: what in it takes a press has it — the rest of it
                // picks the panel up, as the bar does.
                if (hit.kind == Kind::eBar && hit.node->custom.Contains(p) && event.taken) { _pressed = {}; _pressedPanel.clear(); return false; }
                return _pressed.kind != Kind::eContent && _pressed.kind != Kind::eNothing;
            }

            case PointerEvent::Type::eMove: {
                const glm::vec2 by = p - _pressAt;
                switch (_pressed.kind) {
                case Kind::eSplitter: {
                    // The line is of the tree as it was when it was pressed: made again since, it is gone.
                    if (_pressRevision != layout.revision) return false;
                    Node& node = *_pressed.node;
                    const bool h = node.axis == Axis::eHorizontal;
                    const float length = (h ? node.rect.Width() : node.rect.Height()) - Gap();
                    if (length > 1.f) {
                        float at = (h ? p.x - node.rect.left : p.y - node.rect.top) - Gap() * 0.5f;
                        // Held where a side would be narrower than what is in it needs: the line goes no further.
                        if (h) at = KeepWidths(node, length, at);
                        if (node.extent >= 0.f) {
                            const float extent = std::clamp(node.extentSecond ? length - at : at, std::min(S().minAreaSize, length * 0.5f), length * 0.9f);
                            if (extent != node.extent) { node.extent = extent; if (node.onResize) node.onResize(extent); Changed(); }
                        } else {
                            const float ratio = std::clamp(at / length, 0.05f, 0.95f);
                            if (ratio != node.ratio) { node.ratio = ratio; if (node.onResize) node.onResize(ratio); Changed(); }
                        }
                    }
                    return true;
                }
                case Kind::eResize:
                    if (Floating* f = FloatById(_pressed.floatId)) {
                        const float least = f->root ? std::max(S().minFloatSize, MinWidth(*f->root) + 2.f) : S().minFloatSize;
                        f->rect = Rect::LTRB(_floatAtPress.left, _floatAtPress.top, std::max(_floatAtPress.left + least, _floatAtPress.right + by.x),
                                             std::max(_floatAtPress.top + S().minFloatSize, _floatAtPress.bottom + by.y));
                        Changed();
                    }
                    return true;
                case Kind::eCard:
                    // Past a little slop: something in the card that wants the drag itself — a drag
                    // source — is asked first, and says so by then.
                    if (glm::length(by) <= 5.f) return false;
                    [[fallthrough]];
                case Kind::eBar:
                case Kind::eFrame:
                    // A float is moved by its bar. A docked panel's bar is the panel in hand, as its title is.
                    if (Floating* f = FloatById(_pressed.floatId)) {
                        MoveFloat(*f, p);
                        return true;
                    }
                    if (_pressed.kind != Kind::eBar) return false;
                    [[fallthrough]];
                case Kind::eButton:
                case Kind::eTab:
                    // A panel in hand: by its button on a stripe, or by its title.
                    if (!_dragging) {
                        if (glm::length(by) <= 5.f || _pressedPanel.empty()) return false;
                        _dragging = true;
                        _dragged = _pressedPanel;
                    }
                    _dragAt = p;
                    if (const auto there = SpacePoint(p)) _zone = ZoneAt(*there, _dragged);
                    else _zone = {};
                    MarkNeedsPaint();
                    return true;
                default:
                    return false;
                }
            }

            case PointerEvent::Type::eUp: {
                const Hit pressed = _pressed;
                // A float that was in hand is put down: in the space if it fits there, else where it is.
                if (pressed.kind == Kind::eBar || pressed.kind == Kind::eFrame || pressed.kind == Kind::eCard)
                    if (Floating* f = FloatById(pressed.floatId)) Settle(*f);
                if (_dragging) {
                    _dragging = false;
                    Drop(p);   // while _pressed still says which surface the drag began in
                    _pressed = {};
                    _zone = {};
                    _dragged.clear();
                    MarkNeedsPaint();
                    return true;
                }
                _pressed = {};
                const Hit now = Probe(p);
                const std::string panel = _pressedPanel;
                // A button on a stripe, pressed and let go: its panel opens — or, open already, is folded away.
                if (pressed.kind == Kind::eButton && now.kind == Kind::eButton && now.stripe == pressed.stripe
                    && _stripes[now.stripe].buttons[static_cast<std::size_t>(now.button)].panel == panel) {
                    if (Group* group = layout.GroupOf(panel)) {
                        group->shown = group->shown == panel ? std::string() : panel;
                        layout.dirty = true;
                        Changed();
                    }
                    return true;
                }
                if (pressed.kind == Kind::eClose && now.kind == Kind::eClose && now.node == pressed.node && !panel.empty()) {
                    layout.closed.insert(panel);
                    Changed();
                    if (_config.options.onClosed) _config.options.onClosed(panel);
                    return true;
                }
                if (pressed.kind == Kind::eHide && now.kind == Kind::eHide && now.node == pressed.node) {
                    if (Group* group = layout.GroupOf(panel); group && group->shown == panel) {
                        group->shown.clear();
                        layout.dirty = true;
                        Changed();
                    }
                    return true;
                }
                if (pressed.kind == Kind::eRedock && now.kind == Kind::eRedock) {
                    if (Floating* f = FloatById(pressed.floatId)) { Redock(*f); Changed(); }
                    return true;
                }
                return pressed.kind != Kind::eContent && pressed.kind != Kind::eNothing;
            }

            case PointerEvent::Type::eCancel:
                _pressed = {};
                if (_dragging) { _dragging = false; _zone = {}; _dragged.clear(); MarkNeedsPaint(); }
                return false;
            default:
                return false;
            }
        }

        // ---- painting --------------------------------------------------------------------------------------

        void RenderDock::PaintNode(Canvas& canvas, const glm::vec2 offset, Node& node)
        {
            if (!node.visible) return;
            const Theme& t = Theme::Current();
            if (node.split) {
                PaintNode(canvas, offset, *node.a);
                PaintNode(canvas, offset, *node.b);
                if (!node.line.Empty()) {
                    const bool hot = (_hover.kind == Hit::Kind::eSplitter && _hover.node == &node) || (_pressed.kind == Hit::Kind::eSplitter && _pressed.node == &node);
                    // The gap between two islands is nothing drawn — but under the pointer, or in hand,
                    // a line down its middle says it can be dragged.
                    if (hot) {
                        const Rect gap = node.line.Shift(offset);
                        const bool upright = gap.Height() > gap.Width();
                        const glm::vec2 c = gap.Center();
                        const Rect mark = upright ? Rect::LTRB(c.x - 1.f, gap.top + S().radius, c.x + 1.f, gap.bottom - S().radius)
                                                  : Rect::LTRB(gap.left + S().radius, c.y - 1.f, gap.right - S().radius, c.y + 1.f);
                        canvas.DrawRRect({ mark, 1.f }, Paint::Fill(t.primary));
                    }
                }
                return;
            }
            if (node.hole) return;      // nothing docked there: nothing drawn, and what is behind shows
            // Docked, it is an island: all of it — its bar, its body, what it shows — cut to rounded corners.
            // (A float is a card already, cut to its own.)
            const bool island = !node.bare && FloatOf(&node) == nullptr;
            if (island) {
                canvas.Save();
                canvas.ClipRRect({ node.rect.Shift(offset), S().radius });
            }
            // With no title bar it is its content and nothing else: no bar, no surface under it.
            if (!node.bare) {
                const Rect bar = node.bar.Shift(offset);
                canvas.DrawRect(bar, Paint::Fill(t.surface));
                canvas.DrawRect(node.body.Shift(offset), Paint::Fill(t.surface));
                canvas.DrawRect(Rect::LTRB(bar.left, bar.bottom - 1.f, bar.right, bar.bottom), Paint::Fill(t.border));
                // The title on the left — each panel's, where it holds several, the one shown underlined.
                const bool several = node.shown.size() > 1;
                for (std::size_t i = 0; i < node.shown.size(); ++i) {
                    const std::size_t index = node.shown[i];
                    const Rect tab = node.tabRects[i].Shift(offset);
                    // No room for it, short of the buttons: not drawn — and never drawn under them.
                    if (tab.Width() < S().tabPadding + 6.f) continue;
                    const bool active = index == node.active;
                    const bool hot = _hover.kind == Hit::Kind::eTab && _hover.node == &node && _hover.tab == index;
                    const Paragraph& title = Title(node.tabs[index]);
                    TextStyle style = title.Style();
                    style.color = active || hot ? t.text : t.textMuted;
                    // The one shown, of several, as the design marks it: in the accent (Material), on a
                    // piece of glass (Apple's) — or underlined, below.
                    if (several && active && t.design == ThemeDesign::eMaterial) style.color = t.primary;
                    if (several && active && t.design == ThemeDesign::eCupertino) {
                        const Rect pill = Rect::LTRB(tab.left + 2.f, tab.top + 4.f, std::max(tab.left + 2.f, tab.right - 2.f), tab.bottom - 4.f);
                        detail::PaintGlass(canvas, t, { pill, pill.Height() * 0.5f }, colors::Transparent, t.IsDark() ? 0.04f : -0.1f, false);
                    }
                    canvas.Save();
                    canvas.ClipRect(tab);
                    canvas.DrawText(title.Text(), { tab.left + S().tabPadding, std::round(tab.top + (tab.Height() - title.Size().y) * 0.5f) }, style);
                    canvas.Restore();
                    if (several && active) {
                        const float from = tab.left + S().tabPadding, to = std::max(from, tab.right - S().tabPadding);
                        switch (t.design) {
                        case ThemeDesign::eMaterial: canvas.DrawRRect({ Rect::LTRB(from, tab.bottom - 3.f, to, tab.bottom), Radii(3.f, 3.f, 0.f, 0.f) }, Paint::Fill(t.primary)); break;
                        case ThemeDesign::eCupertino: break;    // the glass under it says so
                        case ThemeDesign::eFluent: {
                            // A short mark of the accent, in the middle.
                            const float mid = (from + to) * 0.5f, half = std::min(10.f, (to - from) * 0.5f);
                            canvas.DrawRRect({ Rect::LTRB(mid - half, tab.bottom - 4.f, mid + half, tab.bottom - 1.f), 1.5f }, Paint::Fill(t.primary));
                            break;
                        }
                        default: canvas.DrawRect(Rect::LTRB(from, tab.bottom - 2.f, to, tab.bottom), Paint::Fill(t.primary)); break;
                        }
                    }
                }
                // Between the titles and the buttons, the panel's own widget.
                if (const auto bar = _barIndex.find(node.tabs[node.active]); bar != _barIndex.end() && bar->second < _placed.size() && _placed[bar->second] && !node.custom.Empty()) {
                    RenderObject* custom = Children()[bar->second];
                    canvas.Save();
                    canvas.ClipRect(node.custom.Shift(offset));
                    PaintChildAt(*custom, canvas, offset + custom->Offset());
                    canvas.Restore();
                }
                // The buttons on the right. Last, the cross that closes the panel shown.
                const bool closable = Closable(node);
                if (closable) {
                    const Rect button = BarButton(node, 0).Shift(offset);
                    const bool over = _hover.kind == Hit::Kind::eClose && _hover.node == &node;
                    if (over) canvas.DrawRRect({ button, 6.f }, Paint::Fill(t.surfacePressed));
                    const glm::vec2 c = button.Center();
                    const kui::Paint cross = Paint::Stroked(over ? t.text : t.textMuted, 1.5f);
                    canvas.DrawLine(c + glm::vec2(-3.5f, -3.5f), c + glm::vec2(3.5f, 3.5f), cross);
                    canvas.DrawLine(c + glm::vec2(-3.5f, 3.5f), c + glm::vec2(3.5f, -3.5f), cross);
                }
                const Rect second = BarButton(node, closable ? 1 : 0).Shift(offset);
                if (node.tool) {
                    // Docked round the edge: the line that folds it away, back to its button.
                    const bool over = _hover.kind == Hit::Kind::eHide && _hover.node == &node;
                    if (over) canvas.DrawRRect({ second, 6.f }, Paint::Fill(t.surfacePressed));
                    const glm::vec2 c = second.Center();
                    canvas.DrawLine(c + glm::vec2(-4.f, 0.f), c + glm::vec2(4.f, 0.f), Paint::Stroked(over ? t.text : t.textMuted, 1.5f));
                } else if (const Floating* f = FloatOf(&node); f && f->window) {
                    // Out over the desktop: the button that puts it back into the space.
                    const bool over = _hover.kind == Hit::Kind::eRedock && _hover.node == &node;
                    if (over) canvas.DrawRRect({ second, 6.f }, Paint::Fill(t.surfacePressed));
                    const glm::vec2 c = second.Center();
                    canvas.DrawRect(Rect::FromCenter(c, 12.f, 10.f), Paint::Stroked(over ? t.text : t.textMuted, 1.5f));
                    canvas.DrawRect(Rect::LTRB(c.x - 6.f, c.y + 1.f, c.x + 6.f, c.y + 5.f), Paint::Fill(over ? t.text : t.textMuted));
                }
            }
            if (RenderObject* child = ChildOf(node.tabs[node.active])) {
                const auto it = _index.find(node.tabs[node.active]);
                if (it->second < _placed.size() && _placed[it->second]) {
                    canvas.Save();
                    canvas.ClipRect(node.body.Shift(offset));
                    PaintChildAt(*child, canvas, offset + child->Offset());
                    canvas.Restore();
                }
            }
            if (island) canvas.Restore();
        }

        /** A stripe: its buttons, the one of each open panel in the accent, and the lines between a side's parts. */
        void RenderDock::PaintStripe(Canvas& canvas, const glm::vec2 offset, const int s)
        {
            const Stripe& stripe = _stripes[s];
            if (!stripe.shown) return;
            const Theme& t = Theme::Current();
            auto& layout = L();
            canvas.DrawRect(stripe.rect.Shift(offset), Paint::Fill(t.background));
            for (const auto& line : stripe.separators) canvas.DrawRect(line.Shift(offset), Paint::Fill(t.border));
            for (std::size_t i = 0; i < stripe.buttons.size(); ++i) {
                const Button& button = stripe.buttons[i];
                const Rect box = button.rect.Shift(offset);
                const Group* group = layout.GroupOf(button.panel);
                const bool open = group && group->shown == button.panel;
                const bool hot = _hover.kind == Hit::Kind::eButton && _hover.stripe == s && _hover.button == static_cast<int>(i);
                const Paragraph& icon = Icon(button.panel);
                TextStyle style = icon.Style();
                style.color = open ? t.onPrimary : hot ? t.text : t.textMuted;
                // The button of a panel that is open, as the design marks it.
                switch (t.design) {
                case ThemeDesign::eMaterial:
                    // A pill washed with the accent, its letter in the accent.
                    if (open) { canvas.DrawRRect({ box, box.Height() * 0.5f }, Paint::Fill(t.primary.WithAlpha(0.26f))); style.color = t.primary; }
                    else if (hot) canvas.DrawRRect({ box, box.Height() * 0.5f }, Paint::Fill(t.text.WithAlpha(0.08f)));
                    break;
                case ThemeDesign::eCupertino:
                    // A piece of glass, the accent's.
                    if (open) detail::PaintGlass(canvas, t, { box, 9.f }, t.primary, 0.f, false);
                    else if (hot) detail::PaintGlass(canvas, t, { box, 9.f }, colors::Transparent, 0.f, false);
                    break;
                case ThemeDesign::eFluent:
                    // A quiet patch, and a mark of the accent at its side.
                    if (open || hot) canvas.DrawRRect({ box, 4.f }, Paint::Fill(open ? t.surfaceHover : t.surfaceHover.WithAlpha(0.6f)));
                    if (open) {
                        const float x = s == 0 ? box.left + 1.f : box.right - 4.f, y = box.Center().y;
                        canvas.DrawRRect({ Rect::LTRB(x, y - 8.f, x + 3.f, y + 8.f), 1.5f }, Paint::Fill(t.primary));
                        style.color = t.text;
                    }
                    break;
                default:
                    if (open) canvas.DrawRRect({ box, 8.f }, Paint::Fill(t.primary));
                    else if (hot) canvas.DrawRRect({ box, 8.f }, Paint::Fill(t.surfaceHover));
                    break;
                }
                // Its icon, in the colour its state gives the button — or, with none, its title's first letter.
                const Meta* meta = MetaOf(button.panel);
                if (meta && meta->icon) {
                    const float side = std::round(box.Width() * 0.6f);
                    meta->icon->Draw(canvas, Rect::FromCenter(glm::round(box.Center()), side, side), style.color);
                } else {
                    canvas.DrawText(icon.Text(), { std::round(box.Center().x - icon.Size().x * 0.5f), std::round(box.Center().y - icon.Size().y * 0.5f) }, style);
                }
            }
        }

        /** A float as it is drawn: a card with its shadow — or, with no title bar, its content alone. */
        void RenderDock::PaintFloat(Canvas& canvas, const glm::vec2 offset, Floating& f)
        {
            const Theme& t = Theme::Current();
            // No title bar: whatever its content draws is all there is of it.
            if (f.root->bare) { PaintNode(canvas, offset, *f.root); return; }
            const Rect r = f.rect.Shift(offset);
            // As round as the islands docked in the space are: the space's style says how round, for both.
            const RRect card { r, S().radius };
            canvas.DrawShadow(card, colors::Black.WithAlpha(0.45f), 16.f, { 0.f, 5.f });
            // Its bar, body and content cut to its rounded corners.
            canvas.Save();
            canvas.ClipRRect(card);
            canvas.DrawRect(r, Paint::Fill(t.border));
            PaintNode(canvas, offset, *f.root);
            if (!f.fit) {
                // In from the corner by as much as its rounding takes off it: where the arc crosses the diagonal.
                const glm::vec2 corner = r.TopLeft() + r.Size() - glm::vec2(S().radius * (1.f - 0.7071f));
                const kui::Paint grip = Paint::Stroked(t.textMuted, 1.f);
                canvas.DrawLine(corner + glm::vec2(-10.f, -3.f), corner + glm::vec2(-3.f, -10.f), grip);
                canvas.DrawLine(corner + glm::vec2(-6.f, -3.f), corner + glm::vec2(-3.f, -6.f), grip);
            }
            canvas.Restore();
            // Its border, round the corners too: what is under its content there is cut away with them.
            canvas.DrawRRect({ r.Inflate(-0.5f), std::max(S().radius - 0.5f, 0.f) }, Paint::Stroked(t.border, 1.f));
        }

        /** One of the two things it shows on: the space in its window, or (@p overlay) the desktop around it. */
        void RenderDock::PaintSurface(Canvas& canvas, const glm::vec2 offset, const bool overlay)
        {
            const Theme& t = Theme::Current();
            auto& layout = L();
            // Only what is docked and what floats is drawn: the middle, with nothing in it, shows what is
            // behind the space — the scene, usually.
            if (!overlay) {
                if (layout.root) {
                    // The ground the islands stand on — unless the middle is empty: then what is behind
                    // the space shows there, and between the islands and round their corners too.
                    const std::function<bool(const Node&)> open = [&open](const Node& node) {
                        return node.split ? open(*node.a) || open(*node.b) : node.hole;
                    };
                    if (!open(*layout.root)) canvas.DrawRect(_ground.Shift(offset), Paint::Fill(t.background));
                    PaintNode(canvas, offset, *layout.root);
                }
                PaintStripe(canvas, offset, 0);
                PaintStripe(canvas, offset, 1);
            }
            for (auto& f : layout.floats)
                if (f.root && f.window == overlay) PaintFloat(canvas, offset, f);

            // A panel in hand: where it would be shown, where its button would go, and its title under the pointer.
            if (!_dragging) return;
            if (_zone.valid && !overlay) {
                canvas.DrawRect(_zone.preview.Shift(offset), Paint::Fill(t.primary.WithAlpha(0.28f)).SetStroke(1.5f, t.primary));
                if (!_zone.marker.Empty()) canvas.DrawRRect({ _zone.marker.Shift(offset), 1.f }, Paint::Fill(t.primary));
            }
            // Shown by the window while the pointer is in the space, and by the overlay once it is not.
            const auto inSpace = SpacePoint(_dragAt);
            const bool overSpace = inSpace && Rect::FromSize(Size()).Contains(*inSpace);
            const auto at = overlay ? OverlayPoint(_dragAt) : inSpace;
            if (!at || overlay == overSpace) return;
            const Paragraph& title = Title(_dragged);
            const Rect ghost = Rect::XYWH(at->x - _grab.x, at->y - _grab.y, title.MaxIntrinsicWidth() + 2.f * S().tabPadding, S().titleBarHeight).Shift(offset);
            canvas.DrawRRect({ ghost, std::min(t.radius, S().titleBarHeight * 0.5f) }, Paint::Fill(t.surfaceHover.WithAlpha(0.9f)).SetStroke(1.f, t.primary));
            TextStyle style = title.Style();
            style.color = t.text;
            canvas.DrawText(title.Text(), { ghost.left + S().tabPadding, ghost.top + (ghost.Height() - title.Size().y) * 0.5f }, style);
        }

        void RenderDock::Paint(Canvas& canvas, const glm::vec2 offset)
        {
            if (!_config.layout) return;
            // Panels that are not shown keep their state, and are simply not painted.
            PaintSurface(canvas, offset, false);
            if (_overlay.scene && _overlay.layer) {
                Canvas desktop;
                // From where the overlay's window is: the desktop's corner, unless it is fitted to its panels.
                const glm::vec2 shift = GetOwner() ? glm::vec2(_overlay.at - _overlay.origin) / GetOwner()->desktopScale : glm::vec2(0.f);
                PaintSurface(desktop, -OverlayOrigin - shift, true);
                _overlay.layer->SetPicture(desktop.Finish());
            }
        }

        struct DockWidget final : RenderObjectWidget {
            RenderDock::Config config;
            std::vector<Widget> children;
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderDock>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderDock&>(object).Set(config); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };
    }

    Widget DockSpace(std::shared_ptr<DockLayout> layout, std::vector<DockPanel> panels, DockOptions options)
    {
        auto widget = std::make_shared<DockWidget>();
        widget->config.layout = layout ? std::move(layout) : std::make_shared<DockLayout>();
        widget->config.options = std::move(options);
        for (auto& panel : panels) {
            widget->config.panels.push_back({ panel.id, panel.title, panel.closable, panel.dockable, panel.showTitleBar, panel.icon,
                                              static_cast<bool>(panel.titleBar) });
            // Each panel a layer of its own, kept by its id: where it is shown changes, what it is does not.
            widget->children.push_back(RepaintBoundary(panel.content ? std::move(panel.content) : SizedBox(0.f, 0.f)).Key(panel.id));
        }
        // Then the widgets of the title bars that have one, in the same order: see RenderDock::Set.
        for (auto& panel : panels)
            if (panel.titleBar) widget->children.push_back(RepaintBoundary(std::move(panel.titleBar)).Key(panel.id + "\x1ftitle bar"));
        return Widget(widget);
    }
}
