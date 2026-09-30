/* The C interface, from C: a scene written as callbacks, a frame-graph pass, input fed to it, and its
   state saved — on an application with no windowing system, so it needs no display. Plain C11, which
   is also the check that koral_c.h is C. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <koral_c.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAILED: %s (%s:%d) %s\n", what, __FILE__, __LINE__, koral_last_error()); ++failures; } } while (0)

typedef struct Paint {
    KoralBuffer* readback;
    KoralImage* screen;
    int updates;
    int spacePressedAt;
    uint32_t width, height;
    int green;
    char state[64];
} Paint;

static void setup(KoralPassBuilder* builder, void* user) {
    (void)user;
    koral_pass_write(builder, KORAL_SCREEN, KORAL_IMAGE_TRANSFER_DST | KORAL_IMAGE_TRANSFER_SRC);
    koral_pass_side_effect(builder);
}
static void initialize_pass(KoralPassResources* resources, void* user) {
    ((Paint*)user)->screen = koral_pass_image(resources, KORAL_SCREEN);
}
static void record(KoralCommandBuffer* commands, void* user) {
    Paint* paint = (Paint*)user;
    koral_cmd_clear_image(commands, paint->screen, 0.f, paint->green ? 1.f : 0.f, 0.f, 1.f);
    koral_cmd_copy_image_to_buffer(commands, paint->screen, paint->readback);
}

static void initialize(KoralScene* scene, void* user) {
    Paint* paint = (Paint*)user;
    paint->readback = koral_buffer_create(16 * 16 * 4, KORAL_BUFFER_TRANSFER_DST, KORAL_MEMORY_READBACK);
    KoralPassCallbacks pass = {0};
    pass.user = paint;
    pass.setup = setup;
    pass.initialize = initialize_pass;
    pass.record = record;
    koral_graph_add_pass(scene, "Paint", &pass);
    CHECK(koral_input_bind_action("Jump", "Key.Space, Gamepad.A") == KORAL_OK, "bind an action");
    CHECK(koral_input_bind_action("Nope", "Key.Nothing") == KORAL_ERROR, "a source that is not one is refused");
    CHECK(strstr(koral_input_bindings(), "Gamepad.A") != NULL, "the bindings, as JSON");
}
static void update(KoralScene* scene, void* user) {
    Paint* paint = (Paint*)user;
    (void)scene;
    ++paint->updates;
    koral_window_extent(&paint->width, &paint->height);
    if (koral_input_action("Jump") == KORAL_PRESSED) paint->spacePressedAt = paint->updates;
}
static const char* save_state(void* user) {
    Paint* paint = (Paint*)user;
    snprintf(paint->state, sizeof paint->state, "{\"updates\":%d}", paint->updates);
    return paint->state;
}
static void load_state(const char* json, void* user) {
    sscanf(json, "{\"updates\":%d}", &((Paint*)user)->updates);
}
static void destroy(void* user) {
    Paint* paint = (Paint*)user;
    koral_buffer_destroy(paint->readback);
    free(paint);
}

static Paint* g_paint = NULL;

static KoralSceneCallbacks make_paint(const char* arguments_json, void* factory_user) {
    (void)factory_user;
    Paint* paint = (Paint*)calloc(1, sizeof(Paint));
    paint->green = arguments_json && strstr(arguments_json, "\"green\"") != NULL;
    g_paint = paint;
    KoralSceneCallbacks callbacks = {0};
    callbacks.user = paint;
    callbacks.initialize = initialize;
    callbacks.update = update;
    callbacks.save_state = save_state;
    callbacks.load_state = load_state;
    callbacks.destroy = destroy;
    return callbacks;
}

int main(void) {
    KoralAppSettings settings = {0};
    settings.platform = KORAL_PLATFORM_NONE;
    if (koral_app_create(&settings) != KORAL_OK) {
        printf("SKIPPED: no Vulkan device: %s\n", koral_last_error());
        return 0;
    }
    CHECK(koral_register_scene("Paint", make_paint, NULL) == KORAL_OK, "register");
    CHECK(koral_open("Nope", NULL, NULL) == NULL, "an unknown scene is not opened");
    CHECK(strlen(koral_last_error()) > 0, "and the reason is kept");

    KoralScene* scene = koral_open_offscreen("Paint", 16, 16, "{\"green\": \"yes\"}");
    CHECK(scene != NULL, "open offscreen");
    CHECK(strcmp(koral_scene_name(scene), "Paint") == 0, "its name");
    CHECK(g_paint && g_paint->green, "the arguments reached the factory");

    for (int i = 0; i < 3; ++i) CHECK(koral_app_frame(), "a frame");
    koral_input_feed_key(scene, KORAL_KEY_SPACE, true);
    CHECK(koral_app_frame(), "a frame");
    for (int i = 0; i < 2; ++i) CHECK(koral_app_frame(), "a frame");   /* the frames in flight */

    CHECK(g_paint->updates == 6, "updated every frame");
    CHECK(g_paint->width == 16 && g_paint->height == 16, "its window's size, through the current scene");
    CHECK(g_paint->spacePressedAt == 4, "fed input arrives the next frame, pressed");

    unsigned char texel[4] = {0};
    CHECK(koral_buffer_read(g_paint->readback, texel, 4, 0) == KORAL_OK, "read back");
    CHECK(texel[0] == 0 && texel[1] == 255 && texel[2] == 0, "the pass painted the screen green");

    CHECK(strcmp(koral_scene_save_state(scene), "{\"updates\":6}") == 0, "its state, as it saves it");
    CHECK(koral_scene_load_state(scene, "{\"updates\":40}") == KORAL_OK && g_paint->updates == 40, "and loads it");

    koral_close(scene);
    CHECK(!koral_app_frame(), "no scene left");
    koral_app_destroy();

    if (failures) { fprintf(stderr, "%d failed\n", failures); return 1; }
    printf("PASSED\n");
    return 0;
}
