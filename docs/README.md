# Koral documentation

- [Scenes and the application](scenes.md) — `kor::App`, scenes and their hooks, windows, offscreen
  scenes, views, navigation, shared state, scene libraries, hot reload with state.
- [Buffers, images and meshes](resources.md) — resources, memory types, uploads and readbacks, vertex layouts,
  matrices and instancing.
- [Shaders, pipelines and GPU features](pipelines.md) — shaders and reflection, pipelines, descriptor sets, push
  constants, and asking the device for features (`kor::Feature`).
- [Command buffers](commands.md) — recording, automatic barriers, End/Submit and threads, one-off work, work in
  the frame, timers.
- [Tokens and tasks](tokens.md) — events, timelines, coroutines, cancelling a task.
- [Modules, the camera, and debug drawing](modules.md) — writing and using modules, `kcam`, `Scene::Debug`.
- [The frame graph](frame-graph.md) — passes, resources, memory sharing, async compute, debug lines.
- [Interfaces with koral-ui](ui.md) — the retained UI module: element shaders, the canvas, widgets.
- [Input](input.md) — keys, mouse, gamepads, actions and axes, rebinding, feeding offscreen scenes.
- [Parallel work and waiting](parallel.md) — `kor::ParallelFor`, and uploads and readbacks that don't
  stall the CPU.
- [Mathematics](math.md) — `kmath.h`: vectors, matrices, quaternions, transforms and cameras, geometry and
  intersection, random numbers and noise, easing and splines, colour, vectorised bulk operations; the same in C,
  C# and Kotlin.
- [Reflection and serialization](reflection.md) — describing types, JSON, the inspector.
- [Scenes in C#](csharp.md) — the C# bindings, `koral-dotnet`, and scripts reloaded while they run.
- [Kotlin and Compose](kotlin.md) — the JVM bindings over java.lang.foreign, and interfaces in Jetpack Compose.
- [The C interface](c-api.md) — `koral_c.h`, for bindings to other languages.
- [Configuring a project](configuration.md) — `koral.json` and the runtime's flags.
- [Errors](errors.md) — `kor::Error`, `Result`, and how failures are reported.
- [v2 foundation design](v2-foundation-design.md) — tokens, threading and the scheduler.
