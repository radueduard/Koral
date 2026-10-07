#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "kui/canvas.h"
#include "kui/widgets.h"

namespace kui
{
    /**
     * @brief A picture made of filled outlines — an SVG's shapes — on a box of its own (its view box),
     *        drawn in one colour at whatever size it is shown.
     *
     * What it reads of an SVG is what icons are made of: `<path>`, `<circle>`, `<ellipse>`, `<rect>`,
     * `<polygon>` and `<polyline>`, inside `<g>`s, with their `transform`s, `fill="none"`, `fill-rule`,
     * `opacity` and `fill-opacity`. Its colours are not kept: everything is drawn in the tint it is given,
     * at its opacity — which is how a two-tone icon has its lighter part. Strokes, text, gradients and
     * images are not drawn.
     */
    class KUI_API VectorImage {
    public:
        /** @brief One of its shapes, at the opacity it is drawn with. */
        struct Shape {
            Path path;
            float opacity = 1.f;
        };

        /** @brief @p svg, the text of an SVG document, read. Empty (nothing to draw) when it is not one. */
        static VectorImage FromSvg(std::string_view svg);

        /** @brief The box its shapes are drawn on, in their own units: an icon's is 0, 0 to 24, 24. */
        [[nodiscard]] const Rect& ViewBox() const { return _viewBox; }
        [[nodiscard]] std::span<const Shape> Shapes() const { return _shapes; }
        [[nodiscard]] bool Empty() const { return _shapes.empty(); }

        /** @brief Drawn over @p rect — stretched, so give it a rect of the view box's proportions — in @p tint. */
        void Draw(Canvas& canvas, const Rect& rect, Color tint) const;

    private:
        Rect _viewBox = Rect::XYWH(0.f, 0.f, 24.f, 24.f);
        std::vector<Shape> _shapes;
    };

    /** @brief The five drawings each Material icon comes in. */
    enum class IconStyle : std::uint8_t { eFilled, eOutlined, eRounded, eSharp, eTwoTone };

    /**
     * @brief One of the Material icons kui carries — every one Jetpack Compose has (core and extended), the very
     *        drawings Google publishes — by its name, as Material spells it (`"arrow_back"`) or as Compose
     *        does (`"ArrowBack"`). Null when there is no icon of that name.
     *
     * @code
     * kui::Button(kui::Row({ kui::Icon(kui::MaterialIcon("add")), kui::Text("New") }), add);
     * @endcode
     */
    KUI_API std::shared_ptr<const VectorImage> MaterialIcon(std::string_view name, IconStyle style = IconStyle::eFilled);
    /** @brief The names of the Material icons kui carries, as Material spells them. */
    KUI_API std::span<const std::string_view> MaterialIconNames();

    /**
     * @brief @p icon, in @p tint — the theme's text colour unless given another — 24 units square unless
     *        its parent sizes it (`.Size(48, 48)`), as Compose's Icon is.
     */
    KUI_API Widget Icon(std::shared_ptr<const VectorImage> icon, Color tint = colors::Inherit);
    /** @brief The Material icon named @p name, in @p style. @see MaterialIcon */
    KUI_API Widget Icon(std::string_view name, IconStyle style = IconStyle::eFilled, Color tint = colors::Inherit);
}
