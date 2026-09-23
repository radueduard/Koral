# Configuring a project

A Koral project is configured by the `koral.json` at its root, not by a command line. The file says
which window to open, which backend to use, and — the part that matters most day to day — **where
the project keeps its assets and shaders**, so a scene can ask for `textures/wood.png` without
knowing where on disk the project ended up.

`koral.json` is also Koral Hub's project file. The Hub writes it, the runtime reads it, and the two
share no code: this schema is the whole contract between them.

```json
{
  "schemaVersion": 1,
  "name": "My Game",
  "rendering": {
    "api": "Vulkan",
    "platform": "auto",
    "window": {
      "width": 1280,
      "height": 720,
      "resizable": true,
      "fullscreen": false,
      "borderless": false,
      "transparent": false,
      "vsync": true,
      "imguiIni": "imgui.ini"
    }
  },
  "paths": {
    "assetDirectories":  ["assets"],
    "shaderDirectories": ["shaders"],
    "moduleDirectories": []
  },
  "modules": ["some-observer-module"]
}
```

Every key is optional. Drop the ones you do not care about; they keep whatever value the layer
beneath them set. `name` is the window title unless `rendering.window.title` overrides it.

The Hub's own keys — `color`, `frameworkVersion`, `kind`, `libraries` — mean nothing to the runtime
and are ignored, along with any key from a newer Hub than the runtime knows about.

## The three layers

Settings are applied in this order, each overriding the one before it:

1. **The scene library's own defaults.** A project may export
   `extern "C" kor::ProjectConfig* CreateProjectConfig()` alongside `CreateScene`. This is how the
   project wants to be run, compiled into the binary.
2. **`koral.json`.** What its author configured, changeable without recompiling. This is the normal
   place to put things.
3. **Command-line flags.** The last word, for overriding one option on one run.

So `--width 1920` beats the file, and the file beats the binary. Nothing is lost in between: a flag
that overrides the width leaves the height the file set exactly as it was.

## Where the config file is found

```
Koral_Runtime <scene_library> [options]
```

Without `--config`, the runtime looks for the nearest `koral.json` **at or above the scene library**.
A library builds into a subdirectory of its project, so the search walks up and lands on the config
at the project root:

```
my-game/
  koral.json                       <- found
  assets/
  shaders/
  cmake-build-debug/
    libmy-game.so                  <- what the runtime is handed
```

This is why launching a project takes no arguments beyond the library itself — from the Hub's ▶, from
`cmake --build --preset debug --target run`, from a CLion or VS Code Run, or from a bare shell. None
of them carry any settings, so none of them can disagree about them.

Failing that, the runtime searches the same way from the working directory. In full, the order is:

1. `--config <file>` — an explicit path. Naming a file that does not exist is an error, not a
   fallback: you asked for settings that would otherwise silently go unapplied.
2. `$KORAL_CONFIG` — the same, from the environment.
3. The nearest `koral.json` at or above the scene library.
4. The nearest `koral.json` at or above the working directory.

Running with no config at all is fine — the project then gets its compiled-in defaults, plus
whatever the flags say.

## Modules

Modules are optional engine features — shared libraries built against Koral's module API
(`include/module.h`). Seven ship with the engine, and a project links the ones it uses:

| Module | For |
|---|---|
| `koral-mesh` | Vertex formats and the meshes built from them |
| `koral-model-import` | Reading model files (glTF, FBX, OBJ…) into CPU-side arrays |
| `koral-image-import` | Reading images: PNG/JPEG/EXR/HDR, KTX/KTX2, and cubemaps |
| `koral-image-export` | Writing images: one subimage to any container, or a whole texture as KTX2 |
| `koral-image-compress` | Turning an image into a GPU-compressed KTX2 (a build-time step) |
| `koral-camera` | Camera objects and their controllers |
| `koral-gui-extras` | ImGui widgets on top of the engine's GUI: log and statistics panels, a viewport, a gizmo, a camera panel, a gradient editor, a file browser |

Asset loading in particular is *all* modules now: the engine itself reads no files but shaders and its
own config, so a project that generates its geometry and textures procedurally links none of the five
above and carries neither Assimp nor OpenImageIO.

**No module draws an interface.** A module has no opinion about what your application looks like, so
none of them opens a window or adds a panel — what a widget for one of them looks like is
`koral-gui-extras`' business, and drawing it is yours. `<koralCameraPanel.h>` is the case that used to
be otherwise: the camera module drew a "Cameras" panel from its own `RenderUI` hook, and it is now an
opt-in widget built entirely on the public camera API, so a project that wants a different one has
everything it needs to write it.

A module depends on another only when it actually calls into it: `koral-image-compress` reads its
input through `koral-image-import`, and says so, so linking the one brings the other. Where two
modules merely *meet*, the seam is a header instead, belonging to whoever includes it —
`<koralModelMesh.h>` pours imported geometry into a `koral-mesh` vertex format and is templates only,
so it needs both modules linked while neither library needs the other. Include it without `koral-mesh`
on the link line and it says so, rather than failing on a missing include.

**A module the project uses needs no configuration.** Link it (`target_link_libraries(MyScene
PRIVATE Koral koral-camera)`), include its header, and call its API; the library registers itself
with the runtime as it is loaded, and `modules` stays empty.

`modules` is for the other case: a module nothing links against, whose whole contribution is a
lifecycle hook — an observer, a profiler, a physics step nobody calls into. Each entry is either:

- a **bare name** (`"koral-camera"`), decorated for the platform (`libkoral-camera.so`,
  `koral-camera.dll`, `libkoral-camera.dylib`) and searched for in `paths.moduleDirectories`, then
  beside the scene library, then in the framework's own `modules/` directory; or
- a **path** (relative to the config file, or absolute), used as written.

Listing a module that is also linked is harmless — both routes are keyed on the module's id, so it
is loaded once either way.

Modules are loaded before the graphics device exists and initialized in the order their declared
dependencies require, so the order of this list does not matter. What does matter is completeness:
a module required by a listed module must be reachable too — by being linked, or by being listed
here — and a missing required dependency stops startup with an error naming both modules.

`paths.moduleDirectories` is searched before the framework's own modules, so a project can override
a shipped module by dropping its own build in one of these directories. Most projects need only the
bare names and no directories at all.

## Asset and shader directories

`paths.assetDirectories` and `paths.shaderDirectories` are the roots that **relative** paths are
resolved against, tried in the order listed. They apply to:

- `kimg::LoadImage` / `LoadImageAsync` / `LoadCubemap...` — textures, from the image import module.
  Output paths — what the export and compression modules write — are never resolved: a file you are
  writing is not a file you are looking for.
- `kmdl::Importer::Load` — models, from the model import module. A model's own material textures are
  looked for beside the model first, and if they are not there, across these same roots.
- Shaders: GLSL/SPIR-V paths, `#include`s, and Slang module imports.
- Anything that calls `kor::assetPath` or `kor::shaderPath` directly.

A relative directory is resolved **against the config file**, so the project can be moved or checked
out anywhere without the file needing an edit. An absolute one is used as written.

The config's roots are searched **before** the engine's own, and the engine's are never dropped:

```
"assetDirectories": ["assets", "../shared/assets"]

  resolving "textures/wood.png":
    1. <project>/assets/textures/wood.png
    2. <project>/../shared/assets/textures/wood.png
    3. <koral install>/assets/textures/wood.png     <- shipped with the engine
    4. ./textures/wood.png                          <- the working directory, last resort
```

That ordering means a project can shadow one of the engine's assets by giving a file the same name,
without thereby losing access to the rest of them — the GUI fonts and built-in shaders keep working.

The working directory is consulted only after every root has failed. Before the search roots existed
a relative path simply meant "relative to wherever you launched from", and this keeps code that still
does that working; because it is last, it can never shadow a project's own directories.

An **absolute** path handed to a loader is never resolved; it is opened exactly as given.

If a listed directory does not exist, the runtime warns at startup and carries on. That is almost
always a typo, and it is far easier to see it named in a warning than to work out later why a texture
never loaded.

For compatibility, the original singular form is still read:

```json
"paths": { "assetsDir": "assets", "shadersDir": "shaders" }
```

## Flags

| Flag                                 | Effect                                                                               |
|--------------------------------------|--------------------------------------------------------------------------------------|
| `--config <file>`                    | Config file to read.                                                                 |
| `--assets <dir>`                     | Prepend an asset search directory. Repeatable; the last one given is searched first. |
| `--shaders <dir>`                    | Prepend a shader search directory. Repeatable.                                       |
| `--module <name>`                    | Load an additional module, by name or path. Repeatable.                              |
| `--modules-dir <dir>`                | Prepend a module search directory. Repeatable.                                       |
| `--title <text>`                     | Window title.                                                                        |
| `--width <n>`, `--height <n>`        | Window size.                                                                         |
| `--api <name>`                       | `Vulkan` or `OpenGL`.                                                                |
| `--platform <name>`                  | Linux windowing system: `auto`, `x11` or `wayland` (ignored elsewhere).             |
| `--imgui-ini <file>`                 | Where ImGui saves its layout (default: beside `koral.json`).                         |
| `--fullscreen` / `--no-fullscreen`   | Open fullscreen.                                                                     |
| `--resizable` / `--no-resizable`     | Allow the window to be resized.                                                      |
| `--borderless` / `--decorated`       | Drop or keep the window decorations.                                                 |
| `--transparent` / `--no-transparent` | Transparent framebuffer.                                                             |
| `--vsync` / `--no-vsync`             | Wait for vertical blank.                                                             |
| `--help`                             | Print the above.                                                                     |

Flags are for a one-off run, not for wiring a project up — that is what the file is for.

Unlike a config directory, a directory given on the command line is relative to your **working
directory**: a path typed at a shell prompt means what it means there.

An unknown flag is an error and the runtime stops. Ignoring it would mean watching a setting fail to
take effect with no clue as to why.

## Windowing platform (Linux)

`rendering.platform` (or `--platform`) picks the windowing system a Linux build opens on: `x11`,
`wayland`, or `auto`. `auto` (the default) lets GLFW choose — normally Wayland when a Wayland session
is present, X11 otherwise. It is ignored on Windows and macOS, and a request for a platform this GLFW
build or session cannot provide falls back to automatic selection with a warning.

One exception: **OpenGL always runs on X11/XWayland**, because the OpenGL loader resolves entry points
through GLX and cannot drive a Wayland/EGL context. Selecting `wayland` together with the OpenGL
backend is ignored (with a warning); use Vulkan for a native Wayland window.

The platform also decides **ImGui multi-viewport** (dragging panels out into their own OS windows). It
is enabled everywhere *except* Wayland: viewports need the app to place a window at an absolute screen
position, and Wayland deliberately withholds global coordinates (GLFW reports the operation as
unavailable), so detached panels could not be placed. This is a positioning limit, not something a
borderless or transparent window works around — run on `x11` if you want viewports under Linux.

## ImGui layout file

`rendering.window.imguiIni` (or `--imgui-ini`) is where Dear ImGui persists its layout — window
positions and docking. Left unset, it defaults to `imgui.ini` **beside `koral.json`**, so each project
keeps its own layout and it is found again whatever directory you launch from — rather than ImGui's
default of dropping `imgui.ini` into the working directory. A relative path in the file resolves
against the config's directory (against the working directory for the flag), and any missing parent
directories are created. With no config file at all, ImGui keeps its own default.

## Compatibility

`schemaVersion` names the schema the file was written against. A runtime reading a *newer* file warns
once and ignores the keys it does not know, rather than refusing to start — new keys are only ever
additions. Unknown keys are ignored for the same reason.

Because the Hub and the engine ship separately and share nothing but this file, a schema change has
to be made on both sides. The engine's guard is
`ProjectConfig.ParsesADocumentInTheShapeTheHubWrites` in `tests/test_project_config.cpp`: a document
written exactly as the Hub's `model.rs` serializes it. Keep it in step, or Hub-launched projects
break with nothing in either build to say so.
