# Kotlin and Compose

Koral runs on the JVM through `java.lang.foreign` (JDK 25). Every call goes straight to `koral_c.h` and
`koralUI_c.h`, with no JNI and no glue library.

- **The C++ API, in Kotlin**, as the C# one is: the same classes, builders and chained commands, named in
  Kotlin's lowerCamel (`Shader.Builder().setPath(...)`, `commands.bindMesh(mesh).drawIndexed()`).
- **Interfaces in Jetpack Compose**, on Compose's own runtime and compiler plugin: `@Composable` functions,
  `remember { mutableStateOf(...) }`, trailing lambdas and modifier chains. An `Applier` turns what they
  emit into koral-ui widgets.
- **Edits applied while it runs**, keeping state (see [Hot reload](#hot-reload)).

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    Column(Modifier.padding(20.dp).background(Color(0xFF22252B), RoundedCornerShape(12.dp)),
           verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Clicked $count times")
        Button(onClick = { count++ }) { Text("Add one") }
    }
}

class Menu : Scene() {
    override fun initialize() { setContent { Counter() } }
}

fun main() {
    App().use { app ->
        app.register("Menu") { Menu() }
        app.open("Menu", WindowSettings(title = "Menu"))
        app.run()
    }
}
```

The whole sample is `bindings/kotlin/samples/counter`. Run it with `./gradlew :samples:counter:run`.

## In Koral Hub

**New Project → Kotlin** makes a Gradle project with a scene, a Compose panel and
`fun main(args: Array<String>) = App.launch(args) { register<MyScene>() }`. The choice is offered when the
chosen framework's SDK was built with its Kotlin bindings.

- **▶** runs it with hot reload (Gradle's `hotRun`). On the JetBrains Runtime any edit applies; on another
  JDK, edits to what methods do apply.
- **Build** compiles it. The first build downloads Gradle and the libraries.
- **Open in IDE:** IntelliJ IDEA opens the Gradle build as it is; for VS Code, the Hub writes a run task
  and recommends the Kotlin extensions.

The Hub finds JDK 25 and the JetBrains Runtime where installers put them (and in `JAVA_HOME`, `KORAL_JBR`
and `~/.local/jdk`). With no JDK 25 on the machine, the first build downloads Eclipse Temurin into the
Hub's own tools (its Toolchain window lists it, and .NET for C#, beside CMake and Ninja). It writes them, with where the SDK is, into a `gradle.properties` that git ignores.
`App.launch` reads koral.json as the C++ runtime does, and opens its `scene` (or the first registered).

## Using the SDK

An SDK built with `-DKORAL_BUILD_KOTLIN=ON` carries the libraries as a Maven repository, `share/Koral/maven`.
A project depends on them like any other library:

```kotlin
plugins {
    kotlin("jvm") version "2.4.20"
    kotlin("plugin.compose") version "2.4.20"
    application
}
repositories {
    maven(url = "${System.getenv("KORAL_SDK")}/share/Koral/maven")
    mavenCentral()
}
dependencies { implementation("koral:koral-ui:0.1.0") }   // koral:koral alone, without interfaces
application { applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED") }
```

At run time, `KORAL_SDK` is also where the native libraries are found.

## Building

`bindings/kotlin` is a Gradle build of its own, with the wrapper included. It needs:

- **JDK 25.** Set `JAVA_HOME` to it.
- **Koral's library**, found from one of these, in order:
  - `KORAL_LIBRARY`: the file itself;
  - `KORAL_SDK`: an SDK, whose `lib/` is used;
  - the working directory.
- **koral-ui**, from `modules/` beside Koral's library, or from the file `KORAL_UI_LIBRARY` names.

Programs that run Koral need `--enable-native-access=ALL-UNNAMED`. On macOS a window opens only on the process's
first thread, which a JVM's `main()` is not on: `App.launch` runs the program there by itself, and a program that
makes its `App` itself does so inside `onFirstThread { }` — or is started with `-XstartOnFirstThread`, as this
build starts the sample. Otherwise opening the application throws, saying so.

Pointer positions (`pointerInput`) and `onSizeChanged` sizes are in pixels, as Compose's are: on a Retina display
twice the layout's units, so a place over a viewport is a place in its image.

| Project    | What it is                                                                                     |
|------------|------------------------------------------------------------------------------------------------|
| `:koral`    | The application and its scenes, resources, commands and the frame graph; hot reload.           |
| `:koral-ui` | Compose on koral-ui (`koral.compose`), and koral-ui's C interface (`koral.ui.interop`).        |
| `:tests`    | End-to-end tests on a real device with no display, each test class in a JVM of its own.        |

CMake builds the bindings by default wherever it finds a JDK 25 or later: in `KORAL_JAVA_HOME`, `JAVA_HOME`,
or where installers and the JetBrains IDEs put them (`-DKORAL_JAVA_HOME=<jdk>` names one,
`-DKORAL_BUILD_KOTLIN=OFF` leaves them out). A build tree configured before this keeps the `OFF` it cached,
until it is configured with `-DKORAL_BUILD_KOTLIN=ON`. Building them does three things:
- it publishes the libraries into the SDK's Maven repository (`stage`, `install`);
- `Kotlin.Bindings`, in ctest, runs the Gradle tests against the build's Koral (on the JetBrains Runtime too,
  when `KORAL_JBR` names one);
- `Kotlin.InteropMatchesTheCInterface`, in ctest, checks the generated interop.

## Hot reload

Edits are applied to the running program in place, as C#'s are. A changed class is redefined, so every
object keeps its fields, and the new code runs from the next frame:

```
./gradlew :samples:counter:hotRun
```

Save a source file and the program does the rest:
1. It runs the build's compile command.
2. Between two frames, it redefines the classes that changed.
3. It recomposes every Compose interface.

What a composable `remember`s survives the reload, with one exception. A composable whose own code changed
starts afresh: an added call would otherwise read the wrong remembered value. Everything around it keeps its
state. In the sample, retitling the panel or adding a line to it keeps the count, because the count is
remembered in `Counter`, not in the column's content.

How much can change depends on the JVM:

| JVM                                                                     | Edits it applies                                                      |
|-------------------------------------------------------------------------|-----------------------------------------------------------------------|
| Any JDK 25                                                              | What methods do: text, colours, numbers, logic, a composable's arguments |
| [JetBrains Runtime](https://github.com/JetBrains/JetBrainsRuntime) 25 (`KORAL_JBR`), run with `-XX:+AllowEnhancedClassRedefinition` | Anything, including added or removed methods, fields, lambdas and composable calls |

On a standard JDK, an edit that changes a class's shape is reported and nothing changes. Adding a composable
call or a lambda usually changes the shape. Restart to apply such an edit.

A `hotRun` task needs three things, all passed by the sample's `build.gradle.kts`:

- **Koral's jar as the JVM agent:** `-javaagent:koral.jar`. It is what redefines classes.
- **`koral.hotReload=true`.** The environment variable `KORAL_HOT_RELOAD=1` also works.
- **What to watch:**
  - `koral.hotReload.sources`, the source directories, together with `koral.hotReload.compile`, the command
    that compiles them;
  - or neither of the two: then the class directories on the class path are watched, for an IDE or a
    `--continuous` build to write into.

Code can apply class files itself with `HotReload.apply`, which the tests use. It can also be told about every
reload through `HotReload.onReloaded`.

## The interop is generated

`tools/generate_kotlin.py` reads `koral_c.h`, `koralUI_c.h` and the C++ enumerations, and writes:

- `koral/interop/KoralNative.kt` and `koral/ui/interop/KuiNative.kt`: struct layouts with C's padding, and
  one downcall per function. Strings are converted on the way in, and struct returns take a `SegmentAllocator`.
- `koral/Enums.kt` and `koral/ui/Enums.kt`: the same names and values as C++ (`Key.eSpace`).

Run it again after changing a C header. `--check` fails when the files are out of date.

## Scenes

A scene subclasses `Scene` and overrides the hooks it needs:
- `initialize`, `fixedUpdate`, `update`, `lateUpdate`, `render(commands)`;
- `onResize`, `onSuspend`, `onResume`, `onCloseRequested`;
- `shutdown`, and `saveState`/`loadState` for `App.reloadScenes`.

It reaches its own `window`, `input`, `time`, `debug` and `graph`, and can add `views`: frame graphs of their
own, drawing into images. Inside its code, `Window.current`, `Input.current`, `Time.current` and
`Debug.current` are the scene's, as `kor::Window::` and the rest are in C++.

`App` registers scenes by name, or by class (`app.register<Menu>()`, named by `@SceneName` or the class
name). They are opened in windows, or offscreen at a size of their own (which is how the tests run them), and
`Navigator` moves between them. `App.shared(key) { ... }` makes one object for every scene that asks.

Native objects follow Koral's ownership rule. Anything a scene makes while it is being made, or in one of its
hooks, belongs to that scene and is closed when the scene shuts down. Anything else belongs to the application.
Several applications may be made one after another in a process, but only one at a time.

### Waiting without stalling

`scene.launch { }` starts a coroutine of the scene, as C# awaits in a hook. It resumes between frames,
on the frame's thread, with the scene current again, and it is cancelled when the scene shuts down:

```kotlin
override fun initialize() {
    launch {
        CommandBuffer.singleTimeCommand { it.generateMipmaps(texture) }.await()   // a kor::Token
        val hits = results.readAsync(GpuLayout.Int)                              // no stall
        delay(500)
    }
}
```

## Resources and commands

Builders are C++'s: chained, and poisoning rather than throwing. A failed build makes a resource that says
why (`isPoisoned`, `failure`); a pipeline made from a shader that does not compile names it as the cause, and
repairs itself when the file is fixed.

```kotlin
val pipeline = GraphicsPipeline.Builder()
    .setVertexShader(Shader.Builder().setPath("mesh.vert").build(), layout)
    .setFragmentShader(Shader.Builder().setPath("mesh.frag").build())
    .setDepthStencilState(DepthStencilState(depthCompareOp = CompareOp.eLessOrEqual))
    .build()
val set = DescriptorSet.Builder(pipeline, 0).write("camera", camera).write("albedo", albedo, sampler).build()

override fun record(commands: CommandBuffer) {
    commands.beginRendering()
        .bindGraphicsPipeline(pipeline).bindDescriptorSet(0, set)
        .pushConstant("model", transform)   // by name, checked against the shader's type
        .bindMesh(mesh).drawIndexed()
        .endRendering()
}
```

There is everything C# has: buffers, images, views, samplers, buffer views, shaders, the three pipelines,
descriptor sets, framebuffers, meshes, acceleration structures, every command, and the frame graph with
created and imported resources, previous-frame reads, async compute, `CpuPass` and `DebugDrawPass`.

The JVM has no structs to copy as they are, so data crosses in one of three ways:
- **as primitive arrays** (`FloatArray`, `IntArray`, `ByteArray`...) or a `MemorySegment`;
- **as a list with its `GpuLayout`**, which says how one value is written: `GpuLayout.Vec4`, `GpuLayout.Mat4`,
  or one of your own;
- **as push constants:** a number, `7u`, a vector, a `Mat4` or an array.

`Vec2`, `Vec3`, `Vec4`, `IVec*`, `UVec*`, `Mat3`, `Mat4` and `Quat` are kmath's types, as immutable values, and
kmath's functions are top-level: `dot`, `normalize`, `perspective` (Vulkan's depth range), `lookAt`, `compose`,
`Random`, `Noise`, `ease`, `Bulk`... — see [math](math.md).

## Compose

`Scene.setContent(theme, scale) { ... }` sets up the composition:
- it adds a `UiPass` that draws over whatever the scene's graph already draws;
- it recomposes before every `update`.

Each frame:
1. Snapshot changes are applied.
2. Recomposition runs on the frame's thread.
3. Only the nodes that changed hand koral-ui a new widget.

Every node keeps the widget it made, until one of its parameters, its modifier or its children change. An
unchanged subtree reaches koral-ui as the same widget, and koral-ui skips it.

Recomposition is scoped as it is in Compose. A state read only inside a `Button`'s content recomposes that
content, not the column around it.

| Composable                                          | koral-ui                                                       |
|-----------------------------------------------------|----------------------------------------------------------------|
| `Box(modifier, contentAlignment) { }`               | `Stack`, with `Modifier.align` per child (`StackAlign`)        |
| `Row` / `Column(modifier, arrangement, alignment) { }` | `Row` / `Column`, as big as their contents (`MainAxisSize::eMin`) |
| `Spacer(modifier)`                                  | an empty `SizedBox`, sized by its modifier or its `weight`    |
| `Text(text, color, fontSize, textAlign, ...)`       | `Text`, in `LocalContentColor` or the theme's text colour     |
| `Button` / `OutlinedButton` / `TextButton { }`      | `Button(child)`: primary, secondary and plain                 |
| `Checkbox`, `Switch`, `Slider`, `LinearProgressIndicator` | the controls of the same names                          |
| `TextField(value, onValueChange, placeholder)`      | `TextField`, controlled: it shows `value`, and what `onValueChange` made of an edit |
| `Canvas(modifier) { drawCircle(...) }`              | `CustomPaint`, with a Compose-style `DrawScope`. It draws again when state it read while drawing changes, without composing again |
| `Picture(image, contentDescription, modifier, contentScale)` | `Image`: a Koral image (a texture, or what a View draws), `Fit`, `Crop`, `FillBounds` or `None`. Compose's `Image` under another name: `Image` is Koral's own type, the thing this shows |
| `LazyColumn(modifier, state, contentPadding, verticalArrangement) { item { }; items(list, key) { } }`, `LazyRow`, `LazyVerticalGrid` | `LazyList`: only the items in view are composed, each a composition of its own and as long as it is (`itemHeight` / `itemWidth`, koral-ui's own: all one length, which a list of very many lays out faster) |

Modifiers apply outermost first, as in Compose:

| Modifiers                                                       | What they do                         |
|-----------------------------------------------------------------|--------------------------------------|
| `padding`                                                       | padding                              |
| `background(color, shape)`, `border(width, color, shape)`       | fill and outline, shaped             |
| `size`, `width`, `height`                                       | a fixed size                         |
| `fillMaxWidth`, `fillMaxHeight`, `fillMaxSize`                  | all the space allowed                |
| `clickable`                                                     | clicks, with hover and press shown   |
| `onSizeChanged { size -> }`                                     | its size in pixels, when it changes: resize a viewport's texture to it |
| `alpha`                                                         | transparency                         |
| `clip(shape)`                                                   | cut to a shape                       |
| `dragSource(type, payload)`, `dropTarget(type) { }`             | drag and drop                        |
| `offset(x, y)`                                                  | moved where drawn and clicked, not laid out |
| `verticalScroll`, `horizontalScroll`                            | scrolling                            |
| `weight` (in a `Row` or `Column`), `align` (in any scope)       | the parent's placement               |

`DrawScope` has the following, with `Brush` gradients (linear, radial and sweep) and `Stroke` styles:
- `drawRect`, `drawRoundRect`, `drawCircle`, `drawOval`, `drawArc`, `drawLine`, `drawPath` and `drawText`;
- `translate`, `rotate`, `scale` and `clipRect` blocks.

Shapes are `RectangleShape`, `RoundedCornerShape(dp)` and `CircleShape`.

### Themes and fonts

A `Theme` is every choice of a colour and a size the controls are drawn with, as one value, read in a composable
as `LocalTheme.current`. There are four families, each light and dark, each in its own accent or one given:

| | dark | light | by its parts |
|---|---|---|---|
| koral-ui's own (One UI's shapes, coral) | `KoralDarkTheme` | `KoralLightTheme` | `Themes.koral(dark, accent)` |
| Material 3 | `MaterialDarkTheme` | `MaterialLightTheme` | `Themes.material(dark, accent)` |
| Cupertino | `CupertinoDarkTheme` | `CupertinoLightTheme` | `Themes.cupertino(dark, accent)` |
| Windows (Fluent) | `WindowsDarkTheme` | `WindowsLightTheme` | `Themes.windows(dark, accent)` |

```kotlin
val ui = setContent(theme = Themes.material(dark = false, accent = Color(0xFF00897B))) { App() }
ui.theme = Themes.windows()                      // changed while it runs: the whole interface, where it stands
KoralTheme(CupertinoLightTheme) { Preview() }    // or for a part of it
val mine = KoralDarkTheme.copy(radius = 8.dp, buttonRadius = 4.dp, controlHeight = 30.dp)
```

`Themes.windows()` with nothing said is dark or light as Windows is set, in Windows' accent, in Segoe UI:
`SystemAppearance.isDark` and `.accent` are what it reads (`isKnown` is false where the system has no such
setting). `Themes.of(family, dark, accent)` picks by a `ThemeFamily`; `theme.withAccent(color)` is any theme in
another accent. Besides the colours a theme has `radius` (cards, menus, panels), `buttonRadius`, `fieldRadius`,
`checkboxRadius` (unspecified: a circle), `controlHeight`, `fontSize` and `fontFamily`.

Fonts are Compose's `Font` and `FontFamily`:

```kotlin
val Inter = FontFamily(
    Font("assets/fonts/Inter-Regular.ttf"),
    Font("assets/fonts/Inter-Bold.ttf", FontWeight.Bold),
    Font("assets/fonts/Inter-Italic.ttf", style = FontStyle.Italic))
Text("Hello", fontFamily = Inter, fontWeight = FontWeight.Bold)
setContent(theme = KoralDarkTheme.copy(fontFamily = Inter)) { … }      // everywhere, the controls too
```

A `Font` is a TrueType or OpenType file by its path, a `File`, its bytes (`Font(name, bytes)`) or a resource
(`Font.resource("fonts/Inter.ttf")`). `FontFamily.SansSerif`, `Serif`, `Monospace` and `Cursive` are the system's
where it has them. A family gives the font nearest the weight and style asked for; where it has none that heavy or
that slanted, koral-ui thickens or slants the nearest — a family's own bold and italic look better.

Besides Compose's own names there are the controls a tools interface is made of. None keeps anything of
its own: the caller's state says whether a header is open or which tab is in front, and hears when it
should change.

| Composable | |
|---|---|
| `RadioButton(selected, onClick, label)`, `Selectable(label, selected, onClick)` | one of several; a line that can be picked |
| `DragValue`, `Dropdown`, `ColorPicker`, `ColorEdit` | a number dragged; a list under a field; a colour, inline or under a swatch |
| `Slider` and `DragValue` over an `Int`, a `Double`, a `Dp` or a `TextUnit` | the value comes back as what it was given: `Slider(radius, { radius = it }, valueRange = 0.dp..24.dp)` |
| `CollapsingHeader(title, expanded, onExpandedChange) { }` | folds its content, which is composed only while open |
| `TreeNode(label, expanded, onExpandedChange, leaf, selected, onClick) { }` | a node of a tree, its content further in |
| `TabRow(tabs, selected, onSelected)` | a row of titles |
| `MenuBar(listOf(Menu("File", items)))`, `ContextMenuArea(items) { }` | menus |
| `Tooltip(text) { }` | a tip by the pointer |
| `Dialog(open, onDismiss, dialog = { }) { }` | a card over its content, which is dimmed while it shows |
| `Plot(values, kind, range, overlay)` | a line or bars |
| `Table(columns, rowCount) { row, column -> }` | cells under columns that line up |
| `Divider`, `Enabled(enabled) { }`, `BulletText` | |
| `StepSlider(value, steps, onValueChange, labels)` | a slider that stops only at its steps |
| `GradientEditor(stops, onStopsChange)` | a gradient's `ColorStop`s: added, moved, recoloured, removed |
| `StatusBar(message, level) { trailing }` | the last thing said, along the foot of the window |

### Docking and drag and drop

```kotlin
val layout = rememberDockLayout {
    dock("scene")
    dock("inspector", DockSide.eRight, "scene", 0.25f)
}
DockSpace(layout, onLayoutChanged = { settings.save(layout.save()) }) {
    panel("scene", "Scene", closable = false) { SceneView() }
    panel("inspector", "Inspector") { Inspector() }
}

Box(Modifier.size(36.dp).background(red).dragSource("color", red))
Box(Modifier.fillMaxSize().dropTarget("color") { fill = it.payload as Color })
```

Tabs are dragged between groups, onto edges, into the middle (a float) and out of the window (a window of
their own), as [koral-ui's docking](ui.md#docking) describes. A panel's composable keeps what it remembers
wherever the panel goes.

A LazyColumn item keeps what it remembers while it stays within the range the list keeps (the view and a
screen either side). One that leaves it is disposed, and composed afresh when it comes back, as in Compose.
Give items a `key` so their state moves with them when the list changes.

### What is Compose's, and what is not

The names, parameter names and parameter order are Jetpack Compose's wherever Compose has the thing: `Text`
(with `TextStyle`, `TextAlign.Center`, `FontWeight`…), `Button`, `Checkbox`, `Switch`, `RadioButton`,
`Slider` (with `steps`), `TextField` (composable `label` and `placeholder`), `Surface`, `Card`, `Scaffold`,
`TabRow` and `Tab`, `Dialog` and `AlertDialog`, `HorizontalDivider`, `CircularProgressIndicator`,
`MaterialTheme.colorScheme` / `.typography` / `.shapes`, `Modifier.pointerInput { detectTapGestures / detectDragGestures }`,
`onSizeChanged`, `widthIn` / `sizeIn`, `wrapContentSize`, `shadow`, and the animation names (`animate*AsState`,
`Animatable`, `tween` / `spring` / `snap`, `AnimatedVisibility`, `Crossfade`, `rememberInfiniteTransition`).
Menus follow Compose for Desktop: `MenuBar { Menu("File") { Item("Open", onClick = { }) } }`,
`ContextMenuArea(items = { listOf(ContextMenuItem("Copy") { }) })`. They are in `koral.compose`, not `androidx.compose.*`.

What koral-ui adds keeps Compose's conventions — the value, then `on…Change`, then `modifier`, content last — and
where one of them takes something Compose's own does not (a `RadioButton`'s `label`, a `TextField`'s `width` and
`onSubmit`), it is a named parameter after Compose's.

Also Compose's: `Layout(content) { measurables, constraints -> layout(w, h) { placeable.place(x, y) } }`,
`Modifier.graphicsLayer` / `rotate` / `scale`, `aspectRatio`, `fillMaxWidth(0.5f)`, `DropdownMenu` and
`DropdownMenuItem`, `Popup`, `Icon(Icons.Default.Add, …)` (some fifty icons, drawn rather than loaded),
`rememberScrollState()` with `value`, `maxValue`, `scrollTo` and `animateScrollTo`, `awaitPointerEventScope`,
`ButtonDefaults.buttonColors(…)` with `shape`, `colors` and `border` on the buttons, `MaterialTheme(colorScheme) { }`,
`LazyRow`, `LazyVerticalGrid`. A `TextField` selects with Shift and the arrows or a drag, copies, cuts and pastes
with Control and C, X and V; as Compose's it is as many lines as are typed (from `minLines` to `maxLines`), and one
with `singleLine = true`, where Enter submits; Tab goes from one field to the next.

And: `Text`'s `fontWeight`, `fontStyle`, `fontFamily`, `textDecoration`, `maxLines`, `minLines` and
`overflow = TextOverflow.Ellipsis`; a `Slider`'s `onValueChangeFinished`; `detectTapGestures(onLongPress = …)`;
`FocusRequester` with `Modifier.focusRequester` on a field, and `LocalFocusManager.current.clearFocus()`;
`BoxWithConstraints` (its content is composed from the frame after it is first laid out); `AnimatedVisibility` with
`fadeIn`, `expandVertically` / `Horizontally` / `In`, `slideInVertically` / `Horizontally`, `scaleIn` and their
exits, added together with `+`; `MaterialTheme(colorScheme) { }`, which recolours everything inside it, koral-ui's
own controls too.

And: lazy lists as Compose writes them — `LazyColumn` and `LazyRow` with items as long as they are,
`contentPadding`, `Arrangement.spacedBy`, and a `LazyListState` (`rememberLazyListState()`,
`firstVisibleItemIndex`, `firstVisibleItemScrollOffset`, `scrollToItem`); `LazyVerticalGrid` with
`GridCells.Fixed` or `GridCells.Adaptive`, which counts its columns from the width it is given;
`Modifier.width(IntrinsicSize.Max)` and `height(IntrinsicSize.Min)`, and a `Measurable`'s intrinsic sizes in a
`Layout`; `SubcomposeLayout`.

Where it differs from Compose:
- An intrinsic size is the size something is with all the room there is that way: `Min` and `Max` are the same.
- `SubcomposeLayout` composes a slot in the frame after its rule first asks for it, so a layout whose parts depend
  on one another settles over that many frames; and a slot is one measurable, whatever it emits.
- `animateScrollToItem` goes there at once. A lazy list has no `reverseLayout`, sticky headers or `layoutInfo`.

### Not yet

- **`Modifier.offset`** moves where a click lands, but a click reaches it only inside its parent's box.
- **`Picture`** shows a Koral `Image`; loading files is the image modules' (`kimg`), which have no Kotlin face yet.
