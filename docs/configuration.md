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
    "window": {
      "width": 1280,
      "height": 720,
      "resizable": true,
      "fullscreen": false,
      "borderless": false,
      "transparent": false,
      "vsync": true
    }
  },
  "paths": {
    "assetDirectories":  ["assets"],
    "shaderDirectories": ["shaders"]
  }
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

## Asset and shader directories

`paths.assetDirectories` and `paths.shaderDirectories` are the roots that **relative** paths are
resolved against, tried in the order listed. They apply to:

- `Importer::LoadImage` / `LoadImageAsync` — textures.
- `Importer::Load` — models. A model's own material textures are looked for beside the model first,
  and if they are not there, across these same roots.
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

| Flag | Effect |
|------|--------|
| `--config <file>` | Config file to read. |
| `--assets <dir>` | Prepend an asset search directory. Repeatable; the last one given is searched first. |
| `--shaders <dir>` | Prepend a shader search directory. Repeatable. |
| `--title <text>` | Window title. |
| `--width <n>`, `--height <n>` | Window size. |
| `--api <name>` | `Vulkan` or `OpenGL`. |
| `--fullscreen` / `--no-fullscreen` | Open fullscreen. |
| `--resizable` / `--no-resizable` | Allow the window to be resized. |
| `--borderless` / `--decorated` | Drop or keep the window decorations. |
| `--transparent` / `--no-transparent` | Transparent framebuffer. |
| `--vsync` / `--no-vsync` | Wait for vertical blank. |
| `--help` | Print the above. |

Flags are for a one-off run, not for wiring a project up — that is what the file is for.

Unlike a config directory, a directory given on the command line is relative to your **working
directory**: a path typed at a shell prompt means what it means there.

An unknown flag is an error and the runtime stops. Ignoring it would mean watching a setting fail to
take effect with no clue as to why.

## Compatibility

`schemaVersion` names the schema the file was written against. A runtime reading a *newer* file warns
once and ignores the keys it does not know, rather than refusing to start — new keys are only ever
additions. Unknown keys are ignored for the same reason.

Because the Hub and the engine ship separately and share nothing but this file, a schema change has
to be made on both sides. The engine's guard is
`ProjectConfig.ParsesADocumentInTheShapeTheHubWrites` in `tests/test_project_config.cpp`: a document
written exactly as the Hub's `model.rs` serializes it. Keep it in step, or Hub-launched projects
break with nothing in either build to say so.
