# Interfaces with koral-ui

`koral-ui` (namespace `kui`, header `<koralUI.h>`) is a retained-mode interface library in three
layers, each usable without the ones above it:

1. **Elements**: a rectangle and a fragment shader that fills it. The base of everything else.
2. **The canvas**: lines, arcs, rectangles, circles, triangles, curves, paths, images and text,
   recorded into pictures and shown through layers.
3. **Widgets**: declarative building blocks laid out by constraints, rebuilt only where their state
   changed.

It is a module like any other: link it and it is there, leave it out and nothing of it is.

```cmake
target_link_libraries(MyScene PRIVATE Koral koral-ui)
```

## Widgets

A `kui::Ui` holds a widget tree. Give it a root, draw it with a `UiPass`, and update it each frame:

```cpp
#include <koralUI.h>

struct Counter : kui::StatefulWidget {
    int n = 0;
    kui::Widget Build() override {
        return kui::Column({
            kui::Text(std::format("Clicked {} times", n)),
            kui::Button("+", [this] { SetState([&] { ++n; }); }),
        }, { .gap = 8.f });
    }
};

class Menu : public kor::Scene {
    kui::Ui _ui { kui::Make<Counter>() };
    void Initialize() override { Graph().Add<kui::UiPass>(_ui); }
    void Update() override { _ui.Update(); }
};
```

`Update` hands the scene's pointer and keyboard to the widgets, rebuilds what `SetState` marked, lays
out what changed size and records what changed look. When nothing changed it does nothing, and the frame
uploads nothing. While the pointer is over a widget or text is being typed into one,
`Input::InterfaceWantsMouse()` / `InterfaceWantsKeyboard()` say so, so a camera stays put under a panel.

### Writing widgets

- A **`StatelessWidget`** describes itself from what it was given: override `Build() const`.
- A **`StatefulWidget`** keeps state in its members. `Build()` describes it from them, and
  `SetState(change)` applies a change and builds it again before the next frame. The instance first
  placed at a spot in the tree is the one kept there; when the parent builds again, the new instance it
  makes is offered to `DidUpdateWidget(newer)`, where the widget copies the configuration it takes from
  its parent. `InitState`, `Dispose` and `Animate(tick)` (called every frame until it returns false)
  complete it.
- Widgets are matched to what was there before by type and position. Give children a key with
  `std::move(widget).Key("id")` to keep each one's state when a list is reordered.

```cpp
struct Card : kui::StatelessWidget {
    std::string title;
    explicit Card(std::string t) : title(std::move(t)) {}
    kui::Widget Build() const override {
        const auto& theme = kui::Theme::Current();
        return kui::Container({ .padding = kui::EdgeInsets::All(12),
                                .decoration = { .color = theme.surface, .radius = 8 } },
                              kui::Text(title));
    }
};
```

### The building blocks

| Layout | |
|---|---|
| `Row`, `Column`, `Flex` | Children along an axis: `FlexOptions` for alignment, size and gap |
| `Expanded`, `Flexible` | A share of a row's or column's leftover space |
| `Container` | Padding, margin, decoration, size and alignment in one |
| `Padding`, `Align`, `Center`, `SizedBox`, `ConstrainedBox`, `DecoratedBox` | One job each |
| `Stack`, `Positioned` | Children on top of each other |
| `ScrollView` | A child larger than the box, scrolled by the wheel |
| `ListView(children)` | A scrolling list whose items each keep a layer of their own |
| `ListView(count, extent, builder, onRange)` | Only the items near the view exist: a million rows cost a screenful. `onRange`, if given, is told which items the list keeps |
| `LazyList(options, builder)` | The same, down or across, with items as long as they like (one not yet built is taken to be `estimatedExtent`), a gap between them and padding at the ends. `onScrolled` says which item is first in view and how far into it; `jumpIndex` puts one there when `jump` changes |
| `Intrinsic(width, height, child)` | The child as wide, or as tall, as it is with all the room there is: a column as wide as its widest child, so the others can be stretched to it |

| Painting and input | |
|---|---|
| `Text`, `Image` | |
| `CustomPaint(painter)` | Draw with the canvas, in the box |
| `ShaderBox(shader, params)` | An element shader filling the box |
| `GestureDetector` | Tap, pan, hover, enter, exit and scroll |
| `RepaintBoundary`, `Opacity` | A layer of its own: repaints alone; opacity costs nothing to change |
| `ClipRRect(radius, child)` | The child, cut to its box with rounded corners |
| `Translate(offset, child)` | The child, moved where it paints and is hit; layout unchanged. A click still reaches it only inside its parent's box |

| Controls | |
|---|---|
| `Button(child, onPressed)` | Any widget, made a button: a container that calls back when clicked. `ButtonStyle::ePrimary`, `eSecondary`, or `ePlain` (just the child, clickable). `Button("label", ...)` is the same holding text |
| `Checkbox`, `Switch`, `Slider`, `ProgressBar`, `TextField` | Styled by the Ui's `kui::Theme` (`Dark()`, `Light()`, or your own). A `TextField` with `controlled` set always shows its `text`, as a Compose or React field does |
| `Themed(theme, child)` | `child` in a theme of its own, whatever the Ui's is. `Theme` also says how round a button, a field and a checkbox are (`buttonRadius`, `fieldRadius`, `checkboxRadius`; negative: `radius`, and a circle). `QuerySystemAppearance()` is whether the system is dark, and its accent |
| `Text(text, style, align, wrap, maxLines, ellipsis)` | `TextStyle` has `weight` (400 as drawn, 700 bold: the font thickened), `italic`, `underline` and `lineThrough`; `maxLines` keeps so many lines, the last ending in an ellipsis when asked. `Slider(..., onFinished)` says when it is let go of; a `TextField`'s `focus`, when it changes, gives it the keyboard, and `Ui::ClearFocus()` takes it away |
| `RadioButton`, `Selectable` | One of several choices; a line of a list that can be picked |
| `DragValue`, `Dropdown` | A number changed by dragging across it; a field that opens a list under itself |
| `ColorPicker`, `ColorEdit` | A square of saturation and brightness over bars of hue and alpha; a swatch that opens one under itself |
| `ContextMenu(items, child)`, `MenuBar(menus)` | A menu where the right button is pressed; a row of titles that each open one |
| `CollapsingHeader`, `TreeNode` | A header that folds what is under it; a node of a tree, its children further in. Whoever builds them keeps whether they are open, and is told when that should change |
| `TabBar(tabs, selected, onSelected)` | A row of titles, the one in front underlined |
| `Tooltip(text, child)` | The text shows by the pointer while it is over the child |
| `SizeObserver(onChanged, child)` | Tells how big the child was laid out, in units and in pixels, when that changes: what a viewport resizes its texture by |
| `Modal(open, child, dialog)` | The dialog on a card over the child, which is dimmed and deaf while it shows |
| `Plot(values, options)` | A line through the values, or bars: frame times, a histogram |
| `Table(columns, rows)` | Rows of cells under columns that line up — fixed widths, or shares of what is left |
| `Separator`, `Disabled(child)` | A line between two things; a subtree faded and deaf to the pointer |
| `StepSlider(value, steps, onChanged)` | A slider that stops only at its steps: a wide rounded track, and in it a rounded thumb one step wide. `labels` writes a name in each step |
| `GradientEditor(stops, onChanged)` | A bar showing the gradient over a handle for each stop: press a handle to pick it, drag to move it, press the bar to add one, right-click to remove; a colour picker under it for the picked stop |
| `StatusBar(message, level)` | A bar along the foot of a window with the last thing said, after a mark of its level (info, warning, error), in the colour of the window behind the docked panels; `trailing` for what goes at its right end |

### Three ways to write it

A widget's look is given in whichever way reads best; they make the same thing and can be mixed.

```cpp
// Modifiers: each wraps the widget, so a chain reads inside out.
kui::Text("Hi").Padding(8).Background(surface, 12).OnTap(go)

// Designated initialisers: every options struct is a plain aggregate.
kui::Container({ .padding = kui::EdgeInsets::All(8), .decoration = { .color = surface, .radius = 12 } }, kui::Text("Hi"))

// Chained setters.
kui::Container(kui::ContainerOptions{}.SetPadding(kui::EdgeInsets::All(8)).SetDecoration(...), kui::Text("Hi"))
```

Order matters in a chain, as nesting does: padding before a background is inside it, padding after is
outside (a margin).

| Modifiers | |
|---|---|
| `Padding(all)`, `Padding(h, v)`, `Padding(insets)` | Space around it |
| `Background(color, radius)`, `Border(width, color, radius)`, `Shadow(color, blur, offset, radius)`, `Decorated(decoration)` | A box behind it |
| `Size(w, h)`, `Width(w)`, `Height(h)`, `Constrained(constraints)` | Its size |
| `Expanded(flex)`, `Flexible(flex)` | Its share of a Row or Column |
| `Center()`, `Align(alignment)`, `AlignSelf(alignment)`, `Positioned(options)` | Where it goes |
| `Opacity(o)`, `Clip(radius)`, `Offset(by)`, `Scrollable(axis)`, `RepaintBoundary()` | How it is shown |
| `OnTap(fn)`, `Gestures(options)` | The pointer |
| `Draggable(data)`, `OnDrop(type, fn)`, `DropTarget(options)` | Drag and drop |

C# has the same chain, as extension methods: `Text("Hi").Padding(8).Background(surface, 12).OnTap(Go)`.

Option structs are chainable as builders are, as well as filled in by name:
`kui::FlexOptions{}.SetGap(8).SetMainAxisAlignment(kui::MainAxisAlignment::eEnd)` is
`{ .mainAxisAlignment = kui::MainAxisAlignment::eEnd, .gap = 8 }`.

Layout is Flutter's: constraints go down, sizes come back up, and the parent places each child. A
render object whose size cannot affect its parent is a relayout boundary, so a change inside it lays out
nothing outside it.

**Everything fits its contents by default.** A Row or Column is as long as its children, and a Container
is as big as its child plus its padding. Only a few things fill the space they are given:

- **`Expanded`:** a weighted share along a Row or Column.
- **`CrossAxisAlignment::eStretch`:** across one.
- **`Align`, `Center`, or a Container with an alignment:** they fill in order to place their child, which
  is how something is centred on the screen.
- **`MainAxisSize::eMax`:** a Row or Column asked to be as long as allowed.

Controls with no content of their own (Slider, TextField, ProgressBar) are 200 units wide unless a
stretching parent makes them wider. A CustomPaint or ShaderBox without a size is as big as its child, or
nothing without one, unless the parent sets its size.

## Drag and drop

`Draggable` makes a widget something that can be picked up; `DropTarget` makes one somewhere it can be put.
A drag carries a `DragData`: a type, which targets go by, and a payload of any kind.

```cpp
kui::SizedBox(36, 36).Background(red, 6).Draggable({ "color", red })

kui::DropTarget(kui::DropTargetOptions{}
        .AcceptsType("color")
        .OnEnter([this](const kui::DragData&) { SetState([&] { hot = true; }); })
        .OnLeave([this] { SetState([&] { hot = false; }); })
        .OnDrop([this](const kui::DragData& d, kor::Vec2) { SetState([&] { fill = *d.As<kui::Color>(); }); }),
    Well(fill, hot))
```

- Moving the pressed pointer a little starts the drag. A ghost of the widget follows the pointer, or the
  `feedback` widget when `DraggableOptions` gives one.
- Of nested targets, the deepest that accepts the drag gets it. `OnEnter` and `OnLeave` are for showing it.
- Letting go anywhere else, or pressing Esc, cancels; `onDragEnd` hears whether a target took it.
- A drag can land in another `Ui`: one drawn over the first in the same window, or one in another window
  where the platform says where windows are (not Wayland).

## Docking

`DockSpace` fills what it is given with panels in tabs. The arrangement is a `DockLayout`, which lives
outside the widgets: make one, keep it, and give it to the `DockSpace` each build.

```cpp
auto layout = std::make_shared<kui::DockLayout>();          // a member of the scene
layout->Dock("scene")
       .Dock("inspector", kui::DockSide::eRight, "scene", 0.25f)
       .Dock("log", kui::DockSide::eBottom, "scene", 0.3f)
       .Dock("assets", kui::DockSide::eCenter, "log");       // a tab beside the log

kui::DockSpace(layout, {
    { "scene", "Scene", SceneView(), false },                // not closable
    { "inspector", "Inspector", kui::Make<Inspector>() },
    { "log", "Log", LogView() },
    { "assets", "Assets", Assets() },
})
```

The space is arranged as the tool windows of the JetBrains IDEs are. Down each side is a stripe of square
buttons, one glyph each: a docked panel's. A button opens its panel, in front of whichever of its area
was open; the button of the panel that is open folds the area away.

| Buttons… | Open their panels… |
|---|---|
| from the top of a stripe | down that side of the space. A side can be in several parts, one over the other, each showing one panel; a line between the buttons separates one part's from the next's (`Dock(panel, DockArea::eLeft, part)`) |
| at the foot of a stripe | along the bottom, which runs from one stripe to the other under both sides: the left stripe's in its left part, the right stripe's in its right (`DockArea::eBottomLeft`, `eBottomRight`) |

The middle is whatever the sides and the bottom leave: empty, it shows the scene and lets the pointer
through; panels docked there (`DockArea::eCenter`, or `Dock(panel)` as above) are tabs. An open panel has a
title bar — its title on the left; on the right a button that folds it away and, when it is closable, one
that closes it.

With the mouse:

| Drag a panel's button, or its title… | It… |
|---|---|
| onto a stripe, between a part's buttons | joins that part, there among its buttons |
| onto the line between two parts, or under the last | becomes a part of its own |
| to the foot of a stripe | goes to that end of the bottom |
| onto a docked panel | joins that panel's group, in front of it |
| onto the lower part of a panel docked down a side | docks under it, as a part of its own |
| into the left or right margin of the space (within 15% of that edge) | docks down that side: the margin is one band a level of the side, and one more — a level's band joins that level, the last makes a level of its own under them |
| into the bottom margin of the space | docks along the bottom: in its left part from the left half, its right from the right |
| into the middle (onto its title bar, or the 30% about its centre) | docks there, a tab among whatever else is in the middle |
| anywhere else in the space | floats there, where its title bar moves it and its corner resizes it |
| out of the window | floats over the desktop (one see-through, click-through window over every monitor) |

Between two areas is a gap (`DockOptions::gap`, 6 by default; `DockSpace(gap = 8.dp)` in Kotlin). Dragging it resizes them, and over it the pointer turns into the arrows that say which way.

**A panel keeps its state wherever it goes.** It is one widget that stays in one place in the tree; only
where it is shown changes. A stateful widget, a scroll position or a text field moved to another group, a
float or another window is still the same one. Panels in tabs that are not in front keep theirs too.

From code: `Dock`, `Float(panel, rect)`, `PopOut(panel, size)`, `Close`, `Open`, `Activate`, `IsOpen`,
`IsFloating`. `Save()` gives the arrangement as text and `Load(text)` takes it back;
`DockOptions::onChanged` says when to save, and `onClosed` which panel's × was clicked.

### Windows of their own

A panel pulled out of the window is still drawn and updated by the same `Ui`; the new window only shows
it, and its pointer and keys go to the same widgets. Closing that window docks the panel back.

- **On Windows, macOS and X11** the window opens under the pointer, and its tabs can be dragged back into
  the main window or into another panel's window.
- **On Wayland** a program cannot place its windows or learn where they are. The window opens where the
  compositor puts it, and its tab bar has a button that docks it back.
- `DockOptions::style` (a `DockStyle`; `DockSpace(style = DockStyle(...))` in Kotlin) is every size the space is drawn and handled with: title bar height, stripe width, button size and gaps, tab padding, the resize grip, the least a float or an area can be, the islands' corner radius, and how much of an edge or of the middle takes a drop. `gap` and `stripeGap` are beside it in the options.
- `DockOptions::multiViewport = false` keeps every panel inside the space: a tab dropped outside does nothing.
- It needs a `Ui` updated with `Update()` in a scene of a running `kor::App`. Offscreen, or with
  `Update(input, viewport, dt)`, `PopOut` floats the panel inside the space instead.

`modules/ui/samples/docking.cpp` (the `koral_ui_docking` target) has all of it in a window to try.

## The node editor

`kui::NodeEditor(graph, options)` (`kui/nodes.h`) shows nodes with typed ports and the wires between them.
Like the other controls it shows what it is given and reports what the user did: the graph is yours, and
each change arrives as a callback for you to apply and build again.

```cpp
kui::NodeGraph graph;
graph.nodes.push_back({ .id = "blur", .title = "Blur", .position = { 40, 40 },
                        .inputs = { { .id = "in", .label = "Image", .type = "image" } },
                        .outputs = { { .id = "out", .label = "Image", .type = "image" } },
                        .body = kui::DragValue(radius, onRadius) });        // any widgets, under the ports
auto editor = kui::NodeEditor(graph, kui::NodeEditorOptions{}
    .OnConnect([&](const kui::GraphWire& wire) { model.Connect(wire); })
    .OnMove([&](const std::vector<std::string>& nodes, kor::Vec2 by) { model.Move(nodes, by); })
    .OnDelete([&](const auto& nodes, const auto& wires, const auto& comments) { model.Delete(nodes, wires, comments); }));
```

| Does | How |
|---|---|
| Zoom, pan, frame | The wheel zooms about the pointer. The middle or right button drags the view. F frames what is picked, or everything. |
| Pick | Click a node, a wire or a comment. Shift or Control adds. A drag over nothing picks what its box touches. Control+A, Escape. |
| Move | Drag a node: everything picked moves. Drag a comment's title: it moves with the nodes inside it. Its corner resizes it. |
| Wire | Drag from a port to one it fits: the same type, or whatever `canConnect` says. Drag off a wired input to pick the wire up. A wire let go over nothing goes to `onWireDropped`. |
| Keys | Delete removes what is picked. Control+C, V and D copy, paste at the pointer and duplicate. |
| Menu | A right click on nothing calls `onContextMenu` with where in the graph it was. |

A node with an `error` is outlined in red and shows it. Where the view looks, how near, what is picked and
any drag under way are the editor's own, so panning, zooming and dragging build nothing again, and what is
out of view isn't drawn: 300 nodes cost under a millisecond a frame to drag. It is C++ only for now.

## The canvas

Every call returns the canvas, so drawing chains:

```cpp
kui::Canvas canvas;
canvas.DrawRRect({ kui::Rect::XYWH(20, 20, 160, 48), 12.f }, kui::Paint::Fill(kui::Color::Hex(0x3F51B5)))
      .DrawLine({ 20, 90 }, { 180, 90 }, kui::Paint::Stroked(kui::colors::White, 2.f))
      .Save()
      .Translate({ 100, 150 })
      .Rotate(0.3f)
      .ClipRRect({ kui::Rect::XYWH(-40, -40, 80, 80), 8.f })
      .DrawCircle({ 0, 0 }, 30.f, kui::Paint{}.SetGradient(kui::Gradient::Radial({ 0, 0 }, 30.f, { { 0, red }, { 1, blue } })))
      .Restore();

kui::Path star;            // MoveTo, LineTo, QuadTo, CubicTo, ArcTo, Close, AddRect, AddCircle ...
canvas.DrawPath(star, kui::Paint::Fill(yellow).SetStroke({ .width = 2, .color = orange, .join = kui::StrokeJoin::eRound }));

auto layer = kui::Layer::Create();
layer->SetPicture(canvas.Finish());
```

### Drawing with the pen

The canvas also draws a path a segment at a time, as an HTML canvas does. `BeginPath` starts it empty
and `MoveTo` starts a new part of it. `Fill` and `Stroke` draw the path as it stands and leave it, so one
path can be filled and then outlined:

```cpp
canvas.BeginPath()
      .MoveTo({ 10, 10 })
      .DrawLineTo({ 100, 10 })
      .DrawArcTo({ 120, 10 }, { 120, 30 }, 20.f)   // a rounded corner: towards (120, 10), turning to head for (120, 30)
      .DrawLineTo({ 120, 80 })
      .DrawQuadTo({ 60, 120 }, { 10, 80 })
      .ClosePath()
      .Fill(kui::Paint::Fill(blue))
      .Stroke(kui::Paint::Stroked(white, 2.f));
```

The pen has `DrawLineTo`, `DrawQuadTo`, `DrawCubicTo`, and two forms of `DrawArcTo`: one takes a centre,
a radius, a start and a sweep; the other is the rounded corner shown above.

- **Shapes:** `DrawRect`, `DrawRRect`, `DrawCircle`, `DrawOval`, `DrawArc` (stroked, or a pie),
  `DrawLine`, `DrawTriangle`, `DrawQuadraticBezier`, `DrawCubicBezier`, `DrawPolyline`,
  `DrawPolygon`, `DrawPath`, `DrawShadow`.
- **Images, text and elements:** `DrawImage`, `DrawParagraph`, `DrawText`, `DrawElement`.
- **Composing:** `DrawLayer` (another layer, shown as it is each frame) and `DrawPicture` (copied in).
- **`Paint`:** a fill (colour or linear, radial or sweep gradient), a stroke (width, colour, cap, join,
  miter limit) drawn over it, and an opacity.

Coordinates are logical units with y growing downwards; `Renderer::SetScale` / `ViewSettings::scale`
turn them into pixels.

Without widgets, draw a layer tree with a `kui::Renderer`, through the same `kui::UiPass`:

```cpp
kui::Renderer _ui;                 // a scene member
_ui.SetRoot(layer);
Graph().Add<kui::UiPass>(_ui);     // over the screen, or a graph image
```

### How it is drawn

Every built-in shape is one instance of a signed-distance shader, which gives an exact anti-aliased
edge at any scale and rotation. Glyphs come from a signed-distance atlas, so text at any size costs the
same. Each glyph's contours are united before its distances are taken, because fonts made from variable
ones (Inter among them) overlap their strokes. A line's baseline is put on the pixel grid unless the text
is turned or slanted, which keeps it sharp at fractional scales. On an sRGB target, dark text is covered as
if blended in sRGB, so black on white doesn't come out thin and grey. Paths, and strokes with joins, are made clean with Clipper2 (overlaps united under the fill rule,
strokes offset into one outline) and triangulated on their own vertices with a one-pixel fringe that
shares them. A translucent stroke never darkens where it crosses itself. A whole interface of shapes,
text and images is usually one instanced draw; tessellated paths and element shaders start draws of
their own where they fall in paint order.

**What a frame costs:**

- **A picture** does all its work (tessellation, text layout, conversion to GPU instances) once, when
  it is recorded.
- **A layer** owns slots in the frame's GPU tables.
  - If its picture changes, it rewrites only its own slots, plus a four-byte-per-instance draw order.
  - If it moves or fades (a scroll, an animation), only its row in the layer table is rewritten.
- **An unchanged frame** does no CPU work and uploads nothing.

## Element shaders

The base element: a rectangle the UI places, filled by a fragment shader written against a small
contract. The UI handles the quad, rounded corners, clipping, the anti-aliased edge and the colour
encoding.

```glsl
#version 450
#include <koralUI.glsl>

struct Params { vec4 from; vec4 to; float speed; };
KUI_PARAMETERS(Params)

vec4 kuiShade(KuiFragment f) {      // f.position, f.size, f.uv, f.pixel, f.time
    const Params p = kuiParameters(f);
    return mix(p.from, p.to, 0.5 + 0.5 * sin(f.uv.x * 6.0 + f.time * p.speed));
}
```

```slang
import koralUI;
struct Params { float4 from; float4 to; float speed; };
[[vk::binding(0, 1)]] StructuredBuffer<Params> parameters;

[shader("fragment")]
float4 fragmentMain(KuiVaryings v) : SV_Target {
    KuiFragment f = kuiFragment(v);
    Params p = parameters[f.parameters];
    return kuiFinish(f, lerp(p.from, p.to, f.uv.x));
}
```

```cpp
auto plasma = kui::ElementShader::Load("plasma.frag.glsl");      // or ("plasma.slang", "fragmentMain")
struct Params { kor::Vec4 from, to; float speed, pad[3]; };      // std430, as the shader declares it
canvas.DrawElement(plasma, rect, Params{ ... }, 12.f);             // 12: corner radius
kui::ShaderBox(plasma, Params{ ... })                             // the same, as a widget
```

Elements drawn with one shader are one instanced draw, each with its own parameters. Edits to the shader
file are picked up while running. The colour `kuiShade` returns is straight alpha in sRGB, like every
other colour in the UI.

## From C

`koralUI_c.h` is the C++ API object for object (`kui_<class>_<member>`), with koral_c.h's conventions:
failures in `koral_last_error()`, enumerations as their C++ values.

- **Handles:** shared C++ objects (pictures, layers, widgets, fonts, gradients, element shaders) are
  released with their `_release`. Canvases, paths, paragraphs, renderers and Uis are `_new`/`_destroy`.
- **Callbacks** are `{invoke, user, destroy}`: `destroy` is called once the C++ side lets the callback go.
- **Widgets written in C:** `kui_stateless_widget` and `kui_stateful_widget` take build callbacks and a
  `type` that tells kinds apart, as a C++ class would. `kui_state_set_state` rebuilds one.

`modules/ui/tests/test_capi.c` is a complete example.

## From C#

`Koral.UI` (shipped with koral-dotnet, so scripts can use it) is the C++ API in C#: the same names and
the same chaining. `using static Koral.UI.Widgets;` makes the building blocks read as they do in C++:

```csharp
using Koral.UI;
using static Koral.UI.Widgets;

public sealed class Counter : StatefulWidget
{
    int n;
    public override Widget Build() => Column([
        Text($"Clicked {n} times"),
        Button("+", () => SetState(() => ++n)),
    ], new FlexOptions().SetGap(8));
}

public sealed class Menu : Scene
{
    readonly Ui _ui = new(new Counter());
    protected override void Initialize() => Graph.Add(new UiPass(_ui));
    protected override void Update() => _ui.Update();
}
```

A `Ui` or `Renderer` made in a scene is that scene's, and is disposed with it. Anything made elsewhere
belongs to the application. Widgets, pictures, layers and paths need no disposing.

**Hot reload.** Run with `koral-dotnet <dir> --hot-reload` and edit a widget while it runs. A `Build`,
or a widget's constructor, is applied in place: every `Ui` builds its widgets again with the new code,
keeping their state, and the scene is not reopened. `samples/Settings` shows it.

In a project with implicit usings, `Koral.UI.Path` meets `System.IO.Path`: alias the one you mean
(`using Path = Koral.UI.Path;`). Scripts run by koral-dotnet do not import `System.IO`, so there it
just works.

## From Kotlin

The Kotlin bindings put Jetpack Compose on koral-ui: composables with trailing lambdas and modifier
chains, on Compose's own runtime. See [Kotlin and Compose](kotlin.md).

## Input it needs

Text fields read `kor::Input::TypedText()` (the code points typed this frame) and
`IsKeyRepeated(key)`. `ClaimInterface(claimer, mouse, keyboard)` is how an interface makes
`InterfaceWantsMouse/Keyboard` true. An offscreen scene is fed the same way with `FeedText` and
`FeedKeyRepeat`, which C (`koral_input_feed_text`) and C# (`Input.FeedText`) have too.

## Not yet

- **Docking:** a tab dragged to another window shows its landing zone there but not the tab under the pointer.
- **Drag and drop** does not go to or come from other programs, and the feedback is drawn only in the
  `Ui` the drag began in.
- **Text:** no clipboard, no selection, no input-method composition, and only the font's kern table
  (no complex shaping). A `TextField` is one line: there is no multi-line field.
- **Controls:** no vertical slider, no sortable or resizable table columns, and a `MenuBar`'s menus have
  no sub-menus or shortcuts.
- **Keyboard:** no moving between controls with the arrows.
- **The node editor** has no C, C# or Kotlin layer yet. A control inside a zoomed node that opens a popup
  (a dropdown) places it as if the node were not zoomed.
- **C#:** the controls added after `ContextMenu` have C functions but no C# wrappers yet.
- **Widgets from C#:** a C# `StatefulWidget` instance placed in two spots at once shares its state
  between them.

## Measuring it

`koral_ui_bench` (built with the UI tests, from `modules/ui/bench/bench.cpp`) draws a grid of cells at one
load after another, every cell changing and none, and prints what a frame costs: the interface's own CPU
work, and the whole frame with nothing waiting for the display. Run it from a Release build:
`koral_ui_bench 200`.

For a test, `kui::debug::Texts(ui)` is every text an interface shows — labels, values, what is typed into
fields — in the order of the tree: what a test reads an interface by.
