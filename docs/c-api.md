# The C interface

`koral_c.h` is Koral in plain C (C99) — what a binding to another language is built on. Any language
with a C foreign-function interface can call it, and nothing about it depends on the C++ compiler that
built Koral. It covers running scenes: the application, scenes written as tables of callbacks, the
current scene's window, input (actions and gamepads included) and time, navigation, saved state,
debug lines, and a rendering subset — buffers, images, pipelines from shader files, descriptor sets,
frame-graph passes and the common command-buffer operations.

```c
#include <koral_c.h>

static void update(KoralScene* scene, void* user) {
    if (koral_input_action("Quit") == KORAL_PRESSED) koral_navigate_quit();
}
static void initialize(KoralScene* scene, void* user) {
    koral_input_bind_action("Quit", "Key.Escape, Gamepad.Back");
}
static KoralSceneCallbacks make_menu(const char* arguments_json, void* factory_user) {
    KoralSceneCallbacks callbacks = {0};
    callbacks.initialize = initialize;
    callbacks.update = update;
    return callbacks;
}

int main(void) {
    KoralAppSettings settings = {0};
    if (koral_app_create(&settings) != KORAL_OK) { puts(koral_last_error()); return 1; }
    koral_register_scene("Menu", make_menu, NULL);
    koral_open("Menu", NULL, NULL);
    int code = koral_app_run();
    koral_app_destroy();
    return code;
}
```

## Conventions

- A function that can fail returns `KoralStatus` (or a null handle) and leaves the reason in
  `koral_last_error()`, per thread. Nothing throws across the boundary.
- A handle a `koral_*_create` function returns is the caller's, freed with the matching `_destroy`.
  Any other handle is borrowed — valid for as long as its documentation says.
- Strings passed in are copied; strings returned are valid until the next call on the same thread.
- Everything runs on the thread that created the application, except a pass's `record` callback, which
  runs on a worker thread alongside other passes.
- Scene arguments are a JSON object of strings; saved state is whatever JSON `save_state` returns,
  handed back to `load_state` before `initialize` after a reload.

## Scenes and passes

A scene is a `KoralSceneCallbacks`: `user` data, a hook for each of the C++ `Scene`'s (all optional),
`interface` to give it an ImGui interface, and `destroy` to free `user`. A frame-graph pass is a
`KoralPassCallbacks` — `setup` declares resources (`koral_pass_read`, `_write`, `_create_image`,
`_side_effect`, `_async_compute`), `initialize` looks them up (`koral_pass_image`), `record` records.

`tests/window/test_capi.c` is a complete example, run as a test on a machine with no display: a scene
from C with a graph pass painting its offscreen window, input fed to it, and its state saved.
