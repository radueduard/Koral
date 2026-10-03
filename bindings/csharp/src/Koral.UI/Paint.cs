using System.Numerics;
using Koral.UI.Native;

namespace Koral.UI;

/// <summary>kui::GradientStop: a colour at <see cref="Offset"/> (0 to 1) along a gradient.</summary>
public readonly record struct GradientStop(float Offset, Color Color);

/// <summary>
/// kui::Gradient: a fill that changes colour across a shape, in the shape's own coordinates. Up to eight
/// stops. Immutable, and shared by every paint given it.
/// </summary>
public sealed unsafe class Gradient
{
    internal readonly IntPtr Native;

    private Gradient(IntPtr native) => Native = KuiNative.Check(native);
    ~Gradient() => KuiNative.kui_gradient_release(Native);

    public static Gradient Linear(Vector2 from, Vector2 to, params GradientStop[] stops) =>
        Make(stops, (o, c, n) => KuiNative.kui_gradient_linear(from.Native(), to.Native(), o, c, n));
    public static Gradient Linear(Vector2 from, Vector2 to, Color a, Color b) => Linear(from, to, new GradientStop(0, a), new GradientStop(1, b));
    public static Gradient Radial(Vector2 center, float radius, params GradientStop[] stops) =>
        Make(stops, (o, c, n) => KuiNative.kui_gradient_radial(center.Native(), radius, o, c, n));
    public static Gradient Sweep(Vector2 center, float angle, params GradientStop[] stops) =>
        Make(stops, (o, c, n) => KuiNative.kui_gradient_sweep(center.Native(), angle, o, c, n));

    private delegate IntPtr Maker(float* offsets, KuiColor* colors, nuint count);

    private static Gradient Make(GradientStop[] stops, Maker make)
    {
        var offsets = stops.Select(s => s.Offset).ToArray();
        var colors = stops.Select(s => s.Color.Native).ToArray();
        fixed (float* o = offsets)
        fixed (KuiColor* c = colors)
            return new Gradient(make(o, c, (nuint)stops.Length));
    }
}

/// <summary>kui::Stroke: the outline along a shape's edge, centred on it. A width of 0 draws none.</summary>
public record struct Stroke()
{
    public float Width { get; set; }
    public Color Color { get; set; } = Color.Black;
    public StrokeCap Cap { get; set; } = StrokeCap.eButt;
    public StrokeJoin Join { get; set; } = StrokeJoin.eMiter;
    public float MiterLimit { get; set; } = 4f;
    public readonly bool Visible => Width > 0 && Color.Visible;
}

/// <summary>
/// kui::Paint: how a shape is drawn — what fills it, what outlines it. Both may be set: the stroke is
/// drawn over the fill. Chainable, as in C++: <c>Paint.Fill(blue).SetStroke(1, Color.White)</c>.
/// </summary>
public sealed class Paint
{
    public Color FillColor { get; set; } = Color.Transparent;
    /// <summary>Fills in place of <see cref="FillColor"/> when set.</summary>
    public Gradient? Gradient { get; set; }
    public Stroke Stroke { get; set; } = new();
    /// <summary>Multiplies fill and stroke alike.</summary>
    public float Opacity { get; set; } = 1f;

    public static Paint Fill(Color color) => new() { FillColor = color };
    public static Paint Stroked(Color color, float width) => new() { Stroke = new Stroke { Color = color, Width = width } };

    public Paint SetFill(Color color) { FillColor = color; return this; }
    public Paint SetGradient(Gradient gradient) { Gradient = gradient; return this; }
    public Paint SetStroke(float width, Color color) { Stroke = Stroke with { Width = width, Color = color }; return this; }
    public Paint SetStroke(Stroke stroke) { Stroke = stroke; return this; }
    public Paint SetOpacity(float opacity) { Opacity = opacity; return this; }

    public bool HasFill => Gradient is not null || FillColor.Visible;
    public bool HasStroke => Stroke.Visible;

    internal KuiPaint Native => new()
    {
        fill = FillColor.Native,
        gradient = Gradient?.Native ?? IntPtr.Zero,
        stroke = new KuiStroke { width = Stroke.Width, color = Stroke.Color.Native, cap = (uint)Stroke.Cap, join = (uint)Stroke.Join, miter_limit = Stroke.MiterLimit },
        opacity = Opacity,
    };
}

/// <summary>
/// kui::Path: an outline of lines and curves, filled or stroked by <see cref="Canvas.DrawPath"/>. Chainable:
/// <c>new Path().MoveTo(a).LineTo(b).QuadTo(c, d).Close()</c>.
/// </summary>
public sealed unsafe class Path : IDisposable
{
    private IntPtr _native = KuiNative.kui_path_new();

    ~Path() => Free();

    internal IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    public Path MoveTo(Vector2 p) { KuiNative.kui_path_move_to(Native, p.Native()); return this; }
    public Path LineTo(Vector2 p) { KuiNative.kui_path_line_to(Native, p.Native()); return this; }
    public Path QuadTo(Vector2 control, Vector2 p) { KuiNative.kui_path_quad_to(Native, control.Native(), p.Native()); return this; }
    public Path CubicTo(Vector2 control1, Vector2 control2, Vector2 p) { KuiNative.kui_path_cubic_to(Native, control1.Native(), control2.Native(), p.Native()); return this; }
    /// <summary>An arc of the circle at <paramref name="center"/>, joined by a line from where the path was.</summary>
    public Path ArcTo(Vector2 center, float radius, float start, float sweep) { KuiNative.kui_path_arc_to(Native, center.Native(), radius, start, sweep); return this; }
    /// <summary>A rounded corner: towards <paramref name="corner"/>, turning along an arc of <paramref name="radius"/> to head for <paramref name="to"/>.</summary>
    public Path ArcTo(Vector2 corner, Vector2 to, float radius) { KuiNative.kui_path_arc_to_corner(Native, corner.Native(), to.Native(), radius); return this; }
    public Path Close() { KuiNative.kui_path_close(Native); return this; }
    public Path AddRect(Rect rect) { KuiNative.kui_path_add_rect(Native, rect.Native); return this; }
    public Path AddRRect(RRect rrect) { KuiNative.kui_path_add_rrect(Native, rrect.Rect.Native, rrect.Radii.Native); return this; }
    public Path AddCircle(Vector2 center, float radius) { KuiNative.kui_path_add_circle(Native, center.Native(), radius); return this; }
    public Path AddOval(Rect rect) { KuiNative.kui_path_add_oval(Native, rect.Native); return this; }
    public Path AddPolygon(ReadOnlySpan<Vector2> points, bool close = true)
    {
        fixed (Vector2* p = points) KuiNative.kui_path_add_polygon(Native, (KuiVec2*)p, (nuint)points.Length, KuiNative.Bool(close));
        return this;
    }
    public Path SetFillRule(FillRule rule) { KuiNative.kui_path_set_fill_rule(Native, (uint)rule); return this; }
    public Rect Bounds => Rect.From(KuiNative.kui_path_bounds(Native));

    public void Dispose()
    {
        Free();
        GC.SuppressFinalize(this);
    }

    private void Free()
    {
        if (_native == IntPtr.Zero) return;
        KuiNative.kui_path_destroy(_native);
        _native = IntPtr.Zero;
    }
}
