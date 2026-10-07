//
// koral-ui's C interface: the handles, the callbacks turned into std::functions, and nothing escaping.
//

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <capiInterop.h>
#include <frameGraph.h>
#include <input.h>

#include <koralUI.h>
#include <koralUI_c.h>

using namespace kui;

// ---- the handles ------------------------------------------------------------------------------------------

struct KuiGradient { std::shared_ptr<const Gradient> gradient; };
struct KuiFont { std::shared_ptr<Font> font; };
struct KuiPicture { std::shared_ptr<const Picture> picture; };
struct KuiLayer { std::shared_ptr<Layer> layer; };
struct KuiElementShader { std::shared_ptr<ElementShader> shader; };
struct KuiWidget { Widget widget; };
struct KuiDockLayout { std::shared_ptr<DockLayout> layout; };
struct KuiVectorImage { std::shared_ptr<const VectorImage> image; };

namespace
{
    // ---- errors ----------------------------------------------------------------------------------------

    template <typename Body>
    auto Guarded(Body&& body, decltype(body()) failed) -> decltype(body())
    {
        try {
            koral_set_last_error(nullptr);
            return body();
        } catch (const std::exception& e) {
            koral_set_last_error(e.what());
        } catch (...) {
            koral_set_last_error("an unknown exception");
        }
        return failed;
    }

    template <typename Body>
    void GuardedVoid(Body&& body) { Guarded([&] { body(); return 0; }, 0); }

    template <typename T>
    T& Need(T* object, const char* what)
    {
        if (!object) throw std::runtime_error(std::string("no ") + what + " was given");
        return *object;
    }

    Canvas& CanvasOf(KuiCanvas* c) { return Need(reinterpret_cast<Canvas*>(c), "canvas"); }
    Path& PathOf(KuiPath* p) { return Need(reinterpret_cast<Path*>(p), "path"); }
    Paragraph& ParagraphOf(KuiParagraph* p) { return Need(reinterpret_cast<Paragraph*>(p), "paragraph"); }
    Renderer& RendererOf(KuiRenderer* r) { return Need(reinterpret_cast<Renderer*>(r), "renderer"); }
    Ui& UiOf(KuiUi* v) { return Need(reinterpret_cast<Ui*>(v), "ui"); }

    // ---- values ----------------------------------------------------------------------------------------

    glm::vec2 V(const KuiVec2 v) { return { v.x, v.y }; }
    KuiVec2 V(const glm::vec2 v) { return { v.x, v.y }; }
    Color C(const KuiColor c) { return { c.r, c.g, c.b, c.a }; }
    KuiColor C(const Color c) { return { c.r, c.g, c.b, c.a }; }
    Rect R(const KuiRect r) { return { r.left, r.top, r.right, r.bottom }; }
    KuiRect R(const Rect r) { return { r.left, r.top, r.right, r.bottom }; }
    Radii Rd(const KuiRadii r) { return { r.top_left, r.top_right, r.bottom_right, r.bottom_left }; }
    Transform T(const KuiTransform t) { return { t.a, t.b, t.c, t.d, t.tx, t.ty }; }
    KuiTransform T(const Transform& t) { return { t.a, t.b, t.c, t.d, t.tx, t.ty }; }
    EdgeInsets E(const KuiEdgeInsets e) { return { e.left, e.top, e.right, e.bottom }; }
    Alignment A(const KuiAlignment a) { return { a.x, a.y }; }

    Paint PaintOf(const KuiPaint* p)
    {
        if (!p) throw std::runtime_error("no paint was given");
        Paint paint;
        paint.fill = C(p->fill);
        if (p->gradient) paint.gradient = p->gradient->gradient;
        paint.stroke = { p->stroke.width, C(p->stroke.color), static_cast<StrokeCap>(p->stroke.cap),
                         static_cast<StrokeJoin>(p->stroke.join), p->stroke.miter_limit };
        paint.opacity = p->opacity;
        return paint;
    }

    TextStyle StyleOf(const KuiTextStyle* s)
    {
        TextStyle style;
        if (!s) return style;
        if (s->font) style.font = s->font->font;
        style.size = s->size;
        style.color = C(s->color);
        style.lineHeight = s->line_height;
        style.letterSpacing = s->letter_spacing;
        style.weight = s->weight > 0.f ? s->weight : 400.f;
        style.italic = s->italic;
        style.underline = s->underline;
        style.lineThrough = s->line_through;
        return style;
    }

    Theme ThemeOf(const KuiTheme& t)
    {
        Theme theme;
        theme.background = C(t.background); theme.surface = C(t.surface); theme.surfaceHover = C(t.surface_hover);
        theme.surfacePressed = C(t.surface_pressed); theme.primary = C(t.primary); theme.primaryHover = C(t.primary_hover);
        theme.primaryPressed = C(t.primary_pressed); theme.onPrimary = C(t.on_primary); theme.text = C(t.text);
        theme.textMuted = C(t.text_muted); theme.border = C(t.border); theme.focus = C(t.focus);
        theme.radius = t.radius; theme.controlHeight = t.control_height;
        theme.textStyle = StyleOf(&t.text_style);
        theme.buttonRadius = t.button_radius; theme.fieldRadius = t.field_radius; theme.checkboxRadius = t.checkbox_radius;
        theme.design = t.design <= static_cast<uint32_t>(ThemeDesign::eFluent) ? static_cast<ThemeDesign>(t.design) : ThemeDesign::eKoral;
        return theme;
    }

    void ThemeOf(const Theme& t, KuiTheme* out)
    {
        if (!out) throw std::runtime_error("no theme was given to fill in");
        *out = { C(t.background), C(t.surface), C(t.surfaceHover), C(t.surfacePressed), C(t.primary), C(t.primaryHover),
                 C(t.primaryPressed), C(t.onPrimary), C(t.text), C(t.textMuted), C(t.border), C(t.focus),
                 t.radius, t.controlHeight,
                 { nullptr, t.textStyle.size, C(t.textStyle.color), t.textStyle.lineHeight, t.textStyle.letterSpacing, t.textStyle.weight,
                   t.textStyle.italic, t.textStyle.underline, t.textStyle.lineThrough },
                 t.buttonRadius, t.fieldRadius, t.checkboxRadius, static_cast<uint32_t>(t.design) };
    }

    std::shared_ptr<const Gradient> MakeGradient(Gradient g, const float* offsets, const KuiColor* colors, const size_t count)
    {
        for (std::size_t i = 0; i < count; ++i) g.stops.push_back({ offsets ? offsets[i] : static_cast<float>(i) / std::max<float>(1.f, static_cast<float>(count - 1)), C(colors[i]) });
        return std::make_shared<const Gradient>(std::move(g));
    }

    // ---- callbacks -------------------------------------------------------------------------------------

    /** @brief What a callback's `user` belongs to: let go of (destroy called) when the last copy of the callback goes. */
    struct Owned {
        void* user;
        void (*destroy)(void*);
        Owned(void* u, void (*d)(void*)) : user(u), destroy(d) {}
        ~Owned() { if (destroy) destroy(user); }
        Owned(const Owned&) = delete;
        Owned& operator=(const Owned&) = delete;
    };

    template <typename S>
    std::shared_ptr<Owned> Hold(const S& s)
    {
        if (!s.invoke) {
            if (s.destroy) s.destroy(s.user);   // nothing to call it with: let it go now
            return nullptr;
        }
        return std::make_shared<Owned>(s.user, s.destroy);
    }

    std::function<void()> F(const KuiAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke] { f(o->user); };
    }
    std::function<void(Color)> F(const KuiColorAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const Color c) { f(c.r, c.g, c.b, c.a, o->user); };
    }
    std::function<void(bool)> F(const KuiBoolAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const bool v) { f(v, o->user); };
    }
    std::function<void(float)> F(const KuiFloatAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const float v) { f(v, o->user); };
    }
    std::function<void(glm::vec2)> F(const KuiPointAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const glm::vec2 p) { f(p.x, p.y, o->user); };
    }
    std::function<void(glm::vec2, glm::vec2)> F(const KuiPanAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const glm::vec2 d, const glm::vec2 p) { f(d.x, d.y, p.x, p.y, o->user); };
    }
    std::function<bool(glm::vec2)> F(const KuiScrollAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const glm::vec2 d) { return f(d.x, d.y, o->user); };
    }
    std::function<void(const std::string&)> F(const KuiTextAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const std::string& t) { f(t.c_str(), o->user); };
    }

    /** @brief What a drag from C carries: text, an object of the caller's, or both. */
    struct CPayload {
        std::string text;
        std::shared_ptr<Owned> object;
    };

    std::function<void(const DragData&, glm::vec2)> F(const KuiDropAction& a)
    {
        auto o = Hold(a);
        if (!o) return {};
        return [o, f = a.invoke](const DragData& data, const glm::vec2 at) {
            const char* text = "";
            void* object = nullptr;
            if (const auto* c = data.As<CPayload>()) { text = c->text.c_str(); object = c->object ? c->object->user : nullptr; }
            else if (const auto* s = data.As<std::string>()) text = s->c_str();
            f(data.type.c_str(), text, object, at.x, at.y, o->user);
        };
    }

    /** @brief A widget handle a callback returned: taken, and the handle released. */
    Widget Take(KuiWidget* handle)
    {
        if (!handle) return {};
        Widget w = handle->widget;
        delete handle;
        return w;
    }

    KuiWidget* Give(Widget widget) { return widget ? new KuiWidget { std::move(widget) } : nullptr; }
    Widget W(KuiWidget* handle) { return handle ? handle->widget : Widget {}; }

    std::vector<MenuItem> MenuItems(const KuiMenuItem* items, const std::size_t count)
    {
        std::vector<MenuItem> list;
        for (std::size_t i = 0; items && i < count; ++i)
            list.push_back({ items[i].label ? items[i].label : "", F(items[i].on_selected), !items[i].disabled, items[i].separator });
        return list;
    }

    std::vector<Widget> Children(KuiWidget* const* children, const size_t count)
    {
        std::vector<Widget> out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            if (children && children[i]) out.push_back(children[i]->widget);
        return out;
    }

    FlexOptions FlexOf(const KuiFlexOptions* o)
    {
        FlexOptions f;
        if (!o) return f;
        f.mainAxisAlignment = static_cast<MainAxisAlignment>(o->main_axis_alignment);
        f.crossAxisAlignment = static_cast<CrossAxisAlignment>(o->cross_axis_alignment);
        f.mainAxisSize = static_cast<MainAxisSize>(o->main_axis_size);
        f.gap = o->gap;
        return f;
    }

    Decoration DecorationOf(const KuiDecoration& d)
    {
        Decoration out;
        out.color = C(d.color);
        if (d.gradient) out.gradient = d.gradient->gradient;
        out.borderWidth = d.border_width;
        out.borderColor = C(d.border_color);
        out.radius = Rd(d.radius);
        out.shadowColor = C(d.shadow_color);
        out.shadowBlur = d.shadow_blur;
        out.shadowOffset = V(d.shadow_offset);
        return out;
    }

    std::optional<float> Given(const float v) { return std::isnan(v) ? std::nullopt : std::optional<float>(v); }

    ButtonOptions ButtonOptionsOf(const KuiButtonOptions* o)
    {
        ButtonOptions options;
        if (!o) return options;
        options.style = static_cast<ButtonStyle>(o->style);
        options.width = Given(o->width);
        if (o->has_padding) options.padding = E(o->padding);
        options.enabled = o->enabled;
        return options;
    }

    // ---- widgets written in another language -----------------------------------------------------------

    class CStatelessWidget final : public StatelessWidget {
    public:
        explicit CStatelessWidget(const KuiStatelessCallbacks& c) : _c(c) {}
        ~CStatelessWidget() override { if (_c.destroy) _c.destroy(_c.user); }
        [[nodiscard]] Widget Build() const override { return _c.build ? Take(_c.build(_c.user)) : Widget {}; }
        [[nodiscard]] const void* TypeTag() const override { return _c.type; }
    private:
        KuiStatelessCallbacks _c;
    };

    class CStatefulWidget final : public StatefulWidget {
    public:
        explicit CStatefulWidget(const KuiStatefulCallbacks& c) : _c(c) {}
        ~CStatefulWidget() override { if (_c.destroy) _c.destroy(_c.user); }
        [[nodiscard]] Widget Build() override { return _c.build ? Take(_c.build(Handle(), _c.user)) : Widget {}; }
        void InitState() override { if (_c.init_state) _c.init_state(Handle(), _c.user); }
        void Dispose() override { if (_c.dispose) _c.dispose(Handle(), _c.user); }
        void DidUpdateWidget(const StatefulWidget& newer) override
        {
            if (_c.did_update_widget) _c.did_update_widget(Handle(), _c.user, static_cast<const CStatefulWidget&>(newer)._c.user);
        }
        [[nodiscard]] const void* TypeTag() const override { return _c.type; }

        void Changed() { SetState(); }
        void Tick(std::function<bool(float)> tick) { Animate(std::move(tick)); }
        [[nodiscard]] bool IsMounted() const { return Mounted(); }

    private:
        KuiState* Handle() { return reinterpret_cast<KuiState*>(this); }
        KuiStatefulCallbacks _c;
    };

    CStatefulWidget& StateOf(KuiState* s) { return Need(reinterpret_cast<CStatefulWidget*>(s), "state"); }

    kor::FrameGraph& GraphOf(KoralFrameGraph* g) { return Need(reinterpret_cast<kor::FrameGraph*>(g), "frame graph"); }
    KoralRenderPass* PassHandle(kor::RenderPass& pass) { return reinterpret_cast<KoralRenderPass*>(&pass); }
    std::string TargetOf(const char* target) { return target ? std::string(target) : std::string(kor::FrameGraph::Screen); }
}

extern "C" {

// ==== gradients and fonts ================================================================================

KuiGradient* kui_gradient_linear(const KuiVec2 from, const KuiVec2 to, const float* offsets, const KuiColor* colors, const size_t count)
{
    return Guarded([&] { return new KuiGradient { MakeGradient(Gradient::Linear(V(from), V(to), {}), offsets, colors, count) }; }, static_cast<KuiGradient*>(nullptr));
}
KuiGradient* kui_gradient_radial(const KuiVec2 center, const float radius, const float* offsets, const KuiColor* colors, const size_t count)
{
    return Guarded([&] { return new KuiGradient { MakeGradient(Gradient::Radial(V(center), radius, {}), offsets, colors, count) }; }, static_cast<KuiGradient*>(nullptr));
}
KuiGradient* kui_gradient_sweep(const KuiVec2 center, const float angle, const float* offsets, const KuiColor* colors, const size_t count)
{
    return Guarded([&] { return new KuiGradient { MakeGradient(Gradient::Sweep(V(center), angle, {}), offsets, colors, count) }; }, static_cast<KuiGradient*>(nullptr));
}
void kui_gradient_release(KuiGradient* gradient) { delete gradient; }

KuiFont* kui_font_load(const char* path)
{
    return Guarded([&]() -> KuiFont* {
        auto font = Font::Load(path ? path : "");
        if (!font) throw std::runtime_error(std::string("the font ") + (path ? path : "") + " could not be read");
        return new KuiFont { std::move(font) };
    }, nullptr);
}
KuiFont* kui_font_from_memory(const void* bytes, const size_t size, const char* name)
{
    return Guarded([&]() -> KuiFont* {
        auto font = Font::FromMemory(std::span(static_cast<const std::byte*>(bytes), size), name ? name : "memory");
        if (!font) throw std::runtime_error("those bytes are not a font this can read");
        return new KuiFont { std::move(font) };
    }, nullptr);
}
KuiFont* kui_font_default(void) { return Guarded([] { auto f = Font::Default(); return f ? new KuiFont { f } : nullptr; }, static_cast<KuiFont*>(nullptr)); }
void kui_font_release(KuiFont* font) { delete font; }
void kui_font_set_fallback(KuiFont* font, KuiFont* fallback) { GuardedVoid([&] { Need(font, "font").font->SetFallback(fallback ? fallback->font : nullptr); }); }
float kui_font_ascent(KuiFont* font, const float size) { return Guarded([&] { return Need(font, "font").font->Ascent(size); }, 0.f); }
float kui_font_descent(KuiFont* font, const float size) { return Guarded([&] { return Need(font, "font").font->Descent(size); }, 0.f); }
bool kui_font_has_glyph(KuiFont* font, const uint32_t codepoint) { return Guarded([&] { return Need(font, "font").font->HasGlyph(codepoint); }, false); }

// ==== paths ==============================================================================================

KuiPath* kui_path_new(void) { return reinterpret_cast<KuiPath*>(new Path()); }
void kui_path_destroy(KuiPath* path) { delete reinterpret_cast<Path*>(path); }
void kui_path_move_to(KuiPath* p, const KuiVec2 v) { GuardedVoid([&] { PathOf(p).MoveTo(V(v)); }); }
void kui_path_line_to(KuiPath* p, const KuiVec2 v) { GuardedVoid([&] { PathOf(p).LineTo(V(v)); }); }
void kui_path_quad_to(KuiPath* p, const KuiVec2 c, const KuiVec2 v) { GuardedVoid([&] { PathOf(p).QuadTo(V(c), V(v)); }); }
void kui_path_cubic_to(KuiPath* p, const KuiVec2 c1, const KuiVec2 c2, const KuiVec2 v) { GuardedVoid([&] { PathOf(p).CubicTo(V(c1), V(c2), V(v)); }); }
void kui_path_arc_to(KuiPath* p, const KuiVec2 c, const float r, const float s, const float w) { GuardedVoid([&] { PathOf(p).ArcTo(V(c), r, s, w); }); }
void kui_path_arc_to_corner(KuiPath* p, const KuiVec2 c, const KuiVec2 to, const float r) { GuardedVoid([&] { PathOf(p).ArcTo(V(c), V(to), r); }); }
void kui_path_close(KuiPath* p) { GuardedVoid([&] { PathOf(p).Close(); }); }
void kui_path_add_rect(KuiPath* p, const KuiRect r) { GuardedVoid([&] { PathOf(p).AddRect(R(r)); }); }
void kui_path_add_rrect(KuiPath* p, const KuiRect r, const KuiRadii radii) { GuardedVoid([&] { PathOf(p).AddRRect({ R(r), Rd(radii) }); }); }
void kui_path_add_circle(KuiPath* p, const KuiVec2 c, const float r) { GuardedVoid([&] { PathOf(p).AddCircle(V(c), r); }); }
void kui_path_add_oval(KuiPath* p, const KuiRect r) { GuardedVoid([&] { PathOf(p).AddOval(R(r)); }); }
void kui_path_add_polygon(KuiPath* p, const KuiVec2* points, const size_t count, const bool close)
{
    GuardedVoid([&] { PathOf(p).AddPolygon(std::span(reinterpret_cast<const glm::vec2*>(points), count), close); });
}
void kui_path_set_fill_rule(KuiPath* p, const uint32_t rule) { GuardedVoid([&] { PathOf(p).SetFillRule(static_cast<FillRule>(rule)); }); }
KuiRect kui_path_bounds(KuiPath* p) { return Guarded([&] { return R(PathOf(p).Bounds()); }, KuiRect {}); }

// ==== pictures, layers and element shaders ===============================================================

void kui_picture_release(KuiPicture* picture) { delete picture; }
KuiRect kui_picture_bounds(KuiPicture* p) { return Guarded([&] { return R(Need(p, "picture").picture->Bounds()); }, KuiRect {}); }
size_t kui_picture_instance_count(KuiPicture* p) { return Guarded([&] { return Need(p, "picture").picture->InstanceCount(); }, std::size_t { 0 }); }

KuiLayer* kui_layer_create(void) { return new KuiLayer { Layer::Create() }; }
void kui_layer_release(KuiLayer* layer) { delete layer; }
void kui_layer_set_picture(KuiLayer* l, KuiPicture* p) { GuardedVoid([&] { Need(l, "layer").layer->SetPicture(p ? p->picture : nullptr); }); }
void kui_layer_set_transform(KuiLayer* l, const KuiTransform t) { GuardedVoid([&] { Need(l, "layer").layer->SetTransform(T(t)); }); }
KuiTransform kui_layer_get_transform(KuiLayer* l) { return Guarded([&] { return T(Need(l, "layer").layer->GetTransform()); }, KuiTransform {}); }
void kui_layer_set_opacity(KuiLayer* l, const float o) { GuardedVoid([&] { Need(l, "layer").layer->SetOpacity(o); }); }
float kui_layer_opacity(KuiLayer* l) { return Guarded([&] { return Need(l, "layer").layer->Opacity(); }, 0.f); }

KuiElementShader* kui_element_shader_load(const char* path, const char* entry)
{
    return Guarded([&] { return new KuiElementShader { ElementShader::Load(path ? path : "", entry ? entry : "") }; }, static_cast<KuiElementShader*>(nullptr));
}
void kui_element_shader_release(KuiElementShader* shader) { delete shader; }
bool kui_element_shader_valid(KuiElementShader* s) { return s && s->shader && s->shader->Valid(); }

// ==== text ==============================================================================================

KuiParagraph* kui_paragraph_new(const char* text, const KuiTextStyle* style, const float maxWidth, const uint32_t align)
{
    return Guarded([&] {
        return reinterpret_cast<KuiParagraph*>(new Paragraph(text ? text : "", StyleOf(style), maxWidth, static_cast<TextAlign>(align)));
    }, static_cast<KuiParagraph*>(nullptr));
}
void kui_paragraph_destroy(KuiParagraph* paragraph) { delete reinterpret_cast<Paragraph*>(paragraph); }
void kui_paragraph_layout(KuiParagraph* p, const float maxWidth) { GuardedVoid([&] { ParagraphOf(p).Layout(maxWidth); }); }
KuiVec2 kui_paragraph_size(KuiParagraph* p) { return Guarded([&] { return V(ParagraphOf(p).Size()); }, KuiVec2 {}); }
size_t kui_paragraph_line_count(KuiParagraph* p) { return Guarded([&] { return ParagraphOf(p).LineCount(); }, std::size_t { 0 }); }
float kui_paragraph_line_height(KuiParagraph* p) { return Guarded([&] { return ParagraphOf(p).LineHeight(); }, 0.f); }
float kui_paragraph_min_intrinsic_width(KuiParagraph* p) { return Guarded([&] { return ParagraphOf(p).MinIntrinsicWidth(); }, 0.f); }
float kui_paragraph_max_intrinsic_width(KuiParagraph* p) { return Guarded([&] { return ParagraphOf(p).MaxIntrinsicWidth(); }, 0.f); }
KuiVec2 kui_paragraph_caret_position(KuiParagraph* p, const size_t index) { return Guarded([&] { return V(ParagraphOf(p).CaretPosition(index)); }, KuiVec2 {}); }
size_t kui_paragraph_index_at(KuiParagraph* p, const KuiVec2 point) { return Guarded([&] { return ParagraphOf(p).IndexAt(V(point)); }, std::size_t { 0 }); }

// ==== the canvas ========================================================================================

KuiCanvas* kui_canvas_new(void) { return reinterpret_cast<KuiCanvas*>(new Canvas()); }
void kui_canvas_destroy(KuiCanvas* canvas) { delete reinterpret_cast<Canvas*>(canvas); }
KuiPicture* kui_canvas_finish(KuiCanvas* c) { return Guarded([&] { return new KuiPicture { CanvasOf(c).Finish() }; }, static_cast<KuiPicture*>(nullptr)); }

void kui_canvas_save(KuiCanvas* c) { GuardedVoid([&] { CanvasOf(c).Save(); }); }
void kui_canvas_restore(KuiCanvas* c) { GuardedVoid([&] { CanvasOf(c).Restore(); }); }
size_t kui_canvas_save_count(KuiCanvas* c) { return Guarded([&] { return CanvasOf(c).SaveCount(); }, std::size_t { 0 }); }
void kui_canvas_translate(KuiCanvas* c, const KuiVec2 by) { GuardedVoid([&] { CanvasOf(c).Translate(V(by)); }); }
void kui_canvas_scale(KuiCanvas* c, const KuiVec2 by) { GuardedVoid([&] { CanvasOf(c).Scale(V(by)); }); }
void kui_canvas_rotate(KuiCanvas* c, const float r) { GuardedVoid([&] { CanvasOf(c).Rotate(r); }); }
void kui_canvas_concat(KuiCanvas* c, const KuiTransform t) { GuardedVoid([&] { CanvasOf(c).Concat(T(t)); }); }
void kui_canvas_set_transform(KuiCanvas* c, const KuiTransform t) { GuardedVoid([&] { CanvasOf(c).SetTransform(T(t)); }); }
KuiTransform kui_canvas_current_transform(KuiCanvas* c) { return Guarded([&] { return T(CanvasOf(c).CurrentTransform()); }, KuiTransform {}); }
void kui_canvas_clip_rect(KuiCanvas* c, const KuiRect r) { GuardedVoid([&] { CanvasOf(c).ClipRect(R(r)); }); }
void kui_canvas_clip_rrect(KuiCanvas* c, const KuiRect r, const KuiRadii radii) { GuardedVoid([&] { CanvasOf(c).ClipRRect({ R(r), Rd(radii) }); }); }
void kui_canvas_set_tolerance(KuiCanvas* c, const float t) { GuardedVoid([&] { CanvasOf(c).SetTolerance(t); }); }

void kui_canvas_draw_rect(KuiCanvas* c, const KuiRect r, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawRect(R(r), PaintOf(p)); }); }
void kui_canvas_draw_rrect(KuiCanvas* c, const KuiRect r, const KuiRadii radii, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawRRect({ R(r), Rd(radii) }, PaintOf(p)); }); }
void kui_canvas_draw_circle(KuiCanvas* c, const KuiVec2 center, const float r, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawCircle(V(center), r, PaintOf(p)); }); }
void kui_canvas_draw_oval(KuiCanvas* c, const KuiRect r, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawOval(R(r), PaintOf(p)); }); }
void kui_canvas_draw_arc(KuiCanvas* c, const KuiVec2 center, const float r, const float s, const float w, const bool useCenter, const KuiPaint* p)
{
    GuardedVoid([&] { CanvasOf(c).DrawArc(V(center), r, s, w, useCenter, PaintOf(p)); });
}
void kui_canvas_draw_line(KuiCanvas* c, const KuiVec2 a, const KuiVec2 b, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawLine(V(a), V(b), PaintOf(p)); }); }
void kui_canvas_draw_triangle(KuiCanvas* c, const KuiVec2 a, const KuiVec2 b, const KuiVec2 d, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawTriangle(V(a), V(b), V(d), PaintOf(p)); }); }
void kui_canvas_draw_quadratic_bezier(KuiCanvas* c, const KuiVec2 a, const KuiVec2 k, const KuiVec2 b, const KuiPaint* p)
{
    GuardedVoid([&] { CanvasOf(c).DrawQuadraticBezier(V(a), V(k), V(b), PaintOf(p)); });
}
void kui_canvas_draw_cubic_bezier(KuiCanvas* c, const KuiVec2 a, const KuiVec2 k1, const KuiVec2 k2, const KuiVec2 b, const KuiPaint* p)
{
    GuardedVoid([&] { CanvasOf(c).DrawCubicBezier(V(a), V(k1), V(k2), V(b), PaintOf(p)); });
}
void kui_canvas_draw_polyline(KuiCanvas* c, const KuiVec2* points, const size_t count, const KuiPaint* p)
{
    GuardedVoid([&] { CanvasOf(c).DrawPolyline(std::span(reinterpret_cast<const glm::vec2*>(points), count), PaintOf(p)); });
}
void kui_canvas_draw_polygon(KuiCanvas* c, const KuiVec2* points, const size_t count, const KuiPaint* p)
{
    GuardedVoid([&] { CanvasOf(c).DrawPolygon(std::span(reinterpret_cast<const glm::vec2*>(points), count), PaintOf(p)); });
}
void kui_canvas_draw_path(KuiCanvas* c, KuiPath* path, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).DrawPath(PathOf(path), PaintOf(p)); }); }
void kui_canvas_draw_shadow(KuiCanvas* c, const KuiRect r, const KuiRadii radii, const KuiColor color, const float blur, const KuiVec2 offset, const float spread)
{
    GuardedVoid([&] { CanvasOf(c).DrawShadow({ R(r), Rd(radii) }, C(color), blur, V(offset), spread); });
}
void kui_canvas_draw_image(KuiCanvas* c, KoralImage* image, const KuiRect destination, const KuiRect source, const KuiColor tint)
{
    GuardedVoid([&] { CanvasOf(c).DrawImage(kor::capi::ImageOf(image), R(destination), R(source), C(tint)); });
}
void kui_canvas_draw_paragraph(KuiCanvas* c, KuiParagraph* p, const KuiVec2 at) { GuardedVoid([&] { CanvasOf(c).DrawParagraph(ParagraphOf(p), V(at)); }); }
void kui_canvas_draw_text(KuiCanvas* c, const char* text, const KuiVec2 at, const KuiTextStyle* style)
{
    GuardedVoid([&] { CanvasOf(c).DrawText(text ? text : "", V(at), StyleOf(style)); });
}
void kui_canvas_draw_element(KuiCanvas* c, KuiElementShader* shader, const KuiRect rect, const void* parameters, const size_t size,
                             const KuiRadii radii, const float opacity)
{
    GuardedVoid([&] {
        CanvasOf(c).DrawElement(Need(shader, "element shader").shader, R(rect),
                                std::span(static_cast<const std::byte*>(parameters), parameters ? size : 0), Rd(radii), opacity);
    });
}
void kui_canvas_draw_layer(KuiCanvas* c, KuiLayer* l) { GuardedVoid([&] { CanvasOf(c).DrawLayer(Need(l, "layer").layer); }); }
void kui_canvas_draw_picture(KuiCanvas* c, KuiPicture* p) { GuardedVoid([&] { CanvasOf(c).DrawPicture(*Need(p, "picture").picture); }); }

void kui_canvas_begin_path(KuiCanvas* c) { GuardedVoid([&] { CanvasOf(c).BeginPath(); }); }
void kui_canvas_move_to(KuiCanvas* c, const KuiVec2 p) { GuardedVoid([&] { CanvasOf(c).MoveTo(V(p)); }); }
void kui_canvas_draw_line_to(KuiCanvas* c, const KuiVec2 p) { GuardedVoid([&] { CanvasOf(c).DrawLineTo(V(p)); }); }
void kui_canvas_draw_quad_to(KuiCanvas* c, const KuiVec2 k, const KuiVec2 p) { GuardedVoid([&] { CanvasOf(c).DrawQuadTo(V(k), V(p)); }); }
void kui_canvas_draw_cubic_to(KuiCanvas* c, const KuiVec2 k1, const KuiVec2 k2, const KuiVec2 p) { GuardedVoid([&] { CanvasOf(c).DrawCubicTo(V(k1), V(k2), V(p)); }); }
void kui_canvas_draw_arc_to(KuiCanvas* c, const KuiVec2 center, const float r, const float s, const float w) { GuardedVoid([&] { CanvasOf(c).DrawArcTo(V(center), r, s, w); }); }
void kui_canvas_draw_arc_to_corner(KuiCanvas* c, const KuiVec2 corner, const KuiVec2 to, const float r) { GuardedVoid([&] { CanvasOf(c).DrawArcTo(V(corner), V(to), r); }); }
void kui_canvas_close_path(KuiCanvas* c) { GuardedVoid([&] { CanvasOf(c).ClosePath(); }); }
void kui_canvas_fill(KuiCanvas* c, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).Fill(PaintOf(p)); }); }
void kui_canvas_stroke(KuiCanvas* c, const KuiPaint* p) { GuardedVoid([&] { CanvasOf(c).Stroke(PaintOf(p)); }); }

// ==== drawing a layer tree ================================================================================

KuiRenderer* kui_renderer_new(void) { return reinterpret_cast<KuiRenderer*>(new Renderer()); }
void kui_renderer_destroy(KuiRenderer* renderer) { delete reinterpret_cast<Renderer*>(renderer); }
void kui_renderer_set_root(KuiRenderer* r, KuiLayer* l) { GuardedVoid([&] { RendererOf(r).SetRoot(l ? l->layer : nullptr); }); }
void kui_renderer_set_scale(KuiRenderer* r, const float s) { GuardedVoid([&] { RendererOf(r).SetScale(s); }); }
void kui_renderer_stats(KuiRenderer* r, KuiRendererStats* out)
{
    GuardedVoid([&] {
        const auto& s = RendererOf(r).Stats();
        Need(out, "statistics struct") = { s.instances, s.vertices, s.layers, s.clips, s.draws, s.uploadedBytes, s.composeMs, s.recomposed };
    });
}
KoralRenderPass* kui_graph_add_ui_pass_with_renderer(KoralFrameGraph* graph, KuiRenderer* renderer, const char* target)
{
    return Guarded([&] { return PassHandle(GraphOf(graph).Add<UiPass>(RendererOf(renderer), TargetOf(target))); }, static_cast<KoralRenderPass*>(nullptr));
}

// ==== widgets ===========================================================================================

void kui_widget_release(KuiWidget* widget) { delete widget; }
KuiWidget* kui_widget_retain(KuiWidget* widget) { return widget ? new KuiWidget { widget->widget } : nullptr; }
void kui_widget_set_key(KuiWidget* w, const char* key)
{
    GuardedVoid([&] { auto& h = Need(w, "widget"); h.widget = Widget(h.widget).Key(key ? key : ""); });
}

KuiWidget* kui_stateless_widget(const KuiStatelessCallbacks* c)
{
    return Guarded([&] { return Give(Make<CStatelessWidget>(Need(c, "widget callbacks"))); }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_stateful_widget(const KuiStatefulCallbacks* c)
{
    return Guarded([&] { return Give(Make<CStatefulWidget>(Need(c, "widget callbacks"))); }, static_cast<KuiWidget*>(nullptr));
}
void kui_state_set_state(KuiState* s) { GuardedVoid([&] { StateOf(s).Changed(); }); }
void kui_state_animate(KuiState* s, const KuiTicker tick)
{
    GuardedVoid([&] {
        auto o = Hold(tick);
        if (o) StateOf(s).Tick([o, f = tick.invoke](const float dt) { return f(dt, o->user); });
    });
}
bool kui_state_mounted(KuiState* s) { return Guarded([&] { return StateOf(s).IsMounted(); }, false); }

#define KUI_WIDGET(body) return Guarded([&]() -> KuiWidget* { return Give(body); }, static_cast<KuiWidget*>(nullptr))

KuiWidget* kui_text(const char* text, const KuiTextStyle* style, const uint32_t align, const bool wrap)
{
    KUI_WIDGET(Text(text ? text : "", StyleOf(style), static_cast<TextAlign>(align), wrap));
}
KuiWidget* kui_row(KuiWidget* const* children, const size_t count, const KuiFlexOptions* o) { KUI_WIDGET(Row(Children(children, count), FlexOf(o))); }
KuiWidget* kui_column(KuiWidget* const* children, const size_t count, const KuiFlexOptions* o) { KUI_WIDGET(Column(Children(children, count), FlexOf(o))); }
KuiWidget* kui_flex(const uint32_t axis, KuiWidget* const* children, const size_t count, const KuiFlexOptions* o)
{
    KUI_WIDGET(Flex(static_cast<Axis>(axis), Children(children, count), FlexOf(o)));
}
KuiWidget* kui_expanded(KuiWidget* child, const float flex) { KUI_WIDGET(Expanded(W(child), flex)); }
KuiWidget* kui_flexible(KuiWidget* child, const float flex) { KUI_WIDGET(Flexible(W(child), flex)); }
KuiWidget* kui_padding(const KuiEdgeInsets padding, KuiWidget* child) { KUI_WIDGET(Padding(E(padding), W(child))); }
KuiWidget* kui_align(const KuiAlignment alignment, KuiWidget* child) { KUI_WIDGET(Align(A(alignment), W(child))); }
KuiWidget* kui_center(KuiWidget* child) { KUI_WIDGET(Center(W(child))); }
KuiWidget* kui_sized_box(const float w, const float h, KuiWidget* child) { KUI_WIDGET(SizedBox(w, h, W(child))); }
KuiWidget* kui_constrained_box(const KuiBoxConstraints c, KuiWidget* child)
{
    KUI_WIDGET(ConstrainedBox({ c.min_width, c.max_width, c.min_height, c.max_height }, W(child)));
}
KuiWidget* kui_container(const KuiContainerOptions* o, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& opt = Need(o, "container options");
        ContainerOptions options;
        options.width = opt.width;
        options.height = opt.height;
        options.padding = E(opt.padding);
        options.margin = E(opt.margin);
        options.decoration = DecorationOf(opt.decoration);
        if (opt.has_alignment) options.alignment = A(opt.alignment);
        return Give(Container(std::move(options), W(child)));
    }, nullptr);
}
KuiWidget* kui_decorated_box(const KuiDecoration* d, KuiWidget* child) { KUI_WIDGET(DecoratedBox(DecorationOf(Need(d, "decoration")), W(child))); }
KuiWidget* kui_stack(KuiWidget* const* children, const size_t count, const KuiAlignment alignment) { KUI_WIDGET(Stack(Children(children, count), A(alignment))); }
KuiWidget* kui_positioned(const KuiPositionedOptions* o, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& p = Need(o, "positioned options");
        return Give(Positioned({ Given(p.left), Given(p.top), Given(p.right), Given(p.bottom), Given(p.width), Given(p.height) }, W(child)));
    }, nullptr);
}
KuiWidget* kui_stack_align(const KuiAlignment alignment, KuiWidget* child) { KUI_WIDGET(StackAlign(A(alignment), W(child))); }
KuiWidget* kui_scroll_view(KuiWidget* child, const uint32_t axis) { KUI_WIDGET(ScrollView(W(child), static_cast<Axis>(axis))); }
KuiWidget* kui_list_view(KuiWidget* const* children, const size_t count, const uint32_t axis, const float gap)
{
    KUI_WIDGET(ListView(Children(children, count), static_cast<Axis>(axis), gap));
}
KuiWidget* kui_list_view_builder(const size_t count, const float extent, const KuiItemBuilder builder)
{
    return Guarded([&]() -> KuiWidget* {
        auto o = std::make_shared<Owned>(builder.user, builder.destroy);
        auto build = builder.build;
        return Give(ListView(count, extent, [o, build](const std::size_t i) { return build ? Take(build(i, o->user)) : Widget {}; }));
    }, nullptr);
}
KuiWidget* kui_list_view_builder_with_range(const size_t count, const float extent, const KuiItemBuilder builder, const KuiRangeAction onRange)
{
    return Guarded([&]() -> KuiWidget* {
        auto o = std::make_shared<Owned>(builder.user, builder.destroy);
        auto r = std::make_shared<Owned>(onRange.user, onRange.destroy);
        auto build = builder.build;
        auto range = onRange.invoke;
        return Give(ListView(count, extent, [o, build](const std::size_t i) { return build ? Take(build(i, o->user)) : Widget {}; },
                             [r, range](const std::size_t f, const std::size_t l) { if (range) range(f, l, r->user); }));
    }, nullptr);
}
KuiWidget* kui_lazy_list(const KuiLazyListOptions* o)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& in = Need(o, "lazy list options");
        auto b = std::make_shared<Owned>(in.builder.user, in.builder.destroy);
        auto r = std::make_shared<Owned>(in.on_range.user, in.on_range.destroy);
        auto s = std::make_shared<Owned>(in.on_scrolled.user, in.on_scrolled.destroy);
        const auto build = in.builder.build;
        const auto range = in.on_range.invoke;
        const auto scrolled = in.on_scrolled.invoke;
        LazyListOptions options;
        options.count = in.count;
        options.axis = static_cast<Axis>(in.axis);
        options.itemExtent = in.item_extent;
        options.estimatedExtent = in.estimated_extent > 0.f ? in.estimated_extent : 40.f;
        options.gap = in.gap;
        options.paddingStart = in.padding_start;
        options.paddingEnd = in.padding_end;
        if (range) options.onRange = [r, range](const std::size_t f, const std::size_t l) { range(f, l, r->user); };
        if (scrolled) options.onScrolled = [s, scrolled](const std::size_t i, const float into) { scrolled(i, into, s->user); };
        options.jumpIndex = in.jump_index;
        options.jumpOffset = in.jump_offset;
        options.jump = in.jump;
        return Give(LazyList(std::move(options), [b, build](const std::size_t i) { return build ? Take(build(i, b->user)) : Widget {}; }));
    }, nullptr);
}
KuiWidget* kui_intrinsic(const bool width, const bool height, KuiWidget* child) { KUI_WIDGET(Intrinsic(width, height, W(child))); }
KuiWidget* kui_scroll_view_observed(KuiWidget* child, const uint32_t axis, const KuiPointAction onScrolled, const float jumpTo, const uint32_t jump)
{
    return Guarded([&]() -> KuiWidget* {
        ScrollOptions options;
        options.axis = static_cast<Axis>(axis);
        if (const std::function<void(glm::vec2)> told = F(onScrolled)) options.onScrolled = [told](const float at, const float most) { told({ at, most }); };
        options.jumpTo = jumpTo;
        options.jump = jump;
        return Give(ScrollView(W(child), std::move(options)));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_transform_box(const KuiTransform transform, const KuiAlignment origin, KuiWidget* child) { KUI_WIDGET(TransformBox(T(transform), W(child), A(origin))); }
KuiWidget* kui_aspect_ratio(const float ratio, KuiWidget* child) { KUI_WIDGET(AspectRatio(ratio, W(child))); }
KuiWidget* kui_fractionally_sized_box(const float w, const float h, KuiWidget* child) { KUI_WIDGET(FractionallySizedBox(w, h, W(child))); }

struct KuiLayoutContext { LayoutContext* context; };
size_t kui_layout_count(KuiLayoutContext* context) { return context && context->context ? context->context->count : 0; }
void kui_layout_measure(KuiLayoutContext* context, const size_t index, const float minWidth, const float maxWidth, const float minHeight,
                        const float maxHeight, float* outWidth, float* outHeight)
{
    GuardedVoid([&] {
        const glm::vec2 size = context && context->context ? context->context->measure(index, { minWidth, maxWidth, minHeight, maxHeight }) : glm::vec2 {};
        if (outWidth) *outWidth = size.x;
        if (outHeight) *outHeight = size.y;
    });
}
void kui_layout_place(KuiLayoutContext* context, const size_t index, const float x, const float y)
{
    GuardedVoid([&] { if (context && context->context) context->context->place(index, { x, y }); });
}
KuiWidget* kui_custom_layout(const KuiLayoutRule rule, KuiWidget* const* children, const size_t count)
{
    return Guarded([&]() -> KuiWidget* {
        auto owned = std::make_shared<Owned>(rule.user, rule.destroy);
        const auto layout = rule.layout;
        return Give(CustomLayout([owned, layout](LayoutContext& context, const BoxConstraints& c) -> glm::vec2 {
            if (!layout) return c.Smallest();
            KuiLayoutContext handle { &context };
            float width = 0.f, height = 0.f;
            layout(&handle, c.minWidth, c.maxWidth, c.minHeight, c.maxHeight, &width, &height, owned->user);
            return { width, height };
        }, Children(children, count)));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_popup_anchor(const bool open, KuiWidget* popup, const KuiAction onDismiss, const KuiVec2 offset, const bool below)
{
    KUI_WIDGET(PopupAnchor(open, W(popup), F(onDismiss), V(offset), below));
}
KuiWidget* kui_gesture_detector(const KuiGestureOptions* o, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& g = Need(o, "gesture options");
        GestureOptions options;
        options.onTap = F(g.on_tap);
        options.onTapDown = F(g.on_tap_down);
        options.onTapUp = F(g.on_tap_up);
        options.onPanStart = F(g.on_pan_start);
        options.onPanUpdate = F(g.on_pan_update);
        options.onPanEnd = F(g.on_pan_end);
        options.onEnter = F(g.on_enter);
        options.onExit = F(g.on_exit);
        options.onHover = F(g.on_hover);
        options.onScroll = F(g.on_scroll);
        options.opaque = g.opaque;
        return Give(GestureDetector(std::move(options), W(child)));
    }, nullptr);
}
KuiWidget* kui_custom_paint(const KuiPainter painter, const KuiVec2 size, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        auto o = std::make_shared<Owned>(painter.user, painter.destroy);
        auto paint = painter.paint;
        return Give(CustomPaint([o, paint](Canvas& canvas, const glm::vec2 s) {
            if (paint) paint(reinterpret_cast<KuiCanvas*>(&canvas), s.x, s.y, o->user);
        }, V(size), W(child)));
    }, nullptr);
}
KuiWidget* kui_shader_box(KuiElementShader* shader, const void* parameters, const size_t size, const KuiRadii radius, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        const auto* bytes = static_cast<const std::byte*>(parameters);
        return Give(ShaderBox(Need(shader, "element shader").shader,
                              bytes ? std::vector<std::byte>(bytes, bytes + size) : std::vector<std::byte> {}, Rd(radius), W(child)));
    }, nullptr);
}
KuiWidget* kui_image(KoralImage* image, const uint32_t fit, const KuiVec2 size) { KUI_WIDGET(Image(kor::capi::ImageOf(image), static_cast<ImageFit>(fit), V(size))); }
KuiWidget* kui_repaint_boundary(KuiWidget* child) { KUI_WIDGET(RepaintBoundary(W(child))); }
KuiWidget* kui_opacity(const float opacity, KuiWidget* child) { KUI_WIDGET(Opacity(opacity, W(child))); }
namespace {
    Curve CurveOf(const uint32_t curve) { return curve <= static_cast<uint32_t>(Curve::eEaseOutBack) ? static_cast<Curve>(curve) : Curve::eEaseInOut; }
    AnimationOptions AnimationOf(const float duration, const uint32_t curve) { return AnimationOptions {}.SetDuration(duration).SetCurve(CurveOf(curve)); }
}
KuiWidget* kui_backdrop_filter(KuiWidget* child, const float blur, const KuiColor tint, const float refraction, const float radius)
{
    KUI_WIDGET(BackdropFilter(Backdrop {}.SetBlur(blur).SetTint(C(tint)).SetRefraction(refraction), radius, W(child)));
}
float kui_ease(const uint32_t curve, const float t) { return Guarded([&] { return Ease(CurveOf(curve), t); }, t); }
KuiWidget* kui_animated_opacity(const float opacity, KuiWidget* child, const float duration, const uint32_t curve)
{
    KUI_WIDGET(AnimatedOpacity(opacity, W(child), AnimationOf(duration, curve)));
}
KuiWidget* kui_appear(KuiWidget* child, const float duration, const uint32_t curve, const float rise) { KUI_WIDGET(Appear(W(child), AnimationOf(duration, curve), rise)); }
KuiWidget* kui_reveal(const bool open, KuiWidget* child, const float duration, const uint32_t curve)
{
    KUI_WIDGET(Reveal(open, child ? W(child) : Widget {}, AnimationOf(duration, curve)));
}
KuiWidget* kui_clip_rrect(const KuiRadii radius, KuiWidget* child) { KUI_WIDGET(ClipRRect(Rd(radius), W(child))); }
KuiWidget* kui_translate(const KuiVec2 offset, KuiWidget* child) { KUI_WIDGET(Translate(V(offset), W(child))); }
KuiWidget* kui_ignore_pointer(KuiWidget* child) { KUI_WIDGET(IgnorePointer(W(child))); }

KuiWidget* kui_draggable(const KuiDragData* data, KuiWidget* child, const KuiDraggableOptions* o)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& d = Need(data, "drag data");
        CPayload payload { d.text ? d.text : "", d.payload || d.destroy ? std::make_shared<Owned>(d.payload, d.destroy) : nullptr };
        DraggableOptions options;
        if (o) {
            options.feedback = W(o->feedback);
            options.onDragStart = F(o->on_drag_start);
            options.onDragEnd = F(o->on_drag_end);
            options.enabled = !o->disabled;
            options.feedbackInPlace = o->feedback_in_place;
            options.feedbackRadius = o->feedback_radius;
        }
        return Give(Draggable({ d.type ? d.type : "", std::move(payload) }, W(child), std::move(options)));
    }, nullptr);
}

KuiWidget* kui_drop_target(const KuiDropTargetOptions* o, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& t = Need(o, "drop target options");
        DropTargetOptions options;
        if (t.accepts_type) options.AcceptsType(t.accepts_type);
        options.onDrop = F(t.on_drop);
        if (auto enter = F(t.on_enter)) options.onEnter = [enter](const DragData& d) { enter(d, {}); };
        options.onLeave = F(t.on_leave);
        options.onMove = F(t.on_move);
        return Give(DropTarget(std::move(options), W(child)));
    }, nullptr);
}

KuiDockLayout* kui_dock_layout_new(void)
{
    return Guarded([&] { return new KuiDockLayout { std::make_shared<DockLayout>() }; }, static_cast<KuiDockLayout*>(nullptr));
}
void kui_dock_layout_release(KuiDockLayout* layout) { delete layout; }
void kui_dock_layout_dock(KuiDockLayout* l, const char* panel, const uint32_t side, const char* relativeTo, const float fraction)
{
    GuardedVoid([&] { Need(l, "dock layout").layout->Dock(panel ? panel : "", static_cast<DockSide>(side), relativeTo ? relativeTo : "", fraction); });
}
void kui_dock_layout_float(KuiDockLayout* l, const char* panel, const KuiRect rect) { GuardedVoid([&] { Need(l, "dock layout").layout->Float(panel ? panel : "", R(rect)); }); }
void kui_dock_layout_float_at(KuiDockLayout* l, const char* panel, const KuiVec2 at) { GuardedVoid([&] { Need(l, "dock layout").layout->Float(panel ? panel : "", V(at)); }); }
void kui_dock_layout_pop_out(KuiDockLayout* l, const char* panel, const KuiVec2 size) { GuardedVoid([&] { Need(l, "dock layout").layout->PopOut(panel ? panel : "", V(size)); }); }
void kui_dock_layout_close(KuiDockLayout* l, const char* panel) { GuardedVoid([&] { Need(l, "dock layout").layout->Close(panel ? panel : ""); }); }
void kui_dock_layout_open(KuiDockLayout* l, const char* panel) { GuardedVoid([&] { Need(l, "dock layout").layout->Open(panel ? panel : ""); }); }
void kui_dock_layout_dock_in(KuiDockLayout* l, const char* panel, const uint32_t area, const int32_t part)
{
    GuardedVoid([&] { Need(l, "dock layout").layout->Dock(panel ? panel : "", static_cast<DockArea>(area), part); });
}
void kui_dock_layout_hide(KuiDockLayout* l, const char* panel) { GuardedVoid([&] { Need(l, "dock layout").layout->Hide(panel ? panel : ""); }); }
bool kui_dock_layout_is_shown(KuiDockLayout* l, const char* panel) { return Guarded([&] { return Need(l, "dock layout").layout->IsShown(panel ? panel : ""); }, false); }
void kui_dock_layout_activate(KuiDockLayout* l, const char* panel) { GuardedVoid([&] { Need(l, "dock layout").layout->Activate(panel ? panel : ""); }); }
bool kui_dock_layout_is_open(KuiDockLayout* l, const char* panel) { return Guarded([&] { return Need(l, "dock layout").layout->IsOpen(panel ? panel : ""); }, false); }
bool kui_dock_layout_is_floating(KuiDockLayout* l, const char* panel) { return Guarded([&] { return Need(l, "dock layout").layout->IsFloating(panel ? panel : ""); }, false); }
const char* kui_dock_layout_save(KuiDockLayout* l)
{
    thread_local std::string saved;
    return Guarded([&] { saved = Need(l, "dock layout").layout->Save(); return saved.c_str(); }, "");
}
bool kui_dock_layout_load(KuiDockLayout* l, const char* text) { return Guarded([&] { return Need(l, "dock layout").layout->Load(text ? text : ""); }, false); }

KuiWidget* kui_dock_space(KuiDockLayout* l, const KuiDockPanel* panels, const size_t count, const KuiDockOptions* o)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<DockPanel> list;
        for (std::size_t i = 0; i < count; ++i)
            list.push_back({ panels[i].id ? panels[i].id : "", panels[i].title ? panels[i].title : "", W(panels[i].content), !panels[i].fixed,
                             !panels[i].undockable, !panels[i].no_title_bar, panels[i].icon ? panels[i].icon->image : nullptr,
                             panels[i].title_bar ? W(panels[i].title_bar) : Widget {} });
        DockOptions options;
        if (o) {
            options.multiViewport = !o->single_viewport;
            if (o->gap > 0.f) options.gap = o->gap;
            if (o->stripe_gap >= 0.f) options.stripeGap = o->stripe_gap;
            const auto given = [](float& into, const float value) { if (value > 0.f) into = value; };
            const KuiDockStyle& s = o->style;
            given(options.style.titleBarHeight, s.title_bar_height); given(options.style.stripeWidth, s.stripe_width);
            given(options.style.buttonSize, s.button_size); given(options.style.buttonGap, s.button_gap);
            given(options.style.separatorGap, s.separator_gap); given(options.style.tabPadding, s.tab_padding);
            given(options.style.resizeGrip, s.resize_grip); given(options.style.minFloatSize, s.min_float_size);
            given(options.style.minAreaSize, s.min_area_size); given(options.style.radius, s.radius);
            given(options.style.edgeDropMargin, s.edge_drop_margin); given(options.style.centerDropSize, s.center_drop_size);
            given(options.style.underDropStart, s.under_drop_start);
            options.onClosed = F(o->on_closed);
            options.onChanged = F(o->on_changed);
        }
        return Give(DockSpace(Need(l, "dock layout").layout, std::move(list), std::move(options)));
    }, nullptr);
}

KuiWidget* kui_button(const char* label, const KuiAction onPressed, const KuiButtonOptions* o)
{
    KUI_WIDGET(Button(std::string(label ? label : ""), F(onPressed), ButtonOptionsOf(o)));
}
KuiWidget* kui_button_with_child(KuiWidget* child, const KuiAction onPressed, const KuiButtonOptions* o)
{
    KUI_WIDGET(Button(W(child), F(onPressed), ButtonOptionsOf(o)));
}
KuiWidget* kui_checkbox(const bool value, const KuiBoolAction onChanged, const char* label) { KUI_WIDGET(Checkbox(value, F(onChanged), label ? label : "")); }
KuiWidget* kui_switch(const bool value, const KuiBoolAction onChanged) { KUI_WIDGET(Switch(value, F(onChanged))); }
KuiWidget* kui_slider(const float value, const KuiFloatAction onChanged, const float min, const float max) { KUI_WIDGET(Slider(value, F(onChanged), min, max)); }
KuiWidget* kui_slider_finished(const float value, const KuiFloatAction onChanged, const float min, const float max, const KuiAction onFinished)
{
    KUI_WIDGET(Slider(value, F(onChanged), min, max, F(onFinished)));
}
KuiWidget* kui_text_lines(const char* text, const KuiTextStyle* style, const uint32_t align, const bool wrap, const int32_t maxLines, const bool ellipsis)
{
    KUI_WIDGET(Text(text ? text : "", StyleOf(style), static_cast<TextAlign>(align), wrap, maxLines, ellipsis));
}
KuiWidget* kui_themed(const KuiTheme* theme, KuiWidget* child) { KUI_WIDGET(Themed(ThemeOf(Need(theme, "theme")), W(child))); }
bool kui_system_appearance(bool* dark, KuiColor* accent)
{
    return Guarded([&] {
        const SystemAppearance a = QuerySystemAppearance();
        if (dark) *dark = a.dark;
        if (accent) *accent = C(a.accent);
        return a.known;
    }, false);
}
KuiWidget* kui_progress_bar(const float value) { KUI_WIDGET(ProgressBar(value)); }
KuiWidget* kui_drag_value(const float value, const KuiFloatAction onChanged, const float speed, const float min, const float max,
                          const int32_t decimals, const char* label, const float width, const bool typeable, const bool wrap)
{
    KUI_WIDGET(DragValue(value, F(onChanged), DragValueOptions { speed, min, max, decimals, label ? label : "", width, Axis::eHorizontal, typeable, wrap }));
}
KuiWidget* kui_drag_value_vertical(const float value, const KuiFloatAction onChanged, const float speed, const float min, const float max,
                                   const int32_t decimals, const char* label, const float width, const bool typeable, const bool wrap)
{
    KUI_WIDGET(DragValue(value, F(onChanged), DragValueOptions { speed, min, max, decimals, label ? label : "", width, Axis::eVertical, typeable, wrap }));
}
KuiWidget* kui_slider_vertical(const float value, const KuiFloatAction onChanged, const float min, const float max, const KuiAction onFinished)
{
    KUI_WIDGET(Slider(value, F(onChanged), min, max, F(onFinished), Axis::eVertical));
}
KuiWidget* kui_dropdown(const char* const* items, const size_t count, const int32_t selected, const KuiFloatAction onChanged,
                        const float width, const char* placeholder)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<std::string> list;
        for (std::size_t i = 0; i < count; ++i) list.emplace_back(items && items[i] ? items[i] : "");
        std::function<void(float)> changed = F(onChanged);
        return Give(Dropdown(std::move(list), selected, [changed](const int index) { if (changed) changed(static_cast<float>(index)); },
                             DropdownOptions { width, placeholder ? placeholder : "" }));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_context_menu(const KuiMenuItem* items, const size_t count, KuiWidget* child)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<MenuItem> list;
        for (std::size_t i = 0; i < count; ++i)
            list.push_back({ items[i].label ? items[i].label : "", F(items[i].on_selected), !items[i].disabled, items[i].separator });
        return Give(ContextMenu(std::move(list), W(child)));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_menu_bar(const KuiMenu* menus, const size_t count)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<Menu> list;
        for (std::size_t i = 0; menus && i < count; ++i) list.push_back({ menus[i].title ? menus[i].title : "", MenuItems(menus[i].items, menus[i].count) });
        return Give(MenuBar(std::move(list)));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_separator(const bool vertical, const float thickness) { KUI_WIDGET(Separator(vertical ? Axis::eVertical : Axis::eHorizontal, thickness)); }
KuiWidget* kui_disabled(KuiWidget* child, const bool disabled) { KUI_WIDGET(Disabled(W(child), disabled)); }
KuiWidget* kui_radio_button(const bool selected, const KuiAction onSelected, const char* label)
{
    KUI_WIDGET(RadioButton(selected, F(onSelected), label ? label : ""));
}
KuiWidget* kui_selectable(const char* label, const bool selected, const KuiAction onTap) { KUI_WIDGET(Selectable(label ? label : "", selected, F(onTap))); }
KuiWidget* kui_collapsing_header(const char* title, const bool open, const KuiBoolAction onToggled, KuiWidget* child)
{
    KUI_WIDGET(CollapsingHeader(title ? title : "", open, F(onToggled), W(child)));
}
KuiWidget* kui_tree_node(const char* label, const bool open, const KuiBoolAction onToggled, KuiWidget* const* children, const size_t count,
                         const bool leaf, const bool selected, const KuiAction onTap)
{
    KUI_WIDGET(TreeNode(label ? label : "", open, F(onToggled), Children(children, count), TreeNodeOptions { leaf, selected, 18.f, F(onTap) }));
}
KuiWidget* kui_tab_bar(const char* const* tabs, const size_t count, const int32_t selected, const KuiFloatAction onSelected)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<std::string> list;
        for (std::size_t i = 0; i < count; ++i) list.emplace_back(tabs && tabs[i] ? tabs[i] : "");
        std::function<void(float)> picked = F(onSelected);
        return Give(TabBar(std::move(list), selected, [picked](const int index) { if (picked) picked(static_cast<float>(index)); }));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_tooltip(const char* text, KuiWidget* child) { KUI_WIDGET(Tooltip(text ? text : "", W(child))); }
KuiWidget* kui_size_observer(const KuiPanAction onChanged, KuiWidget* child) { KUI_WIDGET(SizeObserver(F(onChanged), W(child))); }
KuiWidget* kui_modal(const bool open, KuiWidget* child, KuiWidget* dialog, const KuiAction onDismiss)
{
    KUI_WIDGET(Modal(open, W(child), W(dialog), F(onDismiss)));
}
KuiWidget* kui_color_picker(const KuiColor color, const KuiColorAction onChanged, const bool alpha, const bool hex, const float width)
{
    KUI_WIDGET(ColorPicker(C(color), F(onChanged), ColorPickerOptions { alpha, hex, width > 0.f ? width : 220.f }));
}
KuiWidget* kui_color_edit(const KuiColor color, const KuiColorAction onChanged, const char* label, const bool alpha)
{
    KUI_WIDGET(ColorEdit(C(color), F(onChanged), label ? label : "", ColorPickerOptions { alpha, true, 220.f }));
}
KuiWidget* kui_plot(const float* values, const size_t count, const uint32_t kind, const float min, const float max, const KuiVec2 size,
                    const char* overlay, const KuiColor color)
{
    return Guarded([&]() -> KuiWidget* {
        PlotOptions options;
        options.kind = kind == 1 ? PlotKind::eHistogram : PlotKind::eLines;
        options.min = min;
        options.max = max;
        options.size = V(size);
        options.overlay = overlay ? overlay : "";
        options.color = C(color);
        return Give(Plot(values ? std::vector<float>(values, values + count) : std::vector<float> {}, std::move(options)));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_step_slider(const int32_t value, const int32_t steps, const KuiFloatAction onChanged, const char* const* labels, const size_t labelCount,
                           const float width)
{
    return Guarded([&]() -> KuiWidget* {
        StepSliderOptions options;
        for (std::size_t i = 0; labels && i < labelCount; ++i) options.labels.emplace_back(labels[i] ? labels[i] : "");
        options.width = width;
        std::function<void(float)> picked = F(onChanged);
        return Give(StepSlider(value, steps, [picked](const int step) { if (picked) picked(static_cast<float>(step)); }, std::move(options)));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_gradient_editor(const float* stops, const size_t count, const KuiStopsAction onChanged, const float width, const bool picker)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<GradientStop> list;
        for (std::size_t i = 0; stops && i < count; ++i) {
            const float* s = stops + i * 5;
            list.push_back({ s[0], Color { s[1], s[2], s[3], s[4] } });
        }
        std::function<void(std::vector<GradientStop>)> changed;
        if (auto o = Hold(onChanged)) {
            changed = [o, f = onChanged.invoke](const std::vector<GradientStop>& now) {
                std::vector<float> flat;
                flat.reserve(now.size() * 5);
                for (const auto& stop : now) flat.insert(flat.end(), { stop.offset, stop.color.r, stop.color.g, stop.color.b, stop.color.a });
                f(flat.data(), now.size(), o->user);
            };
        }
        return Give(GradientEditor(std::move(list), std::move(changed), GradientEditorOptions { width > 0.f ? width : 260.f, picker }));
    }, static_cast<KuiWidget*>(nullptr));
}
KuiWidget* kui_title_bar(const char* title, KuiWidget* leading, KuiWidget* trailing, const float height, const bool buttons)
{
    KUI_WIDGET(TitleBar(title ? title : "", TitleBarOptions { W(leading), W(trailing), height > 0.f ? height : 36.f, buttons }));
}
KuiWidget* kui_status_bar(const char* message, const uint32_t level, KuiWidget* trailing, const float height)
{
    KUI_WIDGET(StatusBar(message ? message : "", static_cast<StatusLevel>(std::min(level, 2u)),
                         StatusBarOptions { W(trailing), height > 0.f ? height : 26.f }));
}
KuiWidget* kui_table(const KuiTableColumn* columns, const size_t columnCount, KuiWidget* const* cells, const size_t rowCount,
                     const bool header, const bool striped, const bool borders, const float rowHeight)
{
    return Guarded([&]() -> KuiWidget* {
        std::vector<TableColumn> list;
        for (std::size_t c = 0; columns && c < columnCount; ++c) list.push_back({ columns[c].title ? columns[c].title : "", columns[c].width, columns[c].flex });
        std::vector<std::vector<Widget>> rows(rowCount);
        for (std::size_t r = 0; cells && r < rowCount; ++r)
            for (std::size_t c = 0; c < columnCount; ++c) rows[r].push_back(W(cells[r * columnCount + c]));
        return Give(Table(std::move(list), std::move(rows), TableOptions { header, striped, borders, rowHeight }));
    }, static_cast<KuiWidget*>(nullptr));
}

KuiWidget* kui_text_field(const KuiTextFieldOptions* o)
{
    return Guarded([&]() -> KuiWidget* {
        const auto& t = Need(o, "text field options");
        TextFieldOptions options;
        options.text = t.text ? t.text : "";
        options.placeholder = t.placeholder ? t.placeholder : "";
        options.onChanged = F(t.on_changed);
        options.onSubmitted = F(t.on_submitted);
        options.controlled = t.controlled;
        options.multiline = t.multiline;
        options.minLines = t.min_lines;
        options.maxLines = t.max_lines;
        options.focus = t.focus;
        options.width = t.width;
        return Give(TextField(std::move(options)));
    }, nullptr);
}

#undef KUI_WIDGET

// ---- icons ----

KuiVectorImage* kui_material_icon(const char* name, const uint32_t style)
{
    return Guarded([&]() -> KuiVectorImage* {
        auto image = MaterialIcon(&Need(name, "name"), static_cast<IconStyle>(style));
        return image ? new KuiVectorImage { std::move(image) } : nullptr;
    }, static_cast<KuiVectorImage*>(nullptr));
}
size_t kui_material_icon_count(void) { return Guarded([] { return MaterialIconNames().size(); }, std::size_t { 0 }); }
const char* kui_material_icon_name(const size_t index)
{
    // The names are views of the compiled-in text, each followed by the SVG's: copied once to end in a nul.
    return Guarded([&]() -> const char* {
        static const std::vector<std::string> names(MaterialIconNames().begin(), MaterialIconNames().end());
        return index < names.size() ? names[index].c_str() : nullptr;
    }, static_cast<const char*>(nullptr));
}
KuiVectorImage* kui_vector_image_from_svg(const char* svg, const size_t length)
{
    return Guarded([&]() -> KuiVectorImage* {
        auto image = VectorImage::FromSvg({ &Need(svg, "svg"), length });
        return image.Empty() ? nullptr : new KuiVectorImage { std::make_shared<const VectorImage>(std::move(image)) };
    }, static_cast<KuiVectorImage*>(nullptr));
}
void kui_vector_image_release(KuiVectorImage* image) { delete image; }
KuiRect kui_vector_image_view_box(KuiVectorImage* image)
{
    return Guarded([&] { const Rect r = Need(image, "image").image->ViewBox(); return KuiRect { r.left, r.top, r.right, r.bottom }; }, KuiRect {});
}
void kui_canvas_draw_vector_image(KuiCanvas* c, KuiVectorImage* image, const KuiRect rect, const KuiColor tint)
{
    GuardedVoid([&] { Need(image, "image").image->Draw(CanvasOf(c), R(rect), C(tint)); });
}
KuiWidget* kui_icon(KuiVectorImage* image, const KuiColor tint)
{
    return Guarded([&]() -> KuiWidget* { return Give(Icon(image ? image->image : nullptr, C(tint))); }, static_cast<KuiWidget*>(nullptr));
}

// ==== the theme and the view ============================================================================

void kui_theme_dark(KuiTheme* theme) { GuardedVoid([&] { ThemeOf(Theme::Dark(), theme); }); }
void kui_theme_light(KuiTheme* theme) { GuardedVoid([&] { ThemeOf(Theme::Light(), theme); }); }
void kui_theme_current(KuiTheme* theme) { GuardedVoid([&] { ThemeOf(Theme::Current(), theme); }); }

KuiUi* kui_ui_new(KuiWidget* root, const KuiTheme* theme, const float scale)
{
    return Guarded([&] {
        UiSettings settings;
        if (theme) settings.theme = ThemeOf(*theme);
        settings.scale = scale > 0.f ? scale : 1.f;
        return reinterpret_cast<KuiUi*>(new Ui(W(root), std::move(settings)));
    }, static_cast<KuiUi*>(nullptr));
}
void kui_ui_destroy(KuiUi* view) { delete reinterpret_cast<Ui*>(view); }
void kui_ui_set_root(KuiUi* v, KuiWidget* root) { GuardedVoid([&] { UiOf(v).SetRoot(W(root)); }); }
void kui_ui_set_theme(KuiUi* v, const KuiTheme* t) { GuardedVoid([&] { UiOf(v).SetTheme(ThemeOf(Need(t, "theme"))); }); }
void kui_ui_clear_focus(KuiUi* v) { GuardedVoid([&] { UiOf(v).ClearFocus(); }); }
void kui_ui_get_theme(KuiUi* v, KuiTheme* t) { GuardedVoid([&] { ThemeOf(UiOf(v).GetTheme(), t); }); }
void kui_ui_set_scale(KuiUi* v, const float s) { GuardedVoid([&] { UiOf(v).SetScale(s); }); }
float kui_ui_pixel_scale(KuiUi* v) { return Guarded([&] { return UiOf(v).PixelScale(); }, 1.f); }
void kui_ui_update(KuiUi* v) { GuardedVoid([&] { UiOf(v).Update(); }); }
void kui_ui_update_with(KuiUi* v, KoralInput* input, const float w, const float h, const float dt)
{
    GuardedVoid([&] { UiOf(v).Update(Need(reinterpret_cast<kor::Input*>(input), "input"), { w, h }, dt); });
}
void kui_ui_reassemble(KuiUi* v) { GuardedVoid([&] { UiOf(v).Reassemble(); }); }
void kui_ui_reassemble_all(void) { GuardedVoid([] { Ui::ReassembleAll(); }); }
void kui_debug_set_paint_bounds(const bool enabled) { debug::SetPaintBounds(enabled); }
bool kui_debug_paint_bounds(void) { return debug::PaintBounds(); }
bool kui_ui_wants_pointer(KuiUi* v) { return Guarded([&] { return UiOf(v).WantsPointer(); }, false); }
bool kui_ui_wants_keyboard(KuiUi* v) { return Guarded([&] { return UiOf(v).WantsKeyboard(); }, false); }
void kui_ui_stats(KuiUi* v, KuiUiStats* out)
{
    GuardedVoid([&] {
        const auto& s = UiOf(v).Stats();
        Need(out, "statistics struct") = { s.builds, s.layouts, s.paints, s.inputMs, s.buildMs, s.layoutMs, s.paintMs };
    });
}
KuiRenderer* kui_ui_renderer(KuiUi* v) { return Guarded([&] { return reinterpret_cast<KuiRenderer*>(&UiOf(v).GetRenderer()); }, static_cast<KuiRenderer*>(nullptr)); }
KoralRenderPass* kui_graph_add_ui_pass(KoralFrameGraph* graph, KuiUi* ui, const char* target)
{
    return Guarded([&] { return PassHandle(GraphOf(graph).Add<UiPass>(UiOf(ui), TargetOf(target))); }, static_cast<KoralRenderPass*>(nullptr));
}

} // extern "C"
