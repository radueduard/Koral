//
// koral-ui: the rest of the controls — what a tools interface is made of besides buttons and fields.
// Lines between things, radio buttons, rows that can be picked, headers that fold what is under them,
// trees, tabs, tips, dialogs, a colour picker, plots and tables. Most are widgets made of other
// widgets; the picker and the plot paint themselves.
//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>

#include <window.h>

#include "boxes.h"
#include "glass.h"
#include "element.h"

namespace kui
{
    namespace {
        /** Something that looks different while the pointer is over it, and can be pressed. */
        struct Hover final : StatefulWidget {
            std::function<Widget(bool hovered)> build;
            std::function<void()> onTap;
            bool hovered = false;

            Hover(std::function<Widget(bool)> b, std::function<void()> t) : build(std::move(b)), onTap(std::move(t)) {}

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& h = static_cast<const Hover&>(newer);
                build = h.build;
                onTap = h.onTap;
            }

            Widget Build() override
            {
                GestureOptions gestures;
                gestures.onEnter = [this] { SetState([this] { hovered = true; }); };
                gestures.onExit = [this] { SetState([this] { hovered = false; }); };
                // Copied out first: what it runs may well take this widget away.
                if (onTap) gestures.onTap = [this] { if (const auto run = onTap) run(); };
                return GestureDetector(std::move(gestures), build(hovered));
            }
        };

        /** A box that paints itself: as wide as it is given room for (or @p width), @p height tall. */
        class RenderPaintBox final : public RenderContainer {
        public:
            void Set(std::function<void(Canvas&, glm::vec2)> painter, const glm::vec2 size, std::string text)
            {
                _painter = std::move(painter);
                _text = std::move(text);
                if (size != _preferred) { _preferred = size; MarkNeedsLayout(); }
                MarkNeedsPaint();
            }
            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
            [[nodiscard]] std::string DebugText() const override { return _text; }
            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                if (!_painter) return;
                canvas.Save();
                canvas.Translate(offset);
                _painter(canvas, Size());
                canvas.Restore();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                SetSize(c.Constrain({ _preferred.x >= 0.f ? _preferred.x : c.HasBoundedWidth() ? c.maxWidth : 200.f,
                                      _preferred.y >= 0.f ? _preferred.y : c.HasBoundedHeight() ? c.maxHeight : 60.f }));
            }

        private:
            std::function<void(Canvas&, glm::vec2)> _painter;
            glm::vec2 _preferred { -1.f, -1.f };
            std::string _text;      ///< What it says, for debug::Texts: it paints its words itself.
        };

        struct PaintBoxWidget final : RenderObjectWidget {
            std::function<void(Canvas&, glm::vec2)> painter;
            glm::vec2 size;
            std::string text;
            PaintBoxWidget(std::function<void(Canvas&, glm::vec2)> p, const glm::vec2 s, std::string t)
                : painter(std::move(p)), size(s), text(std::move(t)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderPaintBox>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderPaintBox&>(object).Set(painter, size, text); }
        };

        /** @p text: what it says, when it paints words of its own — what debug::Texts reads. */
        Widget paintBox(std::function<void(Canvas&, glm::vec2)> painter, const glm::vec2 size, std::string text = {})
        {
            return Make<PaintBoxWidget>(std::move(painter), size, std::move(text));
        }

        /** The arrow before a header or a tree's node: pointing right when shut, down when open. */
        /** @p a, a little of the way to @p b: an outline that can be seen on its surface. */
        Color mix3(const Color a, const Color b)
        {
            constexpr float f = 0.10f;
            return { a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a };
        }

        Widget arrow(const bool open, const Color color)
        {
            return CustomPaint([open, color](Canvas& canvas, const glm::vec2 size) {
                const glm::vec2 c = size * 0.5f;
                const Paint paint = Paint::Stroked(color, 1.5f);
                if (open) {
                    canvas.DrawLine(c + glm::vec2(-4.f, -2.f), c + glm::vec2(0.f, 2.5f), paint);
                    canvas.DrawLine(c + glm::vec2(0.f, 2.5f), c + glm::vec2(4.f, -2.f), paint);
                } else {
                    canvas.DrawLine(c + glm::vec2(-2.f, -4.f), c + glm::vec2(2.5f, 0.f), paint);
                    canvas.DrawLine(c + glm::vec2(2.5f, 0.f), c + glm::vec2(-2.f, 4.f), paint);
                }
            }, { 16.f, 16.f });
        }

        // ---- tooltip ----------------------------------------------------------------------------------

        /** Its child; while the pointer is over it, the view shows its text by the pointer. */
        class RenderTooltip final : public RenderContainer {
        public:
            void Set(std::string text) { _text = std::move(text); }
            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
            bool HandleEvent(const PointerEvent& event) override
            {
                // The innermost has it: it is told first, and one around it leaves what it said alone.
                if (event.type == PointerEvent::Type::eHover)
                    if (Owner* owner = GetOwner(); owner && owner->tooltip.empty()) owner->tooltip = _text;
                return false;
            }

        private:
            std::string _text;
        };

        struct TooltipWidget final : RenderObjectWidget {
            std::string text;
            std::vector<Widget> children;
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderTooltip>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderTooltip&>(object).Set(text); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };

        // ---- size observer ----------------------------------------------------------------------------

        /** Its child, and someone told how big that came out whenever it comes out another size. */
        class RenderSizeObserver final : public RenderContainer {
        public:
            void Set(std::function<void(glm::vec2, glm::vec2)> onChanged) { _onChanged = std::move(onChanged); }

        protected:
            void PerformLayout() override
            {
                if (Child()) RenderContainer::PerformLayout();
                else SetSize(Constraints().Constrain({ Constraints().HasBoundedWidth() ? Constraints().maxWidth : 0.f,
                                                       Constraints().HasBoundedHeight() ? Constraints().maxHeight : 0.f }));
                if (Size() == _told) return;
                _told = Size();
                const float scale = GetOwner() ? GetOwner()->scale : 1.f;
                // Copied out first: what it runs may well replace this widget.
                if (const auto tell = _onChanged) tell(_told, glm::round(_told * scale));
            }

        private:
            std::function<void(glm::vec2, glm::vec2)> _onChanged;
            glm::vec2 _told { -1.f, -1.f };
        };

        struct SizeObserverWidget final : RenderObjectWidget {
            std::function<void(glm::vec2, glm::vec2)> onChanged;
            std::vector<Widget> children;
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderSizeObserver>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderSizeObserver&>(object).Set(onChanged); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };

        // ---- colour -----------------------------------------------------------------------------------

        struct Hsv { float h = 0.f, s = 0.f, v = 0.f; };

        Hsv toHsv(const Color c)
        {
            const float high = std::max({ c.r, c.g, c.b }), low = std::min({ c.r, c.g, c.b }), d = high - low;
            Hsv out { 0.f, high > 0.f ? d / high : 0.f, high };
            if (d > 0.f) {
                if (high == c.r) out.h = std::fmod((c.g - c.b) / d, 6.f);
                else if (high == c.g) out.h = (c.b - c.r) / d + 2.f;
                else out.h = (c.r - c.g) / d + 4.f;
                out.h /= 6.f;
                if (out.h < 0.f) out.h += 1.f;
            }
            return out;
        }

        Color fromHsv(const Hsv in, const float alpha = 1.f)
        {
            const float h = (in.h - std::floor(in.h)) * 6.f;
            const float c = in.v * in.s, x = c * (1.f - std::abs(std::fmod(h, 2.f) - 1.f)), m = in.v - c;
            glm::vec3 rgb;
            switch (static_cast<int>(h)) {
            case 0: rgb = { c, x, 0.f }; break;
            case 1: rgb = { x, c, 0.f }; break;
            case 2: rgb = { 0.f, c, x }; break;
            case 3: rgb = { 0.f, x, c }; break;
            case 4: rgb = { x, 0.f, c }; break;
            default: rgb = { c, 0.f, x }; break;
            }
            return { rgb.r + m, rgb.g + m, rgb.b + m, alpha };
        }

        /** A square of every saturation and value of a hue, a bar of hues under it and, when asked, one of alpha. */
        struct ColorPickerWidget final : StatefulWidget {
            Color color;
            std::function<void(Color)> onChanged;
            ColorPickerOptions options;
            Hsv hsv;            // kept: a grey has no hue of its own, and black no saturation — the picker remembers them

            ColorPickerWidget(const Color c, std::function<void(Color)> f, const ColorPickerOptions o)
                : color(c), onChanged(std::move(f)), options(o), hsv(toHsv(c)) {}

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& p = static_cast<const ColorPickerWidget&>(newer);
                onChanged = p.onChanged;
                options = p.options;
                if (p.color == color) return;
                color = p.color;
                // Unless it is what the picker itself just said, take its hue and saturation from it.
                const Color mine = fromHsv(hsv, color.a);
                if (std::abs(mine.r - color.r) + std::abs(mine.g - color.g) + std::abs(mine.b - color.b) > 0.004f) {
                    const Hsv now = toHsv(color);
                    hsv = { now.s > 0.f ? now.h : hsv.h, now.v > 0.f ? now.s : hsv.s, now.v };
                }
            }

            void Say()
            {
                const Color now = fromHsv(hsv, color.a);
                SetState([this, now] { color = now; });
                if (const auto told = onChanged) told(now);
            }

            Widget Bar(const float width, std::function<void(float)> set, std::function<void(Canvas&, glm::vec2)> painter) const
            {
                const auto at = [set = std::move(set), width](const glm::vec2 local) { set(std::clamp(local.x / std::max(width, 1.f), 0.f, 1.f)); };
                GestureOptions gestures;
                gestures.onTapDown = at;
                gestures.onPanStart = at;
                gestures.onPanUpdate = [at](glm::vec2, const glm::vec2 local) { at(local); };
                return GestureDetector(std::move(gestures), CustomPaint(std::move(painter), { width, 14.f }));
            }

            Widget Build() override
            {
                const Theme t = Theme::Current();
                const float width = options.width > 0.f ? options.width : 220.f, square = std::round(width * 0.62f);
                const Hsv shown = hsv;
                const Color full = fromHsv({ shown.h, 1.f, 1.f });
                const float alpha = color.a;

                const auto pick = [this, width, square](const glm::vec2 local) {
                    hsv.s = std::clamp(local.x / width, 0.f, 1.f);
                    hsv.v = 1.f - std::clamp(local.y / square, 0.f, 1.f);
                    Say();
                };
                GestureOptions gestures;
                gestures.onTapDown = pick;
                gestures.onPanStart = pick;
                gestures.onPanUpdate = [pick](glm::vec2, const glm::vec2 local) { pick(local); };
                Widget field = GestureDetector(std::move(gestures), CustomPaint([t, shown, full](Canvas& canvas, const glm::vec2 size) {
                    const RRect box { Rect::FromSize(size), 10.f };
                    // White to the hue across, and over that clear to black downwards.
                    canvas.DrawRRect(box, Paint {}.SetGradient(Gradient::Linear({ 0.f, 0.f }, { size.x, 0.f }, colors::White, full)));
                    canvas.DrawRRect(box, Paint {}.SetGradient(Gradient::Linear({ 0.f, 0.f }, { 0.f, size.y }, colors::Black.WithAlpha(0.f), colors::Black)));
                    const glm::vec2 at { shown.s * size.x, (1.f - shown.v) * size.y };
                    canvas.DrawCircle(at, 6.f, Paint::Stroked(colors::Black.WithAlpha(0.6f), 3.f));
                    canvas.DrawCircle(at, 6.f, Paint::Fill(fromHsv(shown)).SetStroke(2.f, colors::White));
                }, { width, square }));

                const auto thumb = [](Canvas& canvas, const glm::vec2 size, const float where, const Color fill) {
                    const glm::vec2 at { std::clamp(where * size.x, 7.f, size.x - 7.f), size.y * 0.5f };
                    canvas.DrawCircle(at, 7.f, Paint::Stroked(colors::Black.WithAlpha(0.5f), 3.f));
                    canvas.DrawCircle(at, 7.f, Paint::Fill(fill).SetStroke(2.f, colors::White));
                };
                std::vector<Widget> parts;
                parts.push_back(std::move(field));
                parts.push_back(Bar(width, [this](const float v) { hsv.h = std::min(v, 0.9999f); Say(); }, [shown, full, thumb](Canvas& canvas, const glm::vec2 size) {
                    std::vector<GradientStop> stops;
                    for (int i = 0; i <= 6; ++i) stops.push_back({ static_cast<float>(i) / 6.f, fromHsv({ static_cast<float>(i) / 6.f, 1.f, 1.f }) });
                    canvas.DrawRRect({ Rect::FromSize(size), size.y * 0.5f }, Paint {}.SetGradient(Gradient::Linear({ 0.f, 0.f }, { size.x, 0.f }, std::move(stops))));
                    thumb(canvas, size, shown.h, full);
                }));
                if (options.alpha) {
                    parts.push_back(Bar(width, [this](const float v) {
                        const Color now = color.WithAlpha(v);
                        SetState([this, now] { color = now; });
                        if (const auto told = onChanged) told(now);
                    }, [t, shown, alpha, thumb](Canvas& canvas, const glm::vec2 size) {
                        const Color solid = fromHsv(shown);
                        const RRect bar { Rect::FromSize(size), size.y * 0.5f };
                        canvas.DrawRRect(bar, Paint::Fill(t.surfacePressed));
                        canvas.DrawRRect(bar, Paint {}.SetGradient(Gradient::Linear({ 0.f, 0.f }, { size.x, 0.f }, solid.WithAlpha(0.f), solid)));
                        thumb(canvas, size, alpha, solid);
                    }));
                }
                if (options.hex) {
                    char text[16];
                    const auto byte = [](const float v) { return static_cast<int>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
                    if (options.alpha) std::snprintf(text, sizeof text, "#%02X%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b), byte(color.a));
                    else std::snprintf(text, sizeof text, "#%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b));
                    TextStyle style = t.textStyle;
                    style.color = t.textMuted;
                    style.size = std::max(t.textStyle.size - 2.f, 10.f);
                    const Color swatch = color;
                    parts.push_back(Row({
                        CustomPaint([swatch, t](Canvas& canvas, const glm::vec2 size) {
                            canvas.DrawRRect({ Rect::FromSize(size), 6.f }, Paint::Fill(swatch).SetStroke(1.f, t.border));
                        }, { 28.f, 18.f }),
                        Text(text, style, TextAlign::eStart, false),
                    }, { .gap = 8.f }));
                }
                return Column(std::move(parts), { .crossAxisAlignment = CrossAxisAlignment::eStart, .gap = 10.f });
            }
        };
    }

    namespace {
        // ---- step slider ----------------------------------------------------------------------------------

        /** A track of places, and a thumb as wide as one of them at the one that is chosen. */
        class RenderStepSlider final : public RenderContainer {
        public:
            struct Config {
                int value = 0, steps = 1;
                std::function<void(int)> onChanged;
                StepSliderOptions options;
            };

            void Set(const Config& c)
            {
                if (c.options.width != _config.options.width) MarkNeedsLayout();
                _config = c;
                _config.steps = std::max(c.steps, 1);
                MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    _dragging = true;
                    Pick(event.local.x);
                    MarkNeedsPaint();
                    return true;
                case PointerEvent::Type::eMove:
                    if (!_dragging) return false;
                    Pick(event.local.x);
                    return true;
                case PointerEvent::Type::eUp:
                case PointerEvent::Type::eCancel:
                    _dragging = false;
                    MarkNeedsPaint();
                    return true;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const glm::vec2 size = Size();
                canvas.Save();
                canvas.Translate(offset);
                // The wide one, and in it the one that says where it is.
                canvas.DrawRRect({ Rect::FromSize(size), size.y * 0.5f }, Paint::Fill(t.surfacePressed).SetStroke(1.f, t.border));
                const float step = (size.x - 2.f * Inset) / static_cast<float>(_config.steps);
                const int at = std::clamp(_config.value, 0, _config.steps - 1);
                const float grow = _dragging ? 1.f : 0.f;
                const Rect thumb = Rect::XYWH(Inset + step * static_cast<float>(at), Inset, step, size.y - 2.f * Inset).Inflate(grow);
                canvas.DrawRRect({ thumb, thumb.Height() * 0.5f }, Paint::Fill(t.primary));
                TextStyle style = t.textStyle;
                style.size = std::max(style.size - 2.f, 10.f);
                for (int i = 0; i < _config.steps; ++i) {
                    const glm::vec2 c { Inset + step * (static_cast<float>(i) + 0.5f), size.y * 0.5f };
                    if (static_cast<std::size_t>(i) < _config.options.labels.size() && !_config.options.labels[static_cast<std::size_t>(i)].empty()) {
                        const std::string& label = _config.options.labels[static_cast<std::size_t>(i)];
                        style.color = i == at ? t.onPrimary : t.text;
                        const Paragraph text(label, style);
                        canvas.DrawText(label, { std::round(c.x - text.Size().x * 0.5f), std::round(c.y - text.Size().y * 0.5f) }, style);
                    }
                }
                canvas.Restore();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                const float width = _config.options.width >= 0.f ? _config.options.width : c.HasBoundedWidth() ? c.maxWidth : 240.f;
                SetSize(c.Constrain({ width, std::max(Theme::Current().controlHeight - 4.f, 20.f) }));
            }

        private:
            static constexpr float Inset = 3.f;

            void Pick(const float x)
            {
                const float step = (Size().x - 2.f * Inset) / static_cast<float>(_config.steps);
                const int at = std::clamp(static_cast<int>(std::floor((x - Inset) / std::max(step, 1.f))), 0, _config.steps - 1);
                if (at != _config.value && _config.onChanged) _config.onChanged(at);
            }

            Config _config;
            bool _dragging = false;
        };

        struct StepSliderWidget final : RenderObjectWidget {
            RenderStepSlider::Config config;
            explicit StepSliderWidget(RenderStepSlider::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderStepSlider>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderStepSlider&>(object).Set(config); }
        };

        // ---- gradient editor ------------------------------------------------------------------------------

        constexpr std::size_t MostStops = 8;    // what a gradient holds

        Color lerp(const Color a, const Color b, const float t)
        {
            return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
        }

        /** The colour a gradient of @p stops (in order) has at @p at. */
        Color sample(const std::vector<GradientStop>& stops, const float at)
        {
            if (stops.empty()) return colors::White;
            if (at <= stops.front().offset) return stops.front().color;
            for (std::size_t i = 1; i < stops.size(); ++i) {
                if (at > stops[i].offset) continue;
                const float span = stops[i].offset - stops[i - 1].offset;
                return span > 0.f ? lerp(stops[i - 1].color, stops[i].color, (at - stops[i - 1].offset) / span) : stops[i].color;
            }
            return stops.back().color;
        }

        /** The gradient as a bar, and a handle under it for each stop: pressed, dragged, added and taken away. */
        class RenderGradientBar final : public RenderContainer {
        public:
            struct Config {
                std::vector<GradientStop> stops;    // in order
                int selected = 0;
                float width = 260.f;
                std::function<void(std::vector<GradientStop> stops, int selected)> onChanged;
                std::function<void(int)> onSelected;
            };

            void Set(const Config& c)
            {
                if (c.width != _config.width) MarkNeedsLayout();
                _config = c;
                MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eDown: {
                    const int handle = HandleAt(event.local);
                    if (event.button == kor::MouseButton::eRight) {
                        if (handle < 0 || _config.stops.size() <= 2) return handle >= 0;
                        auto stops = _config.stops;
                        stops.erase(stops.begin() + handle);
                        Tell(std::move(stops), std::min(handle, static_cast<int>(_config.stops.size()) - 2));
                        return true;
                    }
                    if (event.button != kor::MouseButton::eLeft) return false;
                    if (handle >= 0) {
                        _dragging = handle;
                        if (handle != _config.selected && _config.onSelected) _config.onSelected(handle);
                        return true;
                    }
                    // On the bar, where no handle is: a stop of the colour that is there.
                    if (event.local.y > Bar || _config.stops.size() >= MostStops) return true;
                    const float at = OffsetAt(event.local.x);
                    auto stops = _config.stops;
                    const auto where = std::ranges::find_if(stops, [at](const GradientStop& s) { return s.offset > at; });
                    const int index = static_cast<int>(where - stops.begin());
                    stops.insert(where, { at, sample(_config.stops, at) });
                    _dragging = index;
                    Tell(std::move(stops), index);
                    return true;
                }
                case PointerEvent::Type::eMove: {
                    if (_dragging < 0 || _dragging >= static_cast<int>(_config.stops.size())) return false;
                    // Moved, and kept in order: past a neighbour, it changes places with it.
                    auto stops = _config.stops;
                    GradientStop moved = stops[static_cast<std::size_t>(_dragging)];
                    moved.offset = OffsetAt(event.local.x);
                    stops.erase(stops.begin() + _dragging);
                    const auto where = std::ranges::find_if(stops, [&moved](const GradientStop& s) { return s.offset > moved.offset; });
                    const int index = static_cast<int>(where - stops.begin());
                    stops.insert(where, moved);
                    _dragging = index;
                    Tell(std::move(stops), index);
                    return true;
                }
                case PointerEvent::Type::eUp:
                case PointerEvent::Type::eCancel:
                    _dragging = -1;
                    return true;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                canvas.Save();
                canvas.Translate(offset);
                const RRect bar { Rect::LTRB(Pad, 0.f, Size().x - Pad, Bar), 8.f };
                // Two shades behind it, so that a see-through stop shows as one.
                canvas.Save();
                canvas.ClipRRect(bar);
                canvas.DrawRect(bar.rect, Paint::Fill(colors::White));
                constexpr float Check = 8.f;
                for (float x = bar.rect.left; x < bar.rect.right; x += 2.f * Check) {
                    canvas.DrawRect(Rect::XYWH(x, 0.f, Check, Bar * 0.5f), Paint::Fill(Color::Hex(0xB0B0B0)));
                    canvas.DrawRect(Rect::XYWH(x + Check, Bar * 0.5f, Check, Bar * 0.5f), Paint::Fill(Color::Hex(0xB0B0B0)));
                }
                canvas.Restore();
                if (!_config.stops.empty()) {
                    // A gradient starts at its first stop and ends at its last: flat before and after.
                    canvas.DrawRRect(bar, kui::Paint {}.SetGradient(Gradient::Linear({ bar.rect.left, 0.f }, { bar.rect.right, 0.f }, _config.stops)));
                }
                canvas.DrawRRect(bar, Paint::Stroked(t.border, 1.f));
                for (std::size_t i = 0; i < _config.stops.size(); ++i) {
                    const bool on = static_cast<int>(i) == _config.selected;
                    const float x = X(_config.stops[i].offset);
                    // A pin: a point up at the bar, and a swatch of the stop's colour under it.
                    canvas.DrawTriangle({ x, Bar - 1.f }, { x - 5.f, Bar + 6.f }, { x + 5.f, Bar + 6.f }, Paint::Fill(on ? t.primary : t.textMuted));
                    const Rect swatch = Rect::FromCenter({ x, Bar + 12.f }, 13.f, 13.f);
                    canvas.DrawRRect({ swatch, 4.f }, Paint::Fill(_config.stops[i].color.WithAlpha(1.f)).SetStroke(on ? 2.f : 1.f, on ? t.primary : t.textMuted));
                }
                canvas.Restore();
            }

        protected:
            void PerformLayout() override { SetSize(Constraints().Constrain({ _config.width, Bar + 22.f })); }

        private:
            static constexpr float Bar = 26.f, Pad = 8.f;

            [[nodiscard]] float X(const float offset) const { return Pad + std::clamp(offset, 0.f, 1.f) * (Size().x - 2.f * Pad); }
            [[nodiscard]] float OffsetAt(const float x) const { return std::clamp((x - Pad) / std::max(Size().x - 2.f * Pad, 1.f), 0.f, 1.f); }

            /** The handle under @p local — the nearest, when they crowd — or -1. Its pin, or the bar right over it. */
            [[nodiscard]] int HandleAt(const glm::vec2 local) const
            {
                int found = -1;
                float nearest = local.y > Bar - 2.f ? 9.f : 5.f;
                for (std::size_t i = 0; i < _config.stops.size(); ++i) {
                    const float d = std::abs(X(_config.stops[i].offset) - local.x);
                    if (d < nearest) { nearest = d; found = static_cast<int>(i); }
                }
                return found;
            }

            void Tell(std::vector<GradientStop> stops, const int selected)
            {
                // Shown at once, whoever hears of it: what is dragged follows the pointer even where nothing says so again.
                _config.stops = stops;
                _config.selected = selected;
                MarkNeedsPaint();
                if (const auto told = _config.onChanged) told(std::move(stops), selected);
            }

            Config _config;
            int _dragging = -1;
        };

        struct GradientBarWidget final : RenderObjectWidget {
            RenderGradientBar::Config config;
            explicit GradientBarWidget(RenderGradientBar::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderGradientBar>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderGradientBar&>(object).Set(config); }
        };

        std::vector<GradientStop> inOrder(std::vector<GradientStop> stops)
        {
            if (stops.size() > MostStops) stops.resize(MostStops);
            for (auto& stop : stops) stop.offset = std::clamp(stop.offset, 0.f, 1.f);
            std::ranges::stable_sort(stops, {}, &GradientStop::offset);
            return stops;
        }

        struct GradientEditorWidget final : StatefulWidget {
            std::vector<GradientStop> stops;
            std::function<void(std::vector<GradientStop>)> onChanged;
            GradientEditorOptions options;
            int selected = 0;

            GradientEditorWidget(std::vector<GradientStop> s, std::function<void(std::vector<GradientStop>)> f, const GradientEditorOptions o)
                : stops(inOrder(std::move(s))), onChanged(std::move(f)), options(o) {}

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& e = static_cast<const GradientEditorWidget&>(newer);
                stops = e.stops;
                onChanged = e.onChanged;
                options = e.options;
                selected = std::clamp(selected, 0, std::max(static_cast<int>(stops.size()) - 1, 0));
            }

            void Changed(std::vector<GradientStop> now, const int pick)
            {
                SetState([this, &now, pick] { stops = now; selected = pick; });
                if (const auto told = onChanged) told(std::move(now));
            }

            Widget Build() override
            {
                const Theme t = Theme::Current();
                RenderGradientBar::Config bar;
                bar.stops = stops;
                bar.selected = selected;
                bar.width = options.width;
                bar.onChanged = [this](std::vector<GradientStop> now, const int pick) { Changed(std::move(now), pick); };
                bar.onSelected = [this](const int pick) { SetState([this, pick] { selected = pick; }); };
                std::vector<Widget> parts;
                parts.push_back(Make<GradientBarWidget>(std::move(bar)));
                if (stops.empty()) return parts.front();

                const auto at = static_cast<std::size_t>(std::clamp(selected, 0, static_cast<int>(stops.size()) - 1));
                char text[48];
                std::snprintf(text, sizeof text, "Stop %d of %d  ·  %d%%", static_cast<int>(at) + 1, static_cast<int>(stops.size()),
                              static_cast<int>(stops[at].offset * 100.f + 0.5f));
                TextStyle style = t.textStyle;
                style.color = t.textMuted;
                style.size = std::max(style.size - 2.f, 10.f);
                parts.push_back(SizedBox(options.width, -1.f, Row({
                    Expanded(Text(text, style, TextAlign::eStart, false)),
                    Button("Remove", [this, at] {
                        if (stops.size() <= 2 || at >= stops.size()) return;
                        auto now = stops;
                        now.erase(now.begin() + static_cast<std::ptrdiff_t>(at));
                        Changed(std::move(now), std::min(static_cast<int>(at), static_cast<int>(stops.size()) - 2));
                    }, ButtonOptions {}.SetStyle(ButtonStyle::eSecondary).SetEnabled(stops.size() > 2)),
                })));
                if (options.picker) {
                    parts.push_back(ColorPicker(stops[at].color, [this, at](const Color c) {
                        if (at >= stops.size()) return;
                        auto now = stops;
                        now[at].color = c;
                        Changed(std::move(now), static_cast<int>(at));
                    }, ColorPickerOptions {}.SetWidth(options.width)));
                }
                return Column(std::move(parts), { .crossAxisAlignment = CrossAxisAlignment::eStart, .gap = 8.f });
            }
        };

        // ---- title bar ------------------------------------------------------------------------------------

        /**
         * A part of the window's title bar that is the window's own business: the room that moves it, or
         * one of its buttons. Showing one is what takes the system's title bar away.
         */
        class RenderWindowControl final : public RenderContainer {
        public:
            // eSystemButtons: room for the system's own, where they stay (macOS); it moves the window like eDrag.
            enum class Kind : std::uint8_t { eDrag, eMinimize, eMaximize, eClose, eSystemButtons };
            struct Config {
                Kind kind = Kind::eDrag;
                std::string title;          // eDrag: written at its left
                float height = 36.f;
            };

            void Set(const Config& c)
            {
                if (c.height != _config.height || c.kind != _config.kind) MarkNeedsLayout();
                _config = c;
                MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                const kor::Window* window = GetOwner() ? GetOwner()->window : nullptr;
                switch (event.type) {
                case PointerEvent::Type::eEnter: _hovered = true; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eExit: _hovered = false; _pressed = false; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    if (_config.kind != Kind::eDrag && _config.kind != Kind::eSystemButtons) { _pressed = true; MarkNeedsPaint(); return true; }
                    if (window) {
                        // Twice in a moment: bigger, or back. Once: the system moves the window with the pointer.
                        const auto now = std::chrono::steady_clock::now();
                        const bool twice = now - _lastPress < std::chrono::milliseconds(400);
                        _lastPress = twice ? std::chrono::steady_clock::time_point {} : now;
                        if (twice) window->ToggleMaximize();
                        else window->BeginMove();
                    }
                    return true;
                case PointerEvent::Type::eUp:
                    if (_pressed && _hovered && window) {
                        if (_config.kind == Kind::eMinimize) window->Minimize();
                        else if (_config.kind == Kind::eMaximize) window->ToggleMaximize();
                        else if (_config.kind == Kind::eClose) window->RequestClose();
                    }
                    _pressed = false;
                    MarkNeedsPaint();
                    return true;
                case PointerEvent::Type::eCancel: _pressed = false; MarkNeedsPaint(); return false;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const kor::Window* window = GetOwner() ? GetOwner()->window : nullptr;
                // The window is the interface's to head now.
                if (window && !window->HasCustomTitleBar()) window->SetCustomTitleBar(true);
                const glm::vec2 size = Size();
                canvas.Save();
                canvas.Translate(offset);
                if (_config.kind == Kind::eSystemButtons) {
                    // The system draws them.
                } else if (_config.kind == Kind::eDrag) {
                    if (!_config.title.empty()) {
                        TextStyle style = t.textStyle;
                        style.size = std::max(style.size - 2.f, 10.f);
                        style.color = t.textMuted;
                        const Paragraph text(_config.title, style);
                        canvas.Save();
                        canvas.ClipRect(Rect::FromSize(size));
                        canvas.DrawText(_config.title, { 12.f, std::round((size.y - text.Size().y) * 0.5f) }, style);
                        canvas.Restore();
                    }
                } else {
                    const bool close = _config.kind == Kind::eClose;
                    if (_hovered) canvas.DrawRect(Rect::FromSize(size), kui::Paint::Fill(close ? Color::Hex(0xE81123) : _pressed ? t.surfacePressed : t.surfaceHover));
                    const kui::Paint ink = kui::Paint::Stroked(close && _hovered ? colors::White : t.text, 1.f);
                    const glm::vec2 c = glm::round(size * 0.5f) + glm::vec2(0.5f);
                    constexpr float R = 5.f;
                    if (_config.kind == Kind::eMinimize) {
                        canvas.DrawLine(c + glm::vec2(-R, 0.f), c + glm::vec2(R, 0.f), ink);
                    } else if (close) {
                        canvas.DrawLine(c + glm::vec2(-R, -R), c + glm::vec2(R, R), ink);
                        canvas.DrawLine(c + glm::vec2(R, -R), c + glm::vec2(-R, R), ink);
                    } else if (window && window->IsMaximized()) {
                        // Two, one behind the other: it goes back to the size it had.
                        canvas.DrawRRect({ Rect::LTRB(c.x - R, c.y - R + 2.f, c.x + R - 2.f, c.y + R), 1.5f }, ink);
                        canvas.DrawLine(c + glm::vec2(-R + 2.f, -R), c + glm::vec2(R, -R), ink);
                        canvas.DrawLine(c + glm::vec2(R, -R), c + glm::vec2(R, R - 2.f), ink);
                    } else {
                        canvas.DrawRRect({ Rect::FromCenter(c, 2.f * R, 2.f * R), 1.5f }, ink);
                    }
                }
                canvas.Restore();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                const Owner* owner = GetOwner();
                const float width = _config.kind == Kind::eSystemButtons
                    ? (owner && owner->window ? owner->window->SystemButtonsWidth() / owner->desktopScale : 0.f)
                    : _config.kind != Kind::eDrag ? 46.f : c.HasBoundedWidth() ? c.maxWidth : 0.f;
                SetSize(c.Constrain({ width, _config.height }));
            }

        private:
            Config _config;
            bool _hovered = false, _pressed = false;
            std::chrono::steady_clock::time_point _lastPress {};
        };

        struct WindowControlWidget final : RenderObjectWidget {
            RenderWindowControl::Config config;
            explicit WindowControlWidget(RenderWindowControl::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderWindowControl>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderWindowControl&>(object).Set(config); }
        };

        // ---- status bar -----------------------------------------------------------------------------------

        /** The mark before a status line: an i in a ring, a warning's triangle, an error's cross in a disc. */
        Widget statusMark(const StatusLevel level, const Color color)
        {
            return CustomPaint([level, color](Canvas& canvas, const glm::vec2 size) {
                const glm::vec2 c = size * 0.5f;
                switch (level) {
                case StatusLevel::eInfo:
                    canvas.DrawCircle(c, 6.5f, Paint::Stroked(color, 1.5f));
                    canvas.DrawLine(c + glm::vec2(0.f, -0.5f), c + glm::vec2(0.f, 3.5f), Paint::Stroked(color, 1.5f));
                    canvas.DrawCircle(c + glm::vec2(0.f, -3.f), 1.f, Paint::Fill(color));
                    break;
                case StatusLevel::eWarning:
                    canvas.DrawTriangle(c + glm::vec2(0.f, -7.f), c + glm::vec2(-7.5f, 6.f), c + glm::vec2(7.5f, 6.f), Paint::Fill(color));
                    canvas.DrawLine(c + glm::vec2(0.f, -2.5f), c + glm::vec2(0.f, 1.5f), Paint::Stroked(colors::Black, 1.5f));
                    canvas.DrawCircle(c + glm::vec2(0.f, 3.8f), 0.9f, Paint::Fill(colors::Black));
                    break;
                case StatusLevel::eError:
                    canvas.DrawCircle(c, 7.f, Paint::Fill(color));
                    canvas.DrawLine(c + glm::vec2(-2.5f, -2.5f), c + glm::vec2(2.5f, 2.5f), Paint::Stroked(colors::White, 1.5f));
                    canvas.DrawLine(c + glm::vec2(2.5f, -2.5f), c + glm::vec2(-2.5f, 2.5f), Paint::Stroked(colors::White, 1.5f));
                    break;
                }
            }, { 18.f, 18.f });
        }
    }

    Widget StepSlider(const int value, const int steps, std::function<void(int)> onChanged, StepSliderOptions options)
    {
        return Make<StepSliderWidget>(RenderStepSlider::Config { value, steps, std::move(onChanged), std::move(options) });
    }

    Widget GradientEditor(std::vector<GradientStop> stops, std::function<void(std::vector<GradientStop>)> onChanged, const GradientEditorOptions options)
    {
        return Make<GradientEditorWidget>(std::move(stops), std::move(onChanged), options);
    }

    Widget TitleBar(std::string title, TitleBarOptions options)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            using Kind = RenderWindowControl::Kind;
            const Theme t = Theme::Current();
            const float height = options.height;
            std::vector<Widget> row;
#ifdef __APPLE__
            // The system's own buttons stay, at the left, and are the window's buttons here: there whether
            // or not they are asked for.
            constexpr bool own = false;
            row.push_back(Make<WindowControlWidget>(RenderWindowControl::Config { Kind::eSystemButtons, {}, height }));
#else
            constexpr bool own = true;
#endif
            if (options.leading) row.push_back(options.leading);
            row.push_back(Expanded(Make<WindowControlWidget>(RenderWindowControl::Config { Kind::eDrag, title, height })));
            if (options.trailing) row.push_back(options.trailing);
            if (own && options.buttons)
                for (const Kind kind : { Kind::eMinimize, Kind::eMaximize, Kind::eClose })
                    row.push_back(Make<WindowControlWidget>(RenderWindowControl::Config { kind, {}, height }));
            return Container({ .height = height, .decoration = { .color = t.background } }, Row(row));
        });
    }

    Widget StatusBar(std::string message, const StatusLevel level, StatusBarOptions options)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            const Color ink = level == StatusLevel::eError ? Color::Hex(0xF2554B) : level == StatusLevel::eWarning ? Color::Hex(0xF5C242) : t.textMuted;
            TextStyle style = t.textStyle;
            style.size = std::max(style.size - 2.f, 10.f);
            style.color = level == StatusLevel::eInfo ? t.text : ink;
            // One line, cut where the bar ends: the whole of it is in the log.
            std::vector<Widget> row;
            if (!message.empty()) row.push_back(statusMark(level, ink));
            row.push_back(Expanded(ClipRRect(Radii {}, Text(message, style, TextAlign::eStart, false))));
            if (options.trailing) row.push_back(options.trailing);
            // The colour of the window behind the docked panels: it is part of the frame, not one of them.
            return Container({
                .height = options.height,
                .padding = EdgeInsets::Symmetric(10.f, 0.f),
                .decoration = { .color = t.background },
                .alignment = Alignment::CenterLeft(),
            }, Row(row, { .gap = 8.f }));
        });
    }

    // ---- small things ---------------------------------------------------------------------------------

    Widget Separator(const Axis axis, const float thickness)
    {
        const bool across = axis == Axis::eHorizontal;
        // In the theme it is painted in, which is the one where it is.
        return paintBox([](Canvas& canvas, const glm::vec2 size) { canvas.DrawRect(Rect::FromSize(size), Paint::Fill(Theme::Current().border)); },
                        across ? glm::vec2(-1.f, thickness) : glm::vec2(thickness, -1.f));
    }

    Widget BackdropFilter(const Backdrop backdrop, const Radii radius, Widget child)
    {
        return CustomPaint([backdrop, radius](Canvas& canvas, const glm::vec2 size) {
            canvas.DrawBackdrop({ Rect::FromSize(size), radius }, backdrop);
        }, { -1.f, -1.f }, std::move(child));
    }

    // ---- animation --------------------------------------------------------------------------------------

    float Ease(const Curve curve, const float time)
    {
        const float t = std::clamp(time, 0.f, 1.f);
        switch (curve) {
        case Curve::eEaseIn: return t * t * t;
        case Curve::eEaseOut: { const float u = 1.f - t; return 1.f - u * u * u; }
        case Curve::eEaseInOut: return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
        case Curve::eEaseOutBack: { constexpr float c = 1.70158f; const float u = t - 1.f; return 1.f + (c + 1.f) * u * u * u + c * u * u; }
        default: return t;
        }
    }

    namespace {
        /** A value on its way to where it was last told to go, and whatever is built of it. */
        struct AnimatedWidget final : StatefulWidget {
            float target;
            std::function<Widget(float)> builder;
            AnimationOptions options;
            float from = 0.f, value = 0.f, time = 1.f;
            bool running = false;

            AnimatedWidget(const float t, std::function<Widget(float)> b, const AnimationOptions o) : target(t), builder(std::move(b)), options(o) {}

            void InitState() override
            {
                running = false;
                time = 1.f;
                value = from = target;
                // Told where to start: it sets off from there as soon as it is in the tree.
                if (options.initial && *options.initial != target) { value = from = *options.initial; Go(); }
            }

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& a = static_cast<const AnimatedWidget&>(newer);
                builder = a.builder;
                options = a.options;
                if (a.target == target) return;
                target = a.target;
                from = value;
                Go();
            }

            void Go()
            {
                time = 0.f;
                if (options.duration <= 0.f) { value = target; time = 1.f; return; }
                if (running) return;
                running = Animate([this](const float dt) {
                    SetState([&] {
                        time = std::min(time + dt / std::max(options.duration, 1.e-4f), 1.f);
                        value = time >= 1.f ? target : from + (target - from) * Ease(options.curve, time);
                    });
                    running = time < 1.f;
                    return running;
                });
                // Nothing to run it on: there at once, rather than never.
                if (!running) { value = target; time = 1.f; }
            }

            Widget Build() override { return builder ? builder(value) : Widget {}; }
        };

        /** Unfolds its child while open, folds it away when not — and keeps the child it had while it folds. */
        struct RevealWidget final : StatefulWidget {
            bool open;
            Widget child, kept;
            AnimationOptions options;

            RevealWidget(const bool o, Widget c, const AnimationOptions a) : open(o), child(std::move(c)), options(a) {}

            void InitState() override { kept = child; }

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& r = static_cast<const RevealWidget&>(newer);
                open = r.open;
                child = r.child;
                options = r.options;
                if (child) kept = child;
            }

            Widget Build() override
            {
                AnimationOptions o = options;
                o.initial.reset();      // as it is when first shown: nothing unfolds at the start
                return Animated(open ? 1.f : 0.f, [this](const float share) -> Widget {
                    // Folded away: nothing of it is built, laid out or drawn.
                    if (share <= 0.f && !open) { if (!child) kept = {}; return SizedBox(0.f, 0.f); }
                    return detail::RevealBox(share, child ? child : kept);
                }, o);
            }
        };
    }

    Widget Animated(const float target, std::function<Widget(float)> builder, const AnimationOptions options)
    {
        return Make<AnimatedWidget>(target, std::move(builder), options);
    }

    Widget AnimatedOpacity(const float opacity, Widget child, const AnimationOptions options)
    {
        return Animated(opacity, [child = std::move(child)](const float value) { return Opacity(value, child); }, options);
    }

    Widget Appear(Widget child, AnimationOptions options, const float rise)
    {
        if (!options.initial) options.initial = 0.f;
        return Animated(1.f, [child = std::move(child), rise](const float value) {
            const Widget faded = Opacity(std::clamp(value, 0.f, 1.f), child);
            return rise != 0.f ? Translate({ 0.f, std::round((1.f - value) * rise) }, faded) : faded;
        }, options);
    }

    Widget Reveal(const bool open, Widget child, const AnimationOptions options)
    {
        return Make<RevealWidget>(open, std::move(child), options);
    }

    Widget Disabled(Widget child, const bool disabled)
    {
        if (!disabled) return child;
        return Opacity(0.45f, IgnorePointer(std::move(child)));
    }

    Widget RadioButton(const bool selected, std::function<void()> onSelected, std::string label)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            return Make<Hover>([t, selected, label = label](const bool hovered) {
                Widget ring = CustomPaint([t, selected, hovered](Canvas& canvas, const glm::vec2 size) {
                    const glm::vec2 c = size * 0.5f;
                    switch (t.design) {
                    case ThemeDesign::eMaterial:
                        // A thick ring with nothing in it but the dot, and the pointer's wash round it.
                        if (hovered) canvas.DrawCircle(c, 11.f, Paint::Fill((selected ? t.primary : t.text).WithAlpha(0.10f)));
                        canvas.DrawCircle(c, 8.f, Paint::Stroked(selected ? t.primary : hovered ? t.text : t.textMuted, 2.f));
                        if (selected) canvas.DrawCircle(c, 4.5f, Paint::Fill(t.primary));
                        break;
                    case ThemeDesign::eCupertino:
                        // Filled with the accent, a white dot in it, when chosen; the surface in a hairline when not.
                        if (selected) {
                            canvas.DrawCircle(c, 9.f, Paint::Fill(hovered ? t.primaryHover : t.primary));
                            canvas.DrawCircle(c, 3.5f, Paint::Fill(colors::White));
                        } else {
                            canvas.DrawCircle(c, 8.5f, Paint::Fill(hovered ? t.surfaceHover : t.surface).SetStroke(1.f, t.border));
                        }
                        break;
                    case ThemeDesign::eFluent:
                        // Filled with the accent when chosen, the dot in it bigger under the pointer; a thin ring when not.
                        if (selected) {
                            canvas.DrawCircle(c, 9.f, Paint::Fill(t.primary));
                            canvas.DrawCircle(c, hovered ? 5.f : 4.f, Paint::Fill(t.onPrimary));
                        } else {
                            canvas.DrawCircle(c, 8.5f, Paint::Fill(hovered ? t.surfaceHover : t.surface).SetStroke(1.f, t.textMuted));
                        }
                        break;
                    default:
                        canvas.DrawCircle(c, 9.f, Paint::Fill(hovered ? t.surfaceHover : t.surface).SetStroke(1.5f, selected ? t.primary : t.textMuted));
                        if (selected) canvas.DrawCircle(c, 5.f, Paint::Fill(t.primary));
                        break;
                    }
                }, { 22.f, 22.f });
                if (label.empty()) return ring;
                return Row({ ring, Text(label, t.textStyle, TextAlign::eStart, false) }, { .gap = 8.f });
            }, onSelected);
        });
    }

    Widget Selectable(std::string label, const bool selected, std::function<void()> onTap)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            return Make<Hover>([t, selected, label = label](const bool hovered) {
                TextStyle style = t.textStyle;
                const float height = std::max(t.controlHeight - 6.f, 20.f);
                Color fill = selected ? t.primary.WithAlpha(hovered ? 0.95f : 0.8f) : hovered ? t.surfaceHover : colors::Transparent;
                Radii round = std::min(t.radius, 10.f);
                Widget content;
                switch (t.design) {
                case ThemeDesign::eMaterial:
                    // A pill washed with the accent where chosen, with the text's colour under the pointer.
                    fill = selected ? t.primary.WithAlpha(hovered ? 0.30f : 0.22f) : hovered ? t.text.WithAlpha(0.08f) : colors::Transparent;
                    round = height * 0.5f;
                    break;
                case ThemeDesign::eCupertino:
                    // Apple's: the accent itself where chosen, its text in the accent's ink.
                    fill = selected ? t.primary : hovered ? t.text.WithAlpha(0.08f) : colors::Transparent;
                    round = 8.f;
                    if (selected) style.color = t.onPrimary;
                    break;
                case ThemeDesign::eFluent:
                    // Windows': a quiet patch, and a mark of the accent before what is chosen.
                    fill = selected ? (hovered ? t.surfacePressed : t.surfaceHover) : hovered ? t.surfaceHover : colors::Transparent;
                    round = 4.f;
                    if (selected) content = Row({ Container({ .width = 3.f, .height = 16.f, .decoration = { .color = t.primary, .radius = 1.5f } }),
                                                  Text(label, style, TextAlign::eStart, false) }, { .gap = 8.f });
                    break;
                default:
                    if (selected) style.color = t.onPrimary;
                    break;
                }
                if (!content) content = Text(label, style, TextAlign::eStart, false);
                return Container({
                    .height = height,
                    .padding = EdgeInsets::Symmetric(12.f, 0.f),
                    .decoration = { .color = fill, .radius = round },
                    .alignment = Alignment::CenterLeft(),
                }, std::move(content));
            }, onTap);
        });
    }

    // ---- what folds -------------------------------------------------------------------------------------

    Widget CollapsingHeader(std::string title, const bool open, std::function<void(bool)> onToggled, Widget child)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            Widget header = Make<Hover>([t, open, title = title](const bool hovered) {
                Widget line = Row({ arrow(open, t.text), Text(title, t.textStyle, TextAlign::eStart, false) }, { .gap = 6.f });
                switch (t.design) {
                case ThemeDesign::eMaterial: {
                    // Nothing behind it but the pointer's wash, and its title a little heavier.
                    TextStyle heavy = t.textStyle;
                    heavy.weight = 600;
                    return Container({
                        .height = t.controlHeight,
                        .padding = EdgeInsets::Symmetric(10.f, 0.f),
                        .decoration = { .color = t.text.WithAlpha(hovered ? 0.10f : 0.04f), .radius = 4.f },
                        .alignment = Alignment::CenterLeft(),
                    }, Row({ arrow(open, t.text), Text(title, heavy, TextAlign::eStart, false) }, { .gap = 6.f }));
                }
                case ThemeDesign::eCupertino:
                    // A bar of clear glass.
                    return CustomPaint([t, hovered](Canvas& canvas, const glm::vec2 size) {
                        detail::PaintGlass(canvas, t, { Rect::FromSize(size), std::min(size.y * 0.5f, 12.f) }, colors::Transparent,
                                           (t.IsDark() ? -0.06f : -0.3f) + (hovered ? 0.05f : 0.f), false);
                    }, { -1.f, -1.f }, Container({ .height = t.controlHeight, .padding = EdgeInsets::Symmetric(10.f, 0.f), .alignment = Alignment::CenterLeft() },
                                                 std::move(line)));
                case ThemeDesign::eFluent:
                    // A card: the surface, in a thin outline.
                    return Container({
                        .height = t.controlHeight,
                        .padding = EdgeInsets::Symmetric(10.f, 0.f),
                        .decoration = { .color = hovered ? t.surfaceHover : t.surface, .borderWidth = 1.f, .borderColor = mix3(t.border, t.text), .radius = 4.f },
                        .alignment = Alignment::CenterLeft(),
                    }, std::move(line));
                default:
                    return Container({
                        .height = t.controlHeight,
                        .padding = EdgeInsets::Symmetric(10.f, 0.f),
                        .decoration = { .color = hovered ? t.surfacePressed : t.surfaceHover, .radius = std::min(t.radius, 10.f) },
                        .alignment = Alignment::CenterLeft(),
                    }, std::move(line));
                }
            }, [open, onToggled = onToggled] { if (onToggled) onToggled(!open); });
            // What it holds unfolds under it, and folds away: gone, it is not built.
            return Column({ header, Reveal(open && child, child ? Padding(EdgeInsets::Only(8.f, 8.f, 0.f, 4.f), child) : Widget {}) },
                          { .crossAxisAlignment = CrossAxisAlignment::eStretch });
        });
    }

    Widget TreeNode(std::string label, const bool open, std::function<void(bool)> onToggled, std::vector<Widget> children, TreeNodeOptions options)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            const bool leaf = options.leaf;
            const bool selected = options.selected;
            Widget row = Make<Hover>([t, open, leaf, selected, label = label](const bool hovered) {
                TextStyle style = t.textStyle;
                const float height = std::max(t.controlHeight - 10.f, 20.f);
                Color fill = hovered ? t.surfaceHover : colors::Transparent, ink = t.textMuted;
                Radii round = 8.f;
                switch (t.design) {
                case ThemeDesign::eMaterial:
                    // A pill: the accent's wash where chosen, the text's under the pointer.
                    fill = selected ? t.primary.WithAlpha(hovered ? 0.30f : 0.22f) : hovered ? t.text.WithAlpha(0.08f) : colors::Transparent;
                    round = height * 0.5f;
                    break;
                case ThemeDesign::eCupertino:
                    // Apple's: the line chosen is the accent's, its text and its arrow in the accent's ink.
                    fill = selected ? t.primary : hovered ? t.text.WithAlpha(0.07f) : colors::Transparent;
                    round = 6.f;
                    if (selected) { style.color = t.onPrimary; ink = t.onPrimary; }
                    break;
                case ThemeDesign::eFluent:
                    fill = selected ? (hovered ? t.surfacePressed : t.surfaceHover) : hovered ? t.surfaceHover : colors::Transparent;
                    round = 4.f;
                    if (selected) style.color = t.primary;
                    break;
                default:
                    if (selected) style.color = t.primary;
                    break;
                }
                Widget mark = leaf ? CustomPaint([ink](Canvas& canvas, const glm::vec2 size) { canvas.DrawCircle(size * 0.5f, 2.f, Paint::Fill(ink)); }, { 16.f, 16.f })
                                   : arrow(open, ink);
                return Container({
                    .height = height,
                    .padding = EdgeInsets::Symmetric(4.f, 0.f),
                    .decoration = { .color = fill, .radius = round },
                    .alignment = Alignment::CenterLeft(),
                }, Row({ mark, Text(label, style, TextAlign::eStart, false) }, { .gap = 4.f }));
            }, [open, leaf, onToggled = onToggled, onTap = options.onTap] {
                if (onTap) onTap();
                if (!leaf && onToggled) onToggled(!open);
            });
            if (leaf || !open || children.empty()) return row;
            return Column({ row, Padding(EdgeInsets::Only(options.indent, 0.f, 0.f, 0.f),
                                                    Column(children, { .crossAxisAlignment = CrossAxisAlignment::eStretch })) },
                          { .crossAxisAlignment = CrossAxisAlignment::eStretch });
        });
    }

    Widget TabBar(std::vector<std::string> tabs, const int selected, std::function<void(int)> onSelected)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            std::vector<Widget> row;
            if (t.design == ThemeDesign::eCupertino) {
                // Apple's: segments in a grey pill, the one in front lifted off it.
                for (std::size_t i = 0; i < tabs.size(); ++i) {
                    const int index = static_cast<int>(i);
                    const bool on = index == selected;
                    row.push_back(Make<Hover>([t, on, title = tabs[i]](const bool hovered) {
                        TextStyle style = t.textStyle;
                        style.color = on || hovered ? t.text : t.textMuted;
                        Widget segment = Container({
                            .height = std::max(t.controlHeight - 6.f, 20.f),
                            .padding = EdgeInsets::Symmetric(14.f, 0.f),
                        }, Center(Text(title, style, TextAlign::eStart, false)));
                        // The one in front is a piece of glass lying on the bar; the others are the bar's.
                        if (!on) return segment;
                        return CustomPaint([t](Canvas& canvas, const glm::vec2 size) {
                            detail::PaintGlass(canvas, t, { Rect::FromSize(size), size.y * 0.5f }, colors::Transparent, t.IsDark() ? 0.10f : 0.18f);
                        }, { -1.f, -1.f }, segment);
                    }, [index, onSelected] { if (onSelected) onSelected(index); }));
                }
                return Row({ CustomPaint([t](Canvas& canvas, const glm::vec2 size) {
                    detail::PaintGlass(canvas, t, { Rect::FromSize(size), size.y * 0.5f }, colors::Transparent, t.IsDark() ? -0.05f : -0.3f, false);
                }, { -1.f, -1.f }, Padding(EdgeInsets::All(3.f), Row(row, { .mainAxisSize = MainAxisSize::eMin }))) });
            }
            for (std::size_t i = 0; i < tabs.size(); ++i) {
                const int index = static_cast<int>(i);
                const bool on = index == selected;
                row.push_back(Make<Hover>([t, on, title = tabs[i]](const bool hovered) {
                    TextStyle style = t.textStyle;
                    const bool material = t.design == ThemeDesign::eMaterial;
                    style.color = on ? (material ? t.primary : t.text) : hovered ? t.text : t.textMuted;
                    // The one in front is underlined in the accent: Material's line stands on the foot, round at the top only.
                    return CustomPaint([t, on, material](Canvas& canvas, const glm::vec2 size) {
                        if (!on) return;
                        if (material) canvas.DrawRRect({ Rect::LTRB(12.f, size.y - 3.f, size.x - 12.f, size.y), Radii(3.f, 3.f, 0.f, 0.f) }, Paint::Fill(t.primary));
                        else canvas.DrawRRect({ Rect::LTRB(10.f, size.y - 3.f, size.x - 10.f, size.y - 1.f), 1.f }, Paint::Fill(t.primary));
                    }, { -1.f, -1.f }, Container({
                        .height = t.controlHeight,
                        .padding = EdgeInsets::Symmetric(12.f, 0.f),
                        .decoration = { .color = hovered && !on ? t.surfaceHover : colors::Transparent, .radius = 8.f },
                    }, Center(Text(title, style, TextAlign::eStart, false))));
                }, [index, onSelected] { if (onSelected) onSelected(index); }));
            }
            return Column({ Row(row, { .gap = 2.f }), Separator() }, { .crossAxisAlignment = CrossAxisAlignment::eStretch });
        });
    }

    // ---- what is shown over things ----------------------------------------------------------------------

    Widget Tooltip(std::string text, Widget child)
    {
        auto widget = std::make_shared<TooltipWidget>();
        widget->text = std::move(text);
        if (child) widget->children.push_back(std::move(child));
        return Widget(widget);
    }

    Widget SizeObserver(std::function<void(glm::vec2, glm::vec2)> onChanged, Widget child)
    {
        auto widget = std::make_shared<SizeObserverWidget>();
        widget->onChanged = std::move(onChanged);
        if (child) widget->children.push_back(std::move(child));
        return Widget(widget);
    }

    Widget Modal(const bool open, Widget child, Widget dialog, std::function<void()> onDismiss)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            if (!open) return child;
            const Theme t = Theme::Current();
            // Over the child: what dims it and takes every press on it — one outside the dialog dismisses it.
            Widget shade = GestureDetector(GestureOptions {}.OnTap([onDismiss = onDismiss] { if (onDismiss) onDismiss(); }),
                                           Container({ .decoration = { .color = colors::Black.WithAlpha(0.55f) }, .alignment = Alignment::Center() }));
            Widget card = GestureDetector(GestureOptions {}.OnTap([] {}), Container({
                .padding = EdgeInsets::All(20.f),
                .decoration = { .color = t.surface, .borderWidth = 1.f, .borderColor = t.border, .radius = std::min(t.radius, 20.f),
                                .shadowColor = colors::Black.WithAlpha(0.5f), .shadowBlur = 28.f, .shadowOffset = { 0.f, 8.f } },
            }, dialog));
            return Stack({ child, Positioned({ .left = 0.f, .top = 0.f, .right = 0.f, .bottom = 0.f }, shade),
                           Positioned({ .left = 0.f, .top = 0.f, .right = 0.f, .bottom = 0.f }, Center(card)) });
        });
    }

    // ---- colour, plots, tables ----------------------------------------------------------------------------

    Widget ColorPicker(const Color color, std::function<void(Color)> onChanged, const ColorPickerOptions options)
    {
        return Make<ColorPickerWidget>(color, std::move(onChanged), options);
    }

    Widget Plot(std::vector<float> values, PlotOptions options)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            return paintBox([t, values = values, options](Canvas& canvas, const glm::vec2 size) {
                const Color ink = options.color.Visible() ? options.color : t.primary;
                const RRect box { Rect::FromSize(size), 8.f };
                canvas.DrawRRect(box, Paint::Fill(t.surfaceHover.WithAlpha(0.6f)));
                if (!values.empty()) {
                    float low = options.min, high = options.max;
                    if (std::isnan(low)) low = *std::ranges::min_element(values);
                    if (std::isnan(high)) high = *std::ranges::max_element(values);
                    if (!(high > low)) high = low + 1.f;
                    const float pad = 4.f, height = size.y - 2.f * pad, width = size.x - 2.f * pad;
                    const auto y = [&](const float v) { return pad + height * (1.f - std::clamp((v - low) / (high - low), 0.f, 1.f)); };
                    canvas.Save();
                    canvas.ClipRRect(box);
                    if (options.kind == PlotKind::eHistogram) {
                        const float step = width / static_cast<float>(values.size()), gap = step > 3.f ? 1.f : 0.f;
                        for (std::size_t i = 0; i < values.size(); ++i) {
                            const float left = pad + step * static_cast<float>(i);
                            canvas.DrawRect(Rect::LTRB(left, y(values[i]), left + step - gap, size.y - pad), Paint::Fill(ink));
                        }
                    } else if (values.size() > 1) {
                        // A segment at a time, round-ended so that they join: each is one shape the GPU draws, where a
                        // path through them all would be outlined and cut into triangles again every time it changed.
                        Paint line = Paint::Stroked(ink, 1.5f);
                        line.stroke.cap = StrokeCap::eRound;
                        const float step = width / static_cast<float>(values.size() - 1);
                        glm::vec2 from { pad, y(values[0]) };
                        for (std::size_t i = 1; i < values.size(); ++i) {
                            const glm::vec2 to { pad + step * static_cast<float>(i), y(values[i]) };
                            canvas.DrawLine(from, to, line);
                            from = to;
                        }
                    }
                    canvas.Restore();
                }
                if (!options.overlay.empty()) {
                    TextStyle style = t.textStyle;
                    style.size = std::max(t.textStyle.size - 3.f, 10.f);
                    style.color = t.text;
                    const Paragraph text(options.overlay, style);
                    canvas.DrawText(options.overlay, { std::round((size.x - text.Size().x) * 0.5f), 4.f }, style);
                }
            }, options.size, options.overlay);
        });
    }

    Widget Table(std::vector<TableColumn> columns, std::vector<std::vector<Widget>> rows, const TableOptions options)
    {
        // Built where it is in the tree, so that it is in the theme set there.
        return detail::Deferred([=]() -> Widget {
            const Theme t = Theme::Current();
            const auto line = [&](std::vector<Widget> cells, const Color background, const float height) {
                std::vector<Widget> row;
                for (std::size_t c = 0; c < columns.size(); ++c) {
                    Widget cell = Container({ .height = height, .padding = EdgeInsets::Symmetric(10.f, 0.f), .alignment = Alignment::CenterLeft() },
                                            c < cells.size() ? std::move(cells[c]) : Widget {});
                    row.push_back(columns[c].width >= 0.f ? SizedBox(columns[c].width, height, cell) : Expanded(cell, columns[c].flex));
                    if (options.borders && c + 1 < columns.size()) row.push_back(SizedBox(1.f, height, Separator(Axis::eVertical)));
                }
                return Container({ .decoration = { .color = background } }, Row(row, { .crossAxisAlignment = CrossAxisAlignment::eCenter }));
            };
            const float rowHeight = options.rowHeight > 0.f ? options.rowHeight : std::max(t.controlHeight - 4.f, 20.f);
            std::vector<Widget> lines;
            if (options.header) {
                TextStyle style = t.textStyle;
                style.color = t.textMuted;
                std::vector<Widget> titles;
                for (const auto& column : columns) titles.push_back(Text(column.title, style, TextAlign::eStart, false));
                lines.push_back(line(titles, t.surfaceHover, rowHeight));
                if (options.borders) lines.push_back(Separator());
            }
            for (std::size_t r = 0; r < rows.size(); ++r) {
                lines.push_back(line(std::move(rows[r]), options.striped && r % 2 == 1 ? t.surfaceHover.WithAlpha(0.45f) : colors::Transparent, rowHeight));
                if (options.borders && r + 1 < rows.size()) lines.push_back(Separator());
            }
            Widget body = Column(lines, { .crossAxisAlignment = CrossAxisAlignment::eStretch });
            if (!options.borders) return body;
            return ClipRRect(10.f, Container({ .decoration = { .borderWidth = 1.f, .borderColor = t.border, .radius = 10.f } }, body));
        });
    }
}
