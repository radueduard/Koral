# Koral

Koral is a C++23 framework for real-time graphics on Vulkan. A program is a `kor::App` running **scenes**, each with
its own window, input, clock and frame graph; everything the GPU touches is a resource made by a builder that never
throws; command buffers work out their own barriers; and the same API is there in C, C# and Kotlin, object for object.

```cpp
void Render(kor::CommandBuffer& cb) override {
    cb.BeginRendering()                     // the window's framebuffer, cleared
      .BindGraphicsPipeline(_pipeline)
      .Draw(3)
      .EndRendering();
}
```

<div class="grid cards" markdown>

-   **Getting started**

    Build the SDK, write a program that draws a triangle, and see how the pieces fit.

    [Start here](getting-started/index.md)

-   **Core**

    Scenes, resources, pipelines, descriptors, command buffers, the frame graph, tokens and tasks.

    [Scenes and the application](scenes.md)

-   **Modules**

    The camera, debug drawing, the retained UI library, input, maths, networking, reflection.

    [Modules](modules.md)

-   **Languages**

    The C interface every binding is built on, C# with live reload, Kotlin with Jetpack Compose.

    [The C interface](c-api.md)

</div>

## What it gives a program

- **Scenes.** One screen of a program each — a menu, a level, an editor — with hooks for the frame (`Update`,
  `FixedUpdate`, `Render`), navigation between them, offscreen scenes for previews and servers, and hot reload that
  keeps their state. See [Scenes and the application](scenes.md).
- **Resources that explain themselves.** `Build()` returns a `kor::Resource<T>` that is either valid or carries the
  `kor::Error` that says why it is not. Shaders and pipelines rebuild when their files change. See
  [Buffers, images and meshes](resources.md) and [Errors](errors.md).
- **Shaders by name.** Slang or GLSL; descriptor sets, push constants, specialization constants and vertex inputs
  are reflected out of the compiled code and bound by the names they have in the source. See
  [Shaders, pipelines and GPU features](pipelines.md).
- **Barriers worked out.** Each command says what it reads and writes; the command buffer inserts the transitions
  where they are legal. See [Command buffers](commands.md).
- **A frame graph.** Render passes that say what they read, write and create; the graph orders them, drops what is
  not needed, shares memory and moves work to the async compute queue. See [The frame graph](frame-graph.md).
- **Tokens and tasks.** One way to say "after this has happened", awaited with `co_await`. See
  [Tokens and tasks](tokens.md).
- **Modules.** Linked ones (`koral-ui`, `koral-net`, image and model import) and ones a project writes, with the
  hooks a scene has. See [Modules](modules.md).

## The repository

| Folder | |
|---|---|
| `include/`, `src/` | The framework: `#include <koral.h>`, link `Koral::Koral`. |
| `modules/` | The modules that ship with it: camera, mesh, image import, export and compression, model import, UI, networking. |
| `bindings/` | C# (`koral-dotnet`) and Kotlin. |
| `shaders/` | Koral's own shaders, on every project's search path after its own. |
| `tools/` | The editor tooling: Slang and GLSL language servers. |
| `docs/` | These pages. |
