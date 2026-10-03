using System.Numerics;
using Koral.UI.Native;

namespace Koral.UI;

/// <summary>kui::Color: straight alpha, sRGB — as a colour picker or a CSS hex code gives it.</summary>
public readonly record struct Color(float R, float G, float B, float A = 1f)
{
    /// <summary>From 0xRRGGBB, opaque.</summary>
    public static Color Hex(uint rgb) => new(((rgb >> 16) & 0xff) / 255f, ((rgb >> 8) & 0xff) / 255f, (rgb & 0xff) / 255f);
    /// <summary>From 0xRRGGBBAA.</summary>
    public static Color HexA(uint rgba) => Hex(rgba >> 8).WithAlpha((rgba & 0xff) / 255f);
    public Color WithAlpha(float alpha) => this with { A = alpha };
    public bool Visible => A > 0f;

    public static readonly Color Transparent = new(0, 0, 0, 0);
    public static readonly Color Black = new(0, 0, 0);
    public static readonly Color White = new(1, 1, 1);
    public static readonly Color Red = Hex(0xF44336);
    public static readonly Color Green = Hex(0x4CAF50);
    public static readonly Color Blue = Hex(0x2196F3);

    internal KuiColor Native => new() { r = R, g = G, b = B, a = A };
    internal static Color From(KuiColor c) => new(c.r, c.g, c.b, c.a);
}

/// <summary>kui::Rect: by its edges, y growing downwards.</summary>
public readonly record struct Rect(float Left, float Top, float Right, float Bottom)
{
    public static Rect LTRB(float l, float t, float r, float b) => new(l, t, r, b);
    public static Rect XYWH(float x, float y, float w, float h) => new(x, y, x + w, y + h);
    public static Rect FromSize(Vector2 size) => new(0, 0, size.X, size.Y);
    public static Rect FromCenter(Vector2 c, float w, float h) => new(c.X - w * .5f, c.Y - h * .5f, c.X + w * .5f, c.Y + h * .5f);

    public float Width => Right - Left;
    public float Height => Bottom - Top;
    public Vector2 Size => new(Width, Height);
    public Vector2 TopLeft => new(Left, Top);
    public Vector2 Center => new((Left + Right) * .5f, (Top + Bottom) * .5f);
    public bool Empty => !(Right > Left && Bottom > Top);
    public bool Contains(Vector2 p) => p.X >= Left && p.X < Right && p.Y >= Top && p.Y < Bottom;
    public Rect Inflate(float by) => new(Left - by, Top - by, Right + by, Bottom + by);
    public Rect Deflate(float by) => Inflate(-by);
    public Rect Shift(Vector2 by) => new(Left + by.X, Top + by.Y, Right + by.X, Bottom + by.Y);

    internal KuiRect Native => new() { left = Left, top = Top, right = Right, bottom = Bottom };
    internal static Rect From(KuiRect r) => new(r.left, r.top, r.right, r.bottom);
}

/// <summary>kui::Radii: corner radii, clockwise from the top-left. A float converts to all four.</summary>
public readonly record struct Radii(float TopLeft, float TopRight, float BottomRight, float BottomLeft)
{
    public Radii(float all) : this(all, all, all, all) { }
    public static implicit operator Radii(float all) => new(all);
    internal KuiRadii Native => new() { top_left = TopLeft, top_right = TopRight, bottom_right = BottomRight, bottom_left = BottomLeft };
}

/// <summary>kui::RRect: a rectangle with rounded corners.</summary>
public readonly record struct RRect(Rect Rect, Radii Radii);

/// <summary>kui::Transform: x' = a x + c y + tx, y' = b x + d y + ty, composed like matrices.</summary>
public readonly record struct Transform(float A = 1, float B = 0, float C = 0, float D = 1, float Tx = 0, float Ty = 0)
{
    public static Transform Identity => new();
    public static Transform Translation(Vector2 t) => new(Tx: t.X, Ty: t.Y);
    public static Transform Scaling(Vector2 s) => new(s.X, 0, 0, s.Y);
    /// <summary>Clockwise on screen, since y grows downwards.</summary>
    public static Transform Rotation(float radians)
    {
        var (s, c) = MathF.SinCos(radians);
        return new(c, s, -s, c);
    }
    public Vector2 Apply(Vector2 p) => new(A * p.X + C * p.Y + Tx, B * p.X + D * p.Y + Ty);
    public static Transform operator *(Transform x, Transform o) => new(
        x.A * o.A + x.C * o.B, x.B * o.A + x.D * o.B,
        x.A * o.C + x.C * o.D, x.B * o.C + x.D * o.D,
        x.A * o.Tx + x.C * o.Ty + x.Tx, x.B * o.Tx + x.D * o.Ty + x.Ty);

    internal KuiTransform Native => new() { a = A, b = B, c = C, d = D, tx = Tx, ty = Ty };
    internal static Transform From(KuiTransform t) => new(t.a, t.b, t.c, t.d, t.tx, t.ty);
}

/// <summary>kui::EdgeInsets: space around something, per side.</summary>
public readonly record struct EdgeInsets(float Left, float Top, float Right, float Bottom)
{
    public static EdgeInsets All(float v) => new(v, v, v, v);
    public static EdgeInsets Symmetric(float horizontal, float vertical) => new(horizontal, vertical, horizontal, vertical);
    public static EdgeInsets Only(float left = 0, float top = 0, float right = 0, float bottom = 0) => new(left, top, right, bottom);
    public static implicit operator EdgeInsets(float all) => All(all);
    internal KuiEdgeInsets Native => new() { left = Left, top = Top, right = Right, bottom = Bottom };
}

/// <summary>kui::Alignment: (-1, -1) the top-left, (0, 0) the centre, (1, 1) the bottom-right.</summary>
public readonly record struct Alignment(float X, float Y)
{
    public static Alignment TopLeft => new(-1, -1);
    public static Alignment TopCenter => new(0, -1);
    public static Alignment TopRight => new(1, -1);
    public static Alignment CenterLeft => new(-1, 0);
    public static Alignment Center => new(0, 0);
    public static Alignment CenterRight => new(1, 0);
    public static Alignment BottomLeft => new(-1, 1);
    public static Alignment BottomCenter => new(0, 1);
    public static Alignment BottomRight => new(1, 1);
    internal KuiAlignment Native => new() { x = X, y = Y };
}

/// <summary>kui::BoxConstraints: the sizes a parent allows a child.</summary>
public readonly record struct BoxConstraints(float MinWidth = 0, float MaxWidth = float.PositiveInfinity,
                                            float MinHeight = 0, float MaxHeight = float.PositiveInfinity)
{
    public static BoxConstraints Tight(Vector2 size) => new(size.X, size.X, size.Y, size.Y);
    public static BoxConstraints Loose(Vector2 size) => new(0, size.X, 0, size.Y);
    internal KuiBoxConstraints Native => new() { min_width = MinWidth, max_width = MaxWidth, min_height = MinHeight, max_height = MaxHeight };
}

internal static class VectorExtensions
{
    public static KuiVec2 Native(this Vector2 v) => new() { x = v.X, y = v.Y };
    public static Vector2 Managed(this KuiVec2 v) => new(v.x, v.y);
}
