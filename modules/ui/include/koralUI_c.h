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
KUI_API KuiWidget* kui_gesture_detector(const KuiGestureOptions* options, KuiWidget* child);
KUI_API KuiWidget* kui_custom_paint(KuiPainter painter, KuiVec2 size, KuiWidget* child);
KUI_API KuiWidget* kui_shader_box(KuiElementShader* shader, const void* parameters, size_t size, KuiRadii radius, KuiWidget* child);
KUI_API KuiWidget* kui_image(KoralImage* image, uint32_t fit, KuiVec2 size);
KUI_API KuiWidget* kui_repaint_boundary(KuiWidget* child);
KUI_API KuiWidget* kui_opacity(float opacity, KuiWidget* child);
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
    KuiWidget* feedback;            /* what follows the pointer; null: a ghost of the child's size */
    KuiAction on_drag_start;
    KuiBoolAction on_drag_end;      /* whether a target took it */
    bool disabled;
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
KUI_API void kui_dock_layout_pop_out(KuiDockLayout* layout, const char* panel, KuiVec2 size);
KUI_API void kui_dock_layout_close(KuiDockLayout* layout, const char* panel);
KUI_API void kui_dock_layout_open(KuiDockLayout* layout, const char* panel);
KUI_API void kui_dock_layout_activate(KuiDockLayout* layout, const char* panel);
KUI_API bool kui_dock_layout_is_open(KuiDockLayout* layout, const char* panel);
KUI_API bool kui_dock_layout_is_floating(KuiDockLayout* layout, const char* panel);
/** Save(): valid until the next call of it on this thread. */
KUI_API const char* kui_dock_layout_save(KuiDockLayout* layout);
KUI_API bool kui_dock_layout_load(KuiDockLayout* layout, const char* text);

typedef struct KuiDockPanel { const char* id; const char* title; KuiWidget* content; bool fixed; /* no close button */ } KuiDockPanel;
typedef struct KuiDockOptions {
    bool single_viewport;           /* true: panels never get windows of their own */
    KuiTextAction on_closed;        /* a panel's close button was clicked: its id */
    KuiAction on_changed;
} KuiDockOptions;
/** DockSpace(layout, panels, options): options may be null. */
KUI_API KuiWidget* kui_dock_space(KuiDockLayout* layout, const KuiDockPanel* panels, size_t count, const KuiDockOptions* options);

KUI_API KuiWidget* kui_button(const char* label, KuiAction on_pressed, const KuiButtonOptions* options);
/** Button(child, ...): any widget, made a button. */
KUI_API KuiWidget* kui_button_with_child(KuiWidget* child, KuiAction on_pressed, const KuiButtonOptions* options);
KUI_API KuiWidget* kui_checkbox(bool value, KuiBoolAction on_changed, const char* label);
KUI_API KuiWidget* kui_switch(bool value, KuiBoolAction on_changed);
KUI_API KuiWidget* kui_slider(float value, KuiFloatAction on_changed, float min, float max);
KUI_API KuiWidget* kui_progress_bar(float value);
KUI_API KuiWidget* kui_text_field(const KuiTextFieldOptions* options);

/* ==== the theme and the Ui =============================================================================== */

typedef struct KuiTheme {
    KuiColor background, surface, surface_hover, surface_pressed, primary, primary_hover, primary_pressed,
             on_primary, text, text_muted, border, focus;
    float radius;
    float control_height;
    KuiTextStyle text_style;
} KuiTheme;
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
KUI_API void kui_ui_get_theme(KuiUi* view, KuiTheme* theme);
KUI_API void kui_ui_set_scale(KuiUi* view, float scale);
/** Ui::Update(): the current scene's input, window and clock. */
KUI_API void kui_ui_update(KuiUi* view);
KUI_API void kui_ui_update_with(KuiUi* view, KoralInput* input, float width, float height, float dt);
KUI_API void kui_ui_reassemble(KuiUi* view);
KUI_API void kui_ui_reassemble_all(void);
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
