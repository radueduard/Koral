// An interface in C#, run by koral-dotnet: `koral-dotnet samples/Settings --hot-reload`, then edit a Build
// method while it runs — the edit is applied in place and every widget built again, keeping its state:
// tick the checkbox, change a colour below, save, and the box stays ticked.

using Koral.UI;
using static Koral.UI.Widgets;
using Color = Koral.UI.Color;

/// <summary>A settings panel: a widget with state, made of the building blocks.</summary>
public sealed class SettingsPanel : StatefulWidget
{
    private bool _subtitles = true, _fullscreen;
    private float _volume = 0.6f;
    private string _name = "";

    public override Widget Build()
    {
        var theme = Theme.Current;
        return Container(new ContainerOptions()
                .SetWidth(380)
                .SetPadding(20)
                .SetDecoration(new Decoration()
                    .SetColor(theme.Surface)
                    .SetBorder(1, theme.Border)
                    .SetRadius(12)
                    .SetShadow(Color.Black.WithAlpha(0.5f), 14, new Vector2(0, 6))),
            Column([
                Text("Settings", new TextStyle().SetSize(22)),
                TextField(new TextFieldOptions().SetPlaceholder("Player name").SetOnChanged(name => _name = name)),
                Checkbox(_subtitles, on => SetState(() => _subtitles = on), "Show subtitles"),
                Row([Text("Fullscreen"), Switch(_fullscreen, on => SetState(() => _fullscreen = on))],
                    new FlexOptions().SetMainAxisAlignment(MainAxisAlignment.eSpaceBetween)),
                Text($"Volume {_volume * 100:0}%", new TextStyle().SetColor(theme.TextMuted)),
                Slider(_volume, v => SetState(() => _volume = v)),
                Row([
                    Button("Reset", () => SetState(() => (_subtitles, _fullscreen, _volume) = (true, false, 0.6f)), new ButtonOptions().SetStyle(ButtonStyle.eSecondary)),
                    Button("Apply", () => Log.Info($"[settings] {_name}: subtitles {_subtitles}, fullscreen {_fullscreen}, volume {_volume:0.00}")),
                ], new FlexOptions().SetMainAxisAlignment(MainAxisAlignment.eEnd).SetGap(8)),
            ], new FlexOptions().SetCrossAxisAlignment(CrossAxisAlignment.eStretch).SetGap(12)));
    }
}

/// <summary>A canvas of its own beside the panel: drawn with the pen, chained.</summary>
public sealed class Badge : StatelessWidget
{
    public override Widget Build() => CustomPaint((canvas, size) =>
    {
        var c = size / 2;
        canvas.DrawCircle(c, 70, Paint.Fill(Color.Hex(0x2B2F6B)).SetStroke(3, Color.Hex(0x7090FF)))
              .BeginPath()
              .MoveTo(c + new Vector2(-40, 10))
              .DrawLineTo(c + new Vector2(-10, 40))
              .DrawArcTo(c + new Vector2(40, -40), c + new Vector2(45, -40), 6)
              .Stroke(new Paint().SetStroke(new Stroke { Width = 10, Color = Color.White, Cap = StrokeCap.eRound, Join = StrokeJoin.eRound }));
    }, new Vector2(200, 200));
}

public sealed class Settings : Scene
{
    private readonly Ui _ui = new(
        Container(new ContainerOptions().SetColor(Theme.Dark().Background).SetAlignment(Alignment.Center),
            Row([new Badge(), SizedBox(40, 0), new SettingsPanel()])));

    protected override void Initialize() => Graph.Add(new UiPass(_ui));
    protected override void Update() => _ui.Update();
}
