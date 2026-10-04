//
// koral-ui, the drawing layer: a Canvas records shapes, painted as a Paint says, into a Picture.
//

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

#include <image.h>
#include <resource.h>
#include <shader.h>

#include "kuiApi.h"

namespace kui
{
    class Font;
    class Paragraph;
    struct TextStyle;

    // ---- values -----------------------------------------------------------------------------------

    /**
     * @brief A colour, straight (not premultiplied) alpha, in sRGB — as a colour picker or a CSS hex
     *        code gives it. The UI converts it for the target it draws into.
     */
    struct KUI_API Color {
        float r = 0.f, g = 0.f, b = 0.f, a = 0.f;

        constexpr Color() = default;
        constexpr Color(const float r, const float g, const float b, const float a = 1.f) : r(r), g(g), b(b), a(a) {}

        /** @brief From 0xRRGGBB, opaque. */
        static constexpr Color Hex(const std::uint32_t rgb) {
            return { static_cast<float>((rgb >> 16) & 0xff) / 255.f, static_cast<float>((rgb >> 8) & 0xff) / 255.f,
                     static_cast<float>(rgb & 0xff) / 255.f, 1.f };
        }
        /** @brief From 0xRRGGBBAA. */
        static constexpr Color HexA(const std::uint32_t rgba) {
            return Hex(rgba >> 8).WithAlpha(static_cast<float>(rgba & 0xff) / 255.f);
        }
        [[nodiscard]] constexpr Color WithAlpha(const float alpha) const { return { r, g, b, alpha }; }
        [[nodiscard]] constexpr bool Visible() const { return a > 0.f; }
        /** @brief Packed as the GPU reads it: RGBA, 8 bits each. */
        [[nodiscard]] std::uint32_t Packed() const;
        constexpr bool operator==(const Color&) const = default;
    };

    namespace colors {
        inline constexpr Color Transparent { 0.f, 0.f, 0.f, 0.f };
        inline constexpr Color Black { 0.f, 0.f, 0.f, 1.f };
        /// No colour of its own: text of this colour is drawn in the text colour of the theme it is in.
        inline constexpr Color Inherit { 0.f, 0.f, 0.f, -1.f };
        inline constexpr Color White { 1.f, 1.f, 1.f, 1.f };
        inline constexpr Color Red = Color::Hex(0xF44336);
        inline constexpr Color Green = Color::Hex(0x4CAF50);
        inline constexpr Color Blue = Color::Hex(0x2196F3);
    }

    /** @brief An axis-aligned rectangle, by its edges: y grows downwards. */
    struct KUI_API Rect {
        float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;

        static constexpr Rect LTRB(const float l, const float t, const float r, const float b) { return { l, t, r, b }; }
        static constexpr Rect XYWH(const float x, const float y, const float w, const float h) { return { x, y, x + w, y + h }; }
        static constexpr Rect FromSize(const glm::vec2 size) { return { 0.f, 0.f, size.x, size.y }; }
        static constexpr Rect FromCenter(const glm::vec2 c, const float w, const float h) { return { c.x - w * .5f, c.y - h * .5f, c.x + w * .5f, c.y + h * .5f }; }

        [[nodiscard]] constexpr float Width() const { return right - left; }
        [[nodiscard]] constexpr float Height() const { return bottom - top; }
        [[nodiscard]] constexpr glm::vec2 Size() const { return { Width(), Height() }; }
        [[nodiscard]] constexpr glm::vec2 TopLeft() const { return { left, top }; }
        [[nodiscard]] constexpr glm::vec2 Center() const { return { (left + right) * .5f, (top + bottom) * .5f }; }
        [[nodiscard]] constexpr bool Empty() const { return !(right > left && bottom > top); }
        [[nodiscard]] constexpr bool Contains(const glm::vec2 p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
        [[nodiscard]] constexpr Rect Inflate(const float by) const { return { left - by, top - by, right + by, bottom + by }; }
        [[nodiscard]] constexpr Rect Deflate(const float by) const { return Inflate(-by); }
        [[nodiscard]] constexpr Rect Shift(const glm::vec2 by) const { return { left + by.x, top + by.y, right + by.x, bottom + by.y }; }
        [[nodiscard]] Rect Intersect(const Rect& o) const;
        [[nodiscard]] Rect Union(const Rect& o) const;
        constexpr bool operator==(const Rect&) const = default;
    };

    /** @brief Corner radii, clockwise from the top-left. */
    struct Radii {
        float topLeft = 0.f, topRight = 0.f, bottomRight = 0.f, bottomLeft = 0.f;
        constexpr Radii() = default;
        constexpr Radii(const float all) : topLeft(all), topRight(all), bottomRight(all), bottomLeft(all) {}  // NOLINT(*-explicit-constructor)
        constexpr Radii(const float tl, const float tr, const float br, const float bl) : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}
        [[nodiscard]] constexpr bool Zero() const { return topLeft == 0.f && topRight == 0.f && bottomRight == 0.f && bottomLeft == 0.f; }
        constexpr bool operator==(const Radii&) const = default;
    };

    /** @brief A rectangle with rounded corners. */
    struct RRect {
        Rect rect;
        Radii radii;
    };

    /**
     * @brief A 2D affine transform: x' = a x + c y + tx, y' = b x + d y + ty.
     *
     * Composed like matrices: `(A * B).Apply(p) == A.Apply(B.Apply(p))`.
     */
    struct KUI_API Transform {
        float a = 1.f, b = 0.f, c = 0.f, d = 1.f, tx = 0.f, ty = 0.f;

        static constexpr Transform Identity() { return {}; }
        static constexpr Transform Translation(const glm::vec2 t) { return { 1.f, 0.f, 0.f, 1.f, t.x, t.y }; }
        static constexpr Transform Scaling(const glm::vec2 s) { return { s.x, 0.f, 0.f, s.y, 0.f, 0.f }; }
        /** @brief Clockwise on screen, since y grows downwards. */
        static Transform Rotation(float radians);

        [[nodiscard]] constexpr glm::vec2 Apply(const glm::vec2 p) const { return { a * p.x + c * p.y + tx, b * p.x + d * p.y + ty }; }
        [[nodiscard]] constexpr glm::vec2 ApplyVector(const glm::vec2 v) const { return { a * v.x + c * v.y, b * v.x + d * v.y }; }
        [[nodiscard]] Transform Inverse() const;
        [[nodiscard]] constexpr bool IsTranslation() const { return a == 1.f && b == 0.f && c == 0.f && d == 1.f; }
        [[nodiscard]] constexpr bool IsIdentity() const { return IsTranslation() && tx == 0.f && ty == 0.f; }
        /** @brief Whether axis-aligned rectangles stay axis-aligned. */
        [[nodiscard]] constexpr bool IsAxisAligned() const { return b == 0.f && c == 0.f; }
        /** @brief The bounds of @p rect once transformed. */
        [[nodiscard]] Rect MapRect(const Rect& rect) const;

        constexpr Transform operator*(const Transform& o) const {
            return { a * o.a + c * o.b, b * o.a + d * o.b,
                     a * o.c + c * o.d, b * o.c + d * o.d,
                     a * o.tx + c * o.ty + tx, b * o.tx + d * o.ty + ty };
        }
        constexpr bool operator==(const Transform&) const = default;
    };

    // ---- paint ------------------------------------------------------------------------------------

    /** @brief One colour of a gradient, at @p offset along it (0 to 1). */
    struct GradientStop {
        float offset = 0.f;
        Color color;
    };

    /**
     * @brief A fill that changes colour across the shape, in the shape's own coordinates. Up to eight
     *        stops; more are dropped.
     */
    struct KUI_API Gradient {
        enum class Type : std::uint8_t { eLinear, eRadial, eSweep };
        Type type = Type::eLinear;
        glm::vec2 start {};     ///< Linear: where offset 0 is. Radial and sweep: the centre.
        glm::vec2 end {};       ///< Linear: where offset 1 is.
        float radius = 0.f;     ///< Radial: where offset 1 is.
        float angle = 0.f;      ///< Sweep: where offset 0 is, in radians clockwise from +x.
        std::vector<GradientStop> stops;

        static Gradient Linear(glm::vec2 from, glm::vec2 to, std::vector<GradientStop> stops);
        static Gradient Linear(glm::vec2 from, glm::vec2 to, Color a, Color b);
        static Gradient Radial(glm::vec2 center, float radius, std::vector<GradientStop> stops);
        static Gradient Sweep(glm::vec2 center, float angle, std::vector<GradientStop> stops);
    };

    /** @brief How the ends of an open stroke are drawn. */
    enum class StrokeCap : std::uint8_t {
        eButt,      ///< Square, ending exactly at the end point.
        eRound,     ///< A half circle past the end point.
        eSquare,    ///< Square, half the width past the end point.
    };

    /** @brief How the corners where two segments of a stroked path meet are drawn. */
    enum class StrokeJoin : std::uint8_t { eMiter, eRound, eBevel };

    /** @brief The outline drawn along a shape's edge, centred on it. A width of 0 draws none. */
    struct Stroke {
        float width = 0.f;
        Color color = colors::Black;
        StrokeCap cap = StrokeCap::eButt;
        StrokeJoin join = StrokeJoin::eMiter;
        float miterLimit = 4.f;
        [[nodiscard]] bool Visible() const { return width > 0.f && color.Visible(); }
    };

    /**
     * @brief How a shape is drawn: what fills it, what outlines it.
     *
     * Both may be set: the stroke is drawn over the fill. A shape with neither visible draws nothing.
     *
     * @code
     * canvas.DrawRRect({rect, 8.f}, kui::Paint::Fill(kui::Color::Hex(0x303040)).SetStroke(1.f, kui::colors::White));
     * canvas.DrawCircle({50, 50}, 20, kui::Paint{}.SetGradient(kui::Gradient::Radial({50, 50}, 20, {{0, red}, {1, blue}})));
     * @endcode
     */
    struct Paint {
        Color fill = colors::Transparent;
        std::shared_ptr<const Gradient> gradient;   ///< Fills in its place when set.
        Stroke stroke;
        float opacity = 1.f;                        ///< Multiplies fill and stroke alike.

        static Paint Fill(const Color color) { Paint p; p.fill = color; return p; }
        static Paint Stroked(const Color color, const float width) { Paint p; p.stroke.color = color; p.stroke.width = width; return p; }

        Paint& SetFill(const Color color) { fill = color; return *this; }
        Paint& SetGradient(Gradient g) { gradient = std::make_shared<const Gradient>(std::move(g)); return *this; }
        Paint& SetStroke(const float width, const Color color) { stroke.width = width; stroke.color = color; return *this; }
        Paint& SetStroke(const Stroke& s) { stroke = s; return *this; }
        Paint& SetOpacity(const float o) { opacity = o; return *this; }

        [[nodiscard]] bool HasFill() const { return gradient != nullptr || fill.Visible(); }
        [[nodiscard]] bool HasStroke() const { return stroke.Visible(); }
    };

    // ---- paths ------------------------------------------------------------------------------------

    /** @brief Which regions of a self-overlapping path are inside. */
    enum class FillRule : std::uint8_t {
        eNonZero,   ///< Inside where the outline winds round a point any number of times but zero.
        eEvenOdd,   ///< Inside where a ray from the point crosses the outline an odd number of times: holes.
    };

    /**
     * @brief An outline made of lines and curves, filled or stroked by Canvas::DrawPath.
     *
     * Curves are flattened into lines finely enough to be smooth at the size they are drawn, and the
     * result is cut into triangles once, when the path is recorded — a picture replays it for free.
     */
    class KUI_API Path {
    public:
        Path& MoveTo(glm::vec2 p);
        Path& LineTo(glm::vec2 p);
        Path& QuadTo(glm::vec2 control, glm::vec2 p);
        Path& CubicTo(glm::vec2 control1, glm::vec2 control2, glm::vec2 p);
        /** @brief An arc of the circle at @p center, from @p start sweeping @p sweep radians, joined by a line from where the path was. */
        Path& ArcTo(glm::vec2 center, float radius, float start, float sweep);
        /** @brief A rounded corner: towards @p corner, turning along an arc of @p radius to head for @p to. */
        Path& ArcTo(glm::vec2 corner, glm::vec2 to, float radius);
        Path& Close();

        Path& AddRect(const Rect& rect);
        Path& AddRRect(const RRect& rrect);
        Path& AddCircle(glm::vec2 center, float radius);
        Path& AddOval(const Rect& rect);
        Path& AddPolygon(std::span<const glm::vec2> points, bool close = true);

        Path& SetFillRule(const FillRule rule) { _fillRule = rule; return *this; }
        [[nodiscard]] FillRule GetFillRule() const { return _fillRule; }
        [[nodiscard]] bool Empty() const { return _verbs.empty(); }
        [[nodiscard]] Rect Bounds() const;

        /**
         * @brief The outline as polygons: each contour flattened so no point of a curve is further than
         *        @p tolerance from the line drawn for it.
         */
        struct Contour { std::vector<glm::vec2> points; bool closed = false; };
        [[nodiscard]] std::vector<Contour> Flatten(float tolerance = 0.25f) const;

    private:
        enum class Verb : std::uint8_t { eMove, eLine, eQuad, eCubic, eClose };
        std::vector<Verb> _verbs;
        std::vector<glm::vec2> _points;
        FillRule _fillRule = FillRule::eNonZero;
        glm::vec2 _start {}, _current {};
    };

    // ---- element shaders --------------------------------------------------------------------------

    /**
     * @brief A fragment shader that draws what is inside a rectangle: the base element of the UI.
     *
     * The shader is written against a small contract — one function, `kuiShade`, in GLSL with
     * `#include <koralUI.glsl>`, or a fragment entry point in Slang with `import koralUI;` — and the UI
     * does the rest: the quad, rounded corners, clipping, the anti-aliased edge, the colour space.
     * Each element can be given its own parameters, a struct the shader declares. Every element drawn
     * with one shader in a run is one instanced draw. Edits to the file are picked up while running.
     *
     * @code
     * auto plasma = kui::ElementShader::Load("plasma.frag.glsl");
     * struct Params { glm::vec4 from, to; float speed; float pad[3]; };   // std430, as the shader declares it
     * canvas.DrawElement(plasma, kui::Rect::XYWH(10, 10, 200, 120), Params{...}, 12.f);  // 12: corner radius
     * @endcode
     */
    class KUI_API ElementShader {
    public:
        /** @brief A GLSL file (anything but .slang) or a Slang module, whose @p entry is the fragment entry point. */
        static std::shared_ptr<ElementShader> Load(const std::filesystem::path& path, std::string entry = {});

        [[nodiscard]] const kor::ResourceRef<const kor::Shader>& Shader() const { return _shader; }
        [[nodiscard]] std::uint32_t Id() const { return _id; }
        [[nodiscard]] bool Valid() const { return _shader.Valid(); }

    private:
        ElementShader() = default;
        kor::ResourceRef<const kor::Shader> _shader;
        std::uint32_t _id = 0;
    };

    // ---- pictures ---------------------------------------------------------------------------------

    class Layer;

    /**
     * @brief What a Canvas recorded: immutable, cheap to keep, and drawn again with no work.
     *
     * Everything costly — tessellating paths, laying text out, turning shapes into GPU instances —
     * happened while recording. Drawing a picture copies it into the frame; an unchanged frame does
     * not even do that.
     */
    class KUI_API Picture {
    public:
        struct Data;
        explicit Picture(std::unique_ptr<Data> data);
        ~Picture();
        Picture(const Picture&) = delete;
        Picture& operator=(const Picture&) = delete;

        /** @brief What the picture covers, in its own coordinates (before any layer moves it). */
        [[nodiscard]] Rect Bounds() const;
        /** @brief How many GPU instances it holds — primitives, glyphs, images and elements. */
        [[nodiscard]] std::size_t InstanceCount() const;
        [[nodiscard]] const Data& Contents() const { return *_data; }

    private:
        std::unique_ptr<Data> _data;
    };

    /**
     * @brief A retained piece of the frame: a picture, and where it is. What a scroll view or an
     *        animation moves — changing a layer's transform or opacity re-records nothing and uploads
     *        a few bytes.
     *
     * A picture shows another layer where it called Canvas::DrawLayer, inside the clips in effect
     * there. The root layer is what a UiPass draws.
     */
    class KUI_API Layer : public std::enable_shared_from_this<Layer> {
    public:
        static std::shared_ptr<Layer> Create();

        Layer& SetPicture(std::shared_ptr<const Picture> picture);
        [[nodiscard]] const std::shared_ptr<const Picture>& GetPicture() const { return _picture; }
        Layer& SetTransform(const Transform& transform);
        [[nodiscard]] const Transform& GetTransform() const { return _transform; }
        Layer& SetOpacity(float opacity);
        [[nodiscard]] float Opacity() const { return _opacity; }

        /**
         * @brief Bumped whenever any layer anywhere changes: what lets a renderer whose tree is unchanged
         *        skip looking at its layers at all.
         */
        [[nodiscard]] static std::uint64_t Epoch();

        /** @brief Bumped whenever the picture changes; the transform and opacity have their own. */
        [[nodiscard]] std::uint64_t ContentVersion() const { return _contentVersion; }
        [[nodiscard]] std::uint64_t PlacementVersion() const { return _placementVersion; }

    private:
        Layer() = default;
        std::shared_ptr<const Picture> _picture;
        Transform _transform;
        float _opacity = 1.f;
        std::uint64_t _contentVersion = 1, _placementVersion = 1;
    };

    // ---- the canvas -------------------------------------------------------------------------------

    /**
     * @brief Records drawing into a Picture — the low-level layer of the UI, usable on its own.
     *
     * Coordinates are logical units (a UiPass's scale turns them into pixels), with y growing
     * downwards. Save/Restore bracket changes to the transform and the clip.
     *
     * @code
     * kui::Canvas canvas;
     * canvas.DrawRRect({kui::Rect::XYWH(20, 20, 160, 48), 12.f}, kui::Paint::Fill(kui::Color::Hex(0x3F51B5)));
     * canvas.DrawLine({20, 90}, {180, 90}, kui::Paint::Stroked(kui::colors::White, 2.f));
     * canvas.Save();
     * canvas.Translate({100, 150});
     * canvas.Rotate(0.3f);
     * canvas.DrawArc({0, 0}, 30.f, 0.f, 4.f, false, kui::Paint::Stroked(kui::colors::Red, 6.f));
     * canvas.Restore();
     * layer->SetPicture(canvas.Finish());
     * @endcode
     */
    class KUI_API  Canvas {
    public:
        Canvas();
        ~Canvas();
        Canvas(const Canvas&) = delete;
        Canvas& operator=(const Canvas&) = delete;

        // -- state
        Canvas& Save();
        Canvas& Restore();
        [[nodiscard]] std::size_t SaveCount() const;
        Canvas& Translate(glm::vec2 by);
        Canvas& Scale(glm::vec2 by);
        Canvas& Scale(const float by) { return Scale({ by, by }); }
        /** @brief Clockwise on screen, in radians. */
        Canvas& Rotate(float radians);
        Canvas& Concat(const Transform& transform);
        Canvas& SetTransform(const Transform& transform);
        [[nodiscard]] const Transform& CurrentTransform() const;

        /**
         * @brief Says what part of the canvas — in its own coordinates, before any transform — will be
         *        looked at. Nothing is cut by it: it is what QuickReject answers against, so that whoever
         *        draws a great many things can leave out those nobody will see.
         */
        Canvas& SetCullRect(const Rect& rect);
        /**
         * @brief Makes room for about @p shapes of them before any is drawn — as many as the picture this
         *        one replaces had, say — so that a big picture is not copied over and over as it grows.
         */
        Canvas& Reserve(std::size_t shapes);
        /** @brief Whether @p rect, in the current coordinates, is wholly outside what will be looked at. */
        [[nodiscard]] bool QuickReject(const Rect& rect) const;
        /** @brief Nothing outside @p rect is drawn until the matching Restore. */
        Canvas& ClipRect(const Rect& rect);
        Canvas& ClipRRect(const RRect& rrect);

        // -- shapes
        Canvas& DrawRect(const Rect& rect, const Paint& paint);
        Canvas& DrawRRect(const RRect& rrect, const Paint& paint);
        Canvas& DrawCircle(glm::vec2 center, float radius, const Paint& paint);
        Canvas& DrawOval(const Rect& rect, const Paint& paint);
        /**
         * @brief An arc of the circle at @p center, from @p start sweeping @p sweep radians clockwise.
         *        With @p useCenter it is a pie slice; without, only its stroke is drawn.
         */
        Canvas& DrawArc(glm::vec2 center, float radius, float start, float sweep, bool useCenter, const Paint& paint);
        /** @brief A line, drawn with the paint's stroke — or, when it has none, a hairline of its fill colour. */
        Canvas& DrawLine(glm::vec2 from, glm::vec2 to, const Paint& paint);
        Canvas& DrawTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const Paint& paint);
        /** @brief A quadratic curve, stroked. */
        Canvas& DrawQuadraticBezier(glm::vec2 from, glm::vec2 control, glm::vec2 to, const Paint& paint);
        /** @brief A cubic curve, stroked. */
        Canvas& DrawCubicBezier(glm::vec2 from, glm::vec2 control1, glm::vec2 control2, glm::vec2 to, const Paint& paint);
        /** @brief Points joined by straight lines, stroked. */
        Canvas& DrawPolyline(std::span<const glm::vec2> points, const Paint& paint);
        Canvas& DrawPolygon(std::span<const glm::vec2> points, const Paint& paint);
        Canvas& DrawPath(const Path& path, const Paint& paint);
        /** @brief The soft shadow a rounded rectangle casts: @p blur is the Gaussian's standard deviation. */
        Canvas& DrawShadow(const RRect& rrect, Color color, float blur, glm::vec2 offset = {}, float spread = 0.f);

        // -- images and text
        /** @brief @p image (the part @p source covers, in pixels; all of it by default) stretched over @p destination. */
        Canvas& DrawImage(const kor::ResourceRef<const kor::Image>& image, const Rect& destination,
                       const Rect& source = {}, Color tint = colors::White);
        /** @brief Laid-out text, its top-left at @p position. */
        Canvas& DrawParagraph(const Paragraph& paragraph, glm::vec2 position);
        /** @brief One line of text, its top-left at @p position. Lays it out every call: keep a Paragraph for text drawn often. */
        Canvas& DrawText(std::string_view text, glm::vec2 position, const TextStyle& style);

        // -- elements and layers
        /** @brief An element: @p shader fills @p rect, given @p parameters (the shader's struct, std430). */
        Canvas& DrawElement(const std::shared_ptr<ElementShader>& shader, const Rect& rect,
                         std::span<const std::byte> parameters = {}, Radii radii = {}, float opacity = 1.f);
        template <typename T> requires (std::is_trivially_copyable_v<T> && !std::is_same_v<std::remove_cvref_t<T>, std::span<const std::byte>>)
        Canvas& DrawElement(const std::shared_ptr<ElementShader>& shader, const Rect& rect, const T& parameters,
                         const Radii radii = {}, const float opacity = 1.f) {
            return DrawElement(shader, rect, std::as_bytes(std::span(&parameters, 1)), radii, opacity);
        }
        /** @brief Another layer, under the current transform and clip: shown as it is each frame, not as it is now. */
        Canvas& DrawLayer(const std::shared_ptr<Layer>& layer);
        /** @brief A picture, copied in under the current transform and clip. */
        Canvas& DrawPicture(const Picture& picture);

        // -- drawing with a pen
        /**
         * @brief The pen: a path built up a segment at a time, then filled or stroked — as an HTML canvas
         *        does it. BeginPath starts it empty; MoveTo starts a new part of it; Fill and Stroke draw
         *        it as it stands, and leave it, so one path can be filled and then outlined.
         *
         * @code
         * canvas.BeginPath()
         *       .MoveTo({ 10, 10 })
         *       .DrawLineTo({ 100, 10 })
         *       .DrawArcTo({ 120, 10 }, { 120, 30 }, 20.f)   // a rounded corner
         *       .DrawLineTo({ 120, 80 })
         *       .DrawQuadTo({ 60, 120 }, { 10, 80 })
         *       .ClosePath()
         *       .Fill(kui::Paint::Fill(blue))
         *       .Stroke(kui::Paint::Stroked(white, 2.f));
         * @endcode
         */
        Canvas& BeginPath();
        Canvas& MoveTo(glm::vec2 point);
        /** @brief A straight line from where the pen is to @p point. */
        Canvas& DrawLineTo(glm::vec2 point);
        /** @brief A quadratic curve to @p point, pulled towards @p control. */
        Canvas& DrawQuadTo(glm::vec2 control, glm::vec2 point);
        /** @brief A cubic curve to @p point, pulled towards @p control1 then @p control2. */
        Canvas& DrawCubicTo(glm::vec2 control1, glm::vec2 control2, glm::vec2 point);
        /** @brief An arc of the circle at @p center from @p start sweeping @p sweep radians, joined to the pen by a line. */
        Canvas& DrawArcTo(glm::vec2 center, float radius, float start, float sweep);
        /**
         * @brief A rounded corner: a line towards @p corner that turns, along an arc of @p radius, to head
         *        for @p to — the pen stops where the arc meets that second line.
         */
        Canvas& DrawArcTo(glm::vec2 corner, glm::vec2 to, float radius);
        /** @brief A line back to where this part of the path started. */
        Canvas& ClosePath();
        /** @brief Fills the pen's path with @p paint's fill (its stroke is ignored). */
        Canvas& Fill(const Paint& paint);
        /** @brief Outlines the pen's path with @p paint's stroke — or, without one, a hairline of its fill colour. */
        Canvas& Stroke(const Paint& paint);
        /** @brief The path the pen has drawn so far. */
        [[nodiscard]] const Path& CurrentPath() const;

        /** @brief The recording, as a picture. The canvas is empty again afterwards. */
        [[nodiscard]] std::shared_ptr<const Picture> Finish();

        /** @brief How finely curves are flattened, in logical units at scale 1. */
        Canvas& SetTolerance(float tolerance);

        struct State;
    private:
        std::unique_ptr<State> _state;
    };
}
