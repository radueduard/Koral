# The C interface

`koral_c.h` is Koral's C++ API in plain C (C99), object for object: what a binding to another language is
built on. Any language with a C foreign-function interface can call it, and nothing about it depends on
the C++ compiler that built Koral.

It is not a second API. Every function is a member of a C++ class, named `koral_<class>_<member>` in
snake case, and does what that member does. `koral_image_builder_set_format` is
`kor::Image::Builder::SetFormat`, and `koral_cmd_draw_indexed` is `kor::CommandBuffer::DrawIndexed`. So
the C++ headers are its documentation, and what `koral_c.h` itself says is only what the translation adds.

```c
#include <koral_c.h>

static void initialize(KoralScene* scene, void* user) {
    Game* game = user;
    KoralBufferBuilder* builder = koral_buffer_builder_new();
    koral_buffer_builder_set_instance_count(builder, sizeof(Camera));
    koral_buffer_builder_set_usage(builder, 1 << 3 /* kor::Buffer::Usage::eUniform */);
    game->camera = koral_buffer_builder_build(builder);     /* owned; maybe poisoned, never null */
    koral_builder_destroy(builder);
    if (koral_resource_poisoned(game->camera)) puts(koral_resource_error_history(game->camera));
}

int main(void) {
    KoralAppSettings settings = koral_app_settings_default();
    if (koral_app_create(&settings) != KORAL_OK) { puts(koral_last_error()); return 1; }
    koral_app_register("Game", make_game, NULL);
    koral_app_open("Game", NULL, NULL);
    while (koral_app_frame()) {}
    koral_app_destroy();
    return 0;
}
```

## What the translation adds

- **Errors.** A function that can fail returns `KoralStatus` (or a null handle) and leaves the reason in
  `koral_last_error()`, per thread. Nothing throws across the boundary. A command that cannot be
  recorded is kept on its command buffer (`koral_cmd_ok`, `koral_cmd_error`), as in C++.
- **Resources.** Buffers, images, pipelines and the rest are `KoralResource` handles, one type for all
  kinds. The typedefs (`KoralBuffer`, `KoralImage`, ...) only name what each function expects, and a
  handle of the wrong kind is refused with a message. A handle is *owned* (what a builder's `_build`
  returns: a `kor::Resource<T>`) or *borrowed* (what a lookup returns: a `kor::ResourceRef<const T>`).
  `koral_resource_release` frees either: the resource with an owned handle, only the handle with a
  borrowed one. `koral_resource_*` answers what any resource can: valid, poisoned, why (`_error_history`),
  its name, its identity.
- **Builders.** `koral_<class>_builder_new`, a setter per C++ setter, `_build`, then
  `koral_builder_destroy`. The setters are the C++ ones, called on a C++ builder, so their checks and
  warnings are the same. `_build` returns a resource even when the build fails: a poisoned one, as in C++.
- **Enumerations and flags** are passed as the C++ enumerators' values:
  `(uint32_t)kor::Image::Format::eRGBA8_UNORM`, or the bits of a `kor::Flags<E>`.
- **Objects that are not resources** (scenes, windows, inputs, clocks, frame graphs, passes, command
  buffers) are borrowed pointers to the C++ objects, valid while those are. A `KoralScene*` kept past its
  scene is refused. A reload often makes a new scene at the same address, which a pointer cannot tell
  apart, so `koral_scene_life` gives a weak reference that can.
- **Callbacks** (scenes, passes, a debug pass's camera, `SingleTimeCommand`, `Run`) take a `void* user`
  and, where they are kept, a `destroy` to free it when the object goes. A scene factory that cannot make
  its scene returns callbacks with every member zero, having logged why, and the scene is not opened.
- **Settings structs** have `_default()` functions giving the C++ defaults: a zeroed struct is not them.
- **Strings** passed in are copied; strings returned are valid until the next call on the same thread.
- **Threads.** Everything runs on the thread that created the application, except a pass's `record`
  callback and the command-buffer calls made from it, which run on a worker thread alongside other passes.

## Modules

A module's C interface follows the same conventions in a header of its own: koral-ui's is
`koralUI_c.h` ([Interfaces with koral-ui](ui.md#from-c)). It reports its failures through
`koral_last_error()`, by way of `koral_set_last_error`.

## For a runtime of one's own

`koral_project_load` reads a project the way the C++ runtime does: `koral.json` from `--config`,
`KORAL_CONFIG` or the nearest one up, then the flags. It also applies what has to happen before the
application exists: search paths and modules. `koral_project_app_settings` and
`koral_project_window_settings` then fill the structs `koral_app_create` and `koral_app_open` take.

A language that compiles its scenes in-process registers them again after a rebuild and calls
`koral_app_reload_scenes`. Every open scene of those names reopens from the new code, with its saved
state. A language with coroutines of its own resumes them inside their scene with `koral_scene_scope_enter`
and `_exit`, as a `kor::Task` does.

`tests/window/test_capi.c` is a complete example from C, run as a test on a machine with no display.
[The C# bindings](csharp.md) are the larger one: they are built entirely on this interface.
