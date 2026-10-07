using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::Picture: what a canvas recorded — immutable, cheap to keep, drawn again with no work.
/// </summary>
public sealed class Picture
{
    internal readonly IntPtr Native;
    internal Picture(IntPtr native) => Native = KuiNative.Check(native);
    ~Picture() => KuiNative.kui_picture_release(Native);

    public Rect Bounds => Rect.From(KuiNative.kui_picture_bounds(Native));
    public ulong InstanceCount => KuiNative.kui_picture_instance_count(Native);
}

/// <summary>
/// kui::Layer: a retained piece of the frame — a picture, and where it is. Moving or fading one re-records
/// nothing. Chainable: <c>layer.SetPicture(p).SetOpacity(0.5f)</c>.
/// </summary>
public sealed class Layer
{
    internal readonly IntPtr Native = KuiNative.kui_layer_create();
    ~Layer() => KuiNative.kui_layer_release(Native);

    public static Layer Create() => new();

    public Layer SetPicture(Picture? picture) { KuiNative.kui_layer_set_picture(Native, picture?.Native ?? IntPtr.Zero); _picture = picture; return this; }
    public Layer SetTransform(Transform transform) { KuiNative.kui_layer_set_transform(Native, transform.Native); return this; }
    public Layer SetOpacity(float opacity) { KuiNative.kui_layer_set_opacity(Native, opacity); return this; }
    public Transform Transform => Transform.From(KuiNative.kui_layer_get_transform(Native));
    public float Opacity => KuiNative.kui_layer_opacity(Native);
    public Picture? Picture => _picture;

    private Picture? _picture;
}

/// <summary>
/// kui::ElementShader: a fragment shader that fills a rectangle — the UI's base element. GLSL with
/// <c>#include &lt;koralUI.glsl&gt;</c>, or Slang with <c>import koralUI;</c>. Edits to the file are picked up
/// while running.
/// </summary>
public sealed class ElementShader
{
    internal readonly IntPtr Native;
    private ElementShader(IntPtr native) => Native = KuiNative.Check(native);
    ~ElementShader() => KuiNative.kui_element_shader_release(Native);

    /// <summary>A GLSL file (anything but .slang), or a Slang module whose <paramref name="entry"/> is the fragment entry point.</summary>
    public static ElementShader Load(string path, string? entry = null) => new(KuiNative.kui_element_shader_load(path, entry));
    public bool Valid => KuiNative.kui_element_shader_valid(Native).AsBool();
}

/// <summary>
/// kui::Canvas: records drawing into a <see cref="Picture"/>. Every call returns the canvas, so drawing
/// chains — as in C++:
/// <code>
/// canvas.DrawRRect(new(Rect.XYWH(20, 20, 160, 48), 12), Paint.Fill(Color.Hex(0x3F51B5)))
///       .DrawLine(new(20, 90), new(180, 90), Paint.Stroked(Color.White, 2))
///       .BeginPath().MoveTo(a).DrawLineTo(b).DrawArcTo(corner, c, 8).ClosePath().Fill(paint).Stroke(outline);
/// </code>
/// </summary>
public sealed unsafe class Canvas : IDisposable
{
    private IntPtr _native;
    private readonly bool _borrowed;

    public Canvas() => _native = KuiNative.kui_canvas_new();
    /// <summary>A canvas the UI lends a painter: valid during the call only.</summary>
    internal Canvas(IntPtr borrowed)
    {
        _native = borrowed;
        _borrowed = true;
    }

    ~Canvas()
    {
        if (!_borrowed && _native != IntPtr.Zero) KuiNative.kui_canvas_destroy(_native);
    }

    internal IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    public void Dispose()
    {
        if (!_borrowed && _native != IntPtr.Zero) KuiNative.kui_canvas_destroy(_native);
        _native = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    internal void Expire() => _native = IntPtr.Zero;

    private Canvas Done()
    {
        KuiNative.Check();
        return this;
    }

    // -- state
    public Canvas Save() { KuiNative.kui_canvas_save(Native); return this; }
    public Canvas Restore() { KuiNative.kui_canvas_restore(Native); return this; }
    public ulong SaveCount => KuiNative.kui_canvas_save_count(Native);
    public Canvas Translate(Vec2 by) { KuiNative.kui_canvas_translate(Native, by.Native()); return this; }
    public Canvas Scale(Vec2 by) { KuiNative.kui_canvas_scale(Native, by.Native()); return this; }
    public Canvas Scale(float by) => Scale(new Vec2(by));
    /// <summary>Clockwise on screen, in radians.</summary>
    public Canvas Rotate(float radians) { KuiNative.kui_canvas_rotate(Native, radians); return this; }
    public Canvas Concat(Transform transform) { KuiNative.kui_canvas_concat(Native, transform.Native); return this; }
    public Canvas SetTransform(Transform transform) { KuiNative.kui_canvas_set_transform(Native, transform.Native); return this; }
    public Transform CurrentTransform => Transform.From(KuiNative.kui_canvas_current_transform(Native));
    public Canvas ClipRect(Rect rect) { KuiNative.kui_canvas_clip_rect(Native, rect.Native); return this; }
    public Canvas ClipRRect(RRect rrect) { KuiNative.kui_canvas_clip_rrect(Native, rrect.Rect.Native, rrect.Radii.Native); return this; }
    public Canvas SetTolerance(float tolerance) { KuiNative.kui_canvas_set_tolerance(Native, tolerance); return this; }

    // -- shapes
    public Canvas DrawRect(Rect rect, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_rect(Native, rect.Native, &p); return Done(); }
    public Canvas DrawRRect(RRect rrect, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_rrect(Native, rrect.Rect.Native, rrect.Radii.Native, &p); return Done(); }
    public Canvas DrawCircle(Vec2 center, float radius, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_circle(Native, center.Native(), radius, &p); return Done(); }
    public Canvas DrawOval(Rect rect, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_oval(Native, rect.Native, &p); return Done(); }
    /// <summary>An arc of the circle at <paramref name="center"/>, clockwise; a pie slice with <paramref name="useCenter"/>, else only stroked.</summary>
    public Canvas DrawArc(Vec2 center, float radius, float start, float sweep, bool useCenter, Paint paint)
    {
        var p = paint.Native;
        KuiNative.kui_canvas_draw_arc(Native, center.Native(), radius, start, sweep, KuiNative.Bool(useCenter), &p);
        return Done();
    }
    public Canvas DrawLine(Vec2 from, Vec2 to, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_line(Native, from.Native(), to.Native(), &p); return Done(); }
    public Canvas DrawTriangle(Vec2 a, Vec2 b, Vec2 c, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_triangle(Native, a.Native(), b.Native(), c.Native(), &p); return Done(); }
    public Canvas DrawQuadraticBezier(Vec2 from, Vec2 control, Vec2 to, Paint paint)
    {
        var p = paint.Native;
        KuiNative.kui_canvas_draw_quadratic_bezier(Native, from.Native(), control.Native(), to.Native(), &p);
        return Done();
    }
    public Canvas DrawCubicBezier(Vec2 from, Vec2 control1, Vec2 control2, Vec2 to, Paint paint)
    {
        var p = paint.Native;
        KuiNative.kui_canvas_draw_cubic_bezier(Native, from.Native(), control1.Native(), control2.Native(), to.Native(), &p);
        return Done();
    }
    public Canvas DrawPolyline(ReadOnlySpan<Vec2> points, Paint paint)
    {
        var p = paint.Native;
        fixed (Vec2* v = points) KuiNative.kui_canvas_draw_polyline(Native, (KuiVec2*)v, (nuint)points.Length, &p);
        return Done();
    }
    public Canvas DrawPolygon(ReadOnlySpan<Vec2> points, Paint paint)
    {
        var p = paint.Native;
        fixed (Vec2* v = points) KuiNative.kui_canvas_draw_polygon(Native, (KuiVec2*)v, (nuint)points.Length, &p);
        return Done();
    }
    public Canvas DrawPath(Path path, Paint paint) { var p = paint.Native; KuiNative.kui_canvas_draw_path(Native, path.Native, &p); return Done(); }
    public Canvas DrawShadow(RRect rrect, Color color, float blur, Vec2 offset = default, float spread = 0)
    {
        KuiNative.kui_canvas_draw_shadow(Native, rrect.Rect.Native, rrect.Radii.Native, color.Native, blur, offset.Native(), spread);
        return Done();
    }

    // -- images and text
    /// <summary><paramref name="image"/> (the part <paramref name="source"/> covers, in pixels; all of it by default) over <paramref name="destination"/>.</summary>
    public Canvas DrawImage(Image image, Rect destination, Rect source = default, Color? tint = null)
    {
        KuiNative.kui_canvas_draw_image(Native, Resource.HandleOf(image), destination.Native, source.Native, (tint ?? Color.White).Native);
        return Done();
    }
    /// <summary><paramref name="image"/> — an icon, an SVG — stretched over <paramref name="rect"/> in <paramref name="tint"/>.</summary>
    public Canvas DrawVectorImage(VectorImage image, Rect rect, Color tint)
    {
        KuiNative.kui_canvas_draw_vector_image(Native, image.Native, rect.Native, tint.Native);
        GC.KeepAlive(image);
        return Done();
    }
    public Canvas DrawParagraph(Paragraph paragraph, Vec2 position) { KuiNative.kui_canvas_draw_paragraph(Native, paragraph.Native, position.Native()); return Done(); }
    /// <summary>One line of text. Lays it out every call: keep a <see cref="Paragraph"/> for text drawn often.</summary>
    public Canvas DrawText(string text, Vec2 position, TextStyle style)
    {
        var s = style.Native;
        KuiNative.kui_canvas_draw_text(Native, text, position.Native(), &s);
        GC.KeepAlive(style.Font);
        return Done();
    }

    // -- elements and layers
    /// <summary>An element: <paramref name="shader"/> fills <paramref name="rect"/>, given <paramref name="parameters"/> (the shader's struct, std430).</summary>
    public Canvas DrawElement<T>(ElementShader shader, Rect rect, in T parameters, Radii radii = default, float opacity = 1f) where T : unmanaged
    {
        fixed (T* p = &parameters) KuiNative.kui_canvas_draw_element(Native, shader.Native, rect.Native, p, (nuint)sizeof(T), radii.Native, opacity);
        return Done();
    }
    public Canvas DrawElement(ElementShader shader, Rect rect, Radii radii = default, float opacity = 1f)
    {
        KuiNative.kui_canvas_draw_element(Native, shader.Native, rect.Native, null, 0, radii.Native, opacity);
        return Done();
    }
    /// <summary>Another layer, under the current transform and clip: shown as it is each frame.</summary>
    public Canvas DrawLayer(Layer layer) { KuiNative.kui_canvas_draw_layer(Native, layer.Native); return Done(); }
    public Canvas DrawPicture(Picture picture) { KuiNative.kui_canvas_draw_picture(Native, picture.Native); return Done(); }

    // -- the pen
    /// <summary>Starts the pen's path empty. The pen draws a path a segment at a time; Fill and Stroke draw it.</summary>
    public Canvas BeginPath() { KuiNative.kui_canvas_begin_path(Native); return this; }
    public Canvas MoveTo(Vec2 point) { KuiNative.kui_canvas_move_to(Native, point.Native()); return this; }
    public Canvas DrawLineTo(Vec2 point) { KuiNative.kui_canvas_draw_line_to(Native, point.Native()); return this; }
    public Canvas DrawQuadTo(Vec2 control, Vec2 point) { KuiNative.kui_canvas_draw_quad_to(Native, control.Native(), point.Native()); return this; }
    public Canvas DrawCubicTo(Vec2 control1, Vec2 control2, Vec2 point)
    {
        KuiNative.kui_canvas_draw_cubic_to(Native, control1.Native(), control2.Native(), point.Native());
        return this;
    }
    /// <summary>An arc of the circle at <paramref name="center"/>, joined to the pen by a line.</summary>
    public Canvas DrawArcTo(Vec2 center, float radius, float start, float sweep)
    {
        KuiNative.kui_canvas_draw_arc_to(Native, center.Native(), radius, start, sweep);
        return this;
    }
    /// <summary>A rounded corner: towards <paramref name="corner"/>, turning along an arc of <paramref name="radius"/> to head for <paramref name="to"/>.</summary>
    public Canvas DrawArcTo(Vec2 corner, Vec2 to, float radius)
    {
        KuiNative.kui_canvas_draw_arc_to_corner(Native, corner.Native(), to.Native(), radius);
        return this;
    }
    public Canvas ClosePath() { KuiNative.kui_canvas_close_path(Native); return this; }
    /// <summary>Fills the pen's path with <paramref name="paint"/>'s fill.</summary>
    public Canvas Fill(Paint paint) { var p = paint.Native; KuiNative.kui_canvas_fill(Native, &p); return Done(); }
    /// <summary>Outlines the pen's path with <paramref name="paint"/>'s stroke (or a hairline of its fill colour).</summary>
    public Canvas Stroke(Paint paint) { var p = paint.Native; KuiNative.kui_canvas_stroke(Native, &p); return Done(); }

    /// <summary>The recording, as a picture; the canvas is empty again.</summary>
    public Picture Finish() => new(KuiNative.kui_canvas_finish(Native));
}
