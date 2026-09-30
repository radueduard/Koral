# Input

Each scene has its own input: the keyboard, mouse and gamepads, as they were this frame while the
scene's window had them. Events go to the scene whose window they happen in — or one of the windows its
interface undocked — so two scenes in two windows never see each other's keys. Inside a scene it is
`Input::`; from outside, `scene.SceneInput()`.

```cpp
if (Input::IsKeyPressed(kor::Key::eSpace)) Jump();          // the frame it went down
if (Input::IsKeyHeld(kor::Key::eW)) MoveForward();          // every frame after, while down
glm::vec2 look = Input::MousePositionDelta();
```

A key or button is `ePressed` for the one frame it went down, `eHeld` while it stays down,
`eReleased` for the frame it came up, and `eNotPressed` otherwise.

## Gamepads

Up to `Input::MaxGamepads` gamepads, numbered from 0 in the order they were found, with the buttons and
axes of GLFW's gamepad mapping (an Xbox layout). Only the scene whose window has focus sees them, so a
window in the background is not steered by the pad.

```cpp
if (Input::IsGamepadButtonPressed(kor::GamepadButton::eA)) Jump();
float steer = Input::GamepadAxisValue(kor::GamepadAxis::eLeftX);       // -1 to 1, dead zone removed
float gas = Input::GamepadAxisValue(kor::GamepadAxis::eRightTrigger);  // 0 to 1
```

A stick reads 0 inside its dead zone (0.15 by default, `SetGamepadDeadZone`) and the rest of its travel
is stretched back over 0 to 1, so it does not jump as it leaves the zone.

## Actions and axes

What the scene means rather than which key: an **action** is down when any of its sources is, and an
**axis** is the sum of its sources' values, kept in [-1, 1].

```cpp
Input::BindAction("Jump", {kor::Key::eSpace, kor::GamepadButton::eA});
Input::BindAxis("MoveX", {{kor::Key::eD, 1.f}, {kor::Key::eA, -1.f}, kor::GamepadAxis::eLeftX});
Input::BindAxis("MoveY", {{kor::Key::eW, 1.f}, {kor::Key::eS, -1.f}});

if (Input::IsActionPressed("Jump")) Jump();
glm::vec2 move = Input::Axis2D("MoveX", "MoveY");   // no longer than 1: a diagonal is not faster
```

An axis source counts as down for an action past halfway — a trigger pulled, a stick pushed.

Every source has a name — `Key.Space`, `Mouse.Left`, `Gamepad.A`, `GamepadAxis.LeftX`, `-Key.A` for one
scaled by −1, `Key.W*0.5` for any other scale — and the bindings are saved and loaded by name:

```cpp
std::string saved = kor::ToJson(Input::Get().Bindings());
kor::InputBindings bindings;
kor::FromJson(bindings, saved);
Input::Get().SetBindings(bindings);
```

## Feeding a scene

A scene with no OS window of its own — an offscreen one — gets input from whoever shows it:
`FeedKey`, `FeedMouseButton`, `FeedMousePosition`, `FeedMouseDelta`, `FeedScroll`, `FeedGamepadButton`,
`FeedGamepadAxis`, `ReleaseAll`, or `FeedFrom(otherInput, keyboard, mouse)`. What is fed arrives at the
start of the scene's next frame, as a window's events do. `kgui::SceneView` feeds the scene it shows
while its panel is hovered (mouse) or focused (keys and gamepads).
