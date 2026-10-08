# Scenes in C#

Koral's C# API is the C++ API. It has the same classes, the same builders, and the same chained commands,
with the same names and the same behaviour, because every call is the C++ one. A C# builder's setters call a
C++ builder's setters, so what they check, what they warn about, and how a build is poisoned are the same.
It is managed code only: one set of assemblies for Windows, Linux and macOS, finding the SDK's native
library at run time.

```csharp
public sealed class Level : Scene
{
    [Keep] private Vec3 _player;                    // kept across a hot reload
    private GraphicsPipeline _pipeline = null!;
    private Mesh _mesh = null!;

    protected override void Initialize()
    {
        _pipeline = new GraphicsPipeline.Builder()
            .SetVertexShader(new Shader.Builder().SetPath("mesh.vert").GetOrBuild())
            .SetFragmentShader(new Shader.Builder().SetPath("mesh.frag").GetOrBuild())
            .SetDepthStencilState(new DepthStencilState { DepthCompareOp = CompareOp.eLessOrEqual })
            .Build();
        Input.BindAxis("MoveX", Key.eD, new InputSource(Key.eA, -1), GamepadAxis.eLeftX);
    }

    protected override void Update()
    {
        _player.X += Input.Axis("MoveX") * Time.FrameTime;
        Debug.Sphere(_player, 0.5f);
        if (Input.IsKeyPressed(Key.eEsc)) Navigator.Quit();
    }

    protected override void Render(CommandBuffer commandBuffer) =>
        commandBuffer
            .BeginRendering()
            .BindGraphicsPipeline(_pipeline)
            .PushConstant("model", KMath.Translation(_player))
            .BindMesh(_mesh)
            .DrawIndexed()
            .EndRendering();
}
```

## From C++ to C#

The rules are few, and they apply everywhere:

| C++ | C# |
|---|---|
| `kor::Image::Builder{}.SetFormat(f).Build()` | `new Image.Builder().SetFormat(f).Build()` |
| `kor::Buffer::Builder<Vertex>{}.SetData(v)` | `new Buffer.Builder<Vertex>().SetData(v)` — any unmanaged struct, as is; or `new Buffer.RawBuilder().SetData(values, GpuPacking.Std430)` for classes, records and a shader's std430/std140 layout (`ReadAs<T>`, `WriteValues`, `Gpu.Bytes` to match) |
| `kor::Resource<kor::Image>`, `kor::ResourceRef<const kor::Image>` | `Image`: a C# reference already is a handle |
| `image->Extent()` | `image.Extent`: an accessor with nothing to pass is a property |
| `commandBuffer.BindMesh(m).Draw()` | `commandBuffer.BindMesh(m).Draw()` |
| `kor::Image::Format::eRGBA8_UNORM` | `Image.Format.eRGBA8_UNORM`: the same enumerators, `e` and all |
| `Flags<Buffer::Usage>`, `a \| b` | a `[Flags]` enum, `a \| b` |
| `kor::Vec3`, `kor::Mat4`, `kor::Quat`, `kor::UVec2` | `Vec3`, `Mat4`, `Quat`, `UVec2`: the same types (they convert to and from System.Numerics' implicitly) |
| `kor::Dot(a, b)`, `kor::Perspective(...)` | `KMath.Dot(a, b)`, `KMath.Perspective(...)` — or `using static Koral.KMath;` and `Dot(a, b)` ([math](math.md)) |
| `Window::Extent()` in a scene | `Window.Extent` in a scene |
| `kor::log::Info("{} left", n)` | `Log.Info($"{n} left")` |
| `kor::Result<T>` / `VoidResult` | the value, or a `KoralException` with the error |
| `co_await token` | `await token` |

Where C# has something that suits the API better, it is used:

- **`using`**: a buffer mapping is released at the end of its block (`using var mapping = buffer.Map<Camera>()`),
  and a resource disposed at the end of its (`using var staging = new Buffer.Builder<byte>()...Build()`).
- **Implicit conversions** stand where C++ has implicit constructors. A `Key`, `MouseButton` or gamepad
  button or axis is an `InputSource`: `Input.BindAction("Jump", Key.eSpace, GamepadButton.eA)`. A
  `Vector4`, `IVec4` or `float` is a `ClearColor`, as the `std::variant` is. A `Framebuffer` is a `RenderInfo`.
- **`params`**: `BindAction` and `BindAxis` take their sources as arguments.
- **`await`**: see [Waiting](#waiting).
- **Attributes**: `[Keep]` marks state kept across a reload, and `[Scene("Name")]` names a scene class.

### Resources

A resource a builder made is owned by whatever made it. Made in a scene (its constructor or any hook), it
is the scene's and goes with it. Made anywhere else, it is the application's and goes when the
application does. Dispose it sooner with `Dispose()` or `using`. A resource looked up (a pass's
`ImageNamed`, an image's `View()`, a window's `DefaultFramebuffer`) is borrowed, like a `ResourceRef`:
disposing it lets go of the reference and nothing else. `Owned` says which it is.

As in C++, a build never throws. What cannot be built is `Poisoned`, with its `Failure` (code, message,
and the `History` of causes). It is logged once, and a shader or a pipeline made from one repairs itself
when the file is fixed. Passing a poisoned resource to a builder or a command poisons that too, naming
it as the cause. Calling a poisoned resource's own members throws a `KoralException` with the history.

`Buffer` shares its name with `System.Buffer`. Scripts `koral-dotnet` compiles import
`Buffer = Koral.Buffer` for you. A project of your own adds `<Using Include="Koral.Buffer" Alias="Buffer" />`
to its `.csproj`.

### Scenes

`Scene` has the C++ hooks as `protected override`s, and `Window`, `Input`, `Time` and `Debug` nested in it
as `Window::` and the rest are in C++: inside a scene, `Input.IsKeyPressed(Key.eSpace)` is the scene's
own input. From outside, `scene.SceneWindow`, `SceneInput`, `SceneTime` and `SceneDebug` are the objects
themselves.

`App.Open` and `OpenOffscreen` return the `Scene`: the C# scene itself when it is one, or a face on a C++
scene from a library (its members, not its hooks). A reload opens a new scene in the old one's place.
`IsOpen` tells a replaced scene from the new one, even at the same address, and `App.FindScene(name)`
finds the new one.

State kept across a reload is `State()`: an object whose public members are saved by name. When
`State()` returns null (the default), the scene's `[Keep]` fields and properties are saved instead.

### Passes

A `RenderPass` is a class with `Setup`, `Initialize`, `Prepare` and `Record`, added with
`Graph.Add(new MyPass())`, which returns it, as `Add<P>` does. `CpuPass` has `Run` instead of `Record`.
`DebugDrawPass` is Koral's own: `Graph.Add(new DebugDrawPass(SceneDebug, () => camera))`.

`Record` runs on a worker thread alongside other passes (it is `const` in C++). It should only read
what the pass holds; take what it needs from the scene in `Prepare`, which runs on the main thread.

### Waiting

A `Token` is awaitable. So is anything else a scene awaits, because each scene's hooks run with a
`SynchronizationContext` of its own: an `await` resumes at the start of a later frame, on the
application's thread, with the scene current again. That is what a `kor::Task` does around each
resumption.

```csharp
protected override async void Initialize()
{
    await CommandBuffer.SingleTimeCommand(cb => cb.GenerateMipmaps(_texture));
    _ready = true;                   // this scene's, on the application's thread
    await Task.Delay(1000);
    Window.SetTitle("a second later");
}
```

`Buffer.ReadAsync<T>()` is `Read<T>()` without the stall: `var hits = await _hits.ReadAsync<Hit>();`.
Writes and initial data never wait in the first place (see [Parallel work and waiting](parallel.md)).

An exception from an `async void` hook is reported like any hook's (logged, and `App.HookFailed`) rather
than ending the process.

## Running scenes: koral-dotnet

`koral-dotnet` is the runtime for C# scenes. Point it at a directory of `.cs` files, which it compiles
itself with no project file needed, or at a class library built with `dotnet build`:

```sh
koral-dotnet path/to/scenes              # a directory of .cs files
koral-dotnet path/to/bin/Game.dll        # or a built assembly
koral-dotnet path/to/scenes --hot-reload # edit while it runs
```

It reads `koral.json` and the runtime's flags exactly as the C++ runtime does (see
[Configuring a project](configuration.md)), from the scenes' directory up. It opens the scene `"scene"`
or `--scene` names, and the first one it finds when neither does. Every public, concrete class
deriving from `Koral.Scene` is a scene, registered under its class name or its `[Scene("Name")]`.
Scripts are compiled with `System`, `System.Numerics`, `System.Linq`, `System.Collections.Generic` and
`Koral` imported. A `.dll` beside them is referenced too.

With `--hot-reload`, a saved edit is picked up once the files have stopped changing, and does as
little as it can:

| The edit changes | What happens |
|---|---|
| Only method bodies (`Update`, `Record`, `Prepare`, a helper) | Applied to the running code in place, as .NET Hot Reload does: nothing reopens, and the next frame runs the new code. |
| A pass's `Setup` or `Initialize` | Applied in place, and that pass's graph set up again. |
| A scene's constructor or `Initialize`, or any constructor | Applied, and the scenes affected reopened in their windows, with their state. |
| Anything else: a field, a signature, a class, an initializer | A new build: every scene from the scripts reopened in its window, with its state. |

An edit that does not compile is reported (file, line and error) and changes nothing. A scene's state
across a reopen is its `[Keep]` members (or `State()`): a member added in the edit starts at its default,
one removed is dropped.

In-place updates need the process started with `DOTNET_MODIFIABLE_ASSEMBLIES=debug`; `koral-dotnet`
restarts itself with it when it is missing. The runtime does not allow them **while a debugger is
attached**: under one, every edit reopens its scenes instead, still in their windows with their state.
Run without debugging (Ctrl+F5 in VS Code) to have edits applied in place.

Each build is written to a temporary folder with its `.pdb` and loaded from there, so a debugger finds
its symbols and breakpoints in the scripts bind.

### From VS Code

The Hub writes `.vscode/` for a C# project, pointed at this machine's SDK and .NET (and so not committed):

- **F5** starts the scenes under the C# debugger; breakpoints in `src/` stop.
- **Ctrl+F5** starts them without it, with edits applied in place.
- **Terminal → Run Task → Koral: Run scenes** does the same from a terminal.
- `settings.json` points the C# extension at the .NET the Hub found, for completion.

An SDK staged on a machine with the .NET 10 SDK has it in `lib/koral-dotnet/`: `KORAL_BUILD_DOTNET`
is on by default there (`-DKORAL_BUILD_DOTNET=OFF` leaves it out). It needs the .NET 10
runtime: run it as `koral-dotnet`, or as `dotnet lib/koral-dotnet/koral-dotnet.dll`.

## Your own program

```csharp
using Koral;

using var app = new App();
app.Register<Menu>("Menu");
app.Register<Level>("Level");
app.Open("Menu", new WindowSettings { Title = "My Game" });
return app.Run();
```

Reference `bindings/csharp/src/Koral` (and `Koral.Scripting` to compile and reload scripts:
`new ScriptHost(app, directory)`, then `Load()` once and `Poll()` between frames). The native library
is found from, in order:

1. `KORAL_LIBRARY`: the library file itself;
2. `KORAL_SDK`: an SDK, whose `lib` (`bin` on Windows) holds it;
3. beside the application, or one directory up from it (where the SDK puts `koral-dotnet`);
4. the OS's own search: `PATH`, `LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`.

`new App(new AppSettings { Platform = WindowPlatform.eNone })` runs with no windowing system: offscreen
scenes only, on a machine with no display. The bindings' own tests run that way.

## Interfaces

`Koral.UI`, the C# binding of the koral-ui module, ships with koral-dotnet. Widgets, the canvas and
element shaders read as they do in C++, and editing a widget while it runs rebuilds it in place with its
state kept. See [Interfaces with koral-ui](ui.md#from-c-1).

Everything the module has is there, under its C++ name: `LazyList`, `Intrinsic`, `CustomLayout`, `Themed`,
`MenuBar` and `ContextMenu`, `Table`, `TreeNode`, `TabBar`, `Dropdown`, `DragValue`, `StepSlider`, `ColorPicker`,
`GradientEditor`, `Plot`, `Modal`, `TitleBar`, `StatusBar` and the rest. A `TextStyle` has `Weight`, `Italic`,
`Underline` and `LineThrough`, and with no colour of its own (`Color.Inherit`, the default) text is the theme's.
`Themes.Koral`, `Material`, `Cupertino` and `Windows` are the four families of theme, each dark or light and in
an accent — `Themes.Windows()` as Windows itself is set:

```csharp
var ui = new Ui(new Editor(), Themes.Material(dark: false, accent: Color.Hex(0x00897B)));
ui.SetTheme(Themes.Windows());
```

On Windows the managed assembly and the engine are both `Koral.dll`, and .NET loads the managed one first. A
native module that links against the engine would bind to the wrong one, so `Koral.UI` loads its module through
`NativeLibraryResolver.LoadBesideKoral`, which has the `Koral.native.manifest` beside the engine in force while
it does; a module loaded so is linked without a manifest of its own (`/MANIFEST:NO`).

## How it is built

The C# API sits on [the C interface](c-api.md), which is the C++ API object for object. Two scripts
in `bindings/csharp/tools` generate what must not drift. `generate_enums.py` generates every
enumeration from the C++ headers, and `generate_native.py` generates every P/Invoke and interop struct
from `koral_c.h`. A ctest fails when either is stale.

## Not yet

- **Modules, jobs, reflection and semantics.** `kor::Module`, `kor::Job` (use an `App` with
  `WindowPlatform.eNone`), `KORAL_REFLECT` (C# has reflection of its own: `[Keep]`, `State()`), and
  `SemanticSerializer` writes to descriptor sets are C++ only.
- **Verified only on Linux so far.** Nothing in the bindings is platform-specific, and the native library
  is found per OS, but Windows and macOS have not been run.
