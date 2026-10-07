//
// koral-ui: paths, flattening, and the tessellator that fills them.
//

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>

#include "tessellate.h"

namespace kui
{
    // ---- building -----------------------------------------------------------------------------------

    Path& Path::MoveTo(const kor::Vec2 p)
    {
        _verbs.push_back(Verb::eMove);
        _points.push_back(p);
        _start = _current = p;
        return *this;
    }

    Path& Path::LineTo(const kor::Vec2 p)
    {
        if (_verbs.empty()) MoveTo(_current);
        _verbs.push_back(Verb::eLine);
        _points.push_back(p);
        _current = p;
        return *this;
    }

    Path& Path::QuadTo(const kor::Vec2 control, const kor::Vec2 p)
    {
        if (_verbs.empty()) MoveTo(_current);
        _verbs.push_back(Verb::eQuad);
        _points.push_back(control);
        _points.push_back(p);
        _current = p;
        return *this;
    }

    Path& Path::CubicTo(const kor::Vec2 control1, const kor::Vec2 control2, const kor::Vec2 p)
    {
        if (_verbs.empty()) MoveTo(_current);
        _verbs.push_back(Verb::eCubic);
        _points.push_back(control1);
        _points.push_back(control2);
        _points.push_back(p);
        _current = p;
        return *this;
    }

    Path& Path::ArcTo(const kor::Vec2 center, const float radius, const float start, const float sweep)
    {
        // As cubics of at most a quarter turn each, which stay within a hair of the circle.
        const kor::Vec2 first = center + radius * kor::Vec2(std::cos(start), std::sin(start));
        if (_verbs.empty()) MoveTo(first);
        else LineTo(first);
        const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi_v<float> * 0.5f))));
        const float step = sweep / static_cast<float>(pieces);
        const float k = 4.f / 3.f * std::tan(step / 4.f);
        float angle = start;
        for (int i = 0; i < pieces; ++i) {
            const kor::Vec2 d0 { std::cos(angle), std::sin(angle) };
            const kor::Vec2 d1 { std::cos(angle + step), std::sin(angle + step) };
            const kor::Vec2 p0 = center + radius * d0, p3 = center + radius * d1;
            const kor::Vec2 c1 = p0 + radius * k * kor::Vec2(-d0.y, d0.x);
            const kor::Vec2 c2 = p3 - radius * k * kor::Vec2(-d1.y, d1.x);
            CubicTo(c1, c2, p3);
            angle += step;
        }
        return *this;
    }

    Path& Path::ArcTo(const kor::Vec2 corner, const kor::Vec2 to, const float radius)
    {
        if (_verbs.empty()) MoveTo(corner);
        const kor::Vec2 from = _current;
        const kor::Vec2 a = from - corner, b = to - corner;
        const float la = kor::Length(a), lb = kor::Length(b);
        // Nowhere to turn — a point on top of another, or no turn at all: a plain line to the corner.
        if (radius <= 0.f || la < 1e-6f || lb < 1e-6f) return LineTo(corner);
        const kor::Vec2 d0 = a / la, d1 = b / lb;
        const float cosTurn = std::clamp(kor::Dot(d0, d1), -1.f, 1.f);
        const float angle = std::acos(cosTurn);   // between the two legs
        if (angle < 1e-4f || std::abs(angle - std::numbers::pi_v<float>) < 1e-4f) return LineTo(corner);
        // The circle touching both legs sits on the corner's bisector.
        const float tangent = radius / std::tan(angle * 0.5f);
        const kor::Vec2 t0 = corner + d0 * tangent, t1 = corner + d1 * tangent;
        const kor::Vec2 center = corner + kor::Normalize(d0 + d1) * (radius / std::sin(angle * 0.5f));
        const float start = std::atan2(t0.y - center.y, t0.x - center.x);
        float sweep = std::atan2(t1.y - center.y, t1.x - center.x) - start;
        while (sweep > std::numbers::pi_v<float>) sweep -= 2.f * std::numbers::pi_v<float>;
        while (sweep < -std::numbers::pi_v<float>) sweep += 2.f * std::numbers::pi_v<float>;
        return ArcTo(center, radius, start, sweep);
    }

    Path& Path::Close()
    {
        if (!_verbs.empty() && _verbs.back() != Verb::eClose) {
            _verbs.push_back(Verb::eClose);
            _current = _start;
        }
        return *this;
    }

    Path& Path::AddRect(const Rect& r)
    {
        MoveTo({ r.left, r.top }).LineTo({ r.right, r.top }).LineTo({ r.right, r.bottom }).LineTo({ r.left, r.bottom });
        return Close();
    }

    Path& Path::AddRRect(const RRect& rr)
    {
        const Rect& r = rr.rect;
        const float limit = std::min(r.Width(), r.Height()) * 0.5f;
        const float tl = std::min(rr.radii.topLeft, limit), tr = std::min(rr.radii.topRight, limit);
        const float br = std::min(rr.radii.bottomRight, limit), bl = std::min(rr.radii.bottomLeft, limit);
        constexpr float pi = std::numbers::pi_v<float>;
        MoveTo({ r.left + tl, r.top });
        LineTo({ r.right - tr, r.top });
        if (tr > 0.f) ArcTo({ r.right - tr, r.top + tr }, tr, -pi * 0.5f, pi * 0.5f);
        LineTo({ r.right, r.bottom - br });
        if (br > 0.f) ArcTo({ r.right - br, r.bottom - br }, br, 0.f, pi * 0.5f);
        LineTo({ r.left + bl, r.bottom });
        if (bl > 0.f) ArcTo({ r.left + bl, r.bottom - bl }, bl, pi * 0.5f, pi * 0.5f);
        LineTo({ r.left, r.top + tl });
        if (tl > 0.f) ArcTo({ r.left + tl, r.top + tl }, tl, pi, pi * 0.5f);
        return Close();
    }

    Path& Path::AddCircle(const kor::Vec2 center, const float radius)
    {
        _verbs.push_back(Verb::eMove);
        _points.push_back(center + kor::Vec2(radius, 0.f));
        _start = _current = _points.back();
        ArcTo(center, radius, 0.f, 2.f * std::numbers::pi_v<float>);
        return Close();
    }

    Path& Path::AddOval(const Rect& rect)
    {
        // A circle, stretched: the arc's control points scale with it exactly.
        const kor::Vec2 c = rect.Center(), h = rect.Size() * 0.5f;
        constexpr float k = 0.5522847498f;
        MoveTo({ c.x + h.x, c.y });
        CubicTo({ c.x + h.x, c.y + h.y * k }, { c.x + h.x * k, c.y + h.y }, { c.x, c.y + h.y });
        CubicTo({ c.x - h.x * k, c.y + h.y }, { c.x - h.x, c.y + h.y * k }, { c.x - h.x, c.y });
        CubicTo({ c.x - h.x, c.y - h.y * k }, { c.x - h.x * k, c.y - h.y }, { c.x, c.y - h.y });
        CubicTo({ c.x + h.x * k, c.y - h.y }, { c.x + h.x, c.y - h.y * k }, { c.x + h.x, c.y });
        return Close();
    }

    Path& Path::AddPolygon(const std::span<const kor::Vec2> points, const bool close)
    {
        if (points.empty()) return *this;
        MoveTo(points.front());
        for (std::size_t i = 1; i < points.size(); ++i) LineTo(points[i]);
        if (close) Close();
        return *this;
    }

    Rect Path::Bounds() const
    {
        if (_points.empty()) return {};
        Rect r { _points[0].x, _points[0].y, _points[0].x, _points[0].y };
        for (const auto& p : _points) {
            r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y);
            r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y);
        }
        return r;
    }

    std::vector<Path::Contour> Path::Flatten(const float tolerance) const
    {
        const float tol = std::max(tolerance, 1e-4f);
        std::vector<Contour> contours;
        std::size_t p = 0;
        kor::Vec2 current {};
        const auto open = [&]() -> Contour& {
            if (contours.empty() || contours.back().closed) contours.push_back({ { current }, false });
            return contours.back();
        };
        for (const Verb verb : _verbs) {
            switch (verb) {
            case Verb::eMove:
                current = _points[p++];
                // A move after an unclosed contour starts the next one.
                contours.push_back({ { current }, false });
                break;
            case Verb::eLine: {
                current = _points[p++];
                open().points.push_back(current);
                break;
            }
            case Verb::eQuad: {
                const kor::Vec2 p0 = current, p1 = _points[p], p2 = _points[p + 1];
                p += 2;
                // n pieces put each within dd / (8 n^2) of the curve.
                const float dd = kor::Length(p0 - 2.f * p1 + p2);
                const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(dd / (8.f * tol)))), 1, 256);
                auto& c = open();
                for (int i = 1; i <= n; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(n), u = 1.f - t;
                    c.points.push_back(u * u * p0 + 2.f * u * t * p1 + t * t * p2);
                }
                current = p2;
                break;
            }
            case Verb::eCubic: {
                const kor::Vec2 p0 = current, p1 = _points[p], p2 = _points[p + 1], p3 = _points[p + 2];
                p += 3;
                const float dd = std::max(kor::Length(p0 - 2.f * p1 + p2), kor::Length(p1 - 2.f * p2 + p3));
                const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(3.f * dd / (4.f * tol)))), 1, 256);
                auto& c = open();
                for (int i = 1; i <= n; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(n), u = 1.f - t;
                    c.points.push_back(u * u * u * p0 + 3.f * u * u * t * p1 + 3.f * u * t * t * p2 + t * t * t * p3);
                }
                current = p3;
                break;
            }
            case Verb::eClose:
                if (!contours.empty() && !contours.back().closed) {
                    auto& c = contours.back();
                    // The closing point repeats the first; the contour knows it is closed instead.
                    if (c.points.size() > 1 && kor::Length(c.points.back() - c.points.front()) < 1e-6f) c.points.pop_back();
                    c.closed = true;
                    current = c.points.front();
                }
                break;
            }
        }
        std::erase_if(contours, [](const Contour& c) { return c.points.size() < 2; });
        return contours;
    }
}

namespace kui::detail
{
    namespace {
        struct Edge {
            kor::Vec2 top, bottom;   // top.y < bottom.y
            int winding;             // +1 for an edge going down, -1 up
            [[nodiscard]] float XAt(const float y) const {
                const float t = (y - top.y) / (bottom.y - top.y);
                return top.x + (bottom.x - top.x) * t;
            }
        };

        std::vector<Edge> edgesOf(const std::vector<Path::Contour>& contours)
        {
            std::vector<Edge> edges;
            for (const auto& c : contours) {
                const std::size_t n = c.points.size();
                // Filling treats every contour as closed, as a fill must.
                for (std::size_t i = 0; i < n; ++i) {
                    const kor::Vec2 a = c.points[i], b = c.points[(i + 1) % n];
                    if (a.y == b.y) continue;
                    if (a.y < b.y) edges.push_back({ a, b, 1 });
                    else edges.push_back({ b, a, -1 });
                }
            }
            return edges;
        }

        bool inside(const int winding, const FillRule rule) { return rule == FillRule::eNonZero ? winding != 0 : (winding & 1) != 0; }
    }

    void TessellateFill(const std::vector<Path::Contour>& contours, const FillRule rule, const float fringe, Mesh& out)
    {
        const std::vector<Edge> edges = edgesOf(contours);
        if (edges.empty()) return;

        // Every y where something starts, ends or crosses: between two of them no edges cross, so
        // each span between neighbouring edges is a trapezoid.
        std::vector<float> ys;
        ys.reserve(edges.size() * 2);
        for (const auto& e : edges) { ys.push_back(e.top.y); ys.push_back(e.bottom.y); }
        for (std::size_t i = 0; i < edges.size(); ++i) {
            for (std::size_t j = i + 1; j < edges.size(); ++j) {
                const Edge& a = edges[i];
                const Edge& b = edges[j];
                const float y0 = std::max(a.top.y, b.top.y), y1 = std::min(a.bottom.y, b.bottom.y);
                if (y1 <= y0) continue;
                const float d0 = a.XAt(y0) - b.XAt(y0), d1 = a.XAt(y1) - b.XAt(y1);
                if ((d0 < 0.f && d1 > 0.f) || (d0 > 0.f && d1 < 0.f)) ys.push_back(y0 + (y1 - y0) * (d0 / (d0 - d1)));
            }
        }
        std::ranges::sort(ys);
        ys.erase(std::unique(ys.begin(), ys.end(), [](const float a, const float b) { return b - a < 1e-5f; }), ys.end());

        // Edges by where they start, so each band only looks at the ones that can reach it.
        std::vector<std::size_t> order(edges.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::ranges::sort(order, [&](const std::size_t a, const std::size_t b) { return edges[a].top.y < edges[b].top.y; });

        struct Crossing { float x0, x1, mid; int winding; };
        struct Span { float left, right; };
        // A boundary piece's outer end at the bottom of a band, for joining the next band's piece to it.
        struct Side { float inner; kor::Vec2 outer; bool left; };
        std::vector<Crossing> active;
        std::vector<std::size_t> live;
        std::vector<Span> previousSpans;     // the inside at the bottom of the band before
        std::vector<Side> previousSides;
        std::size_t next = 0;

        // The fringe is built from the very vertices the trapezoids use, so the two meet without a
        // crack; and it is only drawn where the inside meets the outside, decided by the winding, so
        // the pieces of a union never fringe each other.
        const auto strip = [&](const kor::Vec2 a, const kor::Vec2 b, const kor::Vec2 na, const kor::Vec2 nb) {
            out.Add(a, 1.f); out.Add(b, 1.f); out.Add(b + nb * fringe, 0.f);
            out.Add(a, 1.f); out.Add(b + nb * fringe, 0.f); out.Add(a + na * fringe, 0.f);
        };
        // Where the inside at one y differs from the inside at the same y one band up: a horizontal
        // boundary, facing up (@p up) or down. Its ends reach a fringe further, under the slanted fringes'.
        const auto horizontal = [&](const std::vector<Span>& inside, const std::vector<Span>& other, const float y, const bool up) {
            const kor::Vec2 n { 0.f, up ? -1.f : 1.f };
            const auto emit = [&](const float a, const float b) {
                // Where two edges meet at a vertex their x there can differ by rounding alone: that is
                // no boundary, and must not grow a fringe.
                if (b - a < 1e-3f) return;
                // A real one reaches a fringe further at each end, under its neighbours' fringes, so a
                // square corner has no notch.
                const float grow = b - a > fringe ? fringe : 0.f;
                out.Add({ a, y }, 1.f); out.Add({ b, y }, 1.f); out.Add(kor::Vec2(b + grow, y) + n * fringe, 0.f);
                out.Add({ a, y }, 1.f); out.Add(kor::Vec2(b + grow, y) + n * fringe, 0.f); out.Add(kor::Vec2(a - grow, y) + n * fringe, 0.f);
            };
            for (const Span& s : inside) {
                float from = s.left;
                for (const Span& o : other) {
                    if (o.right <= from || o.left >= s.right) continue;
                    if (o.left > from) emit(from, o.left);
                    from = std::max(from, o.right);
                }
                if (from < s.right) emit(from, s.right);
            }
        };

        for (std::size_t band = 0; band + 1 < ys.size(); ++band) {
            const float y0 = ys[band], y1 = ys[band + 1], ym = (y0 + y1) * 0.5f;
            while (next < order.size() && edges[order[next]].top.y <= ym) live.push_back(order[next++]);
            std::erase_if(live, [&](const std::size_t i) { return edges[i].bottom.y <= ym; });
            active.clear();
            for (const std::size_t i : live) {
                const Edge& e = edges[i];
                if (e.top.y > ym) continue;
                active.push_back({ e.XAt(y0), e.XAt(y1), e.XAt(ym), e.winding });
            }
            std::ranges::sort(active, [](const Crossing& a, const Crossing& b) { return a.mid < b.mid; });

            std::vector<Span> topSpans, bottomSpans;
            std::vector<Side> sides;
            int winding = 0;
            std::size_t spanStart = 0;
            for (std::size_t i = 0; i < active.size(); ++i) {
                const bool before = inside(winding, rule);
                winding += active[i].winding;
                const bool after = inside(winding, rule);
                if (!before && after) spanStart = i;
                if (after && i + 1 < active.size()) {
                    const Crossing& l = active[i];
                    const Crossing& r = active[i + 1];
                    out.Add({ l.x0, y0 }, 1.f); out.Add({ r.x0, y0 }, 1.f); out.Add({ r.x1, y1 }, 1.f);
                    out.Add({ l.x0, y0 }, 1.f); out.Add({ r.x1, y1 }, 1.f); out.Add({ l.x1, y1 }, 1.f);
                }
                if (before && !after) {
                    const Crossing& l = active[spanStart];
                    const Crossing& r = active[i];
                    topSpans.push_back({ l.x0, r.x0 });
                    bottomSpans.push_back({ l.x1, r.x1 });
                    if (fringe > 0.f) {
                        // Outwards from each side: away from the span, square to the edge.
                        const auto outward = [&](const Crossing& c, const float sign) {
                            kor::Vec2 n { y1 - y0, -(c.x1 - c.x0) };
                            const float len = kor::Length(n);
                            n = len > 0.f ? n / len : kor::Vec2(1.f, 0.f);
                            return n.x * sign < 0.f ? -n : n;
                        };
                        const kor::Vec2 nl = outward(l, -1.f), nr = outward(r, 1.f);
                        strip({ l.x0, y0 }, { l.x1, y1 }, nl, nl);
                        strip({ r.x1, y1 }, { r.x0, y0 }, nr, nr);
                        sides.push_back({ l.x1, kor::Vec2(l.x1, y1) + nl * fringe, true });
                        sides.push_back({ r.x1, kor::Vec2(r.x1, y1) + nr * fringe, false });
                        // Joined to the piece of boundary ending where this one starts: the wedge
                        // between their fringes at a convex corner is filled.
                        for (const Side& s : previousSides) {
                            for (const auto& [x, n, left] : { std::tuple { l.x0, nl, true }, std::tuple { r.x0, nr, false } }) {
                                if (s.left != left || std::abs(s.inner - x) > 1e-3f) continue;
                                const kor::Vec2 inner { x, y0 }, outer = inner + n * fringe;
                                if (kor::Length(outer - s.outer) > 1e-4f) { out.Add(inner, 1.f); out.Add(s.outer, 0.f); out.Add(outer, 0.f); }
                            }
                        }
                    }
                }
            }
            if (fringe > 0.f) {
                horizontal(topSpans, previousSpans, y0, true);
                horizontal(previousSpans, topSpans, y0, false);
            }
            previousSpans = std::move(bottomSpans);
            previousSides = std::move(sides);
        }
        if (fringe > 0.f && !ys.empty()) horizontal(previousSpans, {}, ys.back(), false);
    }

    std::vector<Path::Contour> StrokeOutline(const std::vector<Path::Contour>& contours, const Stroke& stroke, const float tolerance)
    {
        std::vector<Path::Contour> out;
        const float hw = stroke.width * 0.5f;
        if (hw <= 0.f) return out;

        // A circle's worth of points around a round join or cap, as finely as the tolerance asks.
        const int roundSteps = std::clamp(static_cast<int>(std::ceil(std::numbers::pi_v<float> / std::acos(std::max(0.f, 1.f - tolerance / hw)))), 4, 64);

        const auto quad = [&](const kor::Vec2 a, const kor::Vec2 b, const kor::Vec2 n, const float extendA, const float extendB) {
            const kor::Vec2 d = kor::Normalize(b - a);
            const kor::Vec2 a0 = a - d * extendA, b0 = b + d * extendB;
            out.push_back({ { a0 + n * hw, b0 + n * hw, b0 - n * hw, a0 - n * hw }, true });
        };
        const auto disc = [&](const kor::Vec2 c) {
            Path::Contour circle;
            circle.closed = true;
            const int steps = roundSteps * 2;
            for (int i = 0; i < steps; ++i) {
                const float t = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(steps);
                circle.points.push_back(c + hw * kor::Vec2(std::cos(t), std::sin(t)));
            }
            out.push_back(std::move(circle));
        };

        for (const auto& c : contours) {
            // Drop repeated points: a zero-length segment has no direction.
            std::vector<kor::Vec2> pts;
            for (const auto& p : c.points)
                if (pts.empty() || kor::Length(p - pts.back()) > 1e-5f) pts.push_back(p);
            if (c.closed && pts.size() > 2 && kor::Length(pts.back() - pts.front()) <= 1e-5f) pts.pop_back();
            if (pts.size() < 2) {
                if (!pts.empty() && stroke.cap == StrokeCap::eRound) disc(pts[0]);
                continue;
            }
            const std::size_t n = pts.size();
            const std::size_t segments = c.closed ? n : n - 1;

            for (std::size_t i = 0; i < segments; ++i) {
                const kor::Vec2 a = pts[i], b = pts[(i + 1) % n];
                const kor::Vec2 d = kor::Normalize(b - a);
                const kor::Vec2 nrm { -d.y, d.x };
                const bool first = !c.closed && i == 0, last = !c.closed && i == segments - 1;
                const float capA = first && stroke.cap == StrokeCap::eSquare ? hw : 0.f;
                const float capB = last && stroke.cap == StrokeCap::eSquare ? hw : 0.f;
                quad(a, b, nrm, capA, capB);
                if (first && stroke.cap == StrokeCap::eRound) disc(a);
                if (last && stroke.cap == StrokeCap::eRound) disc(b);
            }

            // Joins, at every vertex two segments share.
            const std::size_t joinsFrom = c.closed ? 0 : 1, joinsTo = c.closed ? n : n - 1;
            for (std::size_t v = joinsFrom; v < joinsTo; ++v) {
                const kor::Vec2 p = pts[v], prev = pts[(v + n - 1) % n], next = pts[(v + 1) % n];
                const kor::Vec2 d0 = kor::Normalize(p - prev), d1 = kor::Normalize(next - p);
                const float cross = d0.x * d1.y - d0.y * d1.x;
                if (std::abs(cross) < 1e-4f && kor::Dot(d0, d1) > 0.f) continue;   // straight on: nothing to fill
                // The outer side is where the turn opens up.
                const float side = cross > 0.f ? -1.f : 1.f;
                const kor::Vec2 n0 = kor::Vec2(-d0.y, d0.x) * side, n1 = kor::Vec2(-d1.y, d1.x) * side;
                const kor::Vec2 o0 = p + n0 * hw, o1 = p + n1 * hw;
                StrokeJoin join = stroke.join;
                kor::Vec2 miter {};
                if (join == StrokeJoin::eMiter) {
                    const kor::Vec2 m = kor::Normalize(n0 + n1 + kor::Vec2(1e-9f, 0.f));
                    const float cosHalf = kor::Dot(m, n0);
                    if (cosHalf <= 1e-4f || 1.f / cosHalf > stroke.miterLimit) join = StrokeJoin::eBevel;
                    else miter = p + m * (hw / cosHalf);
                }
                if (join == StrokeJoin::eRound) { disc(p); continue; }
                if (join == StrokeJoin::eMiter) out.push_back({ { p, o0, miter, o1 }, true });
                else out.push_back({ { p, o0, o1 }, true });
            }
        }
        // All one way round, or non-zero would cancel the overlap of two pieces wound opposite ways.
        for (auto& contour : out) {
            float area = 0.f;
            const auto& q = contour.points;
            for (std::size_t i = 0; i < q.size(); ++i) {
                const kor::Vec2 a = q[i], b = q[(i + 1) % q.size()];
                area += a.x * b.y - b.x * a.y;
            }
            if (area < 0.f) std::ranges::reverse(contour.points);
        }
        return out;
    }
}
