using System.Numerics;
using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::Font: a TrueType or OpenType font, drawn at any size from one set of signed-distance glyphs.
/// </summary>
public sealed unsafe class Font
{
    internal readonly IntPtr Native;
    private Font(IntPtr native) => Native = native;
    ~Font() => KuiNative.kui_font_release(Native);

    /// <summary>The font in <paramref name="path"/>, resolved against the asset roots. Throws when it cannot be read.</summary>
    public static Font Load(string path) => new(KuiNative.Check(KuiNative.kui_font_load(path)));
    public static Font FromMemory(ReadOnlySpan<byte> bytes, string name = "memory")
    {
        fixed (byte* b = bytes) return new(KuiNative.Check(KuiNative.kui_font_from_memory(b, (nuint)bytes.Length, name)));
    }
    /// <summary>Inter Regular, as Koral ships it.</summary>
    public static Font Default() => new(KuiNative.Check(KuiNative.kui_font_default()));

    public Font SetFallback(Font? fallback) { KuiNative.kui_font_set_fallback(Native, fallback?.Native ?? IntPtr.Zero); _fallback = fallback; return this; }
    public float Ascent(float size) => KuiNative.kui_font_ascent(Native, size);
    public float Descent(float size) => KuiNative.kui_font_descent(Native, size);
    public bool HasGlyph(int codepoint) => KuiNative.kui_font_has_glyph(Native, (uint)codepoint).AsBool();

    private Font? _fallback;
}

/// <summary>kui::TextStyle: how text looks. Chainable: <c>new TextStyle().SetSize(24).SetColor(Color.White)</c>.</summary>
public record struct TextStyle()
{
    /// <summary>Font.Default() when null.</summary>
    public Font? Font { get; set; }
    public float Size { get; set; } = 14f;
    /// <summary>The theme's text colour, unless it says another.</summary>
    public Color Color { get; set; } = Color.Inherit;
    public float LineHeight { get; set; } = 1.25f;
    public float LetterSpacing { get; set; }
    /// <summary>How heavy its strokes are: 400 is the font as drawn, 700 bold — the font thickened, which a family's own bold font does better.</summary>
    public float Weight { get; set; } = 400f;
    public bool Italic { get; set; }
    public bool Underline { get; set; }
    public bool LineThrough { get; set; }

    public TextStyle SetFont(Font? font) => this with { Font = font };
    public TextStyle SetSize(float size) => this with { Size = size };
    public TextStyle SetColor(Color color) => this with { Color = color };
    public TextStyle SetLineHeight(float lineHeight) => this with { LineHeight = lineHeight };
    public TextStyle SetLetterSpacing(float spacing) => this with { LetterSpacing = spacing };
    public TextStyle SetWeight(float weight) => this with { Weight = weight };
    public TextStyle Bold() => this with { Weight = 700f };
    public TextStyle SetItalic(bool italic = true) => this with { Italic = italic };
    public TextStyle SetUnderline(bool underline = true) => this with { Underline = underline };
    public TextStyle SetLineThrough(bool lineThrough = true) => this with { LineThrough = lineThrough };

    internal readonly KuiTextStyle Native => new()
    {
        font = Font?.Native ?? IntPtr.Zero, size = Size, color = Color.Native, line_height = LineHeight, letter_spacing = LetterSpacing,
        weight = Weight, italic = KuiNative.Bool(Italic), underline = KuiNative.Bool(Underline), line_through = KuiNative.Bool(LineThrough),
    };

    internal static TextStyle From(KuiTextStyle s) => new()
    {
        Size = s.size, Color = Color.From(s.color), LineHeight = s.line_height, LetterSpacing = s.letter_spacing,
        Weight = s.weight > 0f ? s.weight : 400f, Italic = s.italic != 0, Underline = s.underline != 0, LineThrough = s.line_through != 0,
    };
}

/// <summary>kui::Paragraph: text laid out into lines — measured once, drawn any number of times.</summary>
public sealed unsafe class Paragraph : IDisposable
{
    private IntPtr _native;
    private readonly Font? _font;

    public Paragraph(string text, TextStyle style, float maxWidth = float.PositiveInfinity, TextAlign align = TextAlign.eStart)
    {
        var s = style.Native;
        _native = KuiNative.Check(KuiNative.kui_paragraph_new(text, &s, maxWidth, (uint)align));
        _font = style.Font;
    }

    ~Paragraph() => Free();

    internal IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    public Paragraph Layout(float maxWidth) { KuiNative.kui_paragraph_layout(Native, maxWidth); return this; }
    public Vector2 Size => KuiNative.kui_paragraph_size(Native).Managed();
    public ulong LineCount => KuiNative.kui_paragraph_line_count(Native);
    public float LineHeight => KuiNative.kui_paragraph_line_height(Native);
    public float MinIntrinsicWidth => KuiNative.kui_paragraph_min_intrinsic_width(Native);
    public float MaxIntrinsicWidth => KuiNative.kui_paragraph_max_intrinsic_width(Native);
    /// <summary>Top of the caret before the character at byte <paramref name="index"/> (UTF-8).</summary>
    public Vector2 CaretPosition(ulong index) => KuiNative.kui_paragraph_caret_position(Native, (nuint)index).Managed();
    /// <summary>The byte index (UTF-8) of the caret position nearest <paramref name="point"/>.</summary>
    public ulong IndexAt(Vector2 point) => KuiNative.kui_paragraph_index_at(Native, point.Native());

    public void Dispose()
    {
        Free();
        GC.SuppressFinalize(this);
    }

    private void Free()
    {
        if (_native == IntPtr.Zero) return;
        KuiNative.kui_paragraph_destroy(_native);
        _native = IntPtr.Zero;
        GC.KeepAlive(_font);
    }
}
