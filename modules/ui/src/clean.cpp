//
// koral-ui: paths made clean by Clipper — united under their fill rule, or offset into a stroke's one
// outline — then triangulated on their own vertices and given a fringe that shares them.
//

#include <algorithm>
#include <cmath>
#include <map>

#include <clipper2/clipper.h>
#include <clipper2/clipper.triangulation.h>

#include "tessellate.h"

namespace kui::detail
{
    namespace {
        namespace c2 = Clipper2Lib;

        // Coordinates are rounded to a thousandth of a unit: far below anything drawn, and what keeps
        // Clipper's integer arithmetic exact.
        constexpr int Precision = 3;

        /** @brief Every contour, as a fill takes it: closed, whether it was or not. */
        c2::PathsD toClipper(const std::vector<Path::Contour>& contours)
        {
            c2::PathsD paths;
            for (const auto& c : contours) {
                c2::PathD path;
                path.reserve(c.points.size());
                for (const auto& p : c.points) path.emplace_back(p.x, p.y);
                paths.push_back(std::move(path));
            }
            return paths;
        }

        double signedArea(const c2::PathD& path)
        {
            double a = 0.0;
            for (std::size_t i = 0, n = path.size(); i < n; ++i) {
                const auto& p = path[i];
                const auto& q = path[(i + 1) % n];
                a += p.x * q.y - q.x * p.y;
            }
            return a * 0.5;
        }

        bool insideContour(const c2::PathD& path, const glm::dvec2 p)
        {
            bool in = false;
            for (std::size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
                const auto& a = path[i];
                const auto& b = path[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
            }
            return in;
        }

        /**
         * @brief Triangles for clean contours, and the fringe around them. Clean means what Clipper
         *        returns: no crossings, outers and holes wound opposite ways — so the filled side is
         *        the same side of every edge, found once from the largest contour.
         *
         * The fringe is centred on the outline, half of it in and half out, so a pixel the edge cuts in
         * two is half covered: the inside is pulled in by half a fringe, and the fringe runs from there to
         * half a fringe outside. (All of it outside made every path half a pixel bolder all round — enough
         * to close a small icon's gaps.)
         */
        bool emit(const c2::PathsD& clean, const float fringe, Mesh& out)
        {
            if (clean.empty()) return true;
            c2::PathsD triangles;
            if (c2::Triangulate(clean, Precision, triangles, false) != c2::TriangulateResult::success) return false;
            // Clipper's triangulation can say it succeeded and still be wrong — a hole whose sides line up with
            // where its outline's curves begin is filled over (Material's grid_on). Right, its triangles cover
            // exactly what the outline less its holes does; otherwise the sweep does it.
            double covered = 0.0, area = 0.0;
            for (const auto& t : triangles) if (t.size() == 3) covered += std::abs(signedArea(t));
            for (const auto& path : clean) area += signedArea(path);
            if (std::abs(covered - std::abs(area)) > 1e-3 * std::max(1.0, std::abs(area))) return false;
            if (fringe <= 0.f) {
                for (const auto& t : triangles) {
                    if (t.size() != 3) continue;
                    for (const auto& p : t) out.Add({ static_cast<float>(p.x), static_cast<float>(p.y) }, 1.f);
                }
                return true;
            }

            // Which side is filled: off the largest contour (an outer one), its left or its right.
            const auto largest = std::ranges::max_element(clean, {}, [](const c2::PathD& p) { return std::abs(signedArea(p)); });
            float fillLeft = 1.f;
            {
                const auto& path = *largest;
                for (std::size_t i = 0; i < path.size(); ++i) {
                    const glm::dvec2 a { path[i].x, path[i].y }, b { path[(i + 1) % path.size()].x, path[(i + 1) % path.size()].y };
                    const glm::dvec2 d = b - a;
                    const double len = glm::length(d);
                    if (len < 1e-6) continue;
                    const glm::dvec2 left { -d.y / len, d.x / len };
                    fillLeft = insideContour(path, (a + b) * 0.5 + left * 1e-3) ? 1.f : -1.f;
                    break;
                }
            }

            // Each vertex's outward normal — per edge, then per vertex the two joined: a miter, kept from
            // growing a spike — and where the inside's corner moves to, by vertex, for the triangles to find.
            const float half = fringe * 0.5f;
            const auto key = [](const double x, const double y) {
                return std::pair { std::llround(x * 1000.0), std::llround(y * 1000.0) };   // Precision's grid
            };
            std::map<std::pair<long long, long long>, glm::vec2> inset;
            std::vector<std::vector<glm::vec2>> points(clean.size()), normals(clean.size());
            for (std::size_t c = 0; c < clean.size(); ++c) {
                const auto& path = clean[c];
                const std::size_t n = path.size();
                if (n < 3) continue;
                auto& pts = points[c];
                pts.resize(n);
                for (std::size_t i = 0; i < n; ++i) pts[i] = { static_cast<float>(path[i].x), static_cast<float>(path[i].y) };
                std::vector<glm::vec2> edgeNormal(n);
                for (std::size_t i = 0; i < n; ++i) {
                    const glm::vec2 d = pts[(i + 1) % n] - pts[i];
                    const float len = glm::length(d);
                    const glm::vec2 left = len > 0.f ? glm::vec2(-d.y, d.x) / len : glm::vec2(0.f);
                    edgeNormal[i] = -left * fillLeft;
                }
                auto& vertexNormal = normals[c];
                vertexNormal.resize(n);
                for (std::size_t i = 0; i < n; ++i) {
                    const glm::vec2 a = edgeNormal[(i + n - 1) % n], b = edgeNormal[i];
                    glm::vec2 m = a + b;
                    const float l = glm::length(m);
                    if (l < 1e-4f) { vertexNormal[i] = b; continue; }
                    m /= l;
                    const float cosHalf = std::max(glm::dot(m, b), 0.25f);   // at most 4x out, at a hairpin
                    vertexNormal[i] = m / cosHalf;
                }
                for (std::size_t i = 0; i < n; ++i) inset.try_emplace(key(path[i].x, path[i].y), pts[i] - vertexNormal[i] * half);
            }

            for (const auto& t : triangles) {
                if (t.size() != 3) continue;
                for (const auto& p : t) {
                    const auto it = inset.find(key(p.x, p.y));
                    out.Add(it != inset.end() ? it->second : glm::vec2(static_cast<float>(p.x), static_cast<float>(p.y)), 1.f);
                }
            }
            for (std::size_t c = 0; c < clean.size(); ++c) {
                const auto& pts = points[c];
                const auto& vertexNormal = normals[c];
                const std::size_t n = pts.size();
                for (std::size_t i = 0; i < n; ++i) {
                    const std::size_t j = (i + 1) % n;
                    const glm::vec2 a = pts[i] - vertexNormal[i] * half, b = pts[j] - vertexNormal[j] * half;
                    const glm::vec2 a2 = pts[i] + vertexNormal[i] * half, b2 = pts[j] + vertexNormal[j] * half;
                    out.Add(a, 1.f); out.Add(b, 1.f); out.Add(b2, 0.f);
                    out.Add(a, 1.f); out.Add(b2, 0.f); out.Add(a2, 0.f);
                }
            }
            return true;
        }

        /**
         * @brief Clean contours filled by the sweep, for when Clipper cannot triangulate them. The sweep's fringe
         *        is all outside what it is given, so it is given the outline pulled in by half of one: the fringe
         *        then straddles the edge, as emit's does. False when that leaves nothing — thinner than a pixel.
         */
        bool sweep(const c2::PathsD& clean, const float fringe, Mesh& out)
        {
            const c2::PathsD inset = fringe > 0.f ? c2::InflatePaths(clean, -fringe * 0.5, c2::JoinType::Miter, c2::EndType::Polygon, 2.0, Precision) : clean;
            if (inset.empty()) return false;
            std::vector<Path::Contour> contours;
            for (const auto& path : inset) {
                Path::Contour c { {}, true };
                for (const auto& p : path) c.points.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
                contours.push_back(std::move(c));
            }
            TessellateFill(contours, FillRule::eNonZero, fringe, out);
            return true;
        }
    }

    void Fill(const std::vector<Path::Contour>& contours, const FillRule rule, const float fringe, Mesh& out)
    {
        const c2::PathsD clean = c2::Union(toClipper(contours), rule == FillRule::eEvenOdd ? c2::FillRule::EvenOdd : c2::FillRule::NonZero, Precision);
        Mesh mesh;
        if (emit(clean, fringe, mesh)) {
            out.positions.insert(out.positions.end(), mesh.positions.begin(), mesh.positions.end());
            out.coverage.insert(out.coverage.end(), mesh.coverage.begin(), mesh.coverage.end());
            return;
        }
        // Clipper could not triangulate it: the sweep can.
        if (sweep(clean, fringe, out)) return;
        TessellateFill(contours, rule, fringe, out);
    }

    void StrokePath(const std::vector<Path::Contour>& contours, const Stroke& stroke, const float tolerance, const float fringe, Mesh& out)
    {
        const double half = stroke.width * 0.5;
        if (half <= 0.0) return;
        const c2::JoinType join = stroke.join == StrokeJoin::eRound ? c2::JoinType::Round
                                : stroke.join == StrokeJoin::eBevel ? c2::JoinType::Bevel : c2::JoinType::Miter;
        const c2::EndType cap = stroke.cap == StrokeCap::eRound ? c2::EndType::Round
                              : stroke.cap == StrokeCap::eSquare ? c2::EndType::Square : c2::EndType::Butt;

        // Closed contours are stroked joined all the way round; open ones end in their caps. Each set is
        // offset on its own, then the two outlines are one shape.
        c2::PathsD open, closed;
        for (const auto& c : contours) {
            c2::PathD path;
            for (const auto& p : c.points) path.emplace_back(p.x, p.y);
            (c.closed ? closed : open).push_back(std::move(path));
        }
        const double arc = std::max<double>(tolerance, 0.01);
        c2::PathsD outline;
        if (!closed.empty()) outline = c2::InflatePaths(closed, half, join, c2::EndType::Joined, stroke.miterLimit, Precision, arc);
        if (!open.empty()) {
            const auto more = c2::InflatePaths(open, half, join, cap, stroke.miterLimit, Precision, arc);
            outline = outline.empty() ? more : c2::Union(outline, more, c2::FillRule::NonZero, Precision);
        }
        // Offsetting gives one outline per input contour; where they overlap they are united, so a
        // translucent stroke crossing itself is drawn once.
        outline = c2::Union(outline, c2::FillRule::NonZero, Precision);
        Mesh mesh;
        if (emit(outline, fringe, mesh)) {
            out.positions.insert(out.positions.end(), mesh.positions.begin(), mesh.positions.end());
            out.coverage.insert(out.coverage.end(), mesh.coverage.begin(), mesh.coverage.end());
            return;
        }
        if (sweep(outline, fringe, out)) return;
        TessellateFill(StrokeOutline(contours, stroke, tolerance), FillRule::eNonZero, fringe, out);
    }
}
