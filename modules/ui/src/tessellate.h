//
// koral-ui: paths cut into triangles, for what the signed-distance shader cannot draw in one quad.
//

#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <kui/canvas.h>

namespace kui::detail
{
    /** @brief A triangle list, each vertex with its coverage (1 inside, 0 at the fringe's outer edge). */
    struct Mesh {
        std::vector<glm::vec2> positions;
        std::vector<float> coverage;
        void Add(glm::vec2 p, float c) { positions.push_back(p); coverage.push_back(c); }
        [[nodiscard]] bool Empty() const { return positions.empty(); }
    };

    /**
     * @brief Fills @p contours by @p rule: exact for any outline, self-crossing or not, as trapezoids
     *        between the points where edges start, end or cross. With @p fringe > 0, an anti-aliasing
     *        fringe that wide is added outside the filled region.
     */
    void TessellateFill(const std::vector<Path::Contour>& contours, FillRule rule, float fringe, Mesh& out);

    /**
     * @brief The outline of @p stroke drawn along @p contours, as polygons — one per segment, join and
     *        cap — whose union (non-zero) is the stroke. The fallback when Clipper cannot make a clean one.
     */
    std::vector<Path::Contour> StrokeOutline(const std::vector<Path::Contour>& contours, const Stroke& stroke, float tolerance);

    /**
     * @brief Fills @p contours by @p rule: made clean first (overlaps united, crossings resolved), then
     *        triangulated on their own vertices, with an anti-aliasing fringe @p fringe wide sharing
     *        them — so fill and fringe meet without a crack, and nothing is covered twice.
     */
    void Fill(const std::vector<Path::Contour>& contours, FillRule rule, float fringe, Mesh& out);

    /** @brief The same for @p stroke drawn along @p contours: one clean outline, joins and caps included. */
    void StrokePath(const std::vector<Path::Contour>& contours, const Stroke& stroke, float tolerance, float fringe, Mesh& out);
}
