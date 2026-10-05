//
// Glass: what the Cupertino design's controls are made of, after Apple's Liquid Glass — a clear or
// tinted body, lighter at the top, with a bright rim that catches the light along its upper edge and
// a soft shadow under it. Asked to (blur), it shows what is behind it too, blurred and bent at the edge
// as glass bends it (Canvas::DrawBackdrop): for the pieces big enough, or far enough off the page, for
// that to be seen — a menu, a thumb in hand. A button lying on a plain panel has nothing behind it to show.
//
#pragma once

#include <kui/widgets.h>

namespace kui::detail {
    /**
     * @p shape as a piece of glass: clear, or (with a @p tint that can be seen) the tint's. @p lift is how
     * much lighter than at rest — under the pointer more, pressed less.
     */
    inline void PaintGlass(Canvas& canvas, const Theme& t, const RRect& shape, const Color tint = colors::Transparent,
                           const float lift = 0.f, const bool shadow = true, const float opacity = 1.f, const float blur = 0.f)
    {
        const bool dark = t.IsDark();
        const Rect& r = shape.rect;
        const auto lerp = [](const Color a, const Color b, const float f) {
            return Color(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f);
        };
        Color top, bottom;
        if (tint.Visible()) {
            top = lerp(tint, colors::White, 0.22f + lift).WithAlpha(0.94f * opacity);
            bottom = lerp(tint, colors::White, std::max(lift, 0.f)).WithAlpha(0.84f * opacity);
        } else if (dark) {
            top = colors::White.WithAlpha((0.17f + lift) * opacity);
            bottom = colors::White.WithAlpha((0.06f + lift) * opacity);
        } else {
            top = colors::White.WithAlpha(std::min(0.82f + lift, 1.f) * opacity);
            bottom = colors::White.WithAlpha(std::min(0.52f + lift, 1.f) * opacity);
        }
        if (shadow) canvas.DrawShadow(shape, colors::Black.WithAlpha((dark ? 0.34f : 0.13f) * opacity), 6.f, { 0.f, 2.f });
        if (blur > 0.f) canvas.DrawBackdrop(shape, Backdrop {}.SetBlur(blur).SetRefraction(std::min(r.Height() * 0.3f, 12.f)));
        canvas.DrawRRect(shape, Paint {}.SetGradient(Gradient::Linear({ r.left, r.top }, { r.left, r.bottom }, top, bottom)));
        // The rim: faint all round, and bright where the light falls on it, along the top.
        const RRect rim { r.Deflate(0.5f), shape.radii };
        canvas.DrawRRect(rim, Paint::Stroked(colors::White.WithAlpha((dark ? 0.16f : 0.7f) * opacity), 1.f));
        canvas.Save();
        canvas.ClipRect(Rect::LTRB(r.left, r.top, r.right, r.top + r.Height() * 0.42f));
        canvas.DrawRRect(rim, Paint::Stroked(colors::White.WithAlpha((dark ? 0.46f : 1.f) * opacity), 1.f));
        canvas.Restore();
    }
}
