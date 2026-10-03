using Koral.UI.Native;

namespace Koral.UI;

/// <summary>kui::Theme: the colours, shapes and type the built-in controls draw with.</summary>
public sealed record Theme
{
    public Color Background { get; init; }
    public Color Surface { get; init; }
    public Color SurfaceHover { get; init; }
    public Color SurfacePressed { get; init; }
    public Color Primary { get; init; }
    public Color PrimaryHover { get; init; }
    public Color PrimaryPressed { get; init; }
    public Color OnPrimary { get; init; }
    public Color Text { get; init; }
    public Color TextMuted { get; init; }
    public Color Border { get; init; }
    public Color Focus { get; init; }
    public float Radius { get; init; }
    public float ControlHeight { get; init; }
    public TextStyle TextStyle { get; init; } = new();

    public static unsafe Theme Dark() { KuiTheme t; KuiNative.kui_theme_dark(&t); return From(t); }
    public static unsafe Theme Light() { KuiTheme t; KuiNative.kui_theme_light(&t); return From(t); }
    /// <summary>The theme of the Ui being built — what a widget's Build reads.</summary>
    public static unsafe Theme Current { get { KuiTheme t; KuiNative.kui_theme_current(&t); return From(t); } }

    internal static Theme From(KuiTheme t) => new()
    {
        Background = Color.From(t.background), Surface = Color.From(t.surface), SurfaceHover = Color.From(t.surface_hover),
        SurfacePressed = Color.From(t.surface_pressed), Primary = Color.From(t.primary), PrimaryHover = Color.From(t.primary_hover),
        PrimaryPressed = Color.From(t.primary_pressed), OnPrimary = Color.From(t.on_primary), Text = Color.From(t.text),
        TextMuted = Color.From(t.text_muted), Border = Color.From(t.border), Focus = Color.From(t.focus),
        Radius = t.radius, ControlHeight = t.control_height, TextStyle = TextStyle.From(t.text_style),
    };

    internal KuiTheme Native => new()
    {
        background = Background.Native, surface = Surface.Native, surface_hover = SurfaceHover.Native, surface_pressed = SurfacePressed.Native,
        primary = Primary.Native, primary_hover = PrimaryHover.Native, primary_pressed = PrimaryPressed.Native, on_primary = OnPrimary.Native,
        text = Text.Native, text_muted = TextMuted.Native, border = Border.Native, focus = Focus.Native,
        radius = Radius, control_height = ControlHeight, text_style = TextStyle.Native,
    };
}
