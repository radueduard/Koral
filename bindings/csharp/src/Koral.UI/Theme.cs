using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::Theme: the colours, shapes and type the built-in controls draw with. One of <see cref="Themes"/>' —
/// koral-ui's own, Material, Cupertino or Windows, each light and dark and in an accent — or one's own:
/// <c>Themes.Koral() with { Radius = 8 }</c>.
/// </summary>
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
    /// <summary>How round a card, a menu, a panel is — and everything not said below.</summary>
    public float Radius { get; init; }
    public float ControlHeight { get; init; }
    public TextStyle TextStyle { get; init; } = new();
    /// <summary>How round a button is; a field. Not given: <see cref="Radius"/>.</summary>
    public float? ButtonRadius { get; init; }
    public float? FieldRadius { get; init; }
    /// <summary>How round a checkbox's corners are. Not given: it is a circle.</summary>
    public float? CheckboxRadius { get; init; }
    /// <summary>Whose manner the controls are drawn in — what a switch, a slider, a field is made of: kui::ThemeDesign.</summary>
    public ThemeFamily Design { get; init; } = ThemeFamily.Koral;

    public static unsafe Theme Dark() { KuiTheme t; KuiNative.kui_theme_dark(&t); return From(t); }
    public static unsafe Theme Light() { KuiTheme t; KuiNative.kui_theme_light(&t); return From(t); }
    /// <summary>The theme of the Ui being built — what a widget's Build reads.</summary>
    public static unsafe Theme Current { get { KuiTheme t; KuiNative.kui_theme_current(&t); return From(t); } }

    /// <summary>Whether it is a dark theme: by how light its background is.</summary>
    public bool IsDark => Background.Luminance < 0.5f;

    /// <summary>This theme in another accent: what is primary, lighter under the pointer and darker when pressed.</summary>
    public Theme WithAccent(Color accent) => this with
    {
        Primary = accent, PrimaryHover = accent.Mix(Color.White, 0.14f), PrimaryPressed = accent.Mix(Color.Black, 0.14f),
        OnPrimary = accent.Luminance > 0.6f ? Color.Hex(0x101010) : Color.White,
        Focus = accent.Mix(IsDark ? Color.White : Color.Black, 0.3f),
    };

    /// <summary>This theme with <paramref name="font"/> for its text, the controls' too.</summary>
    public Theme WithFont(Font? font) => this with { TextStyle = TextStyle with { Font = font } };

    internal static Theme From(KuiTheme t) => new()
    {
        Background = Color.From(t.background), Surface = Color.From(t.surface), SurfaceHover = Color.From(t.surface_hover),
        SurfacePressed = Color.From(t.surface_pressed), Primary = Color.From(t.primary), PrimaryHover = Color.From(t.primary_hover),
        PrimaryPressed = Color.From(t.primary_pressed), OnPrimary = Color.From(t.on_primary), Text = Color.From(t.text),
        TextMuted = Color.From(t.text_muted), Border = Color.From(t.border), Focus = Color.From(t.focus),
        Radius = t.radius, ControlHeight = t.control_height, TextStyle = TextStyle.From(t.text_style),
        ButtonRadius = t.button_radius >= 0f ? t.button_radius : null, FieldRadius = t.field_radius >= 0f ? t.field_radius : null,
        CheckboxRadius = t.checkbox_radius >= 0f ? t.checkbox_radius : null,
        Design = t.design <= (uint)ThemeFamily.Windows ? (ThemeFamily)t.design : ThemeFamily.Koral,
    };

    internal KuiTheme Native => new()
    {
        background = Background.Native, surface = Surface.Native, surface_hover = SurfaceHover.Native, surface_pressed = SurfacePressed.Native,
        primary = Primary.Native, primary_hover = PrimaryHover.Native, primary_pressed = PrimaryPressed.Native, on_primary = OnPrimary.Native,
        text = Text.Native, text_muted = TextMuted.Native, border = Border.Native, focus = Focus.Native,
        radius = Radius, control_height = ControlHeight, text_style = TextStyle.Native,
        button_radius = ButtonRadius ?? -1f, field_radius = FieldRadius ?? -1f, checkbox_radius = CheckboxRadius ?? -1f,
        design = (uint)Design,
    };
}

/// <summary>How something that goes from one value to another gets there. In kui::Curve's order.</summary>
public enum Curve { Linear, EaseIn, EaseOut, EaseInOut, EaseOutBack }

/// <summary>The families of look an interface can have: each has its colours and sizes, and its own design of every control. In kui::ThemeDesign's order.</summary>
public enum ThemeFamily { Koral, Material, Cupertino, Windows }

/// <summary>
/// The themes, by family: each dark or light, each in its own accent or the caller's.
/// <code>
/// var ui = new Ui(new Editor(), Themes.Material(dark: false, accent: Color.Hex(0x00897B)));
/// ui.SetTheme(Themes.Windows());     // dark or light, and in the accent, as Windows is set
/// </code>
/// </summary>
public static class Themes
{
    /// <summary>Coral: koral-ui's accent.</summary>
    public static readonly Color Coral = Color.Hex(0xFF7F50);

    private static Theme Accented(Theme theme, Color? accent) => accent is { } a ? theme.WithAccent(a) : theme;

    private static Theme Make(uint background, uint surface, uint hover, uint pressed, uint primary, uint primaryHover, uint primaryPressed,
                              uint onPrimary, uint text, uint muted, uint border, uint focus, float radius, float height, float size,
                              float? button, float? field, float? checkbox) => new()
    {
        Background = Color.Hex(background), Surface = Color.Hex(surface), SurfaceHover = Color.Hex(hover), SurfacePressed = Color.Hex(pressed),
        Primary = Color.Hex(primary), PrimaryHover = Color.Hex(primaryHover), PrimaryPressed = Color.Hex(primaryPressed), OnPrimary = Color.Hex(onPrimary),
        Text = Color.Hex(text), TextMuted = Color.Hex(muted), Border = Color.Hex(border), Focus = Color.Hex(focus),
        Radius = radius, ControlHeight = height, TextStyle = new TextStyle { Size = size, Color = Color.Hex(text) },
        ButtonRadius = button, FieldRadius = field, CheckboxRadius = checkbox,
    };

    /// <summary>koral-ui's own: One UI's shapes — round buttons and fields, a circle for a checkbox — on black, or on a soft grey.</summary>
    public static Theme Koral(bool dark = true, Color? accent = null) => Accented(dark ? Theme.Dark() : Theme.Light(), accent);

    /// <summary>Material 3's baseline scheme: pill buttons, fields with small corners, square checkboxes; violet unless <paramref name="accent"/> says.</summary>
    public static Theme Material(bool dark = true, Color? accent = null) => Accented(dark
        ? Make(0x141218, 0x211F26, 0x2B2930, 0x36343B, 0xD0BCFF, 0xDCCBFF, 0xB69DF8, 0x381E72, 0xE6E0E9, 0xCAC4D0, 0x49454F, 0xD0BCFF, 12, 40, 14, 20, 4, 2)
        : Make(0xFEF7FF, 0xF3EDF7, 0xECE6F0, 0xE6E0E9, 0x6750A4, 0x7965AF, 0x5B4597, 0xFFFFFF, 0x1D1B20, 0x49454F, 0xCAC4D0, 0x6750A4, 12, 40, 14, 20, 4, 2), accent) with { Design = ThemeFamily.Material };

    /// <summary>Cupertino: Apple's system colours — gently rounded buttons and fields, round checks, the system blue unless <paramref name="accent"/> says.</summary>
    public static Theme Cupertino(bool dark = true, Color? accent = null) => Accented(dark
        ? Make(0x000000, 0x1C1C1E, 0x2C2C2E, 0x3A3A3C, 0x0A84FF, 0x3B9BFF, 0x0871DB, 0xFFFFFF, 0xFFFFFF, 0x98989F, 0x38383A, 0x64B1FF, 16, 34, 15, 17, 17, null)
        : Make(0xF2F2F7, 0xFFFFFF, 0xE5E5EA, 0xD1D1D6, 0x007AFF, 0x3395FF, 0x0068D9, 0xFFFFFF, 0x000000, 0x8A8A8E, 0xC6C6C8, 0x007AFF, 16, 34, 15, 17, 17, null), accent) with { Design = ThemeFamily.Cupertino };

    /// <summary>
    /// Windows' own (Fluent): small corners all round, in Segoe UI where Windows has it. Unless told, dark or light
    /// as Windows is set, and in Windows' accent.
    /// </summary>
    public static Theme Windows(bool? dark = null, Color? accent = null)
    {
        var system = SystemAppearance.Query();
        var isDark = dark ?? system.Dark;
        var theme = isDark
            ? Make(0x202020, 0x2B2B2B, 0x323232, 0x272727, 0x60CDFF, 0x5BC0EE, 0x56B3DD, 0x000000, 0xFFFFFF, 0xC5C5C5, 0x3D3D3D, 0xFFFFFF, 8, 32, 14, 4, 4, 4)
            : Make(0xF3F3F3, 0xFBFBFB, 0xF0F0F0, 0xE9E9E9, 0x005FB8, 0x1A6FC0, 0x337FC7, 0xFFFFFF, 0x1A1A1A, 0x5F5F5F, 0xE0E0E0, 0x1A1A1A, 8, 32, 14, 4, 4, 4);
        if (Fonts.FromSystem("segoeui.ttf") is { } segoe) theme = theme.WithFont(segoe);
        // Windows' own accent where it has one, lightened on dark as Windows itself lightens it.
        Color? own = system.Known ? (isDark ? system.Accent.Mix(Color.White, 0.45f) : system.Accent) : null;
        return Accented(theme, accent ?? own) with { Design = ThemeFamily.Windows };
    }

    public static Theme Of(ThemeFamily family, bool dark = true, Color? accent = null) => family switch
    {
        ThemeFamily.Material => Material(dark, accent),
        ThemeFamily.Cupertino => Cupertino(dark, accent),
        ThemeFamily.Windows => Windows(dark, accent),
        _ => Koral(dark, accent),
    };
}

/// <summary>Fonts the system has.</summary>
public static class Fonts
{
    private static IEnumerable<string> Folders()
    {
        if (Environment.GetEnvironmentVariable("WINDIR") is { Length: > 0 } windows) yield return System.IO.Path.Combine(windows, "Fonts");
        yield return "/usr/share/fonts/truetype";
        yield return "/usr/share/fonts";
        yield return "/System/Library/Fonts";
        yield return "/Library/Fonts";
    }

    /// <summary>One of the system's fonts by its file name, or null where the system has none of that name.</summary>
    public static Font? FromSystem(string fileName)
    {
        foreach (var folder in Folders())
        {
            var path = System.IO.Path.Combine(folder, fileName);
            if (!File.Exists(path)) continue;
            try { return Font.Load(path); } catch (KoralException) { return null; }
        }
        return null;
    }
}
