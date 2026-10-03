//
// koral-ui: lists. One that holds all its items gives each a layer; one built on demand keeps only
// those in view, positioned in a content layer that the scroll moves.
//

#include <algorithm>
#include <cmath>
#include <cstdint>
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
        /** @brief The items in view, by index, in a layer the scroll position moves. */
        class RenderVirtualList final : public RenderContainer {
        public:
            struct Config {
                std::size_t count = 0;
                float extent = 24.f;
                std::vector<std::size_t> indices;                        // of the children, in order
                std::function<void(std::size_t, std::size_t)> wants;     // the range it would like built
            };

            void Set(Config config)
            {
                const bool layout = config.count != _config.count || config.extent != _config.extent || config.indices != _config.indices;
                _config = std::move(config);
                if (layout) MarkNeedsLayout();
                Scroll(0.f);
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
            [[nodiscard]] glm::vec2 ChildOrigin(const RenderObject& child) const override { return child.Offset() - glm::vec2(0.f, _scroll); }

            bool HandleEvent(const PointerEvent& event) override
            {
                if (event.type != PointerEvent::Type::eScroll) return false;
                return Scroll(-event.delta.y * 48.f);
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                // The items, drawn into the content layer at their places in the whole list — not where
                // the scroll has them, which is the layer's business.
                Canvas content;
                for (auto* child : _children) PaintChildAt(*child, content, child->Offset());
                if (!_content) _content = Layer::Create();
                _content->SetPicture(content.Finish());
                _content->SetTransform(Transform::Translation({ 0.f, -_scroll }));
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
                SetSize({ c.HasBoundedWidth() ? c.maxWidth : 0.f, c.HasBoundedHeight() ? c.maxHeight : _config.extent * static_cast<float>(_config.count) });
                for (std::size_t i = 0; i < _children.size() && i < _config.indices.size(); ++i) {
                    _children[i]->Layout(BoxConstraints::Tight({ Size().x, _config.extent }));
                    // Placed by where the item is in the whole list. Not SetOffset: a child moving inside
                    // the content layer repaints the content, which this does every layout anyway.
                    _children[i]->SetOffset({ 0.f, static_cast<float>(_config.indices[i]) * _config.extent });
                }
                Scroll(0.f);
            }

        private:
            [[nodiscard]] float MaxScroll() const { return std::max(0.f, _config.extent * static_cast<float>(_config.count) - Size().y); }

            /** @brief Scrolls, moving the content layer, and asks for the items now in view. */
            bool Scroll(const float by)
            {
                const float next = std::clamp(_scroll + by, 0.f, MaxScroll());
                const bool moved = next != _scroll;
                _scroll = next;
                if (_content) _content->SetTransform(Transform::Translation({ 0.f, -_scroll }));
                // In view, and a screen either side, so a scroll rarely shows an item before it is built.
                if (_config.extent > 0.f && _config.wants && Size().y > 0.f) {
                    const float screen = Size().y;
                    const auto first = static_cast<std::size_t>(std::max(0.f, (_scroll - screen) / _config.extent));
                    const auto last = std::min(_config.count, static_cast<std::size_t>(std::ceil((_scroll + 2.f * screen) / _config.extent)));
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
            std::size_t count;
            float extent;
            std::function<Widget(std::size_t)> builder;
            std::function<void(std::size_t, std::size_t)> onRange;
            std::size_t first = 0, last = 0;
            std::size_t toldFirst = SIZE_MAX, toldLast = SIZE_MAX;
            /// Items built for the range, kept while they stay in it: the same widget again is a widget
            /// its element does not even look at.
            std::map<std::size_t, Widget> built;

            ListState(const std::size_t n, const float e, std::function<Widget(std::size_t)> b, std::function<void(std::size_t, std::size_t)> r)
                : count(n), extent(e), builder(std::move(b)), onRange(std::move(r))
            {
                last = std::min<std::size_t>(count, 64);   // until the list knows how tall it is
            }

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& l = static_cast<const ListState&>(newer);
                count = l.count;
                extent = l.extent;
                builder = l.builder;
                onRange = l.onRange;
                toldFirst = toldLast = SIZE_MAX;   // a new listener has been told nothing
                built.clear();   // a new builder may build them differently
                last = std::min(last, count);
                first = std::min(first, last);
            }

            Widget Build() override
            {
                auto widget = std::make_shared<VirtualListWidget>();
                widget->config.count = count;
                widget->config.extent = extent;
                widget->config.wants = [this](const std::size_t f, const std::size_t l) {
                    if (f != first || l != last) SetState([&] { first = f; last = l; });
                };
                std::erase_if(built, [&](const auto& e) { return e.first < first || e.first >= last; });
                for (std::size_t i = first; i < last; ++i) {
                    // Keyed by index, so an item that stays in view stays built; each with its own layer.
                    auto& item = built[i];
                    if (!item) item = RepaintBoundary(builder(i)).Key(std::to_string(i));
                    widget->children.push_back(item);
                    widget->config.indices.push_back(i);
                }
                if (onRange && (first != toldFirst || last != toldLast)) {
                    toldFirst = first;
                    toldLast = last;
                    onRange(first, last);
                }
                return Widget(widget);
            }
        };
    }

    Widget ListView(const std::size_t count, const float itemExtent, std::function<Widget(std::size_t)> builder,
                    std::function<void(std::size_t, std::size_t)> onRange)
    {
        return Make<ListState>(count, itemExtent, std::move(builder), std::move(onRange));
    }
}
