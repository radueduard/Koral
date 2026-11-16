# How Koral fits together

The pieces a program meets, in the order it meets them, and the guide that says more about each.

## The application and its scenes

A program is a `kor::App`: the device, the frames, and the modules loaded into it. It runs **scenes**. A scene is
one screen — a menu, a level, an editor — and owns what belongs to it: its window, its input, its clock, its frame
graph, its interface and its debug lines. Inside a scene, `Window::`, `Input::`, `Time::` and `Debug::` are its own.

A frame calls each scene's hooks in order: `FixedUpdate` on a steady step, `Update` once, `LateUpdate`, then
`Render` with the frame's command buffer. Scenes are opened in windows of their own, pushed over each other with
`kor::Navigator`, or drawn offscreen into an image another scene shows.

A program can have its own `main` ([A first program](first-program.md)), or put its scenes in a shared library that
Koral's runtime, `Koral_Runtime`, loads and runs as `koral.json` says — the runtime that also reloads the library
when it is rebuilt, keeping each scene's state.

[Scenes and the application](../scenes.md) · [Configuring a project](../configuration.md)

## Resources

Everything the GPU reads or writes — buffers, images, views, samplers, meshes, shaders, pipelines, framebuffers,
descriptor sets — is made by a builder. `Build()` never throws: it returns a `kor::Resource<T>`, valid or carrying the
`kor::Error` that says why it is not. A `kor::ResourceRef<T>` is a reference to one that does not keep it alive.

Shaders are Slang or GLSL. What a pipeline needs — descriptors, push constants, specialization constants, vertex
inputs — is reflected out of the compiled code and reached by the names it has in the source:
`cb.PushConstant("tint", color)`, `.Write("lights", buffer)`.

[Buffers, images and meshes](../resources.md) · [Shaders, pipelines and GPU features](../pipelines.md) ·
[Descriptors and bindless](../descriptors.md) · [Errors](../errors.md)

## Recording work

A `kor::CommandBuffer` records commands that chain. Each says what it reads and writes, so the barriers and layout
transitions between them are worked out when the recording is resolved — often moved to where they are legal, such as
before a render pass that samples an image. A scene records into the frame's in `Render`; anything else creates one,
records, and submits it.

What happens after work finishes is a `kor::Token`: waited on, or `co_await`ed in a `kor::Task`. `kor::ParallelFor`
spreads CPU work across threads.

[Command buffers](../commands.md) · [Tokens and tasks](../tokens.md) · [Parallel work and waiting](../parallel.md)

## The frame graph

A scene's rendering can be a frame graph: passes that declare what they read, write and create. The graph orders them,
leaves out what nothing uses, places transient resources in shared memory, and runs compute on the async queue where
it can.

[The frame graph](../frame-graph.md)

## Modules

A module is code with the hooks a scene has, loaded into the application. Koral ships linked ones — `koral-ui` for
interfaces, `koral-net` for networking, image and model import — and a project can write its own. The camera module
(`kcam`) and `Scene::Debug` are what most scenes start with.

[Modules, the camera and debug drawing](../modules.md) · [Interfaces with koral-ui](../ui.md) ·
[Input](../input.md) · [Mathematics](../math.md) · [Networking](../networking.md) ·
[Reflection and serialization](../reflection.md)

## Other languages

`koral_c.h` is the C++ API in plain C, object for object. The C# and Kotlin bindings are built on it, with the same
classes, builders and commands: a C# scene reloads while it runs, and a Kotlin one can draw its interface with Jetpack
Compose.

[The C interface](../c-api.md) · [C#](../csharp.md) · [Kotlin and Compose](../kotlin.md)
