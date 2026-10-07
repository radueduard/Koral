# Modules, the camera, and debug drawing

## Modules

A module is a library that extends the runtime: it gets hooks into every frame of every scene, and a public API
that scenes call. The modules shipped with Koral are the camera (`kcam`), meshes (`kmesh`), images (`kimg`),
model import (`kmdl`), the UI (`kui`), and GUI extras.

**Using one** is linking it: `target_link_libraries(MyScene PRIVATE Koral koral-camera)`. The library registers
itself with the runtime when it is loaded. Nothing has to be named in `koral.json`, except for a module that
nothing links against (`"modules": [...]`).

**Writing one** is a class deriving from `kor::Module`, declared once:

```cpp
class Weather final : public kor::Module {
public:
    static constexpr std::string_view ModuleId = "weather";
    static constexpr std::uint32_t ModuleVersion = 1;
private:
    void Initialize() override;                         // after the device, before the scene's Initialize
    void Update() override;                             // every frame, before Scene::Update
    void Render(kor::CommandBuffer& cb) override;       // every frame, before Scene::Render
    void Shutdown() override;                           // before the device goes
};
KORAL_DECLARE_MODULE(Weather);
// or KORAL_DECLARE_MODULE_DEPS(Weather, kor::Dependency{ "camera", 1 }) when it needs another module
```

The hooks are `Initialize`, `FixedUpdate`, `Update`, `LateUpdate`, `Render`, `RenderOverlay`, `OnResize` and
`Shutdown`. They run for each scene, with that scene current. Modules are started in dependency order and shut
down in reverse. A module that needs a GPU feature asks for it like any library (see
[GPU features](pipelines.md#gpu-features-beyond-korals-own)).

## The camera module

```cpp
_player = kcam::PerspectiveCamera::Builder{}
    .SetFovY(glm::radians(70.f))
    .SetPosition({ 0.f, 1.5f, 5.f })
    .LookAt({ 0.f, 0.f, 0.f })
    .SetController({ .kind = kcam::Controller::Kind::eFly })   // moved by the runtime, before the scene's Update
    .SetFollowWindowAspect(true)                               // its aspect kept to the window's
    .Build();

cb.PushConstant("viewProjection", _player->ViewProjection());
```

A camera is an ordinary resource owned by the project. There is no "main camera". Matrices are computed when
read, so setters cost nothing to repeat. A controller or aspect tracking hands the camera to the runtime, which
updates it each frame. Without them the camera is entirely manual. `kcam::OrthographicCamera` is the same with
bounds. A shader can also get the camera's matrices by semantic, without any binding code.

## Debug drawing

Lines and simple shapes, drawn on top of the scene for a frame (or for a `duration` in their `Style`):

```cpp
Debug::Box(aabb.min, aabb.max, { .color = { 1, 0, 0, 1 } });
Debug::Sphere(probe, 0.2f, { .fill = { 0, 1, 0, 0.3f } });
Debug::Arrow(origin, origin + velocity);
Debug::Sphere(target, 0.5f, { .duration = 2.f });
Debug::Frustum(light.ViewProjection());
```

The shapes are `Line`, `Box`, `Circle`, `Sphere`, `Arrow`, `Point`, `Axes`, `Grid`, `Frustum`, `Triangle`, `Quad`,
`Plane`, `Cylinder`, `Cone`, `Capsule`, and icons for cameras and point, spot and directional lights. `Style` sets
`color`, `lineWidth`, `duration` (seconds; 0 is one frame), `onTop` (drawn over everything, not hidden by depth),
and a `fill` colour with or without an `outline`.

`Debug::Gizmo(mode, transform, viewProjection)` draws a translate, rotate or scale handle that the mouse drags,
and edits `transform` in place. It returns whether it changed. `GizmoActive()` and `GizmoHovered()` tell the
rest of the scene to leave the mouse alone.

Add a `kor::DebugDrawPass` to the frame graph to choose where they are drawn: the target, the depth buffer they
test against, and the view-projection.
