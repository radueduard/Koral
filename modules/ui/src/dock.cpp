//
// koral-ui: docking. One render object holds every panel as a child that stays put in the tree — so a
// panel keeps its state wherever it is shown — and lays each out where the layout says: in a tab
// group of the space, in a float over it, or in a window of its own, whose picture it records.
//

#include <kui/dock.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include <app.h>
#include <scene.h>
#include <window.h>

#include <kui/render.h>

namespace kui
{
    namespace {
        constexpr float BarHeight = 28.f;
        constexpr float Splitter = 4.f;
        constexpr float TabPad = 10.f;
        constexpr float CloseSize = 16.f;
        constexpr float Grip = 14.f;
        constexpr float MinFloat = 120.f;
        /// Where a window's part of the space starts, in the space's own coordinates: far from the
        /// space itself and from each other, so one point is in one of them at most.
        constexpr float WindowSpacing = 100000.f;

        struct Node {
            bool split = false;
            Axis axis = Axis::eHorizontal;
            float ratio = 0.5f;                 ///< The first child's share.
            std::unique_ptr<Node> a, b;
            std::vector<std::string> tabs;
            std::size_t active = 0;
            Node* parent = nullptr;

            // What the last layout made of it.
            bool visible = false;
            Rect rect {}, bar {}, body {}, line {};
            std::vector<std::size_t> shown;     ///< The tabs whose panels are there, by index into tabs.
            std::vector<Rect> tabRects;         ///< One per shown.
        };

        struct Floating {
            int id = 0;
            std::unique_ptr<Node> root;
            Rect rect {};
            bool window = false;                ///< In an OS window of its own.
        };

        std::unique_ptr<Node> TabsOf(std::string panel)
        {
            auto node = std::make_unique<Node>();
            node->tabs.push_back(std::move(panel));
            return node;
        }
    }

    struct DockLayout::Impl {
        std::unique_ptr<Node> root;
        std::vector<Floating> floats;           ///< Bottom to top.
        std::set<std::string> closed;
        struct Default { DockSide side = DockSide::eCenter; std::string relativeTo; float fraction = 0.25f; std::optional<Rect> floating; };
        std::map<std::string, Default> defaults;
        int nextFloat = 1;
        std::function<void()> changed;          ///< The dock space showing it.

        void Changed() const { if (changed) changed(); }

        static Node* FindIn(Node* node, const std::string& panel)
        {
            if (!node) return nullptr;
            if (!node->split) return std::ranges::find(node->tabs, panel) != node->tabs.end() ? node : nullptr;
            if (Node* found = FindIn(node->a.get(), panel)) return found;
            return FindIn(node->b.get(), panel);
        }

        Node* Find(const std::string& panel, Floating** in = nullptr)
        {
            if (in) *in = nullptr;
            if (Node* found = FindIn(root.get(), panel)) return found;
            for (auto& f : floats)
                if (Node* found = FindIn(f.root.get(), panel)) { if (in) *in = &f; return found; }
            return nullptr;
        }

        /** The pointer that owns @p node: its parent's, a float's, or the root. */
        std::unique_ptr<Node>& Slot(const Node* node)
        {
            if (node->parent) return node->parent->a.get() == node ? node->parent->a : node->parent->b;
            for (auto& f : floats) if (f.root.get() == node) return f.root;
            return root;
        }

        static Node* FirstTabs(Node* node)
        {
            while (node && node->split) node = node->a.get();
            return node;
        }

        void Remove(const std::string& panel)
        {
            Node* node = Find(panel);
            if (!node) return;
            std::erase(node->tabs, panel);
            if (node->active >= node->tabs.size()) node->active = node->tabs.empty() ? 0 : node->tabs.size() - 1;
            if (!node->tabs.empty()) return;
            // An empty group goes: its sibling takes the space of both.
            if (Node* parent = node->parent) {
                std::unique_ptr<Node> sibling = std::move(parent->a.get() == node ? parent->b : parent->a);
                auto& slot = Slot(parent);
                sibling->parent = parent->parent;
                slot = std::move(sibling);
            } else {
                auto& slot = Slot(node);
                const bool isRoot = &slot == &root;
                slot.reset();
                if (!isRoot) std::erase_if(floats, [](const Floating& f) { return !f.root; });
            }
        }

        /** Puts @p panel (which is nowhere) on @p side of @p target; of the whole space when null. */
        void Insert(const std::string& panel, Node* target, const DockSide side, const float fraction)
        {
            if (!root && !target) { root = TabsOf(panel); return; }
            if (!target) target = side == DockSide::eCenter ? FirstTabs(root.get()) : root.get();
            if (side == DockSide::eCenter) {
                target = FirstTabs(target);
                target->tabs.push_back(panel);
                target->active = target->tabs.size() - 1;
                return;
            }
            auto& slot = Slot(target);
            auto split = std::make_unique<Node>();
            split->split = true;
            split->axis = side == DockSide::eLeft || side == DockSide::eRight ? Axis::eHorizontal : Axis::eVertical;
            split->parent = target->parent;
            const bool first = side == DockSide::eLeft || side == DockSide::eTop;
            const float share = std::clamp(fraction, 0.05f, 0.95f);
            split->ratio = first ? share : 1.f - share;
            auto fresh = TabsOf(panel);
            fresh->parent = split.get();
            std::unique_ptr<Node> old = std::move(slot);
            old->parent = split.get();
            split->a = first ? std::move(fresh) : std::move(old);
            split->b = first ? std::move(old) : std::move(fresh);
            slot = std::move(split);
        }

        Floating& FloatPanel(const std::string& panel, const Rect rect, const bool window)
        {
            Floating f;
            f.id = nextFloat++;
            f.root = TabsOf(panel);
            f.rect = rect;
            f.window = window;
            floats.push_back(std::move(f));
            return floats.back();
        }

        /** A panel the layout has not placed: where its Dock or Float said, or a tab of the first group. */
        void Place(const std::string& panel)
        {
            const auto it = defaults.find(panel);
            if (it == defaults.end()) { Insert(panel, nullptr, DockSide::eCenter, 0.25f); return; }
            const Default& d = it->second;
            if (d.floating) { FloatPanel(panel, *d.floating, false); return; }
            Node* target = d.relativeTo.empty() ? nullptr : Find(d.relativeTo);
            Insert(panel, target, d.side, d.fraction);
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

        static void Write(std::string& out, const Node& node)
        {
            if (node.split) {
                out += std::string("(s ") + (node.axis == Axis::eHorizontal ? "h " : "v ") + Number(node.ratio) + " ";
                Write(out, *node.a);
                out += ' ';
                Write(out, *node.b);
                out += ')';
                return;
            }
            out += "(t " + std::to_string(node.active);
            for (const auto& tab : node.tabs) { out += ' '; Quote(out, tab); }
            out += ')';
        }

        struct Reader {
            std::string_view text;
            std::size_t at = 0;
            bool ok = true;

            void Space() { while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t')) ++at; }
            bool Eat(const char c) { Space(); if (at < text.size() && text[at] == c) { ++at; return true; } return false; }
            std::string Word()
            {
                Space();
                const std::size_t start = at;
                while (at < text.size() && text[at] != ' ' && text[at] != '\n' && text[at] != ')' && text[at] != '(') ++at;
                return std::string(text.substr(start, at - start));
            }
            float Number()
            {
                const std::string word = Word();
                float value = 0.f;
                const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
                if (error != std::errc() || word.empty()) ok = false;
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
            std::unique_ptr<Node> ReadNode()
            {
                if (!Eat('(')) { ok = false; return nullptr; }
                auto node = std::make_unique<Node>();
                const std::string kind = Word();
                if (kind == "s") {
                    node->split = true;
                    node->axis = Word() == "h" ? Axis::eHorizontal : Axis::eVertical;
                    node->ratio = std::clamp(Number(), 0.05f, 0.95f);
                    node->a = ReadNode();
                    node->b = ReadNode();
                    if (!node->a || !node->b) { ok = false; return nullptr; }
                    node->a->parent = node->b->parent = node.get();
                } else if (kind == "t") {
                    node->active = static_cast<std::size_t>(std::max(0.f, Number()));
                    while (auto tab = String()) node->tabs.push_back(std::move(*tab));
                    if (node->tabs.empty()) ok = false;
                } else {
                    ok = false;
                }
                if (!Eat(')')) ok = false;
                return ok ? std::move(node) : nullptr;
            }
        };
    };

    DockLayout::DockLayout() : _impl(std::make_unique<Impl>()) {}
    DockLayout::~DockLayout() = default;

    DockLayout& DockLayout::Dock(std::string panel, const DockSide side, std::string relativeTo, const float fraction)
    {
        _impl->defaults[panel] = { side, relativeTo, fraction, std::nullopt };
        if (_impl->Find(panel)) {
            _impl->Remove(panel);
            _impl->Insert(panel, relativeTo.empty() ? nullptr : _impl->Find(relativeTo), side, fraction);
            _impl->Changed();
        }
        return *this;
    }

    DockLayout& DockLayout::Float(std::string panel, const Rect rect)
    {
        _impl->defaults[panel] = { DockSide::eCenter, {}, 0.25f, rect };
        if (_impl->Find(panel)) {
            _impl->Remove(panel);
            _impl->FloatPanel(panel, rect, false);
            _impl->Changed();
        }
        return *this;
    }

    DockLayout& DockLayout::PopOut(std::string panel, const glm::vec2 size)
    {
        if (_impl->Find(panel)) _impl->Remove(panel);
        _impl->defaults.erase(panel);
        _impl->FloatPanel(panel, Rect::XYWH(40.f, 40.f, size.x, size.y), true);
        _impl->Changed();
        return *this;
    }

    void DockLayout::Close(const std::string& panel) { if (_impl->closed.insert(panel).second) _impl->Changed(); }
    void DockLayout::Open(const std::string& panel) { if (_impl->closed.erase(panel) > 0) { Activate(panel); _impl->Changed(); } }
    bool DockLayout::IsOpen(const std::string& panel) const { return !_impl->closed.contains(panel); }

    void DockLayout::Activate(const std::string& panel)
    {
        if (Node* node = _impl->Find(panel)) {
            node->active = static_cast<std::size_t>(std::ranges::find(node->tabs, panel) - node->tabs.begin());
            _impl->Changed();
        }
    }

    bool DockLayout::IsFloating(const std::string& panel) const
    {
        Floating* in = nullptr;
        return _impl->Find(panel, &in) != nullptr && in != nullptr;
    }

    std::string DockLayout::Save() const
    {
        std::string out = "dock 1\n";
        if (_impl->root) { out += "root "; Impl::Write(out, *_impl->root); out += '\n'; }
        for (const auto& f : _impl->floats) {
            out += "float " + Impl::Number(f.rect.left) + ' ' + Impl::Number(f.rect.top) + ' ' + Impl::Number(f.rect.Width()) + ' '
                 + Impl::Number(f.rect.Height()) + (f.window ? " 1 " : " 0 ");
            Impl::Write(out, *f.root);
            out += '\n';
        }
        if (!_impl->closed.empty()) {
            out += "closed";
            for (const auto& panel : _impl->closed) { out += ' '; Impl::Quote(out, panel); }
            out += '\n';
        }
        return out;
    }

    bool DockLayout::Load(const std::string_view text)
    {
        Impl::Reader reader { text };
        if (reader.Word() != "dock" || reader.Number() != 1.f) return false;
        std::unique_ptr<Node> root;
        std::vector<Floating> floats;
        std::set<std::string> closed;
        int next = 1;
        while (reader.ok) {
            const std::string word = reader.Word();
            if (word.empty()) break;
            if (word == "root") {
                root = reader.ReadNode();
            } else if (word == "float") {
                Floating f;
                const float x = reader.Number(), y = reader.Number(), w = reader.Number(), h = reader.Number();
                f.rect = Rect::XYWH(x, y, std::max(w, MinFloat), std::max(h, MinFloat));
                f.window = reader.Number() != 0.f;
                f.root = reader.ReadNode();
                f.id = next++;
                if (f.root) floats.push_back(std::move(f));
            } else if (word == "closed") {
                while (auto panel = reader.String()) closed.insert(std::move(*panel));
            } else {
                reader.ok = false;
            }
        }
        if (!reader.ok) return false;
        _impl->root = std::move(root);
        _impl->floats = std::move(floats);
        _impl->closed = std::move(closed);
        _impl->nextFloat = next;
        _impl->Changed();
        return true;
    }

    // ---- the window a panel is pulled out into ------------------------------------------------------------

    namespace {
        /** @brief A scene that shows one layer: a float's picture, recorded by the dock space it came from. */
        class DockWindow final : public kor::Scene {
        public:
            void Initialize() override { Graph().Add<UiPass>(renderer); }
            void Update() override
            {
                const glm::uvec2 extent = Window::Extent();
                if (extent != _last) { _last = extent; if (onResized) onResized(); }
            }
            void Shutdown() override { if (onGone) onGone(); }

            Renderer renderer;
            std::function<void()> onResized, onGone;

        private:
            glm::uvec2 _last {};
        };

        class RenderDock final : public RenderContainer {
        public:
            struct Meta { std::string id, title; bool closable = true; };
            struct Config { std::shared_ptr<DockLayout> layout; std::vector<Meta> panels; DockOptions options; };

            ~RenderDock() override
            {
                if (_config.layout) _config.layout->Internal().changed = nullptr;
                for (auto& n : _natives) CloseNative(n);
                if (_registered) std::erase_if(_registered->viewports, [this](const PointerViewport& v) { return v.root == this; });
            }

            void Set(Config config)
            {
                if (_config.layout && _config.layout != config.layout) _config.layout->Internal().changed = nullptr;
                _config = std::move(config);
                _index.clear();
                for (std::size_t i = 0; i < _config.panels.size(); ++i) _index[_config.panels[i].id] = i;
                _titles.clear();
                if (_config.layout) _config.layout->Internal().changed = [this] { MarkNeedsLayout(); MarkNeedsPaint(); };
                MarkNeedsLayout();
                MarkNeedsPaint();
            }

            [[nodiscard]] bool IsRepaintBoundary() const override { return true; }

            bool HitTest(HitTestResult& result, const glm::vec2 position) override
            {
                // Everywhere it shows something — the space, and each window's part of it — it is hit;
                // under that, the panel the point is in.
                const Floating* surface = nullptr;
                if (!OnSurface(position, surface)) return false;
                if (const Node* node = PanelNodeAt(position))
                    if (RenderObject* child = ChildOf(node->tabs[node->active]))
                        child->HitTest(result, position - child->Offset());
                result.Add(this, position);
                return true;
            }

            bool HandleEvent(const PointerEvent& event) override;
            void Paint(Canvas& canvas, glm::vec2 offset) override;

        protected:
            void PerformLayout() override;
            [[nodiscard]] bool SizedByParent() const override { return true; }

        private:
            /** A window of its own for a float: the scene, and where its part of the space starts. */
            struct Native {
                int floatId = 0;
                DockWindow* scene = nullptr;
                glm::vec2 origin {};
                std::shared_ptr<Layer> layer;
                int slot = 0;
            };

            struct Hit {
                enum class Kind : std::uint8_t { eNothing, eContent, eTab, eClose, eBar, eSplitter, eResize, eFrame, eRedock };
                Kind kind = Kind::eNothing;
                Node* node = nullptr;
                std::size_t tab = 0;        ///< Index into the node's tabs.
                int floatId = 0;
                bool operator==(const Hit&) const = default;
            };

            struct Zone {
                Node* node = nullptr;       ///< Null with whole: the empty space.
                DockSide side = DockSide::eCenter;
                Rect preview {};
                bool valid = false;
                bool whole = false;
                bool outer = false;         ///< An edge of the whole space: beside everything docked in it.
            };

            [[nodiscard]] DockLayout::Impl& L() const { return _config.layout->Internal(); }
            [[nodiscard]] bool Present(const std::string& panel) const { return _index.contains(panel) && !L().closed.contains(panel); }
            [[nodiscard]] RenderObject* ChildOf(const std::string& panel) const
            {
                const auto it = _index.find(panel);
                return it != _index.end() && it->second < Children().size() ? Children()[it->second] : nullptr;
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
            [[nodiscard]] Native* NativeOf(const int floatId)
            {
                for (auto& n : _natives) if (n.floatId == floatId) return &n;
                return nullptr;
            }
            [[nodiscard]] Floating* FloatOf(const Node* node) const
            {
                while (node->parent) node = node->parent;
                for (auto& f : L().floats) if (f.root.get() == node) return &f;
                return nullptr;
            }

            const Paragraph& Title(const std::string& panel);
            void Arrange(Node& node, Rect rect);
            void PlaceChildren(const Node& node, std::vector<bool>& placed);
            void SyncNatives();
            void PublishViewports();
            void CloseNative(Native& native);
            [[nodiscard]] bool CanOpenWindows() const;
            bool OnSurface(glm::vec2 position, const Floating*& surface) const;
            [[nodiscard]] const Node* PanelNodeAt(glm::vec2 position) const;
            [[nodiscard]] Hit Probe(glm::vec2 position) const;
            static bool ProbeNode(Node& node, glm::vec2 position, Hit& hit);
            [[nodiscard]] std::optional<glm::vec2> Elsewhere(glm::vec2 position) const;
            [[nodiscard]] Zone ZoneAt(glm::vec2 position, const std::string& panel) const;
            void PaintSurface(Canvas& canvas, glm::vec2 offset, const Floating* window);
            void PaintNode(Canvas& canvas, glm::vec2 offset, Node& node);
            void Drop(glm::vec2 position);
            void Redock(Floating& floating);
            void Changed();

            Config _config;
            std::unordered_map<std::string, std::size_t> _index;
            std::unordered_map<std::string, Paragraph> _titles;
            std::vector<Native> _natives;
            Owner* _registered = nullptr;
            std::vector<bool> _placed;                  ///< Per child: whether the last layout showed it.
            std::optional<glm::ivec2> _spawnAt;         ///< Where on the desktop the next window opens.

            // The gesture in hand.
            Hit _pressed {}, _hover {};
            glm::vec2 _pressAt {}, _grab {};
            Rect _floatAtPress {};
            bool _dragging = false;         ///< A tab, past the slop.
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

        bool RenderDock::CanOpenWindows() const
        {
            return _config.options.multiViewport && GetOwner() && GetOwner()->window && !GetOwner()->window->IsOffscreen() && kor::App::Exists();
        }

        // ---- layout ------------------------------------------------------------------------------------

        void RenderDock::Arrange(Node& node, const Rect rect)
        {
            node.rect = rect;
            if (node.split) {
                // Laid out as if both were there, then the one that is takes it all.
                Arrange(*node.a, rect);
                Arrange(*node.b, rect);
                node.visible = node.a->visible || node.b->visible;
                node.line = {};
                if (!(node.a->visible && node.b->visible)) return;
                const bool h = node.axis == Axis::eHorizontal;
                const float length = (h ? rect.Width() : rect.Height()) - Splitter;
                const float first = std::round(std::max(0.f, length) * node.ratio);
                if (h) {
                    Arrange(*node.a, Rect::LTRB(rect.left, rect.top, rect.left + first, rect.bottom));
                    node.line = Rect::LTRB(rect.left + first, rect.top, rect.left + first + Splitter, rect.bottom);
                    Arrange(*node.b, Rect::LTRB(node.line.right, rect.top, rect.right, rect.bottom));
                } else {
                    Arrange(*node.a, Rect::LTRB(rect.left, rect.top, rect.right, rect.top + first));
                    node.line = Rect::LTRB(rect.left, rect.top + first, rect.right, rect.top + first + Splitter);
                    Arrange(*node.b, Rect::LTRB(rect.left, node.line.bottom, rect.right, rect.bottom));
                }
                return;
            }
            node.shown.clear();
            node.tabRects.clear();
            for (std::size_t i = 0; i < node.tabs.size(); ++i) if (Present(node.tabs[i])) node.shown.push_back(i);
            node.visible = !node.shown.empty();
            if (!node.visible) return;
            if (std::ranges::find(node.shown, node.active) == node.shown.end()) node.active = node.shown.front();
            node.bar = Rect::LTRB(rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + BarHeight));
            node.body = Rect::LTRB(rect.left, node.bar.bottom, rect.right, rect.bottom);
            float x = rect.left;
            for (const std::size_t i : node.shown) {
                const Meta* meta = MetaOf(node.tabs[i]);
                const float width = Title(node.tabs[i]).MaxIntrinsicWidth() + 2.f * TabPad + (meta && meta->closable ? CloseSize + 4.f : 0.f);
                node.tabRects.push_back(Rect::LTRB(x, node.bar.top, std::min(x + width, std::max(x, rect.right)), node.bar.bottom));
                x += width + 1.f;
            }
        }

        void RenderDock::PlaceChildren(const Node& node, std::vector<bool>& placed)
        {
            if (!node.visible) return;
            if (node.split) {
                PlaceChildren(*node.a, placed);
                PlaceChildren(*node.b, placed);
                return;
            }
            const auto it = _index.find(node.tabs[node.active]);
            if (it == _index.end() || it->second >= Children().size()) return;
            RenderObject* child = Children()[it->second];
            child->Layout(BoxConstraints::Tight(glm::max(node.body.Size(), glm::vec2(0.f))));
            child->SetOffset(node.body.TopLeft());
            placed[it->second] = true;
        }

        void RenderDock::CloseNative(Native& native)
        {
            if (!native.scene) return;
            native.scene->onGone = nullptr;
            native.scene->onResized = nullptr;
            DockWindow* scene = native.scene;
            native.scene = nullptr;
            if (kor::App::Exists() && kor::App::Current().IsOpen(scene)) kor::App::Current().Close(*scene);
        }

        void RenderDock::SyncNatives()
        {
            Owner* owner = GetOwner();
            if (owner && _registered != owner) _registered = owner;
            auto& layout = L();
            const bool can = CanOpenWindows();

            // Windows whose float went back into the space, or away.
            for (auto& n : _natives) {
                const Floating* f = FloatById(n.floatId);
                if (!f || !f->window || !can || !f->root) CloseNative(n);
            }
            std::erase_if(_natives, [](const Native& n) { return n.scene == nullptr; });

            for (auto& f : layout.floats) {
                if (!f.window) continue;
                if (!can) { f.window = false; continue; }
                Native* native = NativeOf(f.id);
                if (!native) {
                    // Its window: as big as the float was, where the float was dropped when the platform lets us say.
                    int slot = 1;
                    while (std::ranges::any_of(_natives, [slot](const Native& n) { return n.slot == slot; })) ++slot;
                    const Node* tabs = DockLayout::Impl::FirstTabs(f.root.get());
                    const Meta* meta = tabs && !tabs->tabs.empty() ? MetaOf(tabs->tabs[std::min(tabs->active, tabs->tabs.size() - 1)]) : nullptr;
                    kor::WindowSettings settings;
                    settings.title = meta ? meta->title : "Panel";
                    settings.extent = glm::uvec2(glm::max(f.rect.Size() * owner->scale, glm::vec2(64.f)));
                    if (_spawnAt) settings.position = *_spawnAt;
                    _spawnAt.reset();
                    auto made = std::make_unique<DockWindow>();
                    DockWindow* scene = made.get();
                    if (!kor::App::Current().Open(settings.title, std::move(made), settings)) { f.window = false; continue; }
                    Native fresh;
                    fresh.floatId = f.id;
                    fresh.scene = scene;
                    fresh.slot = slot;
                    fresh.origin = { WindowSpacing * static_cast<float>(slot), 0.f };
                    fresh.layer = Layer::Create();
                    scene->renderer.SetRoot(fresh.layer);
                    const int id = f.id;
                    scene->onResized = [this] { MarkNeedsLayout(); MarkNeedsPaint(); };
                    scene->onGone = [this, id] {
                        // The user closed the window: what it held goes back into the space.
                        if (Native* n = NativeOf(id)) n->scene = nullptr;
                        if (Floating* gone = FloatById(id)) Redock(*gone);
                        std::erase_if(_natives, [id](const Native& n) { return n.floatId == id; });
                        PublishViewports();   // its input and window are going: nothing may ask them anything
                        Changed();
                    };
                    _natives.push_back(std::move(fresh));
                    native = &_natives.back();
                }
                native->scene->renderer.SetScale(owner->scale);
                const glm::vec2 extent = glm::vec2(native->scene->SceneWindow().Extent()) / owner->scale;
                f.rect = Rect::XYWH(native->origin.x, native->origin.y, extent.x, extent.y);
            }

            PublishViewports();
        }

        /** Where the pointer and keys of each of its windows land in the space: told to the view. */
        void RenderDock::PublishViewports()
        {
            if (!_registered) return;
            std::erase_if(_registered->viewports, [this](const PointerViewport& v) { return v.root == this; });
            for (const auto& n : _natives)
                if (n.scene) _registered->viewports.push_back({ &n.scene->SceneInput(), &n.scene->SceneWindow(), this, n.origin });
        }

        void RenderDock::PerformLayout()
        {
            const auto& c = Constraints();
            SetSize({ c.HasBoundedWidth() ? c.maxWidth : c.minWidth, c.HasBoundedHeight() ? c.maxHeight : c.minHeight });
            if (!_config.layout) return;
            auto& layout = L();

            // Panels the layout has not seen yet go where they were told to, or into the first group.
            // One told to go beside another waits for that one, whatever order they were given in.
            std::vector<const Meta*> waiting;
            for (const auto& meta : _config.panels) if (!layout.Find(meta.id)) waiting.push_back(&meta);
            while (!waiting.empty()) {
                const auto ready = std::ranges::find_if(waiting, [&](const Meta* meta) {
                    const auto d = layout.defaults.find(meta->id);
                    if (d == layout.defaults.end() || d->second.relativeTo.empty() || layout.Find(d->second.relativeTo)) return true;
                    return std::ranges::none_of(waiting, [&](const Meta* other) { return other->id == d->second.relativeTo; });
                });
                const auto next = ready != waiting.end() ? ready : waiting.begin();
                layout.Place((*next)->id);
                waiting.erase(next);
            }

            SyncNatives();

            const glm::vec2 size = Size();
            if (layout.root) Arrange(*layout.root, Rect::FromSize(size));
            for (auto& f : layout.floats) {
                if (!f.root) continue;
                if (!f.window) {
                    // Kept where it can be reached: its bar inside the space.
                    const float w = std::clamp(f.rect.Width(), MinFloat, std::max(MinFloat, size.x));
                    const float h = std::clamp(f.rect.Height(), MinFloat, std::max(MinFloat, size.y));
                    const float x = std::clamp(f.rect.left, std::min(0.f, 40.f - w), std::max(0.f, size.x - 40.f));
                    const float y = std::clamp(f.rect.top, 0.f, std::max(0.f, size.y - BarHeight));
                    f.rect = Rect::XYWH(x, y, w, h);
                    Arrange(*f.root, f.rect.Deflate(1.f));
                } else {
                    Arrange(*f.root, f.rect);
                }
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
                if (!f.window && Rect::LTRB(f.rect.right - Grip, f.rect.bottom - Grip, f.rect.right, f.rect.bottom).Contains(position)) {
                    hit.kind = Hit::Kind::eResize;
                    return hit;
                }
                if (!ProbeNode(*f.root, position, hit)) hit.kind = Hit::Kind::eFrame;
                break;
            }
            if (hit.kind == Hit::Kind::eNothing && hit.floatId == 0 && layout.root && Rect::FromSize(Size()).Contains(position))
                ProbeNode(*layout.root, position, hit);

            // On a tab: its close button; on a window's bar: the button that docks it back.
            if (hit.kind == Hit::Kind::eTab) {
                const Meta* meta = MetaOf(hit.node->tabs[hit.tab]);
                const auto shown = static_cast<std::size_t>(std::ranges::find(hit.node->shown, hit.tab) - hit.node->shown.begin());
                const Rect tab = hit.node->tabRects[shown];
                if (meta && meta->closable && Rect::LTRB(tab.right - CloseSize - 4.f, tab.top, tab.right, tab.bottom).Contains(position))
                    hit.kind = Hit::Kind::eClose;
            } else if (hit.kind == Hit::Kind::eBar) {
                const Floating* f = FloatById(hit.floatId);
                if (f && f->window && position.x >= hit.node->bar.right - BarHeight) hit.kind = Hit::Kind::eRedock;
            }
            return hit;
        }

        const Node* RenderDock::PanelNodeAt(const glm::vec2 position) const
        {
            const Hit hit = Probe(position);
            return hit.kind == Hit::Kind::eContent ? hit.node : nullptr;
        }

        /**
         * Where a point of one surface is in another: a drag that began in the space and is now over a
         * panel's window, or the other way round. Only where windows can say where they are; otherwise
         * a point outside its own surface is nowhere.
         */
        std::optional<glm::vec2> RenderDock::Elsewhere(const glm::vec2 position) const
        {
            const Floating* surface = nullptr;
            if (OnSurface(position, surface)) return position;
            const Owner* owner = GetOwner();
            if (!owner || !owner->window || !kor::Window::CanBePositioned()) return std::nullopt;

            // Which surface the gesture is in: the one the press was on.
            const Floating* from = nullptr;
            for (const auto& f : L().floats) if (f.window && f.id == _pressed.floatId) from = &f;
            const float scale = owner->scale;
            const glm::vec2 inSpace = ToGlobal({ 0.f, 0.f });   // the space's corner, in its window
            glm::vec2 desktop;
            if (from) {
                const Native* native = const_cast<RenderDock*>(this)->NativeOf(from->id);
                if (!native || !native->scene) return std::nullopt;
                desktop = glm::vec2(native->scene->SceneWindow().Position()) + (position - native->origin) * scale;
            } else {
                desktop = glm::vec2(owner->window->Position()) + (inSpace + position) * scale;
            }
            for (const auto& n : _natives) {
                if (!n.scene) continue;
                const glm::vec2 local = (desktop - glm::vec2(n.scene->SceneWindow().Position())) / scale;
                const glm::vec2 extent = glm::vec2(n.scene->SceneWindow().Extent()) / scale;
                if (local.x >= 0.f && local.y >= 0.f && local.x < extent.x && local.y < extent.y) return n.origin + local;
            }
            const glm::vec2 local = (desktop - glm::vec2(owner->window->Position())) / scale - inSpace;
            if (Rect::FromSize(Size()).Contains(local)) return local;
            return std::nullopt;
        }

        RenderDock::Zone RenderDock::ZoneAt(const glm::vec2 position, const std::string& panel) const
        {
            Zone zone;
            const Hit hit = Probe(position);

            // The rim of the space itself: beside everything docked in it — unless that is this panel alone.
            constexpr float rim = 14.f;
            const Rect space = Rect::FromSize(Size());
            const Node* root = L().root.get();
            const bool onlyThis = root && !root->split && root->shown.size() == 1 && root->tabs[root->shown.front()] == panel;
            if (hit.floatId == 0 && root && root->visible && !onlyThis && space.Contains(position) && !space.Deflate(rim).Contains(position)) {
                zone.valid = zone.outer = true;
                const float left = position.x, right = space.right - position.x, top = position.y, bottom = space.bottom - position.y;
                const float nearest = std::min({ left, right, top, bottom });
                constexpr float share = 0.25f;
                if (nearest == left) { zone.side = DockSide::eLeft; zone.preview = Rect::LTRB(0.f, 0.f, space.right * share, space.bottom); }
                else if (nearest == right) { zone.side = DockSide::eRight; zone.preview = Rect::LTRB(space.right * (1.f - share), 0.f, space.right, space.bottom); }
                else if (nearest == top) { zone.side = DockSide::eTop; zone.preview = Rect::LTRB(0.f, 0.f, space.right, space.bottom * share); }
                else { zone.side = DockSide::eBottom; zone.preview = Rect::LTRB(0.f, space.bottom * (1.f - share), space.right, space.bottom); }
                return zone;
            }
            if (hit.kind == Hit::Kind::eNothing && hit.floatId == 0) {
                // The space with nothing docked in it takes the panel whole.
                const Node* root = L().root.get();
                if ((!root || !root->visible) && Rect::FromSize(Size()).Contains(position)) {
                    zone.valid = zone.whole = true;
                    zone.preview = Rect::FromSize(Size());
                }
                return zone;
            }
            if (hit.kind != Hit::Kind::eContent && hit.kind != Hit::Kind::eBar && hit.kind != Hit::Kind::eTab && hit.kind != Hit::Kind::eClose) return zone;
            Node* node = hit.node;
            const bool own = std::ranges::find(node->tabs, panel) != node->tabs.end();
            if (hit.kind != Hit::Kind::eContent) {
                // On a tab bar: a tab of that group — which, of its own group, is no change.
                if (own) return zone;
                zone = { node, DockSide::eCenter, node->rect, true };
                return zone;
            }
            // In a panel: near an edge splits it; the middle is not a target (dropping there floats).
            if (own && node->shown.size() < 2) return zone;
            const Rect r = node->body;
            const glm::vec2 t = (position - r.TopLeft()) / glm::max(r.Size(), glm::vec2(1.f));
            constexpr float edge = 0.3f;
            const float nearest = std::min({ t.x, 1.f - t.x, t.y, 1.f - t.y });
            if (nearest > edge) return zone;
            zone.node = node;
            zone.valid = true;
            const Rect whole = node->rect;
            if (nearest == t.x) { zone.side = DockSide::eLeft; zone.preview = Rect::LTRB(whole.left, whole.top, whole.left + whole.Width() * 0.5f, whole.bottom); }
            else if (nearest == 1.f - t.x) { zone.side = DockSide::eRight; zone.preview = Rect::LTRB(whole.left + whole.Width() * 0.5f, whole.top, whole.right, whole.bottom); }
            else if (nearest == t.y) { zone.side = DockSide::eTop; zone.preview = Rect::LTRB(whole.left, whole.top, whole.right, whole.top + whole.Height() * 0.5f); }
            else { zone.side = DockSide::eBottom; zone.preview = Rect::LTRB(whole.left, whole.top + whole.Height() * 0.5f, whole.right, whole.bottom); }
            return zone;
        }

        // ---- the pointer -----------------------------------------------------------------------------------

        void RenderDock::Changed()
        {
            MarkNeedsLayout();
            MarkNeedsPaint();
            if (_config.options.onChanged) _config.options.onChanged();
        }

        void RenderDock::Redock(Floating& floating)
        {
            auto& layout = L();
            std::vector<std::string> panels;
            const std::function<void(const Node&)> collect = [&](const Node& node) {
                if (node.split) { collect(*node.a); collect(*node.b); return; }
                panels.insert(panels.end(), node.tabs.begin(), node.tabs.end());
            };
            if (floating.root) collect(*floating.root);
            const int id = floating.id;
            std::erase_if(layout.floats, [id](const Floating& f) { return f.id == id; });
            for (const auto& panel : panels) layout.Insert(panel, nullptr, DockSide::eCenter, 0.25f);
        }

        void RenderDock::Drop(const glm::vec2 position)
        {
            auto& layout = L();
            const std::string panel = _dragged;
            Floating* in = nullptr;
            Node* source = layout.Find(panel, &in);
            if (!source) return;
            const bool alone = in && !in->root->split && in->root->tabs.size() == 1;
            const glm::vec2 size = glm::max(source->body.Size(), glm::vec2(MinFloat * 2.f, MinFloat * 1.5f));

            if (_zone.valid) {
                Node* target = _zone.node;
                const DockSide side = _zone.side;
                // Removing it may fold the tree around the target: found again by a panel it holds.
                std::string anchor;
                if (target) for (const auto& tab : target->tabs) if (tab != panel) { anchor = tab; break; }
                layout.Remove(panel);
                if (_zone.outer) layout.Insert(panel, nullptr, side, 0.25f);
                else layout.Insert(panel, anchor.empty() ? nullptr : layout.Find(anchor), _zone.whole ? DockSide::eCenter : side, 0.5f);
                Changed();
                return;
            }

            const auto there = Elsewhere(position);
            const Floating* surface = nullptr;
            const bool inSpace = there && OnSurface(*there, surface) && surface == nullptr;
            if (inSpace) {
                // Let go over the space, on no target: it floats there.
                const Rect rect = Rect::XYWH(there->x - _grab.x, there->y - BarHeight * 0.5f, size.x, size.y);
                if (alone && !in->window) { in->rect = rect; Changed(); return; }
                layout.Remove(panel);
                layout.FloatPanel(panel, rect, false);
                Changed();
                return;
            }
            if (there || !CanOpenWindows()) return;   // over another panel's window, or nowhere a window can open

            // Let go outside every window: a window of its own, under the pointer where that can be said.
            const Owner* owner = GetOwner();
            std::optional<glm::ivec2> at;
            if (kor::Window::CanBePositioned()) {
                const Floating* from = FloatById(_pressed.floatId);
                glm::vec2 desktop;
                if (const Native* native = from && from->window ? NativeOf(from->id) : nullptr; native && native->scene)
                    desktop = glm::vec2(native->scene->SceneWindow().Position()) + (position - native->origin) * owner->scale;
                else
                    desktop = glm::vec2(owner->window->Position()) + (ToGlobal({ 0.f, 0.f }) + position) * owner->scale;
                at = glm::ivec2(desktop - glm::vec2(_grab.x, BarHeight * 0.5f) * owner->scale);
            }
            if (alone && in->window) {
                // Already a window to itself: the window moves.
                if (at) if (const Native* native = NativeOf(in->id); native && native->scene) native->scene->SceneWindow().SetPosition(*at);
                return;
            }
            layout.Remove(panel);
            layout.FloatPanel(panel, Rect::XYWH(0.f, 0.f, size.x, size.y), true);
            _spawnAt = at;
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
                return false;
            }
            case PointerEvent::Type::eExit:
                if (_hover.kind != Kind::eNothing) { _hover = {}; MarkNeedsPaint(); }
                return false;

            case PointerEvent::Type::eDown: {
                if (event.button != kor::MouseButton::eLeft) return false;
                const Hit hit = Probe(p);
                _pressed = hit;
                _pressAt = p;
                _dragging = false;
                // A float pressed anywhere comes to the front.
                if (hit.floatId != 0 && !layout.floats.empty() && layout.floats.back().id != hit.floatId) {
                    const auto it = std::ranges::find_if(layout.floats, [&](const Floating& f) { return f.id == hit.floatId; });
                    if (it != layout.floats.end()) { std::rotate(it, it + 1, layout.floats.end()); MarkNeedsLayout(); MarkNeedsPaint(); }
                }
                // (Rotating moved the floats, not their nodes: hit.node still stands.)
                if (const Floating* f = FloatById(hit.floatId)) _floatAtPress = f->rect;
                if (hit.kind == Kind::eTab) {
                    if (hit.node->active != hit.tab) { hit.node->active = hit.tab; Changed(); }
                    const auto shown = static_cast<std::size_t>(std::ranges::find(hit.node->shown, hit.tab) - hit.node->shown.begin());
                    _grab = shown < hit.node->tabRects.size() ? p - hit.node->tabRects[shown].TopLeft() : glm::vec2(20.f, 10.f);
                }
                return hit.kind != Kind::eContent && hit.kind != Kind::eNothing;
            }

            case PointerEvent::Type::eMove: {
                const glm::vec2 by = p - _pressAt;
                switch (_pressed.kind) {
                case Kind::eSplitter: {
                    Node& node = *_pressed.node;
                    const bool h = node.axis == Axis::eHorizontal;
                    const float length = (h ? node.rect.Width() : node.rect.Height()) - Splitter;
                    if (length > 1.f) {
                        const float at = (h ? p.x - node.rect.left : p.y - node.rect.top) - Splitter * 0.5f;
                        const float ratio = std::clamp(at / length, 0.05f, 0.95f);
                        if (ratio != node.ratio) { node.ratio = ratio; Changed(); }
                    }
                    return true;
                }
                case Kind::eResize:
                    if (Floating* f = FloatById(_pressed.floatId)) {
                        f->rect = Rect::LTRB(_floatAtPress.left, _floatAtPress.top, std::max(_floatAtPress.left + MinFloat, _floatAtPress.right + by.x),
                                             std::max(_floatAtPress.top + MinFloat, _floatAtPress.bottom + by.y));
                        Changed();
                    }
                    return true;
                case Kind::eBar:
                case Kind::eFrame:
                    // A float is moved by its bar; a docked group's bar does nothing.
                    if (Floating* f = FloatById(_pressed.floatId); f && !f->window) {
                        f->rect = _floatAtPress.Shift(by);
                        Changed();
                        return true;
                    }
                    return false;
                case Kind::eTab:
                    if (!_dragging) {
                        if (glm::length(by) <= 5.f) return false;
                        _dragging = true;
                        _dragged = _pressed.node->tabs[_pressed.tab];
                    }
                    _dragAt = p;
                    if (const auto there = Elsewhere(p)) _zone = ZoneAt(*there, _dragged);
                    else _zone = {};
                    MarkNeedsPaint();
                    return true;
                default:
                    return false;
                }
            }

            case PointerEvent::Type::eUp: {
                const Hit pressed = _pressed;
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
                if (pressed.kind == Kind::eClose && now.kind == Kind::eClose && now.node == pressed.node && now.tab == pressed.tab) {
                    const std::string panel = pressed.node->tabs[pressed.tab];
                    layout.closed.insert(panel);
                    Changed();
                    if (_config.options.onClosed) _config.options.onClosed(panel);
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
                    canvas.DrawRect(node.line.Shift(offset), Paint::Fill(hot ? t.primary : t.background));
                }
                return;
            }
            canvas.DrawRect(node.bar.Shift(offset), Paint::Fill(t.background));
            canvas.DrawRect(node.body.Shift(offset), Paint::Fill(t.surface));
            for (std::size_t i = 0; i < node.shown.size(); ++i) {
                const std::size_t index = node.shown[i];
                const std::string& panel = node.tabs[index];
                const Rect tab = node.tabRects[i].Shift(offset);
                const bool active = index == node.active;
                const bool hot = (_hover.kind == Hit::Kind::eTab || _hover.kind == Hit::Kind::eClose) && _hover.node == &node && _hover.tab == index;
                if (active) {
                    canvas.DrawRRect({ tab, Radii(t.radius, t.radius, 0.f, 0.f) }, Paint::Fill(t.surface));
                    canvas.DrawRect(Rect::LTRB(tab.left, tab.top, tab.right, tab.top + 2.f), Paint::Fill(t.primary));
                } else if (hot) {
                    canvas.DrawRRect({ tab, Radii(t.radius, t.radius, 0.f, 0.f) }, Paint::Fill(t.surfaceHover));
                }
                Paragraph title = Title(panel);
                TextStyle style = title.Style();
                style.color = active ? t.text : t.textMuted;
                canvas.DrawText(title.Text(), { tab.left + TabPad, tab.top + (tab.Height() - title.Size().y) * 0.5f }, style);
                if (const Meta* meta = MetaOf(panel); meta && meta->closable && (active || hot)) {
                    const glm::vec2 c { tab.right - 4.f - CloseSize * 0.5f, tab.Center().y };
                    const bool over = _hover.kind == Hit::Kind::eClose && _hover.node == &node && _hover.tab == index;
                    if (over) canvas.DrawRRect({ Rect::FromCenter(c, CloseSize, CloseSize), 3.f }, Paint::Fill(t.surfacePressed));
                    const kui::Paint cross = Paint::Stroked(over ? t.text : t.textMuted, 1.5f);
                    canvas.DrawLine(c + glm::vec2(-3.5f, -3.5f), c + glm::vec2(3.5f, 3.5f), cross);
                    canvas.DrawLine(c + glm::vec2(-3.5f, 3.5f), c + glm::vec2(3.5f, -3.5f), cross);
                }
            }
            // A window's bar ends in the button that puts what it holds back into the space.
            if (const Floating* f = FloatOf(&node); f && f->window) {
                const glm::vec2 c = offset + glm::vec2(node.bar.right - BarHeight * 0.5f, node.bar.Center().y);
                const bool over = _hover.kind == Hit::Kind::eRedock && _hover.node == &node;
                if (over) canvas.DrawRRect({ Rect::FromCenter(c, 20.f, 20.f), 3.f }, Paint::Fill(t.surfacePressed));
                canvas.DrawRect(Rect::FromCenter(c, 12.f, 10.f), Paint::Stroked(over ? t.text : t.textMuted, 1.5f));
                canvas.DrawRect(Rect::LTRB(c.x - 6.f, c.y + 1.f, c.x + 6.f, c.y + 5.f), Paint::Fill(over ? t.text : t.textMuted));
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
        }

        void RenderDock::PaintSurface(Canvas& canvas, const glm::vec2 offset, const Floating* window)
        {
            const Theme& t = Theme::Current();
            auto& layout = L();
            if (window) {
                canvas.DrawRect(window->rect.Shift(offset), Paint::Fill(t.background));
                PaintNode(canvas, offset, *window->root);
            } else {
                canvas.DrawRect(Rect::FromSize(Size()).Shift(offset), Paint::Fill(t.background));
                if (layout.root) PaintNode(canvas, offset, *layout.root);
                for (auto& f : layout.floats) {
                    if (f.window || !f.root) continue;
                    const Rect r = f.rect.Shift(offset);
                    canvas.DrawShadow({ r, t.radius }, colors::Black.WithAlpha(0.45f), 16.f, { 0.f, 5.f });
                    canvas.DrawRect(r, Paint::Fill(t.border));
                    PaintNode(canvas, offset, *f.root);
                    const glm::vec2 corner = r.TopLeft() + r.Size();
                    const kui::Paint grip = Paint::Stroked(t.textMuted, 1.f);
                    canvas.DrawLine(corner + glm::vec2(-10.f, -3.f), corner + glm::vec2(-3.f, -10.f), grip);
                    canvas.DrawLine(corner + glm::vec2(-6.f, -3.f), corner + glm::vec2(-3.f, -6.f), grip);
                }
            }

            // A tab in hand: where it would land, and the tab itself under the pointer.
            if (!_dragging) return;
            const Floating* over = nullptr;
            if (_zone.valid && OnSurface(_zone.preview.Center(), over) && over == window) {
                canvas.DrawRect(_zone.preview.Shift(offset), Paint::Fill(t.primary.WithAlpha(0.28f)).SetStroke(1.5f, t.primary));
            }
            const Floating* at = nullptr;
            if (OnSurface(_dragAt, at) && at == window) {
                const Paragraph& title = Title(_dragged);
                const Rect ghost = Rect::XYWH(_dragAt.x - _grab.x, _dragAt.y - _grab.y, title.MaxIntrinsicWidth() + 2.f * TabPad, BarHeight).Shift(offset);
                canvas.DrawRRect({ ghost, t.radius }, Paint::Fill(t.surfaceHover.WithAlpha(0.9f)).SetStroke(1.f, t.primary));
                TextStyle style = title.Style();
                style.color = t.text;
                canvas.DrawText(title.Text(), { ghost.left + TabPad, ghost.top + (ghost.Height() - title.Size().y) * 0.5f }, style);
            }
        }

        void RenderDock::Paint(Canvas& canvas, const glm::vec2 offset)
        {
            if (!_config.layout) return;
            // Panels that are not shown keep their state, and are simply not painted.
            PaintSurface(canvas, offset, nullptr);
            for (auto& n : _natives) {
                const Floating* f = FloatById(n.floatId);
                if (!f || !f->root || !n.layer) continue;
                Canvas window;
                PaintSurface(window, -n.origin, f);
                n.layer->SetPicture(window.Finish());
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
            widget->config.panels.push_back({ panel.id, panel.title, panel.closable });
            // Each panel a layer of its own, kept by its id: where it is shown changes, what it is does not.
            widget->children.push_back(RepaintBoundary(panel.content ? std::move(panel.content) : SizedBox(0.f, 0.f)).Key(panel.id));
        }
        return Widget(widget);
    }
}
