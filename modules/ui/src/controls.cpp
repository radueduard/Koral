//
// koral-ui: the built-in controls. Buttons, checkboxes and switches are widgets made of other widgets —
// what a project's own would look like; the slider and the text field are render objects, since what
// they do with the pointer and the keyboard is their whole point.
//

#include <algorithm>
#include <cmath>
#include <cstdio>
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
                                    .borderColor = t.border, .radius = t.ButtonRadius() },
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
                // One UI's: a circle — a ring while off, filled with the accent and ticked while on.
                Widget box = CustomPaint([t, on, hover](Canvas& canvas, const glm::vec2 size) {
                    const glm::vec2 c = size * 0.5f;
                    const float r = std::min(size.x, size.y) * 0.5f;
                    if (t.checkboxRadius >= 0.f) {
                        // A square, as round at its corners as the theme says: Material's, Windows'.
                        const RRect box { Rect::FromSize(size).Deflate(1.f), t.checkboxRadius };
                        if (on) {
                            canvas.DrawRRect(box, Paint::Fill(hover ? t.primaryHover : t.primary));
                            Path tick;
                            tick.MoveTo({ size.x * 0.27f, size.y * 0.52f }).LineTo({ size.x * 0.43f, size.y * 0.68f }).LineTo({ size.x * 0.74f, size.y * 0.34f });
                            canvas.DrawPath(tick, Paint::Stroked(t.onPrimary, 2.f).SetStroke({ .width = 2.f, .color = t.onPrimary, .cap = StrokeCap::eRound, .join = StrokeJoin::eRound }));
                        } else {
                            canvas.DrawRRect(box, Paint::Fill(hover ? t.surfaceHover : colors::Transparent).SetStroke(1.5f, hover ? t.focus : t.textMuted));
                        }
                        return;
                    }
                    if (on) {
                        canvas.DrawCircle(c, r, Paint::Fill(hover ? t.primaryHover : t.primary));
                        Path tick;
                        tick.MoveTo({ size.x * 0.28f, size.y * 0.52f }).LineTo({ size.x * 0.44f, size.y * 0.67f }).LineTo({ size.x * 0.72f, size.y * 0.36f });
                        canvas.DrawPath(tick, Paint::Stroked(t.onPrimary, 2.f).SetStroke({ .width = 2.f, .color = t.onPrimary, .cap = StrokeCap::eRound, .join = StrokeJoin::eRound }));
                    } else {
                        canvas.DrawCircle(c, r - 0.75f, Paint::Fill(hover ? t.surfaceHover : colors::Transparent).SetStroke(1.5f, hover ? t.focus : t.textMuted));
                    }
                }, { 22.f, 22.f });

                std::vector<Widget> row { std::move(box) };
                if (!label.empty()) {
                    row.push_back(SizedBox(10.f, 0.f));
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
                // One UI's: a wide pill for a track, the accent's while on, and a white thumb that nearly fills it.
                return GestureDetector(std::move(gestures), CustomPaint([t, p](Canvas& canvas, const glm::vec2 size) {
                    const float r = size.y * 0.5f;
                    canvas.DrawRRect({ Rect::FromSize(size), r }, Paint::Fill(mix(t.textMuted.WithAlpha(0.45f), t.primary, p)));
                    const float x = r + (size.x - 2.f * r) * p;
                    canvas.DrawCircle({ x, r }, r - 2.5f, Paint::Fill(colors::White));
                }, { 46.f, 26.f }));
            }
        };

        // ---- slider -------------------------------------------------------------------------------------

        class RenderSlider final : public RenderContainer {
        public:
            struct Config {
                float value, min, max;
                std::function<void(float)> onChanged;
                std::function<void()> onFinished;
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
                    MarkNeedsPaint();
                    Pick(event.local.x);
                    return true;
                case PointerEvent::Type::eMove:
                    if (!_dragging) return false;
                    Pick(event.local.x);
                    return true;   // a drag on a slider is the slider's
                case PointerEvent::Type::eUp:
                case PointerEvent::Type::eCancel: {
                    const bool was = _dragging;
                    _dragging = false;
                    MarkNeedsPaint();
                    if (was) if (const auto done = _config.onFinished) done();
                    return true;
                }
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
                // One UI's: a round-ended track, the accent's up to the value, and a thumb of the accent
                // that grows under the finger, with a halo.
                const float half = 3.f;
                canvas.DrawRRect({ Rect::LTRB(x0 - half, y - half, x1 + half, y + half), half }, Paint::Fill(t.textMuted.WithAlpha(0.35f)));
                canvas.DrawRRect({ Rect::LTRB(x0 - half, y - half, x + half, y + half), half }, Paint::Fill(t.primary));
                if (_dragging) canvas.DrawCircle({ x, y }, 13.f, Paint::Fill(t.primary.WithAlpha(0.22f)));
                canvas.DrawCircle({ x, y }, _dragging ? 10.f : 9.f, Paint::Fill(t.primary));
            }

        protected:
            void PerformLayout() override
            {
                SetSize({ 200.f, 28.f });   // no content to fit: a default, which a stretching parent overrides
            }

        private:
            void Pick(const float x)
            {
                const float knob = 8.f;
                const float f = std::clamp((x - knob) / std::max(Size().x - 2.f * knob, 1.f), 0.f, 1.f);
                const float value = _config.min + (_config.max - _config.min) * f;
                if (value != _config.value && _config.onChanged) _config.onChanged(value);
            }

            Config _config { 0.f, 0.f, 1.f, {}, {} };
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
                const bool shape = options.multiline != _options.multiline || options.minLines != _options.minLines || options.maxLines != _options.maxLines;
                const bool look = options.placeholder != _options.placeholder;
                const bool size = options.width != _options.width;
                const bool first = !_initialised;
                const std::string wanted = options.text;
                const bool controlled = options.controlled;
                const bool take = options.focus != 0 && options.focus != _options.focus;
                _options = options;
                if (take) { _takeFocus = true; MarkNeedsPaint(); }      // taken when it is next painted: it is in the tree by then
                _options.text.clear();
                if (first) {
                    _text = wanted;
                    _caret = _anchor = _text.size();
                    _initialised = true;
                    Reshape();
                } else if (controlled && wanted != _text) {
                    // Its owner said what it shows: an edit refused or changed, or the text set from elsewhere.
                    _text = wanted;
                    _caret = Boundary(std::min(_caret, _text.size()));
                    _anchor = _caret;
                    Reshape();
                } else if (shape) {
                    Reshape();
                }
                if (look) MarkNeedsPaint();
                if (size || shape) MarkNeedsLayout();
            }

            [[nodiscard]] bool IsRepaintBoundary() const override { return true; }   // the caret blinks alone
            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
            [[nodiscard]] bool Focusable() const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    if (GetOwner()) GetOwner()->RequestFocus(this);
                    _caret = _paragraph.IndexAt(event.local - TextOrigin());
                    if (!(GetOwner() && GetOwner()->shift)) _anchor = _caret;    // with Shift, from where it was to here
                    _selecting = true;
                    Blink(true);
                    return true;
                case PointerEvent::Type::eMove:
                    // Dragged: selected from where the button went down to where the pointer is.
                    if (!_selecting) return false;
                    _caret = _paragraph.IndexAt(event.local - TextOrigin());
                    Blink(true);
                    return true;
                case PointerEvent::Type::eUp:
                case PointerEvent::Type::eCancel:
                    _selecting = false;
                    return false;
                case PointerEvent::Type::eHover:
                    if (Owner* owner = GetOwner()) owner->cursor = PointerCursor::eText;
                    return false;
                default: return false;
                }
            }

            void HandleText(const std::u32string_view text) override
            {
                std::string inserted;
                for (const char32_t cp : text)
                    if (cp >= 32 && cp != 127) appendUtf8(inserted, cp);
                if (!inserted.empty()) Replace(inserted);
            }

            bool HandleKey(const kor::Key key, bool) override
            {
                Owner* owner = GetOwner();
                const bool shift = owner && owner->shift, control = owner && owner->control;
                if (control) {
                    switch (key) {
                    case kor::Key::eA: _anchor = 0; _caret = _text.size(); Blink(true); return true;
                    case kor::Key::eC:
                    case kor::Key::eX:
                        if (HasSelection() && owner && owner->setClipboardText) {
                            owner->setClipboardText(_text.substr(Low(), High() - Low()));
                            if (key == kor::Key::eX) Replace({});
                        }
                        return true;
                    case kor::Key::eV:
                        if (owner && owner->clipboardText) {
                            std::string pasted = owner->clipboardText();
                            // One line holds one line: what is pasted loses its line ends.
                            if (!_options.multiline) std::erase_if(pasted, [](const char c) { return c == '\n' || c == '\r'; });
                            else std::erase(pasted, '\r');
                            if (!pasted.empty()) Replace(pasted);
                        }
                        return true;
                    default: break;
                    }
                }
                switch (key) {
                case kor::Key::eBackspace:
                    if (HasSelection()) { Replace({}); return true; }
                    if (_caret == 0) return true;
                    _anchor = Previous(_caret);
                    Replace({});
                    return true;
                case kor::Key::eDelete:
                    if (HasSelection()) { Replace({}); return true; }
                    if (_caret >= _text.size()) return true;
                    _anchor = Next(_caret);
                    Replace({});
                    return true;
                case kor::Key::eLeft:
                    // Without Shift, what is selected is let go of at its near end.
                    Move(!shift && HasSelection() ? Low() : Previous(_caret), shift);
                    return true;
                case kor::Key::eRight:
                    Move(!shift && HasSelection() ? High() : Next(_caret), shift);
                    return true;
                case kor::Key::eUp:
                case kor::Key::eDown: {
                    if (!_options.multiline) return false;
                    const glm::vec2 at = _paragraph.CaretPosition(_caret);
                    const float line = _paragraph.LineHeight();
                    const float y = at.y + line * (key == kor::Key::eUp ? -0.5f : 1.5f);
                    Move(y < 0.f ? 0 : _paragraph.IndexAt({ at.x, y }), shift);
                    return true;
                }
                case kor::Key::eHome:
                case kor::Key::eEnd: {
                    std::size_t to = key == kor::Key::eHome ? 0 : _text.size();
                    if (_options.multiline && !control) {
                        // Of the line the caret is on.
                        const glm::vec2 at = _paragraph.CaretPosition(_caret);
                        to = _paragraph.IndexAt({ key == kor::Key::eHome ? -1.e6f : 1.e6f, at.y + _paragraph.LineHeight() * 0.5f });
                    }
                    Move(to, shift);
                    return true;
                }
                case kor::Key::eEnter:
                case kor::Key::eKPEnter:
                    if (_options.multiline && !control) { Replace("\n"); return true; }
                    if (_options.onSubmitted) _options.onSubmitted(_text);
                    return true;
                case kor::Key::eEsc:
                    if (owner) owner->RequestFocus(nullptr);
                    return true;
                default:
                    return false;
                }
            }

            void FocusChanged(const bool focused) override
            {
                _focused = focused;
                if (!focused) { _anchor = _caret; _selecting = false; }
                Blink(true);
            }

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
                // Asked to take the keyboard: now that it is in the view, it does.
                if (_takeFocus) { _takeFocus = false; if (GetOwner()) GetOwner()->RequestFocus(this); }
                canvas.DrawRRect({ box, _options.multiline ? std::min(t.FieldRadius(), 12.f) : t.FieldRadius() },
                                 kui::Paint::Fill(t.surface).SetStroke(_focused ? 1.5f : 1.f, _focused ? t.focus : t.border));
                canvas.Save();
                canvas.ClipRect(box.Deflate(2.f));

                // Scrolled so the caret stays in view: sideways on one line, up and down on several.
                const glm::vec2 caret = _paragraph.CaretPosition(_caret);
                const float line = _paragraph.LineHeight();
                if (_options.multiline) {
                    const float visible = Size().y - 2.f * Pad;
                    if (caret.y + line - _scroll > visible) _scroll = caret.y + line - visible;
                    if (caret.y < _scroll) _scroll = caret.y;
                    _scroll = std::clamp(_scroll, 0.f, std::max(0.f, _paragraph.Size().y - visible));
                } else {
                    const float visible = Size().x - 2.f * Inset;
                    if (caret.x - _scroll > visible) _scroll = caret.x - visible;
                    if (caret.x < _scroll) _scroll = caret.x;
                }
                const glm::vec2 origin = offset + TextOrigin();

                // What is selected, a line at a time, behind the text.
                if (HasSelection()) {
                    const kui::Paint lit = kui::Paint::Fill(t.primary.WithAlpha(_focused ? 0.4f : 0.2f));
                    std::size_t at = Low();
                    const std::size_t end = High();
                    while (at < end) {
                        const glm::vec2 from = _paragraph.CaretPosition(at);
                        std::size_t to = at;
                        glm::vec2 last = from;
                        while (to < end) {
                            const std::size_t next = Next(to);
                            const glm::vec2 there = _paragraph.CaretPosition(next);
                            if (there.y != from.y) break;
                            to = next;
                            last = there;
                        }
                        // A line's end, selected, shows as a sliver past its last letter.
                        const bool lineEnd = to < end;
                        canvas.DrawRect(Rect::XYWH(origin.x + from.x, origin.y + from.y, std::max(last.x - from.x, 0.f) + (lineEnd ? 5.f : 0.f), line), lit);
                        at = to == at || lineEnd ? Next(to) : to;
                    }
                }
                if (_text.empty() && !_options.placeholder.empty()) {
                    TextStyle muted = t.textStyle;
                    muted.color = t.textMuted;
                    canvas.DrawText(_options.placeholder, origin, muted);
                } else {
                    canvas.DrawParagraph(_paragraph, origin);
                }
                if (_focused && _blink < 0.5f) {
                    const float x = origin.x + caret.x;
                    canvas.DrawRect(Rect::XYWH(std::round(x), origin.y + caret.y, 1.5f, line), kui::Paint::Fill(t.text));
                }
                canvas.Restore();
            }

        protected:
            void PerformLayout() override
            {
                const auto& c = Constraints();
                const float wanted = _options.width >= 0.f ? _options.width : 200.f;   // stretched, the constraints make it wider
                const float width = std::clamp(wanted, c.minWidth, std::max(c.minWidth, c.maxWidth));
                if (!_options.multiline) { SetSize({ width, Theme::Current().controlHeight }); return; }
                // Wrapped at its own width, and as tall as that makes it, between its least and its most lines.
                if (width != _wrapped) { _wrapped = width; Reshape(); }
                const int least = std::max(_options.minLines, 1), most = std::max(_options.maxLines, least);
                const int lines = std::clamp(static_cast<int>(_paragraph.LineCount()), least, most);
                SetSize({ width, static_cast<float>(lines) * _paragraph.LineHeight() + 2.f * Pad });
            }

        private:
            static constexpr float Inset = 14.f;   // clear of a pill's round ends
            static constexpr float Pad = 9.f;      // over and under several lines

            [[nodiscard]] glm::vec2 TextOrigin() const
            {
                if (_options.multiline) return { Inset, Pad - _scroll };
                return { Inset - _scroll, std::round((Size().y - _paragraph.LineHeight()) * 0.5f) };
            }

            [[nodiscard]] bool HasSelection() const { return _anchor != _caret; }
            [[nodiscard]] std::size_t Low() const { return std::min(_anchor, _caret); }
            [[nodiscard]] std::size_t High() const { return std::max(_anchor, _caret); }
            /** The start of the character at or before @p index: never in the middle of one. */
            [[nodiscard]] std::size_t Boundary(std::size_t index) const
            {
                while (index > 0 && index < _text.size() && continuation(_text[index])) --index;
                return index;
            }
            [[nodiscard]] std::size_t Previous(std::size_t index) const
            {
                if (index == 0) return 0;
                --index;
                while (index > 0 && continuation(_text[index])) --index;
                return index;
            }
            [[nodiscard]] std::size_t Next(std::size_t index) const
            {
                if (index >= _text.size()) return _text.size();
                ++index;
                while (index < _text.size() && continuation(_text[index])) ++index;
                return index;
            }

            /** The caret to @p index — and what is selected with it, unless @p extend keeps where it started. */
            void Move(const std::size_t index, const bool extend)
            {
                _caret = std::min(index, _text.size());
                if (!extend) _anchor = _caret;
                Blink(true);
            }

            /** What is selected (or nothing, at the caret) becomes @p with. */
            void Replace(const std::string& with)
            {
                const std::size_t from = Low();
                _text.replace(from, High() - from, with);
                _caret = _anchor = from + with.size();
                Changed();
            }

            void Reshape()
            {
                TextStyle style = Theme::Current().textStyle;
                style.color = Theme::Current().text;
                _paragraph = _options.multiline && _wrapped > 2.f * Inset ? Paragraph(_text, style, _wrapped - 2.f * Inset) : Paragraph(_text, style);
                if (_options.multiline) MarkNeedsLayout();   // another line, or one fewer
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
            std::size_t _caret = 0, _anchor = 0;   // what is selected is between them; nothing, where they are the same
            Paragraph _paragraph;
            float _blink = 0.f, _scroll = 0.f, _wrapped = 0.f;
            bool _focused = false, _initialised = false, _selecting = false, _takeFocus = false;
        };

        // ---- drag value -----------------------------------------------------------------------------------

        std::string formatted(const float value, const int decimals)
        {
            char text[64];
            std::snprintf(text, sizeof text, "%.*f", std::clamp(decimals, 0, 9), static_cast<double>(value));
            return text;
        }

        /** A number in a field, changed by dragging across it. */
        class RenderDragValue final : public RenderContainer {
        public:
            struct Config {
                float value = 0.f;
                std::function<void(float)> onChanged;
                DragValueOptions options;
            };

            void Set(const Config& c)
            {
                const bool look = c.value != _config.value || c.options.label != _config.options.label || c.options.decimals != _config.options.decimals;
                const bool size = c.options.width != _config.options.width;
                _config = c;
                if (size) MarkNeedsLayout();
                if (look) MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eEnter: _hovered = true; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eExit: _hovered = false; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    _dragging = true;
                    _from = _config.value;
                    _travelled = 0.f;
                    MarkNeedsPaint();
                    return true;
                case PointerEvent::Type::eMove: {
                    if (!_dragging) return false;
                    // From where it was pressed, not step by step: the value does not drift when it is held at a limit.
                    _travelled += event.delta.x;
                    const float value = std::clamp(_from + _travelled * _config.options.speed, _config.options.min, _config.options.max);
                    if (value != _config.value && _config.onChanged) _config.onChanged(value);
                    return true;   // a drag on it is its own
                }
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
                const Rect box = Rect::XYWH(offset.x, offset.y, Size().x, Size().y);
                canvas.DrawRRect({ box, t.FieldRadius() }, Paint::Fill(_dragging ? t.surfacePressed : _hovered ? t.surfaceHover : t.surface)
                                                          .SetStroke(_dragging ? 1.5f : 1.f, _dragging ? t.primary : t.border));
                // What says it can be dragged: an arrow at each end.
                const float y = box.Center().y;
                const kui::Paint arrow = Paint::Stroked(_dragging ? t.primary : t.textMuted, 1.5f);
                canvas.DrawLine({ box.left + 13.f, y - 4.f }, { box.left + 9.f, y }, arrow);
                canvas.DrawLine({ box.left + 9.f, y }, { box.left + 13.f, y + 4.f }, arrow);
                canvas.DrawLine({ box.right - 13.f, y - 4.f }, { box.right - 9.f, y }, arrow);
                canvas.DrawLine({ box.right - 9.f, y }, { box.right - 13.f, y + 4.f }, arrow);

                TextStyle style = t.textStyle;
                style.color = t.text;
                const std::string value = formatted(_config.value, _config.options.decimals);
                const Paragraph number(value, style);
                if (_config.options.label.empty()) {
                    canvas.DrawText(value, { std::round(box.Center().x - number.Size().x * 0.5f), std::round(y - number.Size().y * 0.5f) }, style);
                } else {
                    // The label to the left, quieter; the value to the right.
                    TextStyle quiet = style;
                    quiet.color = t.textMuted;
                    const Paragraph label(_config.options.label, quiet);
                    canvas.DrawText(_config.options.label, { box.left + 22.f, std::round(y - label.Size().y * 0.5f) }, quiet);
                    canvas.DrawText(value, { std::round(box.right - 22.f - number.Size().x), std::round(y - number.Size().y * 0.5f) }, style);
                }
            }

        protected:
            void PerformLayout() override
            {
                SetSize({ _config.options.width >= 0.f ? _config.options.width : 120.f, Theme::Current().controlHeight });
            }

        private:
            Config _config;
            float _from = 0.f, _travelled = 0.f;
            bool _dragging = false, _hovered = false;
        };

        struct DragValueWidget final : RenderObjectWidget {
            RenderDragValue::Config config;
            explicit DragValueWidget(RenderDragValue::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderDragValue>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderDragValue&>(object).Set(config); }
        };

        // ---- menus ----------------------------------------------------------------------------------------

        constexpr float MenuRow = 36.f, MenuPad = 6.f, MenuLine = 9.f;

        glm::vec2 menuSize(const std::vector<MenuItem>& items, const float width)
        {
            float height = 2.f * MenuPad;
            for (const auto& item : items) height += item.separator ? MenuLine : MenuRow;
            return { width, height };
        }

        /** How wide a menu of @p items wants to be: its longest line, and room around it. */
        float menuWidth(const std::vector<MenuItem>& items, const float least)
        {
            float width = least;
            const TextStyle style = Theme::Current().textStyle;
            for (const auto& item : items) if (!item.separator) width = std::max(width, Paragraph(item.label, style).MaxIntrinsicWidth() + 44.f);
            return width;
        }

        /** The list a popup shows: one line an item, lit under the pointer; picking one runs it and closes the popup. */
        struct MenuWidget final : StatefulWidget {
            std::vector<MenuItem> items;
            float width;
            int marked;                 ///< The one shown as chosen already (a dropdown's selection), or -1.
            std::function<void()> close;
            int hovered = -1;

            MenuWidget(std::vector<MenuItem> i, const float w, const int m, std::function<void()> c)
                : items(std::move(i)), width(w), marked(m), close(std::move(c)) {}

            void DidUpdateWidget(const StatefulWidget& newer) override
            {
                const auto& m = static_cast<const MenuWidget&>(newer);
                items = m.items;
                width = m.width;
                marked = m.marked;
                close = m.close;
            }

            Widget Build() override
            {
                const Theme& t = Theme::Current();
                std::vector<Widget> rows;
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const MenuItem& item = items[i];
                    if (item.separator) {
                        rows.push_back(Container({ .height = MenuLine, .padding = EdgeInsets::Symmetric(12.f, (MenuLine - 1.f) * 0.5f) },
                                                 Container({ .decoration = { .color = t.border } })));
                        continue;
                    }
                    const int index = static_cast<int>(i);
                    const bool lit = hovered == index && item.enabled;
                    TextStyle style = t.textStyle;
                    style.color = !item.enabled ? t.textMuted.WithAlpha(0.6f) : index == marked ? t.primary : t.text;
                    GestureOptions gestures;
                    gestures.onEnter = [this, index] { SetState([this, index] { hovered = index; }); };
                    gestures.onExit = [this, index] { SetState([this, index] { if (hovered == index) hovered = -1; }); };
                    if (item.enabled) {
                        gestures.onTap = [this, index] {
                            // Copied out first: closing the popup takes this widget, and its items, with it.
                            const auto selected = items[static_cast<std::size_t>(index)].onSelected;
                            const auto shut = close;
                            if (shut) shut();
                            if (selected) selected();
                        };
                    }
                    rows.push_back(GestureDetector(std::move(gestures), Container({
                        .height = MenuRow,
                        .padding = EdgeInsets::Symmetric(14.f, 0.f),
                        .decoration = { .color = lit ? t.surfaceHover : colors::Transparent, .radius = std::min(t.radius, 12.f) },
                        .alignment = Alignment::CenterLeft(),
                    }, Text(item.label, style, TextAlign::eStart, false))));
                }
                return Container({
                    .width = width,
                    .padding = EdgeInsets::All(MenuPad),
                    .decoration = { .color = t.surface, .borderWidth = 1.f, .borderColor = t.border, .radius = std::min(t.radius, 16.f) },
                }, Column(std::move(rows), { .crossAxisAlignment = CrossAxisAlignment::eStretch }));
            }
        };

        // ---- dropdown -------------------------------------------------------------------------------------

        /** A field showing what is selected; pressed, it opens the list under itself. */
        class RenderDropdown final : public RenderContainer {
        public:
            struct Config {
                std::vector<std::string> items;
                int selected = -1;
                std::function<void(int)> onChanged;
                DropdownOptions options;
            };

            void Set(const Config& c)
            {
                const bool size = c.options.width != _config.options.width;
                _config = c;
                if (size) MarkNeedsLayout();
                MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eEnter: _hovered = true; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eExit: _hovered = false; _pressed = false; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    _pressed = true;
                    MarkNeedsPaint();
                    return true;
                case PointerEvent::Type::eUp:
                    if (_pressed) Open();
                    _pressed = false;
                    MarkNeedsPaint();
                    return true;
                case PointerEvent::Type::eCancel:
                    _pressed = false;
                    MarkNeedsPaint();
                    return false;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const Rect box = Rect::XYWH(offset.x, offset.y, Size().x, Size().y);
                canvas.DrawRRect({ box, t.FieldRadius() }, Paint::Fill(_pressed ? t.surfacePressed : _hovered ? t.surfaceHover : t.surface).SetStroke(1.f, t.border));
                const bool chosen = _config.selected >= 0 && _config.selected < static_cast<int>(_config.items.size());
                TextStyle style = t.textStyle;
                style.color = chosen ? t.text : t.textMuted;
                const std::string& text = chosen ? _config.items[static_cast<std::size_t>(_config.selected)] : _config.options.placeholder;
                const Paragraph shown(text, style);
                canvas.Save();
                canvas.ClipRect(Rect::LTRB(box.left + 14.f, box.top, box.right - 30.f, box.bottom));
                canvas.DrawText(text, { box.left + 14.f, std::round(box.Center().y - shown.Size().y * 0.5f) }, style);
                canvas.Restore();
                // The arrow that says there is more under it.
                const glm::vec2 c { box.right - 18.f, box.Center().y + 1.f };
                const kui::Paint arrow = Paint::Stroked(t.textMuted, 1.5f);
                canvas.DrawLine(c + glm::vec2(-4.f, -3.f), c + glm::vec2(0.f, 2.f), arrow);
                canvas.DrawLine(c + glm::vec2(0.f, 2.f), c + glm::vec2(4.f, -3.f), arrow);
            }

        protected:
            void PerformLayout() override
            {
                SetSize({ _config.options.width >= 0.f ? _config.options.width : 200.f, Theme::Current().controlHeight });
            }

        private:
            void Open()
            {
                Owner* owner = GetOwner();
                if (!owner || !owner->showPopup || _config.items.empty()) return;
                std::vector<MenuItem> items;
                for (std::size_t i = 0; i < _config.items.size(); ++i) {
                    const int index = static_cast<int>(i);
                    // What was picked is told to whoever asked now, not to whoever asked when the list opened.
                    items.push_back({ _config.items[i], [this, alive = _alive, index] { if (*alive && _config.onChanged) _config.onChanged(index); } });
                }
                const float width = menuWidth(items, Size().x);
                owner->showPopup(Make<MenuWidget>(items, width, _config.selected, [owner] { if (owner->closePopup) owner->closePopup(); }),
                                 ToGlobal({ 0.f, Size().y + 4.f }), menuSize(items, width));
            }

            Config _config;
            std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
            bool _hovered = false, _pressed = false;

        public:
            ~RenderDropdown() override { *_alive = false; }
        };

        struct DropdownWidget final : RenderObjectWidget {
            RenderDropdown::Config config;
            explicit DropdownWidget(RenderDropdown::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderDropdown>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderDropdown&>(object).Set(config); }
        };

        // ---- context menu ---------------------------------------------------------------------------------

        /** Its child, and a menu where the right button is pressed on it. */
        class RenderContextArea final : public RenderContainer {
        public:
            void Set(std::vector<MenuItem> items) { _items = std::move(items); }

            bool HandleEvent(const PointerEvent& event) override
            {
                if (event.type != PointerEvent::Type::eDown || event.button != kor::MouseButton::eRight) return false;
                // The innermost one under the pointer has it: an area inside another shows its own menu.
                if (event.taken) return false;
                Owner* owner = GetOwner();
                if (!owner || !owner->showPopup || _items.empty()) return false;
                const float width = menuWidth(_items, 160.f);
                owner->showPopup(Make<MenuWidget>(_items, width, -1, [owner] { if (owner->closePopup) owner->closePopup(); }),
                                 event.position, menuSize(_items, width));
                return true;
            }

        private:
            std::vector<MenuItem> _items;
        };

        struct ContextAreaWidget final : RenderObjectWidget {
            std::vector<MenuItem> items;
            std::vector<Widget> children;
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderContextArea>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderContextArea&>(object).Set(items); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return children; }
        };

        // ---- menu bar -------------------------------------------------------------------------------------

        /** A menu's title in the bar; pressed, its menu opens under it. */
        class RenderMenuTitle final : public RenderContainer {
        public:
            void Set(const Menu& menu)
            {
                if (menu.title != _menu.title) MarkNeedsLayout();
                _menu = menu;
                MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eEnter: _hovered = true; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eExit: _hovered = false; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eDown:
                    if (event.button != kor::MouseButton::eLeft) return false;
                    Open();
                    return true;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const Rect box = Rect::XYWH(offset.x, offset.y, Size().x, Size().y);
                if (_hovered) canvas.DrawRRect({ box.Deflate(2.f), 8.f }, Paint::Fill(t.surfaceHover));
                const Paragraph shown(_menu.title, t.textStyle);
                canvas.DrawText(_menu.title, { box.left + 12.f, std::round(box.Center().y - shown.Size().y * 0.5f) }, t.textStyle);
            }

        protected:
            void PerformLayout() override
            {
                const Theme& t = Theme::Current();
                SetSize({ std::ceil(Paragraph(_menu.title, t.textStyle).MaxIntrinsicWidth()) + 24.f, t.controlHeight - 4.f });
            }

        private:
            void Open()
            {
                Owner* owner = GetOwner();
                if (!owner || !owner->showPopup || _menu.items.empty()) return;
                const float width = menuWidth(_menu.items, 180.f);
                owner->showPopup(Make<MenuWidget>(_menu.items, width, -1, [owner] { if (owner->closePopup) owner->closePopup(); }),
                                 ToGlobal({ 0.f, Size().y + 2.f }), menuSize(_menu.items, width));
            }

            Menu _menu;
            bool _hovered = false;
        };

        struct MenuTitleWidget final : RenderObjectWidget {
            Menu menu;
            explicit MenuTitleWidget(Menu m) : menu(std::move(m)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderMenuTitle>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderMenuTitle&>(object).Set(menu); }
        };

        // ---- colour edit ----------------------------------------------------------------------------------

        /** A swatch of a colour, and a label after it; pressed, a picker opens under it. */
        class RenderColorEdit final : public RenderContainer {
        public:
            struct Config {
                Color color;
                std::function<void(Color)> onChanged;
                std::string label;
                ColorPickerOptions options;
            };

            void Set(const Config& c)
            {
                if (c.label != _config.label) MarkNeedsLayout();
                _config = c;
                MarkNeedsPaint();
            }

            [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }

            bool HandleEvent(const PointerEvent& event) override
            {
                switch (event.type) {
                case PointerEvent::Type::eEnter: _hovered = true; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eExit: _hovered = false; MarkNeedsPaint(); return false;
                case PointerEvent::Type::eDown: return event.button == kor::MouseButton::eLeft;
                case PointerEvent::Type::eUp: Open(); return true;
                default: return false;
                }
            }

            void Paint(Canvas& canvas, const glm::vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const Rect swatch = Rect::XYWH(offset.x, offset.y + (Size().y - Swatch.y) * 0.5f, Swatch.x, Swatch.y);
                // What is behind a see-through colour: two shades, so that it shows as one.
                const RRect shape { swatch, 8.f };
                canvas.Save();
                canvas.ClipRRect(shape);
                canvas.DrawRect(swatch, Paint::Fill(colors::White));
                canvas.DrawRect(Rect::LTRB(swatch.Center().x, swatch.top, swatch.right, swatch.bottom), Paint::Fill(Color::Hex(0x9A9A9A)));
                canvas.DrawRect(swatch, Paint::Fill(_config.color));
                canvas.Restore();
                canvas.DrawRRect(shape, Paint::Stroked(_hovered ? t.primary : t.border, _hovered ? 1.5f : 1.f));
                if (!_config.label.empty()) {
                    const Paragraph shown(_config.label, t.textStyle);
                    canvas.DrawText(_config.label, { swatch.right + 8.f, std::round(offset.y + (Size().y - shown.Size().y) * 0.5f) }, t.textStyle);
                }
            }

        protected:
            void PerformLayout() override
            {
                const Theme& t = Theme::Current();
                const float label = _config.label.empty() ? 0.f : std::ceil(Paragraph(_config.label, t.textStyle).MaxIntrinsicWidth()) + 8.f;
                SetSize({ Swatch.x + label, std::max(Swatch.y, t.controlHeight - 8.f) });
            }

        private:
            static constexpr glm::vec2 Swatch { 44.f, 24.f };

            void Open()
            {
                Owner* owner = GetOwner();
                if (!owner || !owner->showPopup) return;
                const Theme& t = Theme::Current();
                const float width = _config.options.width > 0.f ? _config.options.width : 220.f;
                float height = std::round(width * 0.62f) + 24.f;
                if (_config.options.alpha) height += 24.f;
                if (_config.options.hex) height += 30.f;
                constexpr float Pad = 12.f;
                // What is picked is told to whoever asked now, not to whoever asked when the picker opened.
                Widget picker = ColorPicker(_config.color, [this, alive = _alive](const Color c) { if (*alive && _config.onChanged) _config.onChanged(c); },
                                            _config.options);
                owner->showPopup(Container({
                    .padding = EdgeInsets::All(Pad),
                    .decoration = { .color = t.surface, .borderWidth = 1.f, .borderColor = t.border, .radius = std::min(t.radius, 16.f) },
                }, std::move(picker)), ToGlobal({ 0.f, Size().y + 4.f }), { width + 2.f * Pad, height + 2.f * Pad });
            }

            Config _config;
            std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
            bool _hovered = false;

        public:
            ~RenderColorEdit() override { *_alive = false; }
        };

        struct ColorEditWidget final : RenderObjectWidget {
            RenderColorEdit::Config config;
            explicit ColorEditWidget(RenderColorEdit::Config c) : config(std::move(c)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderColorEdit>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderColorEdit&>(object).Set(config); }
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

    Widget Slider(const float value, std::function<void(float)> onChanged, const float min, const float max, std::function<void()> onFinished)
    {
        return Make<SliderWidget>(RenderSlider::Config { value, min, max, std::move(onChanged), std::move(onFinished) });
    }

    Widget ProgressBar(const float value)
    {
        const float v = std::clamp(value, 0.f, 1.f);
        return CustomPaint([v](Canvas& canvas, const glm::vec2 size) {
            const Theme& t = Theme::Current();      // the one it is painted in: where it is
            const float r = size.y * 0.5f;
            canvas.DrawRRect({ Rect::FromSize(size), r }, Paint::Fill(t.surfacePressed));
            if (v > 0.f) canvas.DrawRRect({ Rect::XYWH(0.f, 0.f, std::max(size.x * v, size.y), size.y), r }, Paint::Fill(t.primary));
        }, { 200.f, 6.f });
    }

    Widget TextField(TextFieldOptions options) { return Make<TextFieldWidget>(std::move(options)); }

    Widget DragValue(const float value, std::function<void(float)> onChanged, DragValueOptions options)
    {
        return Make<DragValueWidget>(RenderDragValue::Config { value, std::move(onChanged), std::move(options) });
    }

    Widget Dropdown(std::vector<std::string> items, const int selected, std::function<void(int)> onChanged, DropdownOptions options)
    {
        return Make<DropdownWidget>(RenderDropdown::Config { std::move(items), selected, std::move(onChanged), std::move(options) });
    }

    Widget MenuBar(std::vector<Menu> menus)
    {
        std::vector<Widget> titles;
        for (auto& menu : menus) titles.push_back(Make<MenuTitleWidget>(std::move(menu)));
        return Row(std::move(titles), { .gap = 2.f });
    }

    Widget ColorEdit(const Color color, std::function<void(Color)> onChanged, std::string label, const ColorPickerOptions options)
    {
        return Make<ColorEditWidget>(RenderColorEdit::Config { color, std::move(onChanged), std::move(label), options });
    }

    Widget ContextMenu(std::vector<MenuItem> items, Widget child)
    {
        auto widget = std::make_shared<ContextAreaWidget>();
        widget->items = std::move(items);
        if (child) widget->children.push_back(std::move(child));
        return Widget(widget);
    }
}
