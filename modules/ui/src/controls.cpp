//
// koral-ui: the built-in controls. Buttons, checkboxes and switches are widgets made of other widgets —
// what a project's own would look like; the slider and the text field are render objects, since what
// they do with the pointer and the keyboard is their whole point.
//

#include <algorithm>
#include <cmath>
#include <numbers>

#include "boxes.h"

namespace kui
{
    namespace {
        Color mix(const Color a, const Color b, const float t)
        {
            return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
        }

        // ---- button ---------------------------------------------------------------------------------

        struct ButtonWidget final : StatefulWidget {
            Widget child;
            std::string label;          // a label's button: its child is made in Build, in the style's colour
            std::function<void()> onPressed;
            ButtonOptions options;
            bool hovered = false, pressed = false;

            ButtonWidget(Widget c, std::string l, std::function<void()> p, ButtonOptions o)
                : child(std::move(c)), label(std::move(l)), onPressed(std::move(p)), options(o) {}

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& b = static_cast<const ButtonWidget&>(newer);
                child = b.child;
                label = b.label;
                onPressed = b.onPressed;
                options = b.options;
            }

            Widget Build() override
            {
                const Theme& t = Theme::Current();
                const bool enabled = options.enabled && onPressed;
                const bool primary = options.style == ButtonStyle::ePrimary;
                const bool plain = options.style == ButtonStyle::ePlain;

                Color background = primary
                    ? (pressed ? t.primaryPressed : hovered ? t.primaryHover : t.primary)
                    : plain ? (pressed ? t.text.WithAlpha(0.12f) : hovered ? t.text.WithAlpha(0.06f) : colors::Transparent)
                            : (pressed ? t.surfacePressed : hovered ? t.surfaceHover : t.surface);
                Color foreground = primary ? t.onPrimary : t.text;
                if (!enabled) { background = background.WithAlpha(background.a * 0.45f); foreground = foreground.WithAlpha(0.5f); }

                Widget content = child;
                EdgeInsets padding = options.padding.value_or(plain ? EdgeInsets {} : EdgeInsets::Symmetric(14.f, 0.f));
                if (!content) {
                    TextStyle style = t.textStyle;
                    style.color = foreground;
                    content = Text(label, style, TextAlign::eCenter, false);
                    // A label's button is a control's height: the text centred in it.
                    if (!options.padding && !plain) padding.top = padding.bottom = std::max(0.f, (t.controlHeight - style.size * style.lineHeight) * 0.5f);
                }
                if (options.width) content = SizedBox(std::max(0.f, *options.width - padding.Horizontal()), -1.f, Center(std::move(content)));

                GestureOptions gestures;
                gestures.onEnter = [this] { SetState([this] { hovered = true; }); };
                gestures.onExit = [this] { SetState([this] { hovered = false; pressed = false; }); };
                if (enabled) {
                    gestures.onTapDown = [this](glm::vec2) { SetState([this] { pressed = true; }); };
                    gestures.onTapUp = [this] { SetState([this] { pressed = false; }); };
                    gestures.onTap = [this] { if (onPressed) onPressed(); };
                }
                return GestureDetector(std::move(gestures), Container({
                    .padding = padding,
                    .decoration = { .color = background, .borderWidth = options.style == ButtonStyle::eSecondary ? 1.f : 0.f,
                                    .borderColor = t.border, .radius = t.radius },
                }, std::move(content)));
            }
        };

        // ---- checkbox ---------------------------------------------------------------------------------

        struct CheckboxWidget final : StatefulWidget {
            bool value;
            std::function<void(bool)> onChanged;
            std::string label;
            bool hovered = false;

            CheckboxWidget(const bool v, std::function<void(bool)> c, std::string l) : value(v), onChanged(std::move(c)), label(std::move(l)) {}

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& c = static_cast<const CheckboxWidget&>(newer);
                value = c.value;
                onChanged = c.onChanged;
                label = c.label;
            }

            Widget Build() override
            {
                const Theme& t = Theme::Current();
                const bool on = value, hover = hovered;
                Widget box = CustomPaint([t, on, hover](Canvas& canvas, const glm::vec2 size) {
                    const Rect r = Rect::FromSize(size);
                    if (on) {
                        canvas.DrawRRect({ r, 4.f }, Paint::Fill(hover ? t.primaryHover : t.primary));
                        Path tick;
                        tick.MoveTo({ size.x * 0.24f, size.y * 0.52f }).LineTo({ size.x * 0.43f, size.y * 0.70f }).LineTo({ size.x * 0.76f, size.y * 0.32f });
                        canvas.DrawPath(tick, Paint::Stroked(t.onPrimary, 2.f).SetStroke({ .width = 2.f, .color = t.onPrimary, .cap = StrokeCap::eRound, .join = StrokeJoin::eRound }));
                    } else {
                        canvas.DrawRRect({ r, 4.f }, Paint::Fill(hover ? t.surfaceHover : t.surface).SetStroke(1.5f, hover ? t.focus : t.border));
                    }
                }, { 18.f, 18.f });

                std::vector<Widget> row { std::move(box) };
                if (!label.empty()) {
                    row.push_back(SizedBox(8.f, 0.f));
                    row.push_back(Text(label, t.textStyle, TextAlign::eStart, false));
                }
                GestureOptions gestures;
                gestures.onEnter = [this] { SetState([this] { hovered = true; }); };
                gestures.onExit = [this] { SetState([this] { hovered = false; }); };
                gestures.onTap = [this] { if (onChanged) onChanged(!value); };
                return GestureDetector(std::move(gestures), Row(std::move(row), { .mainAxisSize = MainAxisSize::eMin }));
            }
        };

        // ---- switch -----------------------------------------------------------------------------------

        struct SwitchWidget final : StatefulWidget {
            bool value;
            std::function<void(bool)> onChanged;
            float position = 0.f;    // 0 off, 1 on: animated between
            bool animating = false;

            SwitchWidget(const bool v, std::function<void(bool)> c) : value(v), onChanged(std::move(c)) {}

            void InitState() override { position = value ? 1.f : 0.f; }

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& s = static_cast<const SwitchWidget&>(newer);
                onChanged = s.onChanged;
                if (s.value != value) {
                    value = s.value;
                    if (!animating) {
                        animating = true;
                        Animate([this](const float dt) {
                            const float target = value ? 1.f : 0.f;
                            const float step = dt * 8.f;
                            SetState([&] { position = std::abs(target - position) <= step ? target : position + (target > position ? step : -step); });
                            animating = position != target;
                            return animating;
                        });
                    }
                }
            }

            Widget Build() override
            {
                const Theme& t = Theme::Current();
                const float p = position;
                GestureOptions gestures;
                gestures.onTap = [this] { if (onChanged) onChanged(!value); };
                return GestureDetector(std::move(gestures), CustomPaint([t, p](Canvas& canvas, const glm::vec2 size) {
                    const float r = size.y * 0.5f;
                    canvas.DrawRRect({ Rect::FromSize(size), r }, Paint::Fill(mix(t.surfacePressed, t.primary, p)));
                    const float x = r + (size.x - 2.f * r) * p;
                    canvas.DrawCircle({ x, r }, r - 3.f, Paint::Fill(colors::White));
                }, { 36.f, 20.f }));
            }
        };

        // ---- slider -------------------------------------------------------------------------------------

        class RenderSlider final : public RenderContainer {
        public:
            struct Config {
                float value, min, max;
                std::function<void(float)> onChanged;
            };

            void Set(const Config& c)
            {
                const bool changed = c.value != _config.value || c.min != _config.min || c.max != _config.max;
                _config = c;
                if (changed) MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    _dragging = true;
                    Pick(event.local.x);
                    return true;
                case PointerEvent::Type::eMove:
                    if (!_dragging) return false;
                    Pick(event.local.x);
                    return true;   // a drag on a slider is the slider's
                case PointerEvent::Type::eUp:
                case PointerEvent::Type::eCancel:
                    _dragging = false;
                    return true;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const float range = _config.max - _config.min;
                const float f = range != 0.f ? std::clamp((_config.value - _config.min) / range, 0.f, 1.f) : 0.f;
                const float knob = 8.f;
                const float y = offset.y + Size().y * 0.5f;
                const float x0 = offset.x + knob, x1 = offset.x + Size().x - knob;
                const float x = x0 + (x1 - x0) * f;
                canvas.DrawRRect({ Rect::LTRB(x0, y - 2.f, x1, y + 2.f), 2.f }, Paint::Fill(t.surfacePressed));
                canvas.DrawRRect({ Rect::LTRB(x0, y - 2.f, x, y + 2.f), 2.f }, Paint::Fill(t.primary));
                canvas.DrawCircle({ x, y }, knob, Paint::Fill(colors::White).SetStroke(2.f, t.primary));
            }

        protected:
            void PerformLayout() override
            {
                SetSize({ 200.f, 24.f });   // no content to fit: a default, which a stretching parent overrides
            }

        private:
            void Pick(const float x)
            {
                const float knob = 8.f;
                const float f = std::clamp((x - knob) / std::max(Size().x - 2.f * knob, 1.f), 0.f, 1.f);
                const float value = _config.min + (_config.max - _config.min) * f;
                if (value != _config.value && _config.onChanged) _config.onChanged(value);
            }

            Config _config { 0.f, 0.f, 1.f, {} };
            bool _dragging = false;
        };

        struct SliderWidget final : RenderObjectWidget {
            RenderSlider::Config config;
            explicit SliderWidget(RenderSlider::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderSlider>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderSlider&>(object).Set(config); }
        };

        // ---- text field ---------------------------------------------------------------------------------

        void appendUtf8(std::string& out, const char32_t cp)
        {
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool continuation(const char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

        class RenderTextField final : public RenderContainer {
        public:
            void Set(const TextFieldOptions& options)
            {
                if (!_initialised) {
                    _text = options.text;
                    _caret = _text.size();
                    _initialised = true;
                    Reshape();
                } else if (options.controlled && options.text != _text) {
                    // Its owner said what it shows: an edit refused or changed, or the text set from elsewhere.
                    _text = options.text;
                    _caret = std::min(_caret, _text.size());
                    while (_caret > 0 && _caret < _text.size() && continuation(_text[_caret])) --_caret;
                    Reshape();
                }
                const bool look = options.placeholder != _options.placeholder;
                const bool size = options.width != _options.width;
                _options = options;
                _options.text.clear();
                if (look) MarkNeedsPaint();
                if (size) MarkNeedsLayout();
            }

            [[nodiscard]] bool IsRepaintBoundary() const override { return true; }   // the caret blinks alone
            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                if (event.type != PointerEvent::Type::eDown || event.button != kor::MouseButton::eLeft) return false;
                if (GetOwner()) GetOwner()->RequestFocus(this);
                _caret = _paragraph.IndexAt(event.local - TextOrigin());
                Blink(true);
                return true;
            }

            void HandleText(const std::u32string_view text) override
            {
                std::string inserted;
                for (const char32_t cp : text)
                    if (cp >= 32 && cp != 127) appendUtf8(inserted, cp);
                if (inserted.empty()) return;
                _text.insert(_caret, inserted);
                _caret += inserted.size();
                Changed();
            }

            bool HandleKey(const kor::Key key, bool) override
            {
                switch (key) {
                case kor::Key::eBackspace:
                    if (_caret == 0) return true;
                    {
                        std::size_t from = _caret - 1;
                        while (from > 0 && continuation(_text[from])) --from;
                        _text.erase(from, _caret - from);
                        _caret = from;
                    }
                    Changed();
                    return true;
                case kor::Key::eDelete:
                    if (_caret >= _text.size()) return true;
                    {
                        std::size_t to = _caret + 1;
                        while (to < _text.size() && continuation(_text[to])) ++to;
                        _text.erase(_caret, to - _caret);
                    }
                    Changed();
                    return true;
                case kor::Key::eLeft:
                    if (_caret > 0) { --_caret; while (_caret > 0 && continuation(_text[_caret])) --_caret; }
                    Blink(true);
                    return true;
                case kor::Key::eRight:
                    if (_caret < _text.size()) { ++_caret; while (_caret < _text.size() && continuation(_text[_caret])) ++_caret; }
                    Blink(true);
                    return true;
                case kor::Key::eHome: _caret = 0; Blink(true); return true;
                case kor::Key::eEnd: _caret = _text.size(); Blink(true); return true;
                case kor::Key::eEnter:
                case kor::Key::eKPEnter:
                    if (_options.onSubmitted) _options.onSubmitted(_text);
                    return true;
                case kor::Key::eEsc:
                    if (GetOwner()) GetOwner()->RequestFocus(nullptr);
                    return true;
                default:
                    return false;
                }
            }

            void FocusChanged(const bool focused) override { _focused = focused; Blink(true); }

            void FocusTick(const float dt) override
            {
                const bool before = _blink < 0.5f;
                _blink = std::fmod(_blink + dt, 1.f);
                if ((_blink < 0.5f) != before) MarkNeedsPaint();
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const Rect box = Rect::XYWH(offset.x, offset.y, Size().x, Size().y);
                canvas.DrawRRect({ box, t.radius }, Paint::Fill(t.surface).SetStroke(_focused ? 1.5f : 1.f, _focused ? t.focus : t.border));
                canvas.Save();
                canvas.ClipRect(box.Deflate(2.f));

                // Scrolled sideways so the caret stays in view.
                const glm::vec2 caret = _paragraph.CaretPosition(_caret);
                const float visible = Size().x - 2.f * Inset;
                if (caret.x - _scroll > visible) _scroll = caret.x - visible;
                if (caret.x < _scroll) _scroll = caret.x;
                const glm::vec2 origin = offset + TextOrigin();
                if (_text.empty() && !_options.placeholder.empty()) {
                    TextStyle muted = t.textStyle;
                    muted.color = t.textMuted;
                    canvas.DrawText(_options.placeholder, origin, muted);
                } else {
                    canvas.DrawParagraph(_paragraph, origin);
                }
                if (_focused && _blink < 0.5f) {
                    const float x = origin.x + caret.x;
                    canvas.DrawRect(Rect::XYWH(std::round(x), origin.y + caret.y, 1.5f, _paragraph.LineHeight()), Paint::Fill(t.text));
                }
                canvas.Restore();
            }

        protected:
            void PerformLayout() override
            {
                const float width = _options.width >= 0.f ? _options.width : 200.f;   // stretched, the constraints make it wider
                SetSize({ width, Theme::Current().controlHeight });
            }

        private:
            static constexpr float Inset = 8.f;

            [[nodiscard]] glm::vec2 TextOrigin() const
            {
                return { Inset - _scroll, std::round((Size().y - _paragraph.LineHeight()) * 0.5f) };
            }

            void Reshape()
            {
                TextStyle style = Theme::Current().textStyle;
                style.color = Theme::Current().text;
                _paragraph = Paragraph(_text, style);
            }

            void Changed()
            {
                Reshape();
                Blink(true);
                if (_options.onChanged) _options.onChanged(_text);
            }

            void Blink(const bool on)
            {
                _blink = on ? 0.f : 0.5f;
                MarkNeedsPaint();
            }

            TextFieldOptions _options;
            std::string _text;
            std::size_t _caret = 0;
            Paragraph _paragraph;
            float _blink = 0.f, _scroll = 0.f;
            bool _focused = false, _initialised = false;
        };

        struct TextFieldWidget final : RenderObjectWidget {
            TextFieldOptions options;
            explicit TextFieldWidget(TextFieldOptions o) : options(std::move(o)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderTextField>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderTextField&>(object).Set(options); }
        };
    }

    Widget Button(Widget child, std::function<void()> onPressed, const ButtonOptions options)
    {
        return Make<ButtonWidget>(std::move(child), std::string(), std::move(onPressed), options);
    }

    Widget Button(std::string label, std::function<void()> onPressed, const ButtonOptions options)
    {
        return Make<ButtonWidget>(Widget {}, std::move(label), std::move(onPressed), options);
    }

    Widget Checkbox(const bool value, std::function<void(bool)> onChanged, std::string label)
    {
        return Make<CheckboxWidget>(value, std::move(onChanged), std::move(label));
    }

    Widget Switch(const bool value, std::function<void(bool)> onChanged)
    {
        return Make<SwitchWidget>(value, std::move(onChanged));
    }

    Widget Slider(const float value, std::function<void(float)> onChanged, const float min, const float max)
    {
        return Make<SliderWidget>(RenderSlider::Config { value, min, max, std::move(onChanged) });
    }

    Widget ProgressBar(const float value)
    {
        const Theme& t = Theme::Current();
        const float v = std::clamp(value, 0.f, 1.f);
        return CustomPaint([t, v](Canvas& canvas, const glm::vec2 size) {
            const float r = size.y * 0.5f;
            canvas.DrawRRect({ Rect::FromSize(size), r }, Paint::Fill(t.surfacePressed));
            if (v > 0.f) canvas.DrawRRect({ Rect::XYWH(0.f, 0.f, std::max(size.x * v, size.y), size.y), r }, Paint::Fill(t.primary));
        }, { 200.f, 6.f });
    }

    Widget TextField(TextFieldOptions options) { return Make<TextFieldWidget>(std::move(options)); }
}
