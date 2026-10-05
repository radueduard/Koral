/*
 * koral-ui's C interface: the C++ API (kui::), object for object, for bindings to other languages.
 *
 * It follows koral_c.h's conventions, which are not repeated here: every function is a member of a C++
 * class, named `kui_<class>_<member>` in snake case, and does what that one does — the C++ headers are
 * its documentation. Failures leave their reason in koral_last_error(); nothing throws across the
 * boundary. Enumerations are passed as the C++ enumerators' values.
 *
 * Handles:
 *  - Pictures, layers, gradients, fonts, element shaders and widgets are shared C++ objects
 *    (std::shared_ptr): every handle is released with its kui_<class>_release, which frees the object
 *    once nothing else holds it. Passing one to a function never gives it away: the callee keeps its own.
 *  - Canvases, paths, paragraphs, renderers and views are made with _new and freed with _destroy.
 *  - A KuiState is the stateful widget a tree keeps: borrowed, valid until its dispose callback.
 *
 * Callbacks are {invoke, user, destroy}: `destroy` (may be null) is called with `user` once the C++ side
 * lets the callback go, which is when a binding frees what `user` holds.
 */

#ifndef KORAL_UI_C_H
#define KORAL_UI_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <koral_c.h>

#include "kui/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==== values ========================================================================================== */

typedef struct KuiVec2 { float x, y; } KuiVec2;
typedef struct KuiColor { float r, g, b, a; } KuiColor;                       /* straight alpha, sRGB */
typedef struct KuiRect { float left, top, right, bottom; } KuiRect;
typedef struct KuiRadii { float top_left, top_right, bottom_right, bottom_left; } KuiRadii;
typedef struct KuiTransform { float a, b, c, d, tx, ty; } KuiTransform;
typedef struct KuiEdgeInsets { float left, top, right, bottom; } KuiEdgeInsets;
typedef struct KuiAlignment { float x, y; } KuiAlignment;
typedef struct KuiBoxConstraints { float min_width, max_width, min_height, max_height; } KuiBoxConstraints;   /* INFINITY for unbounded */

typedef struct KuiGradient KuiGradient;
typedef struct KuiFont KuiFont;

typedef struct KuiStroke {
    float width;
    KuiColor color;
    uint32_t cap;                       /* kui::StrokeCap */
    uint32_t join;                      /* kui::StrokeJoin */
    float miter_limit;
} KuiStroke;

typedef struct KuiPaint {
    KuiColor fill;
    KuiGradient* gradient;              /* fills in place of `fill` when not null */
    KuiStroke stroke;
    float opacity;
} KuiPaint;

typedef struct KuiTextStyle {
    KuiFont* font;                      /* null: Font::Default() */
    float size;
    KuiColor color;
    float line_height;
    float letter_spacing;
    float weight;                       /* 400: the font as it is drawn; 700: bold. 0 is taken as 400 */
    bool italic, underline, line_through;
} KuiTextStyle;

/* ==== callbacks ======================================================================================== */

typedef struct KuiAction { void (*invoke)(void* user); void* user; void (*destroy)(void* user); } KuiAction;
typedef struct KuiBoolAction { void (*invoke)(bool value, void* user); void* user; void (*destroy)(void* user); } KuiBoolAction;
typedef struct KuiFloatAction { void (*invoke)(float value, void* user); void* user; void (*destroy)(void* user); } KuiFloatAction;
typedef struct KuiPointAction { void (*invoke)(float x, float y, void* user); void* user; void (*destroy)(void* user); } KuiPointAction;
typedef struct KuiPanAction { void (*invoke)(float dx, float dy, float x, float y, void* user); void* user; void (*destroy)(void* user); } KuiPanAction;
typedef struct KuiScrollAction { bool (*invoke)(float dx, float dy, void* user); void* user; void (*destroy)(void* user); } KuiScrollAction;
typedef struct KuiTextAction { void (*invoke)(const char* text, void* user); void* user; void (*destroy)(void* user); } KuiTextAction;
typedef struct KuiTicker { bool (*invoke)(float dt, void* user); void* user; void (*destroy)(void* user); } KuiTicker;

/* ==== gradients and fonts ============================================================================ */

/** kui::Gradient::Linear / Radial / Sweep, with up to eight stops. */
KUI_API KuiGradient* kui_gradient_linear(KuiVec2 from, KuiVec2 to, const float* offsets, const KuiColor* colors, size_t count);
KUI_API KuiGradient* kui_gradient_radial(KuiVec2 center, float radius, const float* offsets, const KuiColor* colors, size_t count);
KUI_API KuiGradient* kui_gradient_sweep(KuiVec2 center, float angle, const float* offsets, const KuiColor* colors, size_t count);
KUI_API void kui_gradient_release(KuiGradient* gradient);

/** Font::Load (null when it cannot be read: see koral_last_error) / FromMemory / Default. */
KUI_API KuiFont* kui_font_load(const char* path);
KUI_API KuiFont* kui_font_from_memory(const void* bytes, size_t size, const char* name);
KUI_API KuiFont* kui_font_default(void);
KUI_API void kui_font_release(KuiFont* font);
KUI_API void kui_font_set_fallback(KuiFont* font, KuiFont* fallback);
KUI_API float kui_font_ascent(KuiFont* font, float size);
KUI_API float kui_font_descent(KuiFont* font, float size);
KUI_API bool kui_font_has_glyph(KuiFont* font, uint32_t codepoint);

/* ==== paths ============================================================================================ */

typedef struct KuiPath KuiPath;
KUI_API KuiPath* kui_path_new(void);
KUI_API void kui_path_destroy(KuiPath* path);
KUI_API void kui_path_move_to(KuiPath* path, KuiVec2 p);
KUI_API void kui_path_line_to(KuiPath* path, KuiVec2 p);
KUI_API void kui_path_quad_to(KuiPath* path, KuiVec2 control, KuiVec2 p);
KUI_API void kui_path_cubic_to(KuiPath* path, KuiVec2 control1, KuiVec2 control2, KuiVec2 p);
KUI_API void kui_path_arc_to(KuiPath* path, KuiVec2 center, float radius, float start, float sweep);
KUI_API void kui_path_arc_to_corner(KuiPath* path, KuiVec2 corner, KuiVec2 to, float radius);
KUI_API void kui_path_close(KuiPath* path);
KUI_API void kui_path_add_rect(KuiPath* path, KuiRect rect);
KUI_API void kui_path_add_rrect(KuiPath* path, KuiRect rect, KuiRadii radii);
KUI_API void kui_path_add_circle(KuiPath* path, KuiVec2 center, float radius);
KUI_API void kui_path_add_oval(KuiPath* path, KuiRect rect);
KUI_API void kui_path_add_polygon(KuiPath* path, const KuiVec2* points, size_t count, bool close);
KUI_API void kui_path_set_fill_rule(KuiPath* path, uint32_t rule);
KUI_API KuiRect kui_path_bounds(KuiPath* path);

/* ==== pictures, layers and element shaders ============================================================== */

typedef struct KuiPicture KuiPicture;
typedef struct KuiLayer KuiLayer;
typedef struct KuiElementShader KuiElementShader;

KUI_API void kui_picture_release(KuiPicture* picture);
KUI_API KuiRect kui_picture_bounds(KuiPicture* picture);
KUI_API size_t kui_picture_instance_count(KuiPicture* picture);

KUI_API KuiLayer* kui_layer_create(void);
KUI_API void kui_layer_release(KuiLayer* layer);
KUI_API void kui_layer_set_picture(KuiLayer* layer, KuiPicture* picture);   /* null: nothing */
KUI_API void kui_layer_set_transform(KuiLayer* layer, KuiTransform transform);
KUI_API KuiTransform kui_layer_get_transform(KuiLayer* layer);
KUI_API void kui_layer_set_opacity(KuiLayer* layer, float opacity);
KUI_API float kui_layer_opacity(KuiLayer* layer);

/** ElementShader::Load: never null; check kui_element_shader_valid. `entry` may be null (GLSL). */
KUI_API KuiElementShader* kui_element_shader_load(const char* path, const char* entry);
KUI_API void kui_element_shader_release(KuiElementShader* shader);
KUI_API bool kui_element_shader_valid(KuiElementShader* shader);

/* ==== text ============================================================================================== */

typedef struct KuiParagraph KuiParagraph;
KUI_API KuiParagraph* kui_paragraph_new(const char* text, const KuiTextStyle* style, float max_width, uint32_t align);
KUI_API void kui_paragraph_destroy(KuiParagraph* paragraph);
KUI_API void kui_paragraph_layout(KuiParagraph* paragraph, float max_width);
KUI_API KuiVec2 kui_paragraph_size(KuiParagraph* paragraph);
KUI_API size_t kui_paragraph_line_count(KuiParagraph* paragraph);
KUI_API float kui_paragraph_line_height(KuiParagraph* paragraph);
KUI_API float kui_paragraph_min_intrinsic_width(KuiParagraph* paragraph);
KUI_API float kui_paragraph_max_intrinsic_width(KuiParagraph* paragraph);
KUI_API KuiVec2 kui_paragraph_caret_position(KuiParagraph* paragraph, size_t index);
KUI_API size_t kui_paragraph_index_at(KuiParagraph* paragraph, KuiVec2 point);

/* ==== the canvas ======================================================================================== */

typedef struct KuiCanvas KuiCanvas;
KUI_API KuiCanvas* kui_canvas_new(void);
KUI_API void kui_canvas_destroy(KuiCanvas* canvas);
/** Canvas::Finish: the recording as a picture (released with kui_picture_release); the canvas is empty again. */
KUI_API KuiPicture* kui_canvas_finish(KuiCanvas* canvas);

KUI_API void kui_canvas_save(KuiCanvas* canvas);
KUI_API void kui_canvas_restore(KuiCanvas* canvas);
KUI_API size_t kui_canvas_save_count(KuiCanvas* canvas);
KUI_API void kui_canvas_translate(KuiCanvas* canvas, KuiVec2 by);
KUI_API void kui_canvas_scale(KuiCanvas* canvas, KuiVec2 by);
KUI_API void kui_canvas_rotate(KuiCanvas* canvas, float radians);
KUI_API void kui_canvas_concat(KuiCanvas* canvas, KuiTransform transform);
KUI_API void kui_canvas_set_transform(KuiCanvas* canvas, KuiTransform transform);
KUI_API KuiTransform kui_canvas_current_transform(KuiCanvas* canvas);
KUI_API void kui_canvas_clip_rect(KuiCanvas* canvas, KuiRect rect);
KUI_API void kui_canvas_clip_rrect(KuiCanvas* canvas, KuiRect rect, KuiRadii radii);
KUI_API void kui_canvas_set_tolerance(KuiCanvas* canvas, float tolerance);

KUI_API void kui_canvas_draw_rect(KuiCanvas* canvas, KuiRect rect, const KuiPaint* paint);
KUI_API void kui_canvas_draw_rrect(KuiCanvas* canvas, KuiRect rect, KuiRadii radii, const KuiPaint* paint);
KUI_API void kui_canvas_draw_circle(KuiCanvas* canvas, KuiVec2 center, float radius, const KuiPaint* paint);
KUI_API void kui_canvas_draw_oval(KuiCanvas* canvas, KuiRect rect, const KuiPaint* paint);
KUI_API void kui_canvas_draw_arc(KuiCanvas* canvas, KuiVec2 center, float radius, float start, float sweep, bool use_center, const KuiPaint* paint);
KUI_API void kui_canvas_draw_line(KuiCanvas* canvas, KuiVec2 from, KuiVec2 to, const KuiPaint* paint);
KUI_API void kui_canvas_draw_triangle(KuiCanvas* canvas, KuiVec2 a, KuiVec2 b, KuiVec2 c, const KuiPaint* paint);
KUI_API void kui_canvas_draw_quadratic_bezier(KuiCanvas* canvas, KuiVec2 from, KuiVec2 control, KuiVec2 to, const KuiPaint* paint);
KUI_API void kui_canvas_draw_cubic_bezier(KuiCanvas* canvas, KuiVec2 from, KuiVec2 control1, KuiVec2 control2, KuiVec2 to, const KuiPaint* paint);
KUI_API void kui_canvas_draw_polyline(KuiCanvas* canvas, const KuiVec2* points, size_t count, const KuiPaint* paint);
KUI_API void kui_canvas_draw_polygon(KuiCanvas* canvas, const KuiVec2* points, size_t count, const KuiPaint* paint);
KUI_API void kui_canvas_draw_path(KuiCanvas* canvas, KuiPath* path, const KuiPaint* paint);
KUI_API void kui_canvas_draw_shadow(KuiCanvas* canvas, KuiRect rect, KuiRadii radii, KuiColor color, float blur, KuiVec2 offset, float spread);
/** `source` with zero size: the whole image. */
KUI_API void kui_canvas_draw_image(KuiCanvas* canvas, KoralImage* image, KuiRect destination, KuiRect source, KuiColor tint);
KUI_API void kui_canvas_draw_paragraph(KuiCanvas* canvas, KuiParagraph* paragraph, KuiVec2 position);
KUI_API void kui_canvas_draw_text(KuiCanvas* canvas, const char* text, KuiVec2 position, const KuiTextStyle* style);
KUI_API void kui_canvas_draw_element(KuiCanvas* canvas, KuiElementShader* shader, KuiRect rect, const void* parameters, size_t size,
                                     KuiRadii radii, float opacity);
KUI_API void kui_canvas_draw_layer(KuiCanvas* canvas, KuiLayer* layer);
KUI_API void kui_canvas_draw_picture(KuiCanvas* canvas, KuiPicture* picture);

/* the pen */
KUI_API void kui_canvas_begin_path(KuiCanvas* canvas);
KUI_API void kui_canvas_move_to(KuiCanvas* canvas, KuiVec2 point);
KUI_API void kui_canvas_draw_line_to(KuiCanvas* canvas, KuiVec2 point);
KUI_API void kui_canvas_draw_quad_to(KuiCanvas* canvas, KuiVec2 control, KuiVec2 point);
KUI_API void kui_canvas_draw_cubic_to(KuiCanvas* canvas, KuiVec2 control1, KuiVec2 control2, KuiVec2 point);
KUI_API void kui_canvas_draw_arc_to(KuiCanvas* canvas, KuiVec2 center, float radius, float start, float sweep);
KUI_API void kui_canvas_draw_arc_to_corner(KuiCanvas* canvas, KuiVec2 corner, KuiVec2 to, float radius);
KUI_API void kui_canvas_close_path(KuiCanvas* canvas);
KUI_API void kui_canvas_fill(KuiCanvas* canvas, const KuiPaint* paint);
KUI_API void kui_canvas_stroke(KuiCanvas* canvas, const KuiPaint* paint);

/* ==== drawing a layer tree =============================================================================== */

typedef struct KuiRenderer KuiRenderer;
typedef struct KuiRendererStats {
    size_t instances, vertices, layers, clips, draws, uploaded_bytes;
    double compose_ms;
    bool recomposed;
} KuiRendererStats;

KUI_API KuiRenderer* kui_renderer_new(void);
KUI_API void kui_renderer_destroy(KuiRenderer* renderer);
KUI_API void kui_renderer_set_root(KuiRenderer* renderer, KuiLayer* root);
KUI_API void kui_renderer_set_scale(KuiRenderer* renderer, float scale);
KUI_API void kui_renderer_stats(KuiRenderer* renderer, KuiRendererStats* stats);
/** Adds a kui::UiPass drawing @p renderer over @p target (null: the screen). The renderer must outlive the graph's use of it. */
KUI_API KoralRenderPass* kui_graph_add_ui_pass_with_renderer(KoralFrameGraph* graph, KuiRenderer* renderer, const char* target);

/* ==== widgets =========================================================================================== */

typedef struct KuiWidget KuiWidget;
typedef struct KuiState KuiState;

KUI_API void kui_widget_release(KuiWidget* widget);
/** Another handle onto the same widget, released on its own: what a callback returning a widget it keeps hands back. */
KUI_API KuiWidget* kui_widget_retain(KuiWidget* widget);
/** Widget::Key: before the widget is given to a parent. */
KUI_API void kui_widget_set_key(KuiWidget* widget, const char* key);

/** A widget built from others, in another language. `type` tells kinds apart as a C++ class would. */
typedef struct KuiStatelessCallbacks {
    KuiWidget* (*build)(void* user);           /* a new handle, which the caller releases */
    void* user;
    void (*destroy)(void* user);
    const void* type;
} KuiStatelessCallbacks;
KUI_API KuiWidget* kui_stateless_widget(const KuiStatelessCallbacks* widget);

/**
 * A widget with state, in another language: the instance first placed in a tree is kept there (its
 * `user` with it), and each newer one is offered to did_update_widget as `newer`.
 */
typedef struct KuiStatefulCallbacks {
    KuiWidget* (*build)(KuiState* state, void* user);
    void (*init_state)(KuiState* state, void* user);
    void (*dispose)(KuiState* state, void* user);
    void (*did_update_widget)(KuiState* state, void* user, void* newer);
    void* user;
    void (*destroy)(void* user);
    const void* type;
} KuiStatefulCallbacks;
KUI_API KuiWidget* kui_stateful_widget(const KuiStatefulCallbacks* widget);
KUI_API void kui_state_set_state(KuiState* state);
KUI_API void kui_state_animate(KuiState* state, KuiTicker tick);
KUI_API bool kui_state_mounted(KuiState* state);

typedef struct KuiFlexOptions { uint32_t main_axis_alignment, cross_axis_alignment, main_axis_size; float gap; } KuiFlexOptions;
typedef struct KuiDecoration {
    KuiColor color;
    KuiGradient* gradient;
    float border_width;
    KuiColor border_color;
    KuiRadii radius;
    KuiColor shadow_color;
    float shadow_blur;
    KuiVec2 shadow_offset;
} KuiDecoration;
typedef struct KuiContainerOptions {
    float width, height;                        /* negative: sized by the child */
    KuiEdgeInsets padding, margin;
    KuiDecoration decoration;
    bool has_alignment;
    KuiAlignment alignment;
} KuiContainerOptions;
typedef struct KuiPositionedOptions { float left, top, right, bottom, width, height; } KuiPositionedOptions;   /* NAN: not given */
typedef struct KuiGestureOptions {
    KuiAction on_tap;
    KuiPointAction on_tap_down;
    KuiAction on_tap_up;
    KuiPointAction on_pan_start;
    KuiPanAction on_pan_update;
    KuiAction on_pan_end;
    KuiAction on_enter;
    KuiAction on_exit;
    KuiPointAction on_hover;
    KuiScrollAction on_scroll;
    bool opaque;
} KuiGestureOptions;
typedef struct KuiButtonOptions {
    uint32_t style;                 /* kui::ButtonStyle */
    float width;                    /* NAN: as wide as its child */
    bool has_padding;               /* false: the style's own */
    KuiEdgeInsets padding;
    bool enabled;
} KuiButtonOptions;
typedef struct KuiTextFieldOptions {
    const char* text;
    const char* placeholder;
    KuiTextAction on_changed;
    KuiTextAction on_submitted;
    float width;
    bool controlled;                /* true: it always shows text (TextFieldOptions::controlled) */
    bool multiline;                 /* several lines, from min_lines to max_lines tall */
    int32_t min_lines, max_lines;
    uint32_t focus;                 /* takes the keyboard when this changes (and is not 0) */
} KuiTextFieldOptions;
typedef struct KuiItemBuilder { KuiWidget* (*build)(size_t index, void* user); void* user; void (*destroy)(void* user); } KuiItemBuilder;
typedef struct KuiRangeAction { void (*invoke)(size_t first, size_t last, void* user); void* user; void (*destroy)(void* user); } KuiRangeAction;
typedef struct KuiPainter { void (*paint)(KuiCanvas* canvas, float width, float height, void* user); void* user; void (*destroy)(void* user); } KuiPainter;

/* The building blocks: each returns a new handle. Children are not given away (see above); null children are left out. */
KUI_API KuiWidget* kui_text(const char* text, const KuiTextStyle* style, uint32_t align, bool wrap);
KUI_API KuiWidget* kui_row(KuiWidget* const* children, size_t count, const KuiFlexOptions* options);
KUI_API KuiWidget* kui_column(KuiWidget* const* children, size_t count, const KuiFlexOptions* options);
KUI_API KuiWidget* kui_flex(uint32_t axis, KuiWidget* const* children, size_t count, const KuiFlexOptions* options);
KUI_API KuiWidget* kui_expanded(KuiWidget* child, float flex);
KUI_API KuiWidget* kui_flexible(KuiWidget* child, float flex);
KUI_API KuiWidget* kui_padding(KuiEdgeInsets padding, KuiWidget* child);
KUI_API KuiWidget* kui_align(KuiAlignment alignment, KuiWidget* child);
KUI_API KuiWidget* kui_center(KuiWidget* child);
KUI_API KuiWidget* kui_sized_box(float width, float height, KuiWidget* child);
KUI_API KuiWidget* kui_constrained_box(KuiBoxConstraints constraints, KuiWidget* child);
KUI_API KuiWidget* kui_container(const KuiContainerOptions* options, KuiWidget* child);
KUI_API KuiWidget* kui_decorated_box(const KuiDecoration* decoration, KuiWidget* child);
KUI_API KuiWidget* kui_stack(KuiWidget* const* children, size_t count, KuiAlignment alignment);
KUI_API KuiWidget* kui_positioned(const KuiPositionedOptions* options, KuiWidget* child);
KUI_API KuiWidget* kui_stack_align(KuiAlignment alignment, KuiWidget* child);
KUI_API KuiWidget* kui_scroll_view(KuiWidget* child, uint32_t axis);
KUI_API KuiWidget* kui_list_view(KuiWidget* const* children, size_t count, uint32_t axis, float gap);
KUI_API KuiWidget* kui_list_view_builder(size_t count, float item_extent, KuiItemBuilder builder);
/** ListView(count, extent, builder, onRange): also told which items [first, last) it keeps, as that changes. */
KUI_API KuiWidget* kui_list_view_builder_with_range(size_t count, float item_extent, KuiItemBuilder builder, KuiRangeAction on_range);
/** Told an item and how far into it: where a lazy list is. */
typedef struct KuiIndexAction { void (*invoke)(size_t index, float offset, void* user); void* user; void (*destroy)(void* user); } KuiIndexAction;
typedef struct KuiLazyListOptions {
    size_t count;
    uint32_t axis;
    float item_extent;              /* every item's; 0 or less: each its own */
    float estimated_extent;         /* an item's, until it is built and measured */
    float gap;
    float padding_start, padding_end;
    KuiItemBuilder builder;
    KuiRangeAction on_range;
    KuiIndexAction on_scrolled;     /* the first item in view, and how far into it the view starts */
    size_t jump_index;              /* put first in view, jump_offset into it, when jump changes (and is not 0) */
    float jump_offset;
    uint32_t jump;
} KuiLazyListOptions;
/** LazyList: only the items in view exist, each as long as it likes; down or across. */
KUI_API KuiWidget* kui_lazy_list(const KuiLazyListOptions* options);
/** Intrinsic: @p child as wide, and as tall, as it is with all the room there is. */
KUI_API KuiWidget* kui_intrinsic(bool width, bool height, KuiWidget* child);
/** ScrollView(child, ScrollOptions): @p on_scrolled hears where it is and how far it can go; it jumps to @p jump_to when @p jump changes. */
KUI_API KuiWidget* kui_scroll_view_observed(KuiWidget* child, uint32_t axis, KuiPointAction on_scrolled, float jump_to, uint32_t jump);
/** TransformBox: @p child drawn and hit through @p transform about @p origin of its own box. */
KUI_API KuiWidget* kui_transform_box(KuiTransform transform, KuiAlignment origin, KuiWidget* child);
KUI_API KuiWidget* kui_aspect_ratio(float ratio, KuiWidget* child);
/** FractionallySizedBox: a share of 0 leaves that way alone. */
KUI_API KuiWidget* kui_fractionally_sized_box(float width_share, float height_share, KuiWidget* child);
/** What a custom layout's rule measures and places its children through: good for the call it is given to. */
typedef struct KuiLayoutContext KuiLayoutContext;
KUI_API size_t kui_layout_count(KuiLayoutContext* context);
KUI_API void kui_layout_measure(KuiLayoutContext* context, size_t index, float min_width, float max_width, float min_height, float max_height,
                                float* out_width, float* out_height);
KUI_API void kui_layout_place(KuiLayoutContext* context, size_t index, float x, float y);
typedef struct KuiLayoutRule {
    /* Measures and places the children through @p context, and says how big the whole is. INFINITY: unbounded. */
    void (*layout)(KuiLayoutContext* context, float min_width, float max_width, float min_height, float max_height,
                   float* out_width, float* out_height, void* user);
    void* user;
    void (*destroy)(void* user);
} KuiLayoutRule;
KUI_API KuiWidget* kui_custom_layout(KuiLayoutRule rule, KuiWidget* const* children, size_t count);
/** PopupAnchor: @p popup shown over everything while @p open, under (or, below false, over) what this is in. */
KUI_API KuiWidget* kui_popup_anchor(bool open, KuiWidget* popup, KuiAction on_dismiss, KuiVec2 offset, bool below);
KUI_API KuiWidget* kui_gesture_detector(const KuiGestureOptions* options, KuiWidget* child);
KUI_API KuiWidget* kui_custom_paint(KuiPainter painter, KuiVec2 size, KuiWidget* child);
KUI_API KuiWidget* kui_shader_box(KuiElementShader* shader, const void* parameters, size_t size, KuiRadii radius, KuiWidget* child);
KUI_API KuiWidget* kui_image(KoralImage* image, uint32_t fit, KuiVec2 size);
KUI_API KuiWidget* kui_repaint_boundary(KuiWidget* child);
KUI_API KuiWidget* kui_opacity(float opacity, KuiWidget* child);
/** The child on glass: what is behind its box shows through, blurred, tinted and bent inwards at the edge. */
KUI_API KuiWidget* kui_backdrop_filter(KuiWidget* child, float blur, KuiColor tint, float refraction, float radius);
/* Animation. A curve is a kui::Curve: 0 linear, 1 ease in, 2 ease out, 3 ease in and out, 4 ease out and back. */
/** Where along its way (0 to 1) something on a curve is, t of the way through its time. */
KUI_API float kui_ease(uint32_t curve, float t);
/** The child, fading to an opacity whenever that is another, over so many seconds. */
KUI_API KuiWidget* kui_animated_opacity(float opacity, KuiWidget* child, float duration, uint32_t curve);
/** The child, fading in when first shown and rising into place by so much. */
KUI_API KuiWidget* kui_appear(KuiWidget* child, float duration, uint32_t curve, float rise);
/** The child (which may be null while it folds away), unfolding downwards while open and folding away when not. */
KUI_API KuiWidget* kui_reveal(bool open, KuiWidget* child, float duration, uint32_t curve);
KUI_API KuiWidget* kui_clip_rrect(KuiRadii radius, KuiWidget* child);
KUI_API KuiWidget* kui_translate(KuiVec2 offset, KuiWidget* child);
KUI_API KuiWidget* kui_ignore_pointer(KuiWidget* child);

/* ---- drag and drop ---- */

/**
 * What a drag carries: its kind, and the thing — as text, as an object of the caller's (released with
 * destroy when the drag's data goes), or both. A drag begun in C++ with something else arrives with neither.
 */
typedef struct KuiDragData { const char* type; const char* text; void* payload; void (*destroy)(void* payload); } KuiDragData;
/** A drag over a target, or dropped on it: what it carries, and where in the target. */
typedef struct KuiDropAction {
    void (*invoke)(const char* type, const char* text, void* payload, float x, float y, void* user);
    void* user;
    void (*destroy)(void* user);
} KuiDropAction;
typedef struct KuiDraggableOptions {
    KuiWidget* feedback;            /* what follows the pointer; null: the child itself, a little seen through */
    KuiAction on_drag_start;
    KuiBoolAction on_drag_end;      /* whether a target took it */
    bool disabled;
    bool feedback_in_place;         /* the feedback is the thing itself: held where it was taken hold of, not by its corner */
    float feedback_radius;          /* how round the outline round the thing in hand is; negative: the theme's radius */
} KuiDraggableOptions;
typedef struct KuiDropTargetOptions {
    const char* accepts_type;       /* null: any drag */
    KuiDropAction on_drop;
    KuiDropAction on_enter;
    KuiAction on_leave;
    KuiDropAction on_move;
} KuiDropTargetOptions;
/** Draggable(data, child, options): options may be null. */
KUI_API KuiWidget* kui_draggable(const KuiDragData* data, KuiWidget* child, const KuiDraggableOptions* options);
KUI_API KuiWidget* kui_drop_target(const KuiDropTargetOptions* options, KuiWidget* child);

/* ---- docking ---- */

/** A kui::DockLayout, shared: released with kui_dock_layout_release; a dock space given it keeps it too. */
typedef struct KuiDockLayout KuiDockLayout;
KUI_API KuiDockLayout* kui_dock_layout_new(void);
KUI_API void kui_dock_layout_release(KuiDockLayout* layout);
/** Dock(panel, side, relativeTo, fraction): side is a kui::DockSide; relative_to may be null (the whole space). */
KUI_API void kui_dock_layout_dock(KuiDockLayout* layout, const char* panel, uint32_t side, const char* relative_to, float fraction);
KUI_API void kui_dock_layout_float(KuiDockLayout* layout, const char* panel, KuiRect rect);
/** Float(panel, at): floating at a place, as big as what it shows. */
KUI_API void kui_dock_layout_float_at(KuiDockLayout* layout, const char* panel, KuiVec2 at);
KUI_API void kui_dock_layout_pop_out(KuiDockLayout* layout, const char* panel, KuiVec2 size);
KUI_API void kui_dock_layout_close(KuiDockLayout* layout, const char* panel);
KUI_API void kui_dock_layout_open(KuiDockLayout* layout, const char* panel);
/** Dock(panel, area, part). area: 0 left, 1 right, 2 bottom-left, 3 bottom-right, 4 center; part: which part of a side, from the top. */
KUI_API void kui_dock_layout_dock_in(KuiDockLayout* layout, const char* panel, uint32_t area, int32_t part);
KUI_API void kui_dock_layout_activate(KuiDockLayout* layout, const char* panel);
/** Hide: folds the panel's area away, when it is the one shown there. */
KUI_API void kui_dock_layout_hide(KuiDockLayout* layout, const char* panel);
/** IsShown: floating, or the one its area shows — and not closed. */
KUI_API bool kui_dock_layout_is_shown(KuiDockLayout* layout, const char* panel);
KUI_API bool kui_dock_layout_is_open(KuiDockLayout* layout, const char* panel);
KUI_API bool kui_dock_layout_is_floating(KuiDockLayout* layout, const char* panel);
/** Save(): valid until the next call of it on this thread. */
KUI_API const char* kui_dock_layout_save(KuiDockLayout* layout);
KUI_API bool kui_dock_layout_load(KuiDockLayout* layout, const char* text);

typedef struct KuiDockPanel {
    const char* id; const char* title; KuiWidget* content;
    bool fixed;                     /* no close button */
    bool undockable;                /* it never docks: it floats on its own, and nothing docks into it */
    bool no_title_bar;              /* floating on its own it is only its content, moved by dragging that */
    const char* icon;               /* the glyph on its button; null or empty: the first letter of its title */
} KuiDockPanel;
/** kui::DockStyle, in its order. A value of 0 (or less) is the default. */
typedef struct KuiDockStyle {
    float title_bar_height, stripe_width, button_size, button_gap, separator_gap, tab_padding, resize_grip, min_float_size, min_area_size, radius;
    float edge_drop_margin, center_drop_size, under_drop_start;
} KuiDockStyle;
typedef struct KuiDockOptions {
    bool single_viewport;           /* true: panels never get windows of their own */
    float gap;                      /* the space between two areas; 0 (or less): the default */
    float stripe_gap;               /* space added round a stripe's buttons, half each side of them; negative: the default */
    KuiDockStyle style;             /* every size it is drawn with; a 0 is the default */
    KuiTextAction on_closed;        /* a panel's close button was clicked: its id */
    KuiAction on_changed;
} KuiDockOptions;
/** DockSpace(layout, panels, options): options may be null. */
KUI_API KuiWidget* kui_dock_space(KuiDockLayout* layout, const KuiDockPanel* panels, size_t count, const KuiDockOptions* options);

/**
 * DragValue: a number changed by dragging across it. @p label may be null; a negative @p width is the default.
 * @p typeable: clicked without being dragged, it turns into a text box for typing the value exactly.
 */
KUI_API KuiWidget* kui_drag_value(float value, KuiFloatAction on_changed, float speed, float min, float max, int32_t decimals,
                                  const char* label, float width, bool typeable);
/** The same, upright: its label over its value, dragged up for more. */
KUI_API KuiWidget* kui_drag_value_vertical(float value, KuiFloatAction on_changed, float speed, float min, float max, int32_t decimals,
                                           const char* label, float width, bool typeable);
/** Dropdown: @p on_changed hears the index picked (a whole number, as a float). @p placeholder may be null. */
KUI_API KuiWidget* kui_dropdown(const char* const* items, size_t count, int32_t selected, KuiFloatAction on_changed, float width,
                                const char* placeholder);
typedef struct KuiMenuItem {
    const char* label;
    KuiAction on_selected;
    bool disabled;
    bool separator;                 /* a line between two groups of items, not an item */
} KuiMenuItem;
/** ContextMenu: @p child, with a menu of @p items where the right button is pressed on it. */
KUI_API KuiWidget* kui_context_menu(const KuiMenuItem* items, size_t count, KuiWidget* child);
typedef struct KuiMenu {
    const char* title;
    const KuiMenuItem* items;
    size_t count;
} KuiMenu;
/** MenuBar: a row of titles, each opening its menu under itself. */
KUI_API KuiWidget* kui_menu_bar(const KuiMenu* menus, size_t count);

/* ---- more controls: Separator, Disabled, RadioButton, Selectable, CollapsingHeader, TreeNode, TabBar, Tooltip, Modal ---- */
KUI_API KuiWidget* kui_separator(bool vertical, float thickness);
KUI_API KuiWidget* kui_disabled(KuiWidget* child, bool disabled);
KUI_API KuiWidget* kui_radio_button(bool selected, KuiAction on_selected, const char* label);
KUI_API KuiWidget* kui_selectable(const char* label, bool selected, KuiAction on_tap);
/** CollapsingHeader: @p on_toggled hears what it should be now. @p child (may be null) shows under it while @p open. */
KUI_API KuiWidget* kui_collapsing_header(const char* title, bool open, KuiBoolAction on_toggled, KuiWidget* child);
KUI_API KuiWidget* kui_tree_node(const char* label, bool open, KuiBoolAction on_toggled, KuiWidget* const* children, size_t count,
                                 bool leaf, bool selected, KuiAction on_tap);
/** TabBar: @p on_selected hears the index picked (a whole number, as a float). */
KUI_API KuiWidget* kui_tab_bar(const char* const* tabs, size_t count, int32_t selected, KuiFloatAction on_selected);
KUI_API KuiWidget* kui_tooltip(const char* text, KuiWidget* child);
/** SizeObserver: @p on_changed hears the child's size in units (the first two) and in pixels (the last two) when it changes. */
KUI_API KuiWidget* kui_size_observer(KuiPanAction on_changed, KuiWidget* child);
KUI_API KuiWidget* kui_modal(bool open, KuiWidget* child, KuiWidget* dialog, KuiAction on_dismiss);

/* ---- colour, plots, tables ---- */
typedef struct KuiColorAction { void (*invoke)(float r, float g, float b, float a, void* user); void* user; void (*destroy)(void* user); } KuiColorAction;
KUI_API KuiWidget* kui_color_picker(KuiColor color, KuiColorAction on_changed, bool alpha, bool hex, float width);
/** ColorEdit: a swatch (and @p label, which may be null) that opens a picker under itself. */
KUI_API KuiWidget* kui_color_edit(KuiColor color, KuiColorAction on_changed, const char* label, bool alpha);
/** Plot: @p kind 0 a line, 1 bars. @p min and @p max may be NaN (the values' own); a negative size is what there is room for;
 *  @p overlay may be null; a transparent @p color is the theme's accent. */
KUI_API KuiWidget* kui_plot(const float* values, size_t count, uint32_t kind, float min, float max, KuiVec2 size, const char* overlay, KuiColor color);
typedef struct KuiTableColumn {
    const char* title;
    float width;                    /* negative: a share of what is left, by flex */
    float flex;
} KuiTableColumn;
/** StepSlider: @p on_changed hears the step picked (a whole number, as a float). @p labels may be null. A negative width fills. */
KUI_API KuiWidget* kui_step_slider(int32_t value, int32_t steps, KuiFloatAction on_changed, const char* const* labels, size_t label_count, float width);
/** A gradient's stops, five floats each: offset, r, g, b, a. */
typedef struct KuiStopsAction { void (*invoke)(const float* stops, size_t count, void* user); void* user; void (*destroy)(void* user); } KuiStopsAction;
/** GradientEditor: @p stops is @p count stops of five floats each (offset, r, g, b, a); @p on_changed hears them the same way. */
KUI_API KuiWidget* kui_gradient_editor(const float* stops, size_t count, KuiStopsAction on_changed, float width, bool picker);
/** TitleBar: the window's title bar, drawn by the interface in place of the system's. @p leading and @p trailing may be null. */
KUI_API KuiWidget* kui_title_bar(const char* title, KuiWidget* leading, KuiWidget* trailing, float height, bool buttons);
/** StatusBar: @p level 0 info, 1 warning, 2 error. @p trailing may be null. */
KUI_API KuiWidget* kui_status_bar(const char* message, uint32_t level, KuiWidget* trailing, float height);
/** Table: @p cells are its rows one after another, @p column_count to a row (a null cell is empty). */
KUI_API KuiWidget* kui_table(const KuiTableColumn* columns, size_t column_count, KuiWidget* const* cells, size_t row_count,
                             bool header, bool striped, bool borders, float row_height);

KUI_API KuiWidget* kui_button(const char* label, KuiAction on_pressed, const KuiButtonOptions* options);
/** Button(child, ...): any widget, made a button. */
KUI_API KuiWidget* kui_button_with_child(KuiWidget* child, KuiAction on_pressed, const KuiButtonOptions* options);
KUI_API KuiWidget* kui_checkbox(bool value, KuiBoolAction on_changed, const char* label);
KUI_API KuiWidget* kui_switch(bool value, KuiBoolAction on_changed);
KUI_API KuiWidget* kui_slider(float value, KuiFloatAction on_changed, float min, float max);
/** The same, with @p on_finished called when it is let go of. */
KUI_API KuiWidget* kui_slider_finished(float value, KuiFloatAction on_changed, float min, float max, KuiAction on_finished);
/** The same, upright: the value grows upwards. */
KUI_API KuiWidget* kui_slider_vertical(float value, KuiFloatAction on_changed, float min, float max, KuiAction on_finished);
/** Text of so many lines at the most (0: any number), the last ending in an ellipsis when asked. */
KUI_API KuiWidget* kui_text_lines(const char* text, const KuiTextStyle* style, uint32_t align, bool wrap, int32_t max_lines, bool ellipsis);
KUI_API KuiWidget* kui_progress_bar(float value);
KUI_API KuiWidget* kui_text_field(const KuiTextFieldOptions* options);

/* ==== the theme and the Ui =============================================================================== */

typedef struct KuiTheme {
    KuiColor background, surface, surface_hover, surface_pressed, primary, primary_hover, primary_pressed,
             on_primary, text, text_muted, border, focus;
    float radius;
    float control_height;
    KuiTextStyle text_style;
    float button_radius, field_radius, checkbox_radius;     /* negative: the theme's radius; a checkbox's negative: a circle */
    uint32_t design;                                        /* kui::ThemeDesign: 0 koral-ui's own, 1 Material, 2 Cupertino, 3 Fluent */
} KuiTheme;
/** Themed: @p child with @p theme in place of the view's. */
KUI_API KuiWidget* kui_themed(const KuiTheme* theme, KuiWidget* child);
/** How the system looks: whether dark, and its accent. Returns whether the system said. */
KUI_API bool kui_system_appearance(bool* dark, KuiColor* accent);
KUI_API void kui_theme_dark(KuiTheme* theme);
KUI_API void kui_theme_light(KuiTheme* theme);
/** Theme::Current: the theme of the Ui being built (the dark one outside a build). */
KUI_API void kui_theme_current(KuiTheme* theme);

typedef struct KuiUi KuiUi;   /* a kui::Ui: a widget tree, live */
typedef struct KuiUiStats {
    size_t builds, layouts, paints;
    double input_ms, build_ms, layout_ms, paint_ms;
} KuiUiStats;

/** @p theme null: Theme::Dark(). */
KUI_API KuiUi* kui_ui_new(KuiWidget* root, const KuiTheme* theme, float scale);
KUI_API void kui_ui_destroy(KuiUi* view);
KUI_API void kui_ui_set_root(KuiUi* view, KuiWidget* root);
KUI_API void kui_ui_set_theme(KuiUi* view, const KuiTheme* theme);
KUI_API void kui_ui_clear_focus(KuiUi* view);
KUI_API void kui_ui_get_theme(KuiUi* view, KuiTheme* theme);
KUI_API void kui_ui_set_scale(KuiUi* view, float scale);
/** Ui::Update(): the current scene's input, window and clock. */
KUI_API void kui_ui_update(KuiUi* view);
KUI_API void kui_ui_update_with(KuiUi* view, KoralInput* input, float width, float height, float dt);
KUI_API void kui_ui_reassemble(KuiUi* view);
KUI_API void kui_ui_reassemble_all(void);
/** debug::SetPaintBounds: every render object outlined where it was laid out, in every interface of the process. */
KUI_API void kui_debug_set_paint_bounds(bool enabled);
KUI_API bool kui_debug_paint_bounds(void);
KUI_API bool kui_ui_wants_pointer(KuiUi* view);
KUI_API bool kui_ui_wants_keyboard(KuiUi* view);
KUI_API void kui_ui_stats(KuiUi* view, KuiUiStats* stats);
KUI_API KuiRenderer* kui_ui_renderer(KuiUi* view);   /* borrowed */
/** Adds a kui::UiPass drawing @p ui over @p target (null: the screen). The Ui must outlive the graph's use of it. */
KUI_API KoralRenderPass* kui_graph_add_ui_pass(KoralFrameGraph* graph, KuiUi* ui, const char* target);

#ifdef __cplusplus
}
#endif

#endif
