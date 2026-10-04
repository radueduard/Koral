//
// koral-ui: what an interface written the way Jetpack Compose writes one asks of the layout — a child drawn
// through a transform, kept to a ratio, sized as a share of the room; a layout whose rule is the caller's
// own; and something shown over everything, anchored to where it was called from.
//

#include <algorithm>
#include <cmath>

#include "boxes.h"
#include "element.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace kui
{
    namespace {
        // ---- transform --------------------------------------------------------------------------------

        /** Its child, drawn — and hit — through a transform about a point of its own box. Layout is the child's. */
        class RenderTransform final : public RenderContainer {
        public:
            struct Config { Transform transform; Alignment origin; };
            void Set(const Config& c)
            {
                if (c.transform == _config.transform && c.origin == _config.origin) return;
                _config = c;
                MarkNeedsPaint();
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                canvas.Save();
                canvas.Translate(offset);
                canvas.Concat(Matrix());
                RenderContainer::Paint(canvas, {});
                canvas.Restore();
            }

            bool HitTest(HitTestResult& result, const glm::vec2 position) override
            {
                // Where the pointer is in the child's own space: it may well be outside this box, turned or grown.
                const bool hit = HitTestChildren(result, Matrix().Inverse().Apply(position));
                if (hit) result.Add(this, position);
                return hit;
            }

            [[nodiscard]] glm::vec2 MapToChild(const RenderObject& child, const glm::vec2 point) const override
            {
                return Matrix().Inverse().Apply(point) - child.Offset();
            }

        private:
            [[nodiscard]] Transform Matrix() const
            {
                const glm::vec2 pivot = _config.origin.Place({}, Size());
                return Transform::Translation(pivot) * _config.transform * Transform::Translation(-pivot);
            }
            Config _config {};
        };

        // ---- aspect ratio -------------------------------------------------------------------------------

        /** As wide as it may be and as tall as that makes it at @p ratio (width over height) — or the other way where only height is bounded. */
        class RenderAspectRatio final : public RenderContainer {
        public:
            void Set(const float ratio)
            {
                if (ratio == _ratio) return;
                _ratio = ratio;
                MarkNeedsLayout();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                const float ratio = _ratio > 0.f ? _ratio : 1.f;
                glm::vec2 size {};
                if (c.HasBoundedWidth()) {
                    size = { c.maxWidth, c.maxWidth / ratio };
                    if (c.HasBoundedHeight() && size.y > c.maxHeight) size = { c.maxHeight * ratio, c.maxHeight };
                } else if (c.HasBoundedHeight()) {
                    size = { c.maxHeight * ratio, c.maxHeight };
                }
                size = c.Constrain(size);
                if (auto* child = Child()) { child->Layout(BoxConstraints::Tight(size)); child->SetOffset({}); }
                SetSize(size);
            }

        private:
            float _ratio = 1.f;
        };

        // ---- a share of the room ------------------------------------------------------------------------

        /** As wide, and as tall, as that share of what it is allowed: Compose's fillMaxWidth(0.5f). A share of 0 leaves that way alone. */
        class RenderFractional final : public RenderContainer {
        public:
            void Set(const glm::vec2 share)
            {
                if (share == _share) return;
                _share = share;
                MarkNeedsLayout();
            }

        protected:
            void PerformLayout() override
            {
                BoxConstraints c = Constraints();
                if (_share.x > 0.f && c.HasBoundedWidth()) c.minWidth = c.maxWidth = std::max(c.minWidth * 0.f, c.maxWidth * _share.x);
                if (_share.y > 0.f && c.HasBoundedHeight()) c.minHeight = c.maxHeight = std::max(c.minHeight * 0.f, c.maxHeight * _share.y);
                if (auto* child = Child()) {
                    child->Layout(c);
                    child->SetOffset({});
                    SetSize(child->Size());
                } else {
                    SetSize(c.Smallest());
                }
            }

        private:
            glm::vec2 _share {};
        };

        // ---- a layout of the caller's own -----------------------------------------------------------------

        class RenderCustomLayout final : public RenderContainer {
        public:
            void Set(std::function<glm::vec2(LayoutContext&, const BoxConstraints&)> layout)
            {
                _layout = std::move(layout);
                MarkNeedsLayout();   // a rule is code: there is no telling whether it would lay out the same
            }

        protected:
            void PerformLayout() override
            {
                std::vector<bool> measured(_children.size(), false);
                LayoutContext context;
                context.count = _children.size();
                context.measure = [&](const std::size_t i, const BoxConstraints& c) -> glm::vec2 {
                    if (i >= _children.size()) return {};
                    _children[i]->Layout(c);
                    measured[i] = true;
                    return _children[i]->Size();
                };
                context.place = [&](const std::size_t i, const glm::vec2 at) { if (i < _children.size()) _children[i]->SetOffset(at); };
                const glm::vec2 size = _layout ? _layout(context, Constraints()) : Constraints().Smallest();
                // What the rule did not measure has no room: laid out all the same, so that it is consistent.
                for (std::size_t i = 0; i < _children.size(); ++i)
                    if (!measured[i]) _children[i]->Layout(BoxConstraints::Tight({}));
                SetSize(size);
            }

        private:
            std::function<glm::vec2(LayoutContext&, const BoxConstraints&)> _layout;
        };

        // ---- shown over everything, from here ---------------------------------------------------------------

        /**
         * Nothing itself — no size — but, while open, its popup is shown over the view: under whatever this
         * is in, at its left. A press outside the popup, or Escape, closes it, and says so.
         */
        class RenderPopupAnchor final : public RenderContainer {
        public:
            struct Config { bool open = false; Widget popup; std::function<void()> onDismiss; glm::vec2 offset {}; bool below = true; };

            void Set(const Config& c)
            {
                const bool again = c.open != _config.open || c.popup.Get() != _config.popup.Get() || c.offset != _config.offset;
                _config = c;
                if (again) { _stale = true; MarkNeedsPaint(); }
            }

            void Paint(Canvas&, glm::vec2) override
            {
                if (!_stale) return;
                _stale = false;
                Owner* owner = GetOwner();
                if (!owner || !owner->showPopup) return;
                if (_config.open && _config.popup) {
                    const RenderObject* in = Parent();
                    const glm::vec2 at = (in ? in->ToGlobal({ 0.f, _config.below ? in->Size().y : 0.f }) : ToGlobal({})) + _config.offset;
                    owner->showPopup(_config.popup, at, { 1.f, 1.f });
                    _shown = true;
                    // Closed by anything but this — a press elsewhere, Escape — whoever opened it hears of it.
                    owner->onPopupClosed = [this, alive = _alive] {
                        if (!*alive) return;
                        _shown = false;
                        if (const auto told = _config.onDismiss) told();
                    };
                } else if (_shown) {
                    Close();
                }
            }

            ~RenderPopupAnchor() override
            {
                *_alive = false;
                if (_shown) Close();
            }

        protected:
            void PerformLayout() override { SetSize({}); }

        private:
            void Close()
            {
                _shown = false;
                if (Owner* owner = GetOwner()) {
                    owner->onPopupClosed = nullptr;
                    if (owner->closePopup) owner->closePopup();
                }
            }

            Config _config;
            std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
            bool _stale = false, _shown = false;
        };

        // ---- as big as it would be ------------------------------------------------------------------------

        /** Its child as wide, or as tall, as the child is with all the room there is — and then held to that. */
        class RenderIntrinsic final : public RenderContainer {
        public:
            struct Config { bool width = false, height = false; };
            void Set(const Config& config)
            {
                if (config.width == _config.width && config.height == _config.height) return;
                _config = config;
                MarkNeedsLayout();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                if (_children.empty()) { SetSize(c.Constrain({ 0.f, 0.f })); return; }
                RenderObject& child = *_children.front();
                // With all the room there is that way: how big it is of itself.
                BoxConstraints free = c;
                if (_config.width) { free.minWidth = 0.f; free.maxWidth = Infinity; }
                if (_config.height) { free.minHeight = 0.f; free.maxHeight = Infinity; }
                child.Layout(free);
                const glm::vec2 own = c.Constrain(child.Size());
                // And held to it, so that what in it fills the room there is fills that.
                BoxConstraints held = c;
                if (_config.width) held.minWidth = held.maxWidth = own.x;
                if (_config.height) held.minHeight = held.maxHeight = own.y;
                child.Layout(held);
                child.SetOffset({ 0.f, 0.f });
                SetSize(c.Constrain(child.Size()));
            }

        private:
            Config _config;
        };

        // ---- a theme for what is under it ------------------------------------------------------------------

        /** Its child, built, laid out and painted with a theme of its own. */
        class RenderTheme final : public RenderContainer {
        public:
            RenderTheme() { ++detail::ThemedCount(); }
            ~RenderTheme() override { --detail::ThemedCount(); }

            void Set(const Theme& theme)
            {
                // The same again, as it is every time what is over it is built: nothing looks otherwise.
                const auto same = [](const Theme& a, const Theme& b) {
                    const TextStyle &s = a.textStyle, &o = b.textStyle;
                    return a.background == b.background && a.surface == b.surface && a.surfaceHover == b.surfaceHover
                        && a.surfacePressed == b.surfacePressed && a.primary == b.primary && a.primaryHover == b.primaryHover
                        && a.primaryPressed == b.primaryPressed && a.onPrimary == b.onPrimary && a.text == b.text
                        && a.textMuted == b.textMuted && a.border == b.border && a.focus == b.focus && a.radius == b.radius
                        && a.controlHeight == b.controlHeight && a.buttonRadius == b.buttonRadius && a.fieldRadius == b.fieldRadius
                        && a.checkboxRadius == b.checkboxRadius && s.font == o.font && s.size == o.size && s.color == o.color
                        && s.lineHeight == o.lineHeight && s.letterSpacing == o.letterSpacing && s.weight == o.weight
                        && s.italic == o.italic && s.underline == o.underline && s.lineThrough == o.lineThrough;
                };
                if (_set && same(theme, _theme)) return;
                // What was built under the old one read it then: it is built again.
                if (_set && GetOwner() && GetOwner()->reassemble) GetOwner()->reassemble();
                _set = true;
                _theme = theme;
                // Everything under it looks otherwise now, and may be another size.
                const std::function<void(RenderObject&)> again = [&again](RenderObject& object) {
                    object.MarkNeedsLayout();
                    object.MarkNeedsPaint();
                    object.VisitChildren(again);
                };
                again(*this);
            }

            [[nodiscard]] const Theme* ProvidedTheme() const override { return &_theme; }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const ThemeScope scope(_theme);
                RenderContainer::Paint(canvas, offset);
            }

        protected:
            void PerformLayout() override
            {
                const ThemeScope scope(_theme);
                RenderContainer::PerformLayout();
            }

        private:
            Theme _theme;
            bool _set = false;
        };

        template <typename R, typename Config>
        struct ComposeWidget final : RenderObjectWidget {
            Config config;
            std::vector<Widget> children;
            void (*update)(R&, const Config&);
            ComposeWidget(Config c, std::vector<Widget> kids, void (*u)(R&, const Config&)) : config(std::move(c)), children(std::move(kids)), update(u) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<R>(); }
            void UpdateRenderObject(RenderObject& object) const override { update(static_cast<R&>(object), config); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };

        template <typename R, typename Config>
        Widget make(Config config, std::vector<Widget> children, void (*update)(R&, const Config&))
        {
            std::erase_if(children, [](const Widget& w) { return !w; });
            return Widget(std::make_shared<ComposeWidget<R, Config>>(std::move(config), std::move(children), update));
        }

        std::vector<Widget> only(Widget child) { return child ? std::vector<Widget> { std::move(child) } : std::vector<Widget> {}; }
    }

    Widget Intrinsic(const bool width, const bool height, Widget child)
    {
        return make<RenderIntrinsic, RenderIntrinsic::Config>({ width, height }, only(std::move(child)),
                                                              [](RenderIntrinsic& r, const RenderIntrinsic::Config& c) { r.Set(c); });
    }

    Widget Themed(Theme theme, Widget child)
    {
        return make<RenderTheme, Theme>(std::move(theme), only(std::move(child)), [](RenderTheme& r, const Theme& c) { r.Set(c); });
    }

    SystemAppearance QuerySystemAppearance()
    {
        SystemAppearance appearance;
#ifdef _WIN32
        const auto number = [](const wchar_t* key, const wchar_t* name, DWORD& out) {
            DWORD size = sizeof out;
            return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &out, &size) == ERROR_SUCCESS;
        };
        DWORD light = 0, accent = 0;
        if (number(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", light)) {
            appearance.dark = light == 0;
            appearance.known = true;
        }
        // The accent, as the window manager has it: alpha, blue, green, red.
        if (number(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", accent)) {
            appearance.accent = { static_cast<float>(accent & 0xffu) / 255.f, static_cast<float>((accent >> 8) & 0xffu) / 255.f,
                                  static_cast<float>((accent >> 16) & 0xffu) / 255.f, 1.f };
            appearance.known = true;
        }
#endif
        return appearance;
    }

    Widget TransformBox(const Transform& transform, Widget child, const Alignment origin)
    {
        return make<RenderTransform, RenderTransform::Config>({ transform, origin }, only(std::move(child)),
                                                              [](RenderTransform& r, const RenderTransform::Config& c) { r.Set(c); });
    }

    Widget AspectRatio(const float ratio, Widget child)
    {
        return make<RenderAspectRatio, float>(ratio, only(std::move(child)), [](RenderAspectRatio& r, const float& c) { r.Set(c); });
    }

    Widget FractionallySizedBox(const float widthShare, const float heightShare, Widget child)
    {
        return make<RenderFractional, glm::vec2>({ widthShare, heightShare }, only(std::move(child)), [](RenderFractional& r, const glm::vec2& c) { r.Set(c); });
    }

    Widget CustomLayout(std::function<glm::vec2(LayoutContext&, const BoxConstraints&)> layout, std::vector<Widget> children)
    {
        using Rule = std::function<glm::vec2(LayoutContext&, const BoxConstraints&)>;
        return make<RenderCustomLayout, Rule>(std::move(layout), std::move(children), [](RenderCustomLayout& r, const Rule& c) { r.Set(c); });
    }

    Widget PopupAnchor(const bool open, Widget popup, std::function<void()> onDismiss, const glm::vec2 offset, const bool below)
    {
        return make<RenderPopupAnchor, RenderPopupAnchor::Config>({ open, std::move(popup), std::move(onDismiss), offset, below }, {},
                                                                  [](RenderPopupAnchor& r, const RenderPopupAnchor::Config& c) { r.Set(c); });
    }
}
