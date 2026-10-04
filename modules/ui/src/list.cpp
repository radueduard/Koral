//
// koral-ui: lists. One that holds all its items gives each a layer; one built on demand keeps only
// those in view, positioned in a content layer that the scroll moves.
//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

#include "boxes.h"

namespace kui
{
    Widget ListView(std::vector<Widget> children, const Axis axis, const float gap)
    {
        for (auto& child : children) child = RepaintBoundary(std::move(child));
        return ScrollView(Flex(axis, std::move(children), { .crossAxisAlignment = CrossAxisAlignment::eStretch, .gap = gap }), axis);
    }

    namespace {
        /**
         * @brief The items in view, by index, in a layer the scroll position moves. Each is as long as
         *        the list says (extent) or, where it does not say, as long as it turns out to be: one not
         *        yet measured is taken to be as long as the estimate, until it is.
         */
        class RenderVirtualList final : public RenderContainer {
        public:
            struct Config {
                std::size_t count = 0;
                Axis axis = Axis::eVertical;
                float extent = 24.f;                                     // every item's; 0 or less: its own
                float estimate = 40.f;
                float gap = 0.f, paddingStart = 0.f, paddingEnd = 0.f;
                std::vector<std::size_t> indices;                        // of the children, in order
                std::function<void(std::size_t, std::size_t)> wants;     // the range it would like built
                std::function<void(std::size_t, float)> onScrolled;
                std::size_t jumpIndex = 0;
                float jumpOffset = 0.f;
                std::uint32_t jump = 0;
            };

            void Set(Config config)
            {
                const bool layout = config.count != _config.count || config.extent != _config.extent || config.indices != _config.indices
                    || config.axis != _config.axis || config.estimate != _config.estimate || config.gap != _config.gap
                    || config.paddingStart != _config.paddingStart || config.paddingEnd != _config.paddingEnd;
                const bool measure = config.extent != _config.extent || config.axis != _config.axis;
                const bool jump = config.jump != 0 && config.jump != _config.jump;
                _config = std::move(config);
                if (measure) _extents.clear();
                if (jump) { _anchor = std::min(_config.jumpIndex, _config.count ? _config.count - 1 : 0); _anchorOffset = _config.jumpOffset; _restore = true; _told = false;
                    // To the very start: what is before the first item is in view too.
                    if (_anchor == 0 && _anchorOffset <= 0.f) _anchorOffset = -_config.paddingStart; }
                if (layout || jump) MarkNeedsLayout();
                else Scroll(0.f);
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
            [[nodiscard]] glm::vec2 ChildOrigin(const RenderObject& child) const override { return child.Offset() - Along(_scroll); }

            bool HandleEvent(const PointerEvent& event) override
            {
                if (event.type != PointerEvent::Type::eScroll) return false;
                // A row goes sideways with the wheel that goes sideways — and with the usual one, which is the one there is.
                const float delta = Vertical() || event.delta.x == 0.f ? event.delta.y : event.delta.x;
                return Scroll(-delta * 48.f);
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                // The items, drawn into the content layer at their places in the whole list — not where
                // the scroll has them, which is the layer's business.
                Canvas content;
                for (auto* child : _children) PaintChildAt(*child, content, child->Offset());
                if (!_content) _content = Layer::Create();
                _content->SetPicture(content.Finish());
                _content->SetTransform(Transform::Translation(-Along(_scroll)));
                canvas.Save();
                canvas.ClipRect(Rect::XYWH(offset.x, offset.y, Size().x, Size().y));
                canvas.Translate(offset);
                canvas.DrawLayer(_content);
                canvas.Restore();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                const bool vertical = Vertical();
                const bool crossBounded = vertical ? c.HasBoundedWidth() : c.HasBoundedHeight();
                const bool mainBounded = vertical ? c.HasBoundedHeight() : c.HasBoundedWidth();
                float cross = crossBounded ? (vertical ? c.maxWidth : c.maxHeight) : 0.f;

                if (!Fixed() && _extents.size() != _config.count) { _extents.resize(_config.count, Unmeasured); _stale = true; }
                for (std::size_t i = 0; i < _children.size() && i < _config.indices.size(); ++i) {
                    BoxConstraints item;
                    // As wide as the list, where the list knows how wide it is; as long as the list says, or as it likes.
                    const float least = crossBounded ? cross : 0.f, most = crossBounded ? cross : Infinity;
                    const float lo = Fixed() ? _config.extent : 0.f, hi = Fixed() ? _config.extent : Infinity;
                    if (vertical) item = { least, most, lo, hi }; else item = { lo, hi, least, most };
                    _children[i]->Layout(item);
                    const glm::vec2 size = _children[i]->Size();
                    if (!crossBounded) cross = std::max(cross, vertical ? size.x : size.y);
                    const std::size_t index = _config.indices[i];
                    if (!Fixed() && index < _extents.size()) {
                        const float extent = vertical ? size.y : size.x;
                        if (_extents[index] != extent) { _extents[index] = extent; _stale = true; }
                    }
                }
                Measure();
                const float main = mainBounded ? (vertical ? c.maxHeight : c.maxWidth) : Total();
                SetSize(vertical ? glm::vec2(cross, main) : glm::vec2(main, cross));
                // Placed by where the item is in the whole list. Not SetOffset: a child moving inside
                // the content layer repaints the content, which this does every layout anyway.
                for (std::size_t i = 0; i < _children.size() && i < _config.indices.size(); ++i)
                    _children[i]->SetOffset(Along(Start(_config.indices[i])));
                // What was first in view still is, however long what is before it has turned out to be.
                if (_restore || (!Fixed() && _told)) _scroll = _config.count ? Start(std::min(_anchor, _config.count - 1)) + _anchorOffset : 0.f;
                _restore = false;
                Scroll(0.f);
                MarkNeedsPaint();
            }

        private:
            static constexpr float Unmeasured = -1.f;
            static constexpr float Infinity = std::numeric_limits<float>::infinity();

            [[nodiscard]] bool Vertical() const { return _config.axis == Axis::eVertical; }
            [[nodiscard]] bool Fixed() const { return _config.extent > 0.f; }
            [[nodiscard]] glm::vec2 Along(const float v) const { return Vertical() ? glm::vec2(0.f, v) : glm::vec2(v, 0.f); }
            [[nodiscard]] float Screen() const { return Vertical() ? Size().y : Size().x; }

            /** @brief Where each item starts, from how long each is: worked out again when one of them changed. */
            void Measure()
            {
                if (Fixed() || !_stale) return;
                _stale = false;
                _starts.resize(_config.count + 1);
                double at = 0.;
                for (std::size_t i = 0; i < _config.count; ++i) {
                    _starts[i] = at;
                    at += (_extents[i] == Unmeasured ? _config.estimate : _extents[i]) + _config.gap;
                }
                _starts[_config.count] = at;
            }

            /** @brief Where item @p index starts, from the start of the list. */
            [[nodiscard]] float Start(const std::size_t index) const
            {
                if (Fixed()) return _config.paddingStart + static_cast<float>(index) * (_config.extent + _config.gap);
                return _config.paddingStart + (index < _starts.size() ? static_cast<float>(_starts[index]) : 0.f);
            }

            /** @brief How long the whole list is. */
            [[nodiscard]] float Total() const
            {
                if (_config.count == 0) return _config.paddingStart + _config.paddingEnd;
                return Start(_config.count) - _config.gap + _config.paddingEnd;
            }

            /** @brief The item at @p position along the list (the last, past its end). */
            [[nodiscard]] std::size_t IndexAt(const float position) const
            {
                if (_config.count == 0) return 0;
                const float p = position - _config.paddingStart;
                if (p <= 0.f) return 0;
                if (Fixed()) return std::min(_config.count - 1, static_cast<std::size_t>(p / (_config.extent + _config.gap)));
                const auto after = std::upper_bound(_starts.begin(), _starts.begin() + static_cast<std::ptrdiff_t>(_config.count), static_cast<double>(p));
                return static_cast<std::size_t>(std::max<std::ptrdiff_t>(after - _starts.begin() - 1, 0));
            }

            [[nodiscard]] float MaxScroll() const { return std::max(0.f, Total() - Screen()); }

            /** @brief Scrolls, moving the content layer, and asks for the items now in view. */
            bool Scroll(const float by)
            {
                const float next = std::clamp(_scroll + by, 0.f, MaxScroll());
                const bool moved = next != _scroll;
                _scroll = next;
                if (_content) _content->SetTransform(Transform::Translation(-Along(_scroll)));
                if ((!Fixed() && _starts.size() != _config.count + 1) || Screen() <= 0.f) return moved;
                // What is first in view, and how far into it the view starts: where the list is, said so that
                // it stays there when what is before it changes length.
                const std::size_t anchor = IndexAt(_scroll);
                const float into = _config.count ? _scroll - Start(anchor) : 0.f;      // less than nothing in what is before the first
                if (anchor != _anchor || into != _anchorOffset || !_told) {
                    _anchor = anchor;
                    _anchorOffset = into;
                    _told = true;
                    if (_config.onScrolled) _config.onScrolled(anchor, std::max(into, 0.f));
                }
                // In view, and a screen either side, so a scroll rarely shows an item before it is built.
                if (_config.wants) {
                    const float screen = Screen();
                    const std::size_t first = IndexAt(_scroll - screen);
                    const std::size_t last = std::min(_config.count, IndexAt(_scroll + 2.f * screen) + 1);
                    if (first != _first || last != _last) {
                        _first = first;
                        _last = last;
                        _config.wants(first, last);
                    }
                }
                return moved;
            }

            Config _config;
            float _scroll = 0.f;
            std::size_t _first = 0, _last = 0;
            std::size_t _anchor = 0;
            float _anchorOffset = 0.f;          // from where the anchor starts: before it, in the padding before the first
            bool _restore = false, _told = false, _stale = true;
            std::vector<float> _extents;        // how long each item was last measured to be
            std::vector<double> _starts;        // where each starts, and after the last where the list ends
            std::shared_ptr<Layer> _content;
        };

        struct VirtualListWidget final : RenderObjectWidget {
            RenderVirtualList::Config config;
            std::vector<Widget> children;
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderVirtualList>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderVirtualList&>(object).Set(config); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };

        /** @brief Which items exist: the range the list last asked for. */
        struct ListState final : StatefulWidget {
            LazyListOptions options;
            std::function<Widget(std::size_t)> builder;
            std::size_t first = 0, last = 0;
            std::size_t toldFirst = SIZE_MAX, toldLast = SIZE_MAX;
            /// Items built for the range, kept while they stay in it: the same widget again is a widget
            /// its element does not even look at.
            std::map<std::size_t, Widget> built;

            ListState(LazyListOptions o, std::function<Widget(std::size_t)> b) : options(std::move(o)), builder(std::move(b))
            {
                // Until the list knows how long it is; from where it is to start.
                first = std::min(options.jump != 0 ? options.jumpIndex : 0, options.count);
                last = std::min<std::size_t>(options.count, first + 64);
            }

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& l = static_cast<const ListState&>(newer);
                const bool jumped = l.options.jump != 0 && l.options.jump != options.jump;
                options = l.options;
                builder = l.builder;
                toldFirst = toldLast = SIZE_MAX;   // a new listener has been told nothing
                built.clear();   // a new builder may build them differently
                if (jumped) { first = std::min(options.jumpIndex, options.count); last = first + 64; }
                last = std::min(last, options.count);
                first = std::min(first, last);
            }

            Widget Build() override
            {
                auto widget = std::make_shared<VirtualListWidget>();
                auto& c = widget->config;
                c.count = options.count;
                c.axis = options.axis;
                c.extent = options.itemExtent;
                c.estimate = std::max(options.estimatedExtent, 1.f);
                c.gap = options.gap;
                c.paddingStart = options.paddingStart;
                c.paddingEnd = options.paddingEnd;
                c.onScrolled = options.onScrolled;
                c.jumpIndex = options.jumpIndex;
                c.jumpOffset = options.jumpOffset;
                c.jump = options.jump;
                c.wants = [this](const std::size_t f, const std::size_t l) {
                    if (f != first || l != last) SetState([&] { first = f; last = l; });
                };
                std::erase_if(built, [&](const auto& e) { return e.first < first || e.first >= last; });
                for (std::size_t i = first; i < last; ++i) {
                    // Keyed by index, so an item that stays in view stays built; each with its own layer.
                    auto& item = built[i];
                    if (!item) item = RepaintBoundary(builder(i)).Key(std::to_string(i));
                    widget->children.push_back(item);
                    c.indices.push_back(i);
                }
                if (options.onRange && (first != toldFirst || last != toldLast)) {
                    toldFirst = first;
                    toldLast = last;
                    options.onRange(first, last);
                }
                return Widget(widget);
            }
        };
    }

    Widget ListView(const std::size_t count, const float itemExtent, std::function<Widget(std::size_t)> builder,
                    std::function<void(std::size_t, std::size_t)> onRange)
    {
        LazyListOptions options;
        options.count = count;
        options.itemExtent = itemExtent;
        options.onRange = std::move(onRange);
        return Make<ListState>(std::move(options), std::move(builder));
    }

    Widget LazyList(LazyListOptions options, std::function<Widget(std::size_t)> builder)
    {
        return Make<ListState>(std::move(options), std::move(builder));
    }
}
