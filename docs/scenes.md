# Scenes and the application

A Koral program is a `kor::App` running **scenes**. A scene is one screen of the program — a menu, a
level, an editor — and it owns everything that belongs to it: its window, its input, its clock, its
render passes, its interface and its debug lines. An application runs any number of them, each in a
window of its own, and switches what a window shows between them.

The runtime (`Koral_Runtime`) is itself one such program: it reads `koral.json`, loads the project's
scene library and opens the scene the project names. A program of your own can do the same from its
`main` — Koral is a framework as well as an engine:

```cpp
int main() {
    kor::App app;                                // the device, the frames, the modules
    app.Register<MenuScene>("Menu");
    app.Register<LevelScene>("Level");
    app.Open("Menu", {.title = "My Game"});
    return app.Run();                            // until the last window closes
}
```

## A scene

```cpp
class Level final : public kor::Scene {
public:
    explicit Level(const kor::SceneArgs& args) : _map(args.String("map", "default")) {}

    void Initialize() override { Window::SetTitle("Level: " + _map); }          // resources go here
    void FixedUpdate() override { _physics.Step(Time::FixedDeltaTime()); }      // steady rate
    void Update() override {
        if (Input::IsActionPressed("Pause")) kor::Navigator::Push("Pause");
        _player.Move(Input::Axis2D("MoveX", "MoveY") * Time::FrameTime());
        Debug::Box(_player.bounds.min, _player.bounds.max, {.color = {0, 1, 0, 1}});
    }
};
```

The hooks, in the order a frame calls them:

| Hook | When |
|---|---|
| `Initialize` | Once, with the window there. Where resources are made. |
| `OnResize(extent)` | The frame after the window changed size. |
| `FixedUpdate` | Once per `Time::FixedDeltaTime()` of the scene's time (0 to 8 times a frame). |
| `Update` | Once a frame. |
| `LateUpdate` | After every module's late update. |
| `Render(commandBuffer)` | Records the frame's own work; the scene's graph runs ahead of it. |
| `OnSuspend` / `OnResume` | Another scene was pushed over this one / popped off it. |
| `OnCloseRequested` | The window was asked to close; return false to keep it open. |
| `Shutdown` | Once, before the scene is destroyed, with everything still alive. |

Inside a scene, `Window::`, `Input::`, `Time::` and `Debug::` are the scene's own. They find it
through `Scene::Current()`, which the application sets around every hook, the frame graph around every
pass it records, and a `kor::Task` around every resumption of a coroutine started in the scene — so a
loader that moved to a background thread still means its own scene's window. From outside a scene,
use `scene.SceneWindow()`, `SceneInput()`, `SceneTime()` and `SceneDebug()`.

### Time

Each scene has its own clock. `Time::SetTimeScale(0)` pauses one scene (its `FrameTime()` and its
fixed steps stop) without touching any other. `FixedUpdate` runs in steps of `FixedDeltaTime()` —
1/60 s unless `Time::SetFixedDeltaTime` — as many as the scene's time moved on by, at most
`Time::MaxFixedSteps` a frame. Inside it, `Time::FrameTime()` is the step, and
`Time::FixedStepFraction()` says how far between steps the frame is, for drawing smoothly.

### An interface

An interface is a module's business, not the framework's: a scene that wants one links koral-ui and
keeps a `kui::Ui`, drawn by a pass in its graph and fed the scene's input each frame:

```cpp
class Editor final : public kor::Scene {
    kui::Ui _ui { kui::DockSpace(_layout, {{"tools", "Tools", Tools()}}) };
    void Initialize() override { Graph().Add<kui::UiPass>(_ui); }
    void Update() override { _ui.Update(); }
};
```

While the pointer is over a panel, or text is being typed, `Input::InterfaceWantsMouse()` and
`InterfaceWantsKeyboard()` say so, and a camera stays put. See [ui.md](ui.md), and koral-gui-extras for
ready-made panels: a log, statistics, an inspector, viewports, a frame graph's own.

## Windows

`App::Open` (or `Navigator::Open` from inside a scene) opens a scene in a new OS window. Every window
is drawn in the same frame and presented together.

### Offscreen scenes

`App::OpenOffscreen` opens a scene in an **offscreen window**: an image rather than an OS window. To the
scene it is the same — its default framebuffer, the frame graph's `Screen` — but what it draws stays in
`Window::Image()`, for another scene to show or the program to read. Offscreen scenes are drawn first
each frame, so a scene showing one shows this frame's picture.

```cpp
_game = kor::Navigator::OpenOffscreen("Level", {.extent = {1280, 720}});
// in the interface: shows it, sizes it to the panel, feeds it input
kgui::SceneView(*_game)
```

An offscreen scene has no input of its own; whoever shows it feeds it (`Input::FeedKey` and the
rest, or `kgui::SceneView`, which does it while the panel is hovered or was last clicked).

An application that opens only offscreen scenes can run with `AppSettings::platform =
WindowPlatform::eNone`: no windowing system, no display needed — a server, a test, a batch render.

## Views

One scene can be drawn several times: `Scene::AddView` gives it another frame graph drawing into an
image of its own. An editor's scene view and game view, a preview, split screen — one world, updated
once, drawn from several cameras.

```cpp
void Editor::Initialize() {
    auto& game = AddView("Game", {.extent = {1280, 720}});
    game.Graph().Add<ForwardPass>(_world, _gameCamera);
}
// and in the interface: kgui::SceneView(*FindView("Game"))
```

Inside a view's passes, `Window::` is the view's target. Views are drawn after `LateUpdate` and before
the scene's own `Render`, so the scene can show them the same frame.

## Navigation

`kor::Navigator` changes what the current scene's window shows, after the frame:

| Call | Effect |
|---|---|
| `Open(name, window, args)` | Opens `name` in a new window (immediately). |
| `OpenOffscreen(name, target, args)` | Opens `name` offscreen (immediately). |
| `Replace(name, args)` | Shows `name` instead; the current scene is shut down. |
| `Push(name, args)` | Shows `name` over it; the current one is suspended until `Pop`. |
| `Pop()` | Shuts the current scene down and shows the one under it. |
| `Close()` | Closes the window and every scene in it. |
| `Quit()` | Closes every window. |

Arguments are `kor::SceneArgs`: named strings, read back as what they are (`String`, `Number`,
`Integer`, `Flag`), so a scene can be opened by name from a menu, a config file or another language.

## Sharing state between scenes

`App::Shared<T>(key, args...)` hands every scene that asks for `key` the same object, made by the first
to ask, alive for as long as anyone holds it — an editor and the game it runs offscreen looking at one
world, a menu and a level sharing a profile:

```cpp
_world = kor::App::Current().Shared<World>("level");
```

## Scene libraries

A project's scenes live in a shared library that exports a table of them:

```cpp
#include <sceneLibrary.h>

KORAL_SCENES(
    KORAL_SCENE("Menu",  MenuScene),
    KORAL_SCENE("Level", LevelScene),   // LevelScene(const kor::SceneArgs&) gets the arguments
)
```

`App::LoadLibrary` registers every scene in it; `UnloadLibrary` closes the windows showing them and
unloads it; `ReloadLibrary` loads it again — rebuilt — and reopens those scenes. The table is plain C
and versioned (`KORAL_SCENE_ABI_VERSION`), so a library built against a different `kor::Scene` is
refused with a message to rebuild it rather than crashing. (A library exporting the older `CreateScene`
still loads, with a warning: it cannot be checked.)

### State that survives a reload

A scene says what state it keeps by returning a reflected member from `State()` (see
[Reflection and serialization](reflection.md)):

```cpp
struct Progress { glm::vec3 camera; int level = 1; };
KORAL_REFLECT(Progress, camera, level)

class Game final : public kor::Scene {
    Progress _progress;
    kor::Ref State() override { return _progress; }
};
```

`ReloadLibrary` saves it before the old code goes, as JSON by field name, and loads it into the new
scene after its constructor and before `Initialize`. A field added in the rebuild keeps its default;
one removed is dropped. `SaveState()` / `LoadState()` are the same thing on demand — for a save game,
or an editor's play-mode snapshot.

The runtime does this on its own with `--hot-reload`: it watches the scene library and, once a rebuild
has finished writing it, reloads it with every scene's state kept.

## Modules

Modules (the camera, the GUI extras, your own) are process-wide: linked or loaded once, initialised
before the first scene opens, and their hooks run around each scene's, with that scene current.
