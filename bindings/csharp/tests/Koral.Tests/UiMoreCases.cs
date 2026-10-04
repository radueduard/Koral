using System.Numerics;
using Koral.UI;
using static Koral.UI.Widgets;
using Buffer = Koral.Buffer;
using Color = Koral.UI.Color;

namespace Koral.Tests;

/// <summary>The rest of koral-ui from C#: a theme for part of an interface, a lazy list, intrinsic sizes, and every other control.</summary>
public sealed class MoreScene : Scene
{
    public const int Side = 160;
    public static MoreScene? Last;
    public readonly Buffer Readback = new Buffer.Builder<byte>().SetInstanceCount(Side * Side * 4)
        .SetUsage(Buffer.Usage.eTransferDst).SetType(Buffer.Type.eReadback).Build();
    public readonly Ui Ui = new(theme: Themes.Koral());
    public int First = -1;
    public float Into = -1;
    public uint Jump;
    public int Finished;
    public float Slid;
    public Vector2 Laid;

    public MoreScene() => Last = this;

    private static Widget Box(float width, float height, Color color) =>
        Container(new ContainerOptions().SetSize(width, height).SetColor(color));

    public Widget Root() => Column([
        // 0 to 10: one of koral-ui's own controls in a theme of its own.
        Themed(Themes.Cupertino(dark: false), SizedBox(100, 10, ProgressBar(1f))),
        // 10 to 50: a list of which only what is in view exists, its items ten tall and red, or thirty and green.
        SizedBox(100, 40, LazyList(100_000, i => Box(-1, i % 2 == 0 ? 10 : 30, i % 2 == 0 ? new Color(1, 0, 0) : new Color(0, 1, 0)),
            new LazyListOptions().SetOnScrolled((first, into) => { First = first; Into = into; }).JumpTo(101, 0, Jump))),
        // 50 to 70: a column as wide as its widest child, the other stretched to it.
        Intrinsic(true, false, Column([Box(40, 10, new Color(1, 0, 0)), Box(-1, 10, new Color(0, 0, 1))],
            new FlexOptions().SetCrossAxisAlignment(CrossAxisAlignment.eStretch))),
        // 70 to 80: a layout of its own, putting a green box thirty along.
        CustomLayout((context, constraints) =>
        {
            Laid = context.Measure(0, BoxConstraints.Loose(new Vector2(100, 10)));
            context.Place(0, new Vector2(30, 0));
            return new Vector2(100, 10);
        }, [Box(10, 10, new Color(0, 1, 0))]),
        // 80 on: a slider that says when it is let go of.
        SizedBox(100, 30, Slider(Slid, v => Slid = v, 0, 1, () => ++Finished)),
        // And every other one, made and laid out: what C gives, C# reaches.
        ScrollView(Column([
            MenuBar([new Menu("File", [new MenuItem("Open"), MenuItem.Separator, new MenuItem("Quit", Enabled: false)])]),
            TabBar(["One", "Two"], 0, _ => { }),
            Table([new TableColumn("Name"), new TableColumn("Size", 40)], [[Text("a"), Text("1")], [Text("b"), null]]),
            TreeNode("Root", true, _ => { }, [TreeNode("Leaf", false, null, leaf: true)]),
            Row([Dropdown(["Low", "High"], 0, _ => { }), DragValue(1.5f, _ => { }, label: "X"), StepSlider(1, 4, _ => { })]),
            Row([ColorEdit(Color.Red, _ => { }, "Tint"), RadioButton(true, null, "On"), Selectable("Pick", false, null)]),
            GradientEditor([new GradientStop(0, Color.Black), new GradientStop(1, Color.White)], _ => { }, 120),
            Plot([1f, 3f, 2f], PlotKind.Bars, overlay: "plot"),
            CollapsingHeader("More", true, _ => { }, Text("under it")),
            Separator(),
            Tooltip("a tip", Text("One line, cut", new TextStyle().Bold().SetItalic().SetUnderline(), TextAlign.eStart, false, 1, true)),
            ContextMenu([new MenuItem("Copy")], Disabled(Button("Off", null))),
            AspectRatio(4f, FractionallySizedBox(0.5f, 0, TransformBox(Transform.Rotation(0.1f), Box(20, 5, Color.Green)))),
            SizeObserver((_, _) => { }, PopupAnchor(false, Text("popup"))),
            TextField(new TextFieldOptions().SetMultiline(2, 4).SetPlaceholder("notes")),
            StatusBar("Ready", StatusLevel.Warning),
        ], new FlexOptions().SetCrossAxisAlignment(CrossAxisAlignment.eStart)), Axis.eVertical, (_, _) => { }),
    ], new FlexOptions().SetCrossAxisAlignment(CrossAxisAlignment.eStart));

    protected override void Initialize()
    {
        Ui.SetRoot(Modal(false, Root(), Text("a dialog")));
        Graph.Add(new UiPass(Ui));
        Graph.Add(new ReadScreen(Readback));
    }

    protected override void Update() => Ui.Update();

    public byte[] Pixel(int x, int y)
    {
        Check.GpuIsDone();
        return Readback.Read<byte>(Side * Side * 4).Skip((y * Side + x) * 4).Take(3).ToArray();
    }
}

public static partial class Cases
{
    /// <summary>Themes by family, a lazy list, intrinsic sizes, a layout of one's own, and the rest of the controls — from C#.</summary>
    public static void ThemesListsAndTheRestOfTheControls()
    {
        using var app = Check.HeadlessApp();

        foreach (var family in Enum.GetValues<ThemeFamily>())
        {
            Check.That(Themes.Of(family, dark: true).IsDark, $"{family}, dark");
            Check.That(!Themes.Of(family, dark: false).IsDark, $"{family}, light");
            Check.Equal(Color.Hex(0xFF2D55), Themes.Of(family, true, Color.Hex(0xFF2D55)).Primary, $"{family} takes an accent");
        }
        Check.That(Themes.Koral().CheckboxRadius is null && Themes.Material().CheckboxRadius is 2f, "a checkbox: a circle, or square");
        if (OperatingSystem.IsWindows()) Check.That(SystemAppearance.Query().Known, "Windows says how it is set");

        app.Register<MoreScene>();
        var scene = app.OpenOffscreen("MoreScene", Offscreen(MoreScene.Side));
        var more = MoreScene.Last!;
        var input = scene.SceneInput;
        input.FeedMousePosition(new Vector2(150, 150));
        Frames(app, 4);

        bool Near(byte[] p, Color c) => Math.Abs(p[0] - c.R * 255) <= 4 && Math.Abs(p[1] - c.G * 255) <= 4 && Math.Abs(p[2] - c.B * 255) <= 4;
        string Said(byte[] p) => $"[{string.Join(", ", p)}]";
        Check.That(Near(more.Pixel(50, 5), Themes.Cupertino(dark: false).Primary), $"a theme of its own: {Said(more.Pixel(50, 5))}");
        Check.That(more.Pixel(10, 15) is [255, 0, 0], $"the list's first item, ten tall: {Said(more.Pixel(10, 15))}");
        Check.That(more.Pixel(10, 35) is [0, 255, 0], $"its second, thirty: {Said(more.Pixel(10, 35))}");
        Check.Equal(0, more.First, "the list says where it is");
        Check.That(more.Pixel(35, 65) is [0, 0, 255], $"Intrinsic: as wide as the widest: {Said(more.Pixel(35, 65))}");
        Check.That(more.Pixel(45, 65) is not [0, 0, 255], "and no wider");
        Check.That(more.Pixel(35, 75) is [0, 255, 0], $"a layout of its own places its child: {Said(more.Pixel(35, 75))}");
        Check.Equal(new Vector2(10, 10), more.Laid, "and measures it");

        more.Jump = 1;
        more.Ui.SetRoot(more.Root());
        Frames(app, 4);
        Check.Equal(101, more.First, "sent to an item, it is first in view");
        Check.That(more.Pixel(10, 15) is [0, 255, 0], $"an odd one: {Said(more.Pixel(10, 15))}");

        void At(float x, float y) { input.FeedMousePosition(new Vector2(x, y)); Frames(app, 1); }
        At(20, 95);
        input.FeedMouseButton(MouseButton.eLeft, true);
        Frames(app, 1);
        At(70, 95);
        Check.Equal(0, more.Finished, "held");
        input.FeedMouseButton(MouseButton.eLeft, false);
        Frames(app, 2);
        Check.Equal(1, more.Finished, "a slider let go of says so");
        Check.That(more.Slid > 0.3f, $"having moved: {more.Slid}");

        more.Ui.SetTheme(Themes.Material(dark: false));
        more.Ui.ClearFocus();
        Frames(app, 3);
        Check.That(Near(more.Pixel(50, 5), Themes.Cupertino(dark: false).Primary), "the part with a theme of its own keeps it when the Ui's changes");
    }
}
