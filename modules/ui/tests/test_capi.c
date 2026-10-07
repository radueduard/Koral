/* koral-ui's C interface, from C: a scene whose interface is a widget written in C — with state, a
   painter drawing with the pen, and a button whose click changes the state — read back pixel by pixel.
   Plain C11, which is also the check that koralUI_c.h is C. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <koral_c.h>
#include <koralUI_c.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAILED: %s (%s:%d) %s\n", what, __FILE__, __LINE__, koral_last_error()); ++failures; } } while (0)

enum { BUFFER_TRANSFER_DST = 1 << 1, BUFFER_TYPE_READBACK = 2, IMAGE_TRANSFER_SRC = 1 << 0, KIND_IMAGE = 1,
       MOUSE_LEFT = 0 };
enum { SIZE = 32 };

typedef struct Ui {
    KuiUi* view;
    KoralBuffer* readback;
    KoralImage* screen;
    int clicks;
    int builds;
    int destroyed;
} Ui;

static Ui* g_ui = NULL;

/* ---- the widget ---- */

static KuiPaint fill(float r, float g, float b) {
    KuiPaint paint = {0};
    paint.fill = (KuiColor){r, g, b, 1.f};
    paint.opacity = 1.f;
    paint.stroke.miter_limit = 4.f;
    return paint;
}

/* Paints its box with the pen: green before the first click, blue after. */
static void paint_box(KuiCanvas* canvas, float width, float height, void* user) {
    const int clicked = *(int*)user;
    KuiPaint paint = clicked ? fill(0.f, 0.f, 1.f) : fill(0.f, 1.f, 0.f);
    kui_canvas_begin_path(canvas);
    kui_canvas_move_to(canvas, (KuiVec2){0.f, 0.f});
    kui_canvas_draw_line_to(canvas, (KuiVec2){width, 0.f});
    kui_canvas_draw_line_to(canvas, (KuiVec2){width, height});
    kui_canvas_draw_line_to(canvas, (KuiVec2){0.f, height});
    kui_canvas_close_path(canvas);
    kui_canvas_fill(canvas, &paint);
}
static void free_int(void* user) { free(user); }

static void clicked(void* user) {
    KuiState* state = (KuiState*)user;
    ++g_ui->clicks;
    kui_state_set_state(state);
}

static KuiWidget* build(KuiState* state, void* user) {
    (void)user;
    ++g_ui->builds;
    int* clicked_now = (int*)malloc(sizeof(int));
    *clicked_now = g_ui->clicks;
    KuiPainter painter = {paint_box, clicked_now, free_int};
    KuiWidget* box = kui_custom_paint(painter, (KuiVec2){-1.f, -1.f}, NULL);
    KuiGestureOptions gestures = {0};
    gestures.on_tap = (KuiAction){clicked, state, NULL};
    gestures.opaque = true;
    KuiWidget* tappable = kui_gesture_detector(&gestures, box);
    kui_widget_release(box);
    return tappable;   /* the caller takes it */
}
static void destroy_widget(void* user) { (void)user; ++g_ui->destroyed; }

static int g_type;   /* its address tells this kind of widget apart */

static KuiWidget* make_root(void) {
    KuiStatefulCallbacks callbacks = {0};
    callbacks.build = build;
    callbacks.destroy = destroy_widget;
    callbacks.type = &g_type;
    return kui_stateful_widget(&callbacks);
}

/* ---- the scene ---- */

static void read_setup(KoralRenderPass* pass, KoralPassBuilder* builder, void* user) {
    (void)pass; (void)user;
    koral_pass_builder_read(builder, KORAL_SCREEN, KIND_IMAGE, IMAGE_TRANSFER_SRC);
    koral_pass_builder_side_effect(builder);
}
static void read_initialize(KoralRenderPass* pass, KoralPassResources* resources, void* user) {
    Ui* ui = (Ui*)user;
    (void)pass;
    koral_resource_release(ui->screen);
    ui->screen = koral_pass_resources_image_named(resources, KORAL_SCREEN);
}
static void read_record(KoralRenderPass* pass, KoralCommandBuffer* commands, void* user) {
    Ui* ui = (Ui*)user;
    (void)pass;
    koral_cmd_copy_image_to_buffer(commands, ui->screen, ui->readback, NULL);
}

static void initialize(KoralScene* scene, void* user) {
    Ui* ui = (Ui*)user;
    KoralBufferBuilder* builder = koral_buffer_builder_new();
    koral_buffer_builder_set_instance_count(builder, SIZE * SIZE * 4);
    koral_buffer_builder_set_usage(builder, BUFFER_TRANSFER_DST);
    koral_buffer_builder_set_type(builder, BUFFER_TYPE_READBACK);
    ui->readback = koral_buffer_builder_build(builder);
    koral_builder_destroy(builder);

    KuiWidget* root = make_root();
    ui->view = kui_ui_new(root, NULL, 1.f);
    kui_widget_release(root);
    CHECK(ui->view != NULL, "a view");
    CHECK(kui_graph_add_ui_pass(koral_scene_graph(scene), ui->view, NULL) != NULL, "its pass");

    KoralPassCallbacks read = {0};
    read.user = ui;
    read.setup = read_setup;
    read.initialize = read_initialize;
    read.record = read_record;
    koral_graph_add(koral_scene_graph(scene), "Read", &read);
}
static void update(KoralScene* scene, void* user) { (void)scene; kui_ui_update(((Ui*)user)->view); }
static void destroy(void* user) {
    Ui* ui = (Ui*)user;
    kui_ui_destroy(ui->view);
    koral_resource_release(ui->screen);
    koral_resource_release(ui->readback);
}

static KoralSceneCallbacks make_ui(const char* arguments_json, void* factory_user) {
    (void)arguments_json;
    KoralSceneCallbacks callbacks = {0};
    callbacks.user = factory_user;
    callbacks.initialize = initialize;
    callbacks.update = update;
    callbacks.destroy = destroy;
    return callbacks;
}

static unsigned char channel(int x, int y, int c) {
    unsigned char pixels[SIZE * SIZE * 4];
    koral_buffer_read(g_ui->readback, pixels, sizeof pixels, 0);
    return pixels[(y * SIZE + x) * 4 + c];
}

int main(void) {
    KoralAppSettings settings = koral_app_settings_default();
    settings.platform = KORAL_PLATFORM_NONE;
    if (koral_app_create(&settings) != KORAL_OK) {
        printf("SKIPPED: no Vulkan device: %s\n", koral_last_error());
        return 0;
    }

    /* The canvas on its own: chained into a picture, with the pen. */
    KuiCanvas* canvas = kui_canvas_new();
    KuiPaint red = fill(1.f, 0.f, 0.f);
    kui_canvas_draw_circle(canvas, (KuiVec2){10.f, 10.f}, 5.f, &red);
    kui_canvas_begin_path(canvas);
    kui_canvas_move_to(canvas, (KuiVec2){0.f, 0.f});
    kui_canvas_draw_arc_to_corner(canvas, (KuiVec2){20.f, 0.f}, (KuiVec2){20.f, 20.f}, 5.f);
    kui_canvas_stroke(canvas, &red);
    KuiPicture* picture = kui_canvas_finish(canvas);
    CHECK(picture && kui_picture_instance_count(picture) >= 2, "a picture: the circle and the stroked path");
    kui_picture_release(picture);
    kui_canvas_destroy(canvas);
    kui_canvas_draw_rect(NULL, (KuiRect){0, 0, 1, 1}, &red);
    CHECK(strstr(koral_last_error(), "no canvas") != NULL, "a missing handle is reported, not crashed on");

    /* Icons: Material's by either spelling, any SVG, and the widget. */
    CHECK(kui_material_icon_count() >= 49 && kui_material_icon_name(0) != NULL, "the Material icons are listed");
    KuiVectorImage* icon = kui_material_icon("ArrowBack", 1);
    CHECK(icon != NULL, "a Material icon by Compose's name");
    KuiRect box = kui_vector_image_view_box(icon);
    CHECK(box.right == 24.f && box.bottom == 24.f, "on a box of 24");
    CHECK(kui_material_icon("no_such_icon", 0) == NULL, "none of a name it does not have");
    const char* svg = "<svg viewBox=\"0 0 10 10\"><circle cx=\"5\" cy=\"5\" r=\"4\"/></svg>";
    KuiVectorImage* circle = kui_vector_image_from_svg(svg, strlen(svg));
    CHECK(circle != NULL, "an SVG of its own");
    canvas = kui_canvas_new();
    kui_canvas_draw_vector_image(canvas, circle, (KuiRect){0, 0, 20, 20}, (KuiColor){1, 1, 1, 1});
    picture = kui_canvas_finish(canvas);
    CHECK(picture && kui_picture_instance_count(picture) >= 1, "an SVG drawn");
    kui_picture_release(picture);
    kui_canvas_destroy(canvas);
    KuiWidget* iconWidget = kui_icon(icon, (KuiColor){0, 0, 0, -1});
    CHECK(iconWidget != NULL, "an Icon");
    kui_widget_release(iconWidget);
    kui_vector_image_release(circle);
    kui_vector_image_release(icon);

    Ui* ui = (Ui*)calloc(1, sizeof(Ui));
    g_ui = ui;
    CHECK(koral_app_register("Ui", make_ui, ui) == KORAL_OK, "register");
    KoralOffscreenSettings target = koral_offscreen_settings_default();
    target.extent[0] = target.extent[1] = SIZE;
    KoralScene* scene = koral_app_open_offscreen("Ui", &target, NULL);
    CHECK(scene != NULL, "open offscreen");
    for (int i = 0; i < 3; ++i) koral_app_frame();
    for (int i = 0; i < 2; ++i) koral_app_frame();   /* the frames in flight */
    CHECK(channel(16, 16, 1) > 240 && channel(16, 16, 2) < 10, "green before the click");
    const int builds = ui->builds;

    KoralInput* input = koral_scene_scene_input(scene);
    koral_input_feed_mouse_position(input, 16.f, 16.f);
    koral_app_frame();
    koral_input_feed_mouse_button(input, MOUSE_LEFT, true);
    koral_app_frame();
    koral_input_feed_mouse_button(input, MOUSE_LEFT, false);
    for (int i = 0; i < 3; ++i) koral_app_frame();
    for (int i = 0; i < 2; ++i) koral_app_frame();   /* the frames in flight */
    CHECK(ui->clicks == 1, "the tap reached C");
    CHECK(ui->builds == builds + 1, "set_state built it once more");
    CHECK(channel(16, 16, 2) > 240 && channel(16, 16, 1) < 10, "blue after it");

    koral_app_close(scene);
    koral_app_frame();
    CHECK(ui->destroyed >= 1, "the widget's user data was let go");
    koral_app_destroy();
    free(ui);

    if (failures) { fprintf(stderr, "%d failed\n", failures); return 1; }
    printf("PASSED\n");
    return 0;
}
