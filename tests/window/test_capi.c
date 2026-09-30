/* The C interface, from C: a scene written as callbacks, a frame-graph pass, builders, a poisoned
   resource, input fed to the scene, and its state saved — on an application with no windowing system,
   so it needs no display. Plain C11, which is also the check that koral_c.h is C. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <koral_c.h>

static int failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAILED: %s (%s:%d) %s\n", what, __FILE__, __LINE__, koral_last_error()); ++failures; } } while (0)

/* The C++ enumerators' values this test uses. */
enum { KEY_SPACE = 32, GAMEPAD_A = 0, SOURCE_KEY = 0, SOURCE_GAMEPAD_BUTTON = 2, KEY_PRESSED = 1,
       BUFFER_TRANSFER_DST = 1 << 1, BUFFER_STORAGE = 1 << 4, BUFFER_TYPE_READBACK = 2,
       IMAGE_TRANSFER_SRC = 1 << 0, IMAGE_TRANSFER_DST = 1 << 1, KIND_IMAGE = 1 };

typedef struct Paint {
    KoralBuffer* readback;
    KoralImage* screen;
    int updates;
    int spacePressedAt;
    uint32_t width, height;
    int green;
    char state[64];
} Paint;

static void setup(KoralRenderPass* pass, KoralPassBuilder* builder, void* user) {
    (void)pass; (void)user;
    koral_pass_builder_write(builder, KORAL_SCREEN, KIND_IMAGE, IMAGE_TRANSFER_DST | IMAGE_TRANSFER_SRC);
    koral_pass_builder_side_effect(builder);
}
static void initialize_pass(KoralRenderPass* pass, KoralPassResources* resources, void* user) {
    Paint* paint = (Paint*)user;
    (void)pass;
    koral_resource_release(paint->screen);
    paint->screen = koral_pass_resources_image_named(resources, KORAL_SCREEN);
}
static void record(KoralRenderPass* pass, KoralCommandBuffer* commands, void* user) {
    Paint* paint = (Paint*)user;
    const float color[4] = {0.f, paint->green ? 1.f : 0.f, 0.f, 1.f};
    (void)pass;
    koral_cmd_clear_color_image(commands, paint->screen, color);
    koral_cmd_copy_image_to_buffer(commands, paint->screen, paint->readback, NULL);
}

static void initialize(KoralScene* scene, void* user) {
    Paint* paint = (Paint*)user;
    KoralBufferBuilder* builder = koral_buffer_builder_new();
    koral_buffer_builder_set_instance_count(builder, 16 * 16 * 4);
    koral_buffer_builder_set_usage(builder, BUFFER_TRANSFER_DST);
    koral_buffer_builder_set_type(builder, BUFFER_TYPE_READBACK);
    paint->readback = koral_buffer_builder_build(builder);
    koral_builder_destroy(builder);
    CHECK(koral_resource_valid(paint->readback) && koral_resource_owned(paint->readback), "a buffer from a builder");
    CHECK(koral_buffer_size(paint->readback) == 16 * 16 * 4, "its size");

    KoralPassCallbacks pass = {0};
    pass.user = paint;
    pass.setup = setup;
    pass.initialize = initialize_pass;
    pass.record = record;
    KoralRenderPass* added = koral_graph_add(koral_scene_graph(scene), "Paint", &pass);
    CHECK(added && strcmp(koral_pass_name(added), "Paint") == 0, "a pass, by its name");

    KoralInputSource jump[2] = {{SOURCE_KEY, KEY_SPACE, 1.f}, {SOURCE_GAMEPAD_BUTTON, GAMEPAD_A, 1.f}};
    koral_input_bind_action(koral_current_input(), "Jump", jump, 2);
    CHECK(strstr(koral_input_bindings(koral_current_input()), "Gamepad.A") != NULL, "the bindings, as JSON");
    KoralInputSource parsed;
    CHECK(koral_input_source_parse("-GamepadAxis.LeftY", &parsed) && parsed.scale == -1.f, "a source by name");
    CHECK(!koral_input_source_parse("Key.Nothing", &parsed), "and a name that is none");
    koral_window_set_title(koral_current_window(), "painting");
}
static void update(KoralScene* scene, void* user) {
    Paint* paint = (Paint*)user;
    (void)scene;
    ++paint->updates;
    koral_window_extent(koral_current_window(), &paint->width, &paint->height);
    if (koral_input_action_state(koral_current_input(), "Jump") == KEY_PRESSED) paint->spacePressedAt = paint->updates;
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
    koral_resource_release(paint->screen);
    koral_resource_release(paint->readback);
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

static KoralSceneCallbacks make_nothing(const char* arguments_json, void* factory_user) {
    (void)arguments_json; (void)factory_user;
    KoralSceneCallbacks none = {0};
    return none;   /* every member zero: the factory could not make its scene */
}

int main(void) {
    KoralAppSettings settings = koral_app_settings_default();
    settings.platform = KORAL_PLATFORM_NONE;
    if (koral_app_create(&settings) != KORAL_OK) {
        printf("SKIPPED: no Vulkan device: %s\n", koral_last_error());
        return 0;
    }
    CHECK(koral_app_register("Paint", make_paint, NULL) == KORAL_OK, "register");
    CHECK(koral_app_register("Nothing", make_nothing, NULL) == KORAL_OK, "register a factory that fails");
    CHECK(koral_app_open("Nope", NULL, NULL) == NULL, "an unknown scene is not opened");
    CHECK(strlen(koral_last_error()) > 0, "and the reason is kept");

    /* A builder given something it cannot build from makes a poisoned resource, not nothing. */
    KoralShaderBuilder* shader_builder = koral_shader_builder_new();
    koral_shader_builder_set_path(shader_builder, "no/such/shader.glsl");
    KoralShader* broken = koral_shader_builder_build(shader_builder);
    koral_builder_destroy(shader_builder);
    CHECK(broken && koral_resource_poisoned(broken) && !koral_resource_valid(broken), "a poisoned shader");
    CHECK(strlen(koral_resource_error_history(broken)) > 0, "that says why");
    KoralComputePipelineBuilder* pipeline_builder = koral_compute_pipeline_builder_new();
    koral_compute_pipeline_builder_set_compute_shader(pipeline_builder, broken);
    KoralComputePipeline* pipeline = koral_compute_pipeline_builder_build(pipeline_builder);
    koral_builder_destroy(pipeline_builder);
    CHECK(koral_resource_poisoned(pipeline), "what is built from it is poisoned too");
    CHECK(strstr(koral_resource_error_history(pipeline), "no/such/shader") != NULL, "and names the cause");
    CHECK(koral_buffer_size(pipeline) == 0 && strstr(koral_last_error(), "expected a buffer") != NULL,
          "a handle of the wrong kind is refused");
    koral_resource_release(pipeline);
    koral_resource_release(broken);

    KoralOffscreenSettings target = koral_offscreen_settings_default();
    target.extent[0] = target.extent[1] = 16;
    CHECK(koral_app_open_offscreen("Nothing", &target, NULL) == NULL, "a factory that fails opens nothing");
    KoralScene* scene = koral_app_open_offscreen("Paint", &target, "{\"green\": \"yes\"}");
    CHECK(scene != NULL, "open offscreen");
    CHECK(strcmp(koral_scene_name(scene), "Paint") == 0, "its name");
    CHECK(koral_app_is_open(scene), "it is open");
    KoralScene* shown[4] = {0};
    CHECK(koral_app_scenes(shown, 4) == 1 && shown[0] == scene, "the scenes the windows show");
    CHECK(g_paint && g_paint->green, "the arguments reached the factory");
    CHECK(strcmp(koral_window_title(koral_scene_scene_window(scene)), "painting") == 0, "its window's title, set from inside");

    for (int i = 0; i < 3; ++i) CHECK(koral_app_frame(), "a frame");
    koral_input_feed_key(koral_scene_scene_input(scene), KEY_SPACE, true);
    CHECK(koral_app_frame(), "a frame");
    for (int i = 0; i < 2; ++i) CHECK(koral_app_frame(), "a frame");   /* the frames in flight */

    CHECK(g_paint->updates == 6, "updated every frame");
    CHECK(g_paint->width == 16 && g_paint->height == 16, "its window's size, through the current scene");
    CHECK(g_paint->spacePressedAt == 4, "fed input arrives the next frame, as the action");
    CHECK(koral_time_frame_count(koral_scene_scene_time(scene)) >= 6, "its clock");

    unsigned char texel[4] = {0};
    CHECK(koral_buffer_read(g_paint->readback, texel, 4, 0) == KORAL_OK, "read back");
    CHECK(texel[0] == 0 && texel[1] == 255 && texel[2] == 0, "the pass painted the screen green");

    CHECK(strcmp(koral_scene_save_state(scene), "{\"updates\":6}") == 0, "its state, as it saves it");
    CHECK(koral_scene_load_state(scene, "{\"updates\":40}") == KORAL_OK && g_paint->updates == 40, "and loads it");

    koral_app_close(scene);
    CHECK(!koral_app_frame(), "no scene left");
    CHECK(!koral_app_is_open(scene), "a closed scene's handle is not open");
    CHECK(strcmp(koral_scene_name(scene), "") == 0, "and is refused rather than read");
    CHECK(koral_scene_load_state(scene, "{}") == KORAL_ERROR, "or written");
    koral_app_destroy();

    if (failures) { fprintf(stderr, "%d failed\n", failures); return 1; }
    printf("PASSED\n");
    return 0;
}
