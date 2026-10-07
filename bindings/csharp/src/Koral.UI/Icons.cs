using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Text;
using Koral.UI.Native;

namespace Koral.UI;

/// <summary>
/// kui::VectorImage: a picture of filled shapes — an SVG's paths, circles, rects and polygons — on a box of its own,
/// drawn in one colour at whatever size it is shown. The Material icons are ones (<see cref="Material"/>).
/// </summary>
public sealed unsafe class VectorImage
{
    internal readonly IntPtr Native;
    private VectorImage(IntPtr native) => Native = native;
    ~VectorImage() => KuiNative.kui_vector_image_release(Native);

    /// <summary><paramref name="svg"/>, the text of an SVG document, read. Null when nothing in it is drawn.</summary>
    public static VectorImage? FromSvg(string svg)
    {
        var native = KuiNative.kui_vector_image_from_svg(svg, (nuint)Encoding.UTF8.GetByteCount(svg));
        return native == IntPtr.Zero ? null : new VectorImage(native);
    }

    /// <summary>
    /// One of the Material icons kui carries — all of Jetpack Compose's, core and extended, the drawings Google publishes —
    /// by its name as Material spells it (<c>"arrow_back"</c>) or as Compose does (<c>"ArrowBack"</c>). Null when
    /// there is none of that name. Each is read once, and the same one handed out after.
    /// </summary>
    public static VectorImage? Material(string name, IconStyle style = IconStyle.eFilled) =>
        s_material.GetOrAdd((name, style), key =>
        {
            var native = KuiNative.kui_material_icon(key.Name, (uint)key.Style);
            return native == IntPtr.Zero ? null : new VectorImage(native);
        });

    /// <summary>The names of the Material icons kui carries, as Material spells them.</summary>
    public static IReadOnlyList<string> MaterialNames => s_names.Value;

    /// <summary>The box its shapes are drawn on, in their own units: an icon's is 0, 0 to 24, 24.</summary>
    public Rect ViewBox
    {
        get { var r = KuiNative.kui_vector_image_view_box(Native); return Rect.LTRB(r.left, r.top, r.right, r.bottom); }
    }

    private static readonly ConcurrentDictionary<(string Name, IconStyle Style), VectorImage?> s_material = new();
    private static readonly Lazy<IReadOnlyList<string>> s_names = new(() =>
    {
        var count = (int)KuiNative.kui_material_icon_count();
        var names = new string[count];
        for (var i = 0; i < count; ++i) names[i] = Marshal.PtrToStringUTF8((IntPtr)KuiNative.kui_material_icon_name((nuint)i)) ?? "";
        return names;
    });
}
