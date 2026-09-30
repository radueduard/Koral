/*
 * Koral's C interface: what a binding to another language is built on.
 *
 * Plain C (C99), so any language with a C foreign-function interface can call it — and nothing about
 * it depends on the C++ compiler that built Koral. It covers what running scenes takes: the
 * application, scenes written as tables of callbacks, the current scene's window, input and time,
 * navigation, saved state, and a rendering subset — buffers, images, pipelines from shader files,
 * descriptor sets, frame-graph passes and the common command-buffer operations.
 *
 *     static void update(KoralScene* scene, void* user) {
 *         if (koral_input_key_state(KORAL_KEY_ESCAPE) == KORAL_PRESSED) koral_navigate_quit();
 *     }
 *     static KoralSceneCallbacks make_menu(const char* arguments_json, void* factory_user) {
 *         KoralSceneCallbacks callbacks = {0};
 *         callbacks.update = update;
 *         return callbacks;
 *     }
 *     int main(void) {
 *         KoralAppSettings settings = {0};
 *         if (koral_app_create(&settings) != KORAL_OK) { puts(koral_last_error()); return 1; }
 *         koral_register_scene("Menu", make_menu, NULL);
 *         koral_open("Menu", NULL, NULL);
 *         int code = koral_app_run();
 *         koral_app_destroy();
 *         return code;
 *     }
 *
 * Conventions:
 *  - A function that can fail returns KoralStatus (or a null handle) and leaves the reason in
 *    koral_last_error(), per thread.
 *  - A handle a koral_*_create function returns is the caller's, freed with the matching
 *    koral_*_destroy. A handle any other function returns is borrowed: valid for as long as the
 *    documentation says, never freed by the caller.
 *  - Strings passed in are copied; strings returned are valid until the next call on the same thread.
 *  - Everything runs on the thread that created the application, except a pass's record callback,
 *    which runs on a worker thread (see KoralPassCallbacks).
 */

#ifndef KORAL_C_H
#define KORAL_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- results ------------------------------------------------------------------------------------ */

typedef enum KoralStatus { KORAL_OK = 0, KORAL_ERROR = 1 } KoralStatus;

/** Why the last call on this thread failed. Empty after a success. */
KORAL_API const char* koral_last_error(void);

/* ---- the application ----------------------------------------------------------------------------- */

typedef enum KoralPlatform {
    KORAL_PLATFORM_AUTO = 0,
    KORAL_PLATFORM_X11 = 1,
    KORAL_PLATFORM_WAYLAND = 2,
    KORAL_PLATFORM_NONE = 3     /* no windowing system: offscreen scenes only, no display needed */
} KoralPlatform;

typedef struct KoralAppSettings {
    KoralPlatform platform;
    uint32_t frames_in_flight;       /* 0 means the default (2) */
    const char* gpu;                 /* a GPU to prefer by name; null or empty lets Koral choose */
    const char* interface_directory; /* where scenes with an interface keep their layout; may be null */
} KoralAppSettings;

/** Brings up the application: the device and everything shared. One per process. */
KORAL_API KoralStatus koral_app_create(const KoralAppSettings* settings);
/** Shuts every scene down, then the application. */
KORAL_API void koral_app_destroy(void);
/** Loads a scene library (KORAL_SCENES) and registers its scenes. */
KORAL_API KoralStatus koral_app_load_library(const char* path);
/** Loads a scene library again, reopening its scenes with their state. */
KORAL_API KoralStatus koral_app_reload_library(const char* path);
/** One frame of every scene. False once no scene is left. */
KORAL_API bool koral_app_frame(void);
/** Frames until no scene is left. The process's exit code. */
KORAL_API int koral_app_run(void);
/** Closes every scene after the frame. */
KORAL_API void koral_app_quit(void);

/* ---- scenes -------------------------------------------------------------------------------------- */

typedef struct KoralScene KoralScene;                 /* borrowed: valid while the scene is open */
typedef struct KoralCommandBuffer KoralCommandBuffer; /* borrowed: valid during the callback */

/**
 * A scene written in C (or anything calling C): its hooks, each optional. `user` is handed back to
 * every one; `destroy` is called once, last, to free it.
 */
typedef struct KoralSceneCallbacks {
    void* user;
    bool interface;   /* the scene has an interface of its own: render_ui is called */
    void (*initialize)(KoralScene* scene, void* user);
    void (*fixed_update)(KoralScene* scene, void* user);
    void (*update)(KoralScene* scene, void* user);
    void (*late_update)(KoralScene* scene, void* user);
    void (*render)(KoralScene* scene, KoralCommandBuffer* commands, void* user);
    void (*render_ui)(KoralScene* scene, void* user);
    void (*on_resize)(KoralScene* scene, uint32_t width, uint32_t height, void* user);
    bool (*on_close_requested)(KoralScene* scene, void* user);   /* null: close */
    void (*shutdown)(KoralScene* scene, void* user);
    /* State kept across a reload of its library, as JSON: returned by save_state (copied at once),
       handed to load_state before initialize. Either may be null. */
    const char* (*save_state)(void* user);
    void (*load_state)(const char* json, void* user);
    void (*destroy)(void* user);
} KoralSceneCallbacks;

/** Makes a scene's callbacks from the arguments it is opened with (a JSON object of strings). */
typedef KoralSceneCallbacks (*KoralSceneFactory)(const char* arguments_json, void* factory_user);

/** Makes @p factory what opening @p name runs. */
KORAL_API KoralStatus koral_register_scene(const char* name, KoralSceneFactory factory, void* factory_user);

typedef struct KoralWindowSettings {
    const char* title;          /* null: the scene's name */
    uint32_t width, height;     /* 0: 1280 x 720 */
    bool fullscreen;
    bool borderless;
    bool no_vsync;
} KoralWindowSettings;

/** Opens @p name in a window of its own. Settings and arguments (a JSON object) may be null. */
KORAL_API KoralScene* koral_open(const char* name, const KoralWindowSettings* settings, const char* arguments_json);
/** Opens @p name offscreen, drawing into an image of @p width x @p height. */
KORAL_API KoralScene* koral_open_offscreen(const char* name, uint32_t width, uint32_t height, const char* arguments_json);
/** Closes a scene's window, after the frame. */
KORAL_API void koral_close(KoralScene* scene);

KORAL_API const char* koral_scene_name(KoralScene* scene);
/** The scene whose code is running on this thread, or null. */
KORAL_API KoralScene* koral_current_scene(void);
/** The scene's saved state, as JSON. */
KORAL_API const char* koral_scene_save_state(KoralScene* scene);
KORAL_API KoralStatus koral_scene_load_state(KoralScene* scene, const char* json);

/* What the current scene's window shows — after the frame. */
KORAL_API void koral_navigate_replace(const char* name, const char* arguments_json);
KORAL_API void koral_navigate_push(const char* name, const char* arguments_json);
KORAL_API void koral_navigate_pop(void);
KORAL_API void koral_navigate_close(void);
KORAL_API void koral_navigate_quit(void);

/* ---- the current scene's window, input and time ------------------------------------------------- */

KORAL_API void koral_window_extent(uint32_t* width, uint32_t* height);
KORAL_API bool koral_window_resized(void);
KORAL_API void koral_window_set_title(const char* title);
KORAL_API void koral_window_resize(uint32_t width, uint32_t height);
KORAL_API void koral_window_close(void);

typedef enum KoralKeyState { KORAL_NOT_PRESSED = 0, KORAL_PRESSED = 1, KORAL_HELD = 2, KORAL_RELEASED = 3 } KoralKeyState;

/* Key codes are GLFW's (and kor::Key's). */
enum { KORAL_KEY_SPACE = 32, KORAL_KEY_A = 65, KORAL_KEY_D = 68, KORAL_KEY_S = 83, KORAL_KEY_W = 87,
       KORAL_KEY_ESCAPE = 256, KORAL_KEY_ENTER = 257, KORAL_KEY_RIGHT = 262, KORAL_KEY_LEFT = 263,
       KORAL_KEY_DOWN = 264, KORAL_KEY_UP = 265 };
enum { KORAL_MOUSE_LEFT = 0, KORAL_MOUSE_RIGHT = 1, KORAL_MOUSE_MIDDLE = 2 };

KORAL_API KoralKeyState koral_input_key(int key);
KORAL_API KoralKeyState koral_input_mouse_button(int button);
KORAL_API void koral_input_mouse_position(float* x, float* y);
KORAL_API void koral_input_mouse_delta(float* x, float* y);
KORAL_API void koral_input_scroll(float* x, float* y);
/** 0 normal, 1 hidden, 2 captured. */
KORAL_API void koral_input_set_cursor_mode(int mode);
/* Gamepads: buttons and axes numbered as kor::GamepadButton / kor::GamepadAxis (GLFW's order). */
KORAL_API bool koral_input_gamepad_connected(int pad);
KORAL_API KoralKeyState koral_input_gamepad_button(int button, int pad);
KORAL_API float koral_input_gamepad_axis(int axis, int pad);

/* Actions and axes, bound to sources by name — "Key.Space,Gamepad.A", "Key.D,-Key.A,GamepadAxis.LeftX". */
KORAL_API KoralStatus koral_input_bind_action(const char* action, const char* sources);
KORAL_API KoralStatus koral_input_bind_axis(const char* axis, const char* sources);
KORAL_API KoralKeyState koral_input_action(const char* action);
KORAL_API float koral_input_axis(const char* axis);
/** Every binding, as JSON (kor::InputBindings); and loaded back. */
KORAL_API const char* koral_input_bindings(void);
KORAL_API KoralStatus koral_input_set_bindings(const char* json);

/** Feeds an offscreen scene input: its window has none of its own. */
KORAL_API void koral_input_feed_key(KoralScene* scene, int key, bool down);
KORAL_API void koral_input_feed_mouse_button(KoralScene* scene, int button, bool down);
KORAL_API void koral_input_feed_mouse_position(KoralScene* scene, float x, float y);

KORAL_API float koral_time_frame(void);        /* scaled; inside fixed_update, the fixed step */
KORAL_API float koral_time_fixed_step(void);
KORAL_API float koral_time_elapsed(void);
KORAL_API uint64_t koral_time_frame_count(void);
KORAL_API void koral_time_set_scale(float scale);
KORAL_API void koral_time_set_fixed_step(float seconds);

/* ---- resources ----------------------------------------------------------------------------------- */

typedef struct KoralBuffer KoralBuffer;
typedef struct KoralImage KoralImage;
typedef struct KoralPipeline KoralPipeline;
typedef struct KoralDescriptorSet KoralDescriptorSet;

enum {  /* buffer usage, combined with | */
    KORAL_BUFFER_TRANSFER_SRC = 1 << 0, KORAL_BUFFER_TRANSFER_DST = 1 << 1, KORAL_BUFFER_UNIFORM = 1 << 3,
    KORAL_BUFFER_STORAGE = 1 << 4, KORAL_BUFFER_VERTEX = 1 << 5, KORAL_BUFFER_INDEX = 1 << 6, KORAL_BUFFER_INDIRECT = 1 << 7
};
typedef enum KoralMemory {
    KORAL_MEMORY_DEVICE = 0,    /* fastest for the GPU; written through a staging copy */
    KORAL_MEMORY_STAGING = 1,   /* for uploads */
    KORAL_MEMORY_READBACK = 2,  /* for reading results back */
    KORAL_MEMORY_DYNAMIC = 3    /* written by the CPU often, read by both */
} KoralMemory;

KORAL_API KoralBuffer* koral_buffer_create(uint64_t size, uint32_t usage, KoralMemory memory);
KORAL_API void koral_buffer_destroy(KoralBuffer* buffer);
KORAL_API KoralStatus koral_buffer_write(KoralBuffer* buffer, const void* data, uint64_t size, uint64_t offset);
/** Reads back what the GPU finished writing: after koral_app_frame, and the frames in flight after it. */
KORAL_API KoralStatus koral_buffer_read(KoralBuffer* buffer, void* data, uint64_t size, uint64_t offset);

enum {  /* image usage, combined with | */
    KORAL_IMAGE_TRANSFER_SRC = 1 << 0, KORAL_IMAGE_TRANSFER_DST = 1 << 1, KORAL_IMAGE_SAMPLED = 1 << 2,
    KORAL_IMAGE_STORAGE = 1 << 3, KORAL_IMAGE_COLOR_ATTACHMENT = 1 << 4, KORAL_IMAGE_DEPTH_ATTACHMENT = 1 << 5
};
typedef enum KoralFormat {
    KORAL_FORMAT_RGBA8_UNORM = 0, KORAL_FORMAT_RGBA8_SRGB = 1, KORAL_FORMAT_RGBA16_SFLOAT = 2,
    KORAL_FORMAT_RGBA32_SFLOAT = 3, KORAL_FORMAT_R32_SFLOAT = 4, KORAL_FORMAT_D32_SFLOAT = 5
} KoralFormat;

KORAL_API KoralImage* koral_image_create(uint32_t width, uint32_t height, KoralFormat format, uint32_t usage);
KORAL_API void koral_image_destroy(KoralImage* image);
KORAL_API void koral_image_extent(KoralImage* image, uint32_t* width, uint32_t* height);

/** A graphics pipeline from shader files (GLSL, or Slang with an entry point; entries may be null). */
KORAL_API KoralPipeline* koral_graphics_pipeline_create(const char* vertex_path, const char* vertex_entry,
                                                        const char* fragment_path, const char* fragment_entry);
KORAL_API KoralPipeline* koral_compute_pipeline_create(const char* path, const char* entry);
KORAL_API void koral_pipeline_destroy(KoralPipeline* pipeline);

/** One resource for a descriptor set, at the binding the shader calls `name`. */
typedef struct KoralDescriptorWrite {
    const char* name;
    KoralBuffer* buffer;   /* or */
    KoralImage* image;     /* with a linear sampler when `sampled`, as a storage image otherwise */
    bool sampled;
} KoralDescriptorWrite;

KORAL_API KoralDescriptorSet* koral_descriptor_set_create(KoralPipeline* pipeline, uint32_t set,
                                                          const KoralDescriptorWrite* writes, size_t count);
KORAL_API void koral_descriptor_set_destroy(KoralDescriptorSet* set);

/* ---- recording ----------------------------------------------------------------------------------- */

/** Opens a render pass on the current window's (or view's) default framebuffer, cleared. */
KORAL_API void koral_cmd_begin_rendering(KoralCommandBuffer* commands);
KORAL_API void koral_cmd_end_rendering(KoralCommandBuffer* commands);
/** Binds a graphics or a compute pipeline, whichever it is. */
KORAL_API void koral_cmd_bind_pipeline(KoralCommandBuffer* commands, KoralPipeline* pipeline);
KORAL_API void koral_cmd_bind_descriptor_set(KoralCommandBuffer* commands, uint32_t index, KoralDescriptorSet* set);
KORAL_API void koral_cmd_push_floats(KoralCommandBuffer* commands, const char* name, const float* values, uint32_t count);
KORAL_API void koral_cmd_push_int(KoralCommandBuffer* commands, const char* name, int32_t value);
KORAL_API void koral_cmd_draw(KoralCommandBuffer* commands, uint32_t vertices, uint32_t instances);
KORAL_API void koral_cmd_dispatch(KoralCommandBuffer* commands, uint32_t x, uint32_t y, uint32_t z);
KORAL_API void koral_cmd_clear_image(KoralCommandBuffer* commands, KoralImage* image, float r, float g, float b, float a);
KORAL_API void koral_cmd_copy_image_to_buffer(KoralCommandBuffer* commands, KoralImage* image, KoralBuffer* buffer);
KORAL_API void koral_cmd_blit(KoralCommandBuffer* commands, KoralImage* from, KoralImage* to);

/* ---- the frame graph ----------------------------------------------------------------------------- */

typedef struct KoralPassBuilder KoralPassBuilder;     /* borrowed: during setup */
typedef struct KoralPassResources KoralPassResources; /* borrowed: during initialize */

/** The name the screen goes by in a graph: the scene's (or view's) window. */
#define KORAL_SCREEN "screen"

/**
 * A render pass: `setup` declares what it uses, `initialize` looks its resources up (when they
 * change), `record` records its commands — on a worker thread, alongside other passes, so it must
 * only touch what it owns. `destroy` frees `user`, once.
 */
typedef struct KoralPassCallbacks {
    void* user;
    void (*setup)(KoralPassBuilder* builder, void* user);
    void (*initialize)(KoralPassResources* resources, void* user);
    void (*record)(KoralCommandBuffer* commands, void* user);
    void (*destroy)(void* user);
} KoralPassCallbacks;

/** Adds a pass to the scene's own graph. */
KORAL_API KoralStatus koral_graph_add_pass(KoralScene* scene, const char* name, const KoralPassCallbacks* pass);

KORAL_API void koral_pass_read(KoralPassBuilder* builder, const char* name, uint32_t image_usage);
KORAL_API void koral_pass_write(KoralPassBuilder* builder, const char* name, uint32_t image_usage);
/** Creates an image, sized to the screen times @p scale (0 means 1). */
KORAL_API void koral_pass_create_image(KoralPassBuilder* builder, const char* name, KoralFormat format, uint32_t usage, float scale);
KORAL_API void koral_pass_side_effect(KoralPassBuilder* builder);
KORAL_API void koral_pass_async_compute(KoralPassBuilder* builder);

/** An image of the graph's, by name. Borrowed: valid until the pass is initialized again. */
KORAL_API KoralImage* koral_pass_image(KoralPassResources* resources, const char* name);

/* ---- debug lines --------------------------------------------------------------------------------- */

/** How a debug shape is drawn: its colour, for how many seconds (0: this frame), and over everything or not. */
typedef struct KoralDebugStyle {
    float r, g, b, a;
    float duration;
    bool on_top;
} KoralDebugStyle;

/* Into the current scene's debug lines; a null style is opaque white, for this frame. */
KORAL_API void koral_debug_line(const float from[3], const float to[3], const KoralDebugStyle* style);
KORAL_API void koral_debug_box(const float min[3], const float max[3], const KoralDebugStyle* style);
KORAL_API void koral_debug_sphere(const float center[3], float radius, const KoralDebugStyle* style);
KORAL_API void koral_debug_arrow(const float from[3], const float to[3], const KoralDebugStyle* style);

/**
 * Adds a pass drawing the scene's debug lines over its screen, with the camera @p view_projection
 * writes (16 floats, column-major) each frame. @p depth names a depth image of the graph's to test the
 * lines against; null draws them on top.
 */
KORAL_API KoralStatus koral_graph_add_debug_pass(KoralScene* scene, void (*view_projection)(float out[16], void* user),
                                                 void* user, const char* depth);

#ifdef __cplusplus
}
#endif

#endif /* KORAL_C_H */
