using System.Numerics;
using Koral.Scripting;
using Koral.UI;
using static Koral.UI.Widgets;
using Buffer = Koral.Buffer;
using Color = Koral.UI.Color;
using UiPaint = Koral.UI.Paint;
using Path = Koral.UI.Path;

namespace Koral.Tests;

/// <summary>Copies the screen where the test can read it, after the interface drew.</summary>
public sealed class ReadScreen(Buffer readback) : RenderPass("Read")
{
    private Image? _screen;
    public override void Setup(PassBuilder builder) => builder.Read(FrameGraph.Screen, Image.Usage.eTransferSrc).SideEffect();
    public override void Initialize(PassResources resources) => _screen = resources.ImageNamed(FrameGraph.Screen);
    public override void Record(CommandBuffer commandBuffer) => commandBuffer.CopyImageToBuffer(_screen!, readback);
}

/// <summary>A widget with state: green until tapped, blue after — painted with the canvas's pen.</summary>
public sealed class Tapper : StatefulWidget
{
    public static int Taps, Builds;
    public int Count;

    public override Widget Build()
    {
        ++Builds;
        var color = Count == 0 ? new Color(0, 1, 0) : new Color(0, 0, 1);
        // Any widget can be a button: here a painted box, with a plain button around it.
        return Button(
            CustomPaint((canvas, size) => canvas
                .BeginPath()
                .MoveTo(Vector2.Zero)
                .DrawLineTo(new Vector2(size.X, 0))
                .DrawLineTo(size)
                .DrawLineTo(new Vector2(0, size.Y))
                .ClosePath()
                .Fill(UiPaint.Fill(color))),
            () => SetState(() => { ++Count; ++Taps; }),
            new ButtonOptions().SetStyle(ButtonStyle.ePlain));
    }
}

public sealed class UiScene : Scene
{
    public static UiScene? Last;
    public readonly Buffer Readback = new Buffer.Builder<byte>().SetInstanceCount(16 * 16 * 4)
        .SetUsage(Buffer.Usage.eTransferDst).SetType(Buffer.Type.eReadback).Build();
    public readonly Ui Ui = new(new Tapper());
    public string Typed = "";

    public UiScene() => Last = this;

    protected override void Initialize()
    {
        Graph.Add(new UiPass(Ui));
        Graph.Add(new ReadScreen(Readback));
    }

    protected override void Update()
    {
        Typed += Input.TypedText;
        Ui.Update();
    }

    public byte[] Pixel(int x, int y)
    {
        Check.GpuIsDone();
        return Readback.Read<byte>(16 * 16 * 4).Skip((y * 16 + x) * 4).Take(4).ToArray();
    }
}

public static partial class Cases
{
    /// <summary>The canvas from C#: chained calls, the pen, a picture and a layer.</summary>
    public static void CanvasChainsIntoAPicture()
    {
        using var app = Check.HeadlessApp();
        using var canvas = new Canvas();
        var picture = canvas
            .DrawRRect(new RRect(Rect.XYWH(0, 0, 40, 20), 6), UiPaint.Fill(Color.Hex(0x3F51B5)).SetStroke(1, Color.White))
            .DrawCircle(new Vector2(10, 10), 4, UiPaint.Fill(Color.Red))
            .BeginPath().MoveTo(Vector2.Zero).DrawArcTo(new Vector2(20, 0), new Vector2(20, 20), 5).Stroke(UiPaint.Stroked(Color.Green, 2))
            .Finish();
        Check.That(picture.InstanceCount >= 3, $"three shapes and more: {picture.InstanceCount}");
        Check.That(picture.Bounds.Width >= 40, $"its bounds: {picture.Bounds}");
        var layer = Layer.Create().SetPicture(picture).SetOpacity(0.5f).SetTransform(Transform.Translation(new Vector2(5, 0)));
        Check.Equal(0.5f, layer.Opacity, "a layer, chained");
        using var path = new Path().MoveTo(Vector2.Zero).LineTo(new Vector2(10, 0)).QuadTo(new Vector2(10, 10), new Vector2(0, 10)).Close();
        Check.Equal(10f, path.Bounds.Width, "a path, chained");
    }

    /// <summary>An interface from C#: a stateful widget drawn, tapped, rebuilt — and gone with its scene.</summary>
    public static void UiDrawsTapsAndRebuilds()
    {
        using var app = Check.HeadlessApp();
        app.Register<UiScene>();
        var scene = app.OpenOffscreen("UiScene", Offscreen(16));
        var ui = UiScene.Last!;
        Frames(app, 3);
        Check.That(ui.Pixel(8, 8) is [0, 255, 0, 255], $"green before a tap: [{string.Join(", ", ui.Pixel(8, 8))}]");
        var builds = Tapper.Builds;

        scene.SceneInput.FeedMousePosition(new Vector2(8, 8));
        Frames(app, 1);
        scene.SceneInput.FeedMouseButton(MouseButton.eLeft, true);
        Frames(app, 1);
        scene.SceneInput.FeedMouseButton(MouseButton.eLeft, false);
        Frames(app, 3);
        Check.Equal(1, Tapper.Taps, "the tap reached C#");
        Check.Equal(builds + 1, Tapper.Builds, "SetState built it once more");
        Check.That(ui.Pixel(8, 8) is [0, 0, 255, 255], $"blue after it: [{string.Join(", ", ui.Pixel(8, 8))}]");
        Check.That(scene.SceneInput.InterfaceWantsMouse, "the pointer over the interface, so a camera stays put");

        ui.Ui.Reassemble();
        Frames(app, 1);
        Check.Equal(builds + 2, Tapper.Builds, "reassembled: built again");
        Frames(app, 2);
        Check.That(ui.Pixel(8, 8) is [0, 0, 255, 255], "with its state kept");

        scene.SceneInput.FeedText("hé");
        Frames(app, 1);
        Check.Equal("hé", ui.Typed, "typed text reaches the scene");

        var widgetUi = ui.Ui;
        app.Close(scene);
        app.Frame();
        Check.That(Throws(() => widgetUi.Update()), "the Ui went with its scene");
    }

    private static bool Throws(Action action)
    {
        try { action(); return false; }
        catch (ObjectDisposedException) { return true; }
    }

    private const string Panel = """
        using Koral.UI;
        using static Koral.UI.Widgets;

        public sealed class Swatch : StatefulWidget
        {
            public static int Builds;
            public int Taps;
            public Swatch() { Taps = 7; }
            public override Widget Build()
            {
                Builds += 1;
                return DecoratedBox(new Decoration { Color = new Koral.UI.Color(1, 0, 0) });
            }
        }

        public sealed class Panel : Scene
        {
            public readonly Ui Ui = new(new Swatch());
            protected override void Initialize() { Graph.Add(new UiPass(Ui)); }
            protected override void Update() { Ui.Update(); }
        }
        """;

    /// <summary>A widget's Build edited while it runs: applied in place, every Ui built again, state kept.</summary>
    public static void InPlaceWidgetEditsRebuildTheUi()
    {
        var directory = Directory.CreateTempSubdirectory("koral-ui-inplace-");
        try
        {
            var script = System.IO.Path.Combine(directory.FullName, "Panel.cs");
            File.WriteAllText(script, Panel);
            using var app = Check.HeadlessApp();
            using var host = new ScriptHost(app, directory.FullName);
            Check.That(host.UpdatesInPlace, "in-place updates are on (DOTNET_MODIFIABLE_ASSEMBLIES=debug)");
            host.Load();
            app.OpenOffscreen("Panel", Offscreen(8));
            Frames(app, 2);
            var scene = app.FindScene("Panel")!;
            var swatchType = scene.GetType().Assembly.GetType("Swatch")!;
            int Builds() => (int)swatchType.GetField("Builds")!.GetValue(null)!;
            var before = Builds();

            File.WriteAllText(script, Panel.Replace("new Koral.UI.Color(1, 0, 0)", "new Koral.UI.Color(0, 1, 0)"));
            Check.That(host.Reload(), "the edit applies");
            Check.Equal(ReloadKind.InPlace, host.LastReload, "a widget's Build edit is applied in place");
            Check.That(ReferenceEquals(app.FindScene("Panel"), scene), "the scene carries on");
            Frames(app, 1);
            Check.Equal(before + 1, Builds(), "the Ui built its widgets again, with the new code");

            // A widget's constructor is the Ui's to run again, not a reason to reopen the scene.
            File.WriteAllText(script, File.ReadAllText(script).Replace("Taps = 7;", "Taps = 8;"));
            Check.That(host.Reload(), "a constructor edit applies");
            Check.Equal(ReloadKind.InPlace, host.LastReload, "in place: widgets are built again by their Ui");
            Check.That(ReferenceEquals(app.FindScene("Panel"), scene), "the scene was not reopened");
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }
}

public sealed class DockingScene : Scene
{
    public static DockingScene? Last;
    public readonly DockLayout Layout = new DockLayout().Dock("src").Dock("dst", DockSide.eRight, "src", 0.5f);
    public readonly Ui Ui = new();
    public string? Dropped;
    public bool? Accepted;
    public int Changes;

    public DockingScene() => Last = this;

    protected override void Initialize()
    {
        Ui.SetRoot(DockSpace(Layout, [
            new DockPanel("src", "Source", SizedBox(40, 40).Background(Color.Hex(0xFF0000), 6)
                .Draggable(new DragData("word", "hello"), new DraggableOptions().SetOnDragEnd(ok => Accepted = ok))
                .Padding(4).Align(Alignment.TopLeft)),
            new DockPanel("dst", "Target", SizedBox(0, 0).OnDrop("word", d => Dropped = d.As<string>()), Closable: false),
        ], new DockOptions().SetOnChanged(() => ++Changes)));
        Graph.Add(new UiPass(Ui));
    }

    protected override void Update() => Ui.Update();
}

public static partial class Cases
{
    /// <summary>Modifier chains, a drag dropped on a target, and a dock space rearranged — from C#.</summary>
    public static void DockingDragAndDropAndModifiers()
    {
        using var app = Check.HeadlessApp();
        app.Register<DockingScene>();
        var scene = app.OpenOffscreen("DockingScene", new OffscreenSettings { Extent = new UVec2(240, 160) });
        var dock = DockingScene.Last!;
        var input = scene.SceneInput;
        Frames(app, 3);

        // Two panels side by side, a bar of 28 over each: the red box is in the left one, the target is the right one.
        void At(float x, float y) { input.FeedMousePosition(new Vector2(x, y)); Frames(app, 1); }
        At(20, 48);
        input.FeedMouseButton(MouseButton.eLeft, true);
        Frames(app, 1);
        At(60, 60);
        At(180, 100);
        input.FeedMouseButton(MouseButton.eLeft, false);
        Frames(app, 2);
        Check.Equal("hello", dock.Dropped, "the drop reached the target, with what was dragged");
        Check.Equal(true, dock.Accepted, "and the source heard it was taken");

        Check.That(!dock.Layout.IsFloating("dst"), "docked, to begin with");
        var before = dock.Layout.Save();
        dock.Layout.Dock("dst", DockSide.eBottom, "src", 0.4f);
        Frames(app, 2);
        Check.That(dock.Layout.Save() != before, "the layout changed");
        var other = new DockLayout();
        Check.That(other.Load(dock.Layout.Save()) && other.Save() == dock.Layout.Save(), "and survives saving");
        Check.That(!other.Load("nonsense"), "what is not a layout is refused");

        dock.Layout.PopOut("dst");   // no display here: it floats inside the space instead
        Frames(app, 2);
        Check.That(dock.Layout.IsFloating("dst"), "popped out");
        dock.Layout.Close("dst");
        Check.That(!dock.Layout.IsOpen("dst"), "closed");
    }
}
