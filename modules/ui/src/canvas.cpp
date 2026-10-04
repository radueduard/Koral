//
// koral-ui: recording. Every draw becomes GPU instances (or a tessellated mesh) here, once, in the
// picture's own space — drawing the picture later is a copy.
//

#include <algorithm>
#include <cstring>
#include <atomic>
#include <cmath>
#include <limits>

#include <log.h>

#include <kui/text.h>
#include <kui/widgets.h>

#include "gpu.h"
#include "tessellate.h"

namespace kui
{
    // ---- values -------------------------------------------------------------------------------------

    std::uint32_t Color::Packed() const
    {
        const auto q = [](const float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }

    Rect Rect::Intersect(const Rect& o) const
    {
        return { std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom) };
    }

    Rect Rect::Union(const Rect& o) const
    {
        if (Empty()) return o;
        if (o.Empty()) return *this;
        return { std::min(left, o.left), std::min(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom) };
    }

    Transform Transform::Rotation(const float radians)
    {
        const float c = std::cos(radians), s = std::sin(radians);
        return { c, s, -s, c, 0.f, 0.f };
    }

    Transform Transform::Inverse() const
    {
        const float det = a * d - b * c;
        if (std::abs(det) < 1e-12f) return { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
        const float id = 1.f / det;
        const float ia = d * id, ib = -b * id, ic = -c * id, idd = a * id;
        return { ia, ib, ic, idd, -(ia * tx + ic * ty), -(ib * tx + idd * ty) };
    }

    Rect Transform::MapRect(const Rect& r) const
    {
        const glm::vec2 p[4] = { Apply({ r.left, r.top }), Apply({ r.right, r.top }), Apply({ r.right, r.bottom }), Apply({ r.left, r.bottom }) };
        Rect out { p[0].x, p[0].y, p[0].x, p[0].y };
        for (const auto& q : p) {
            out.left = std::min(out.left, q.x); out.top = std::min(out.top, q.y);
            out.right = std::max(out.right, q.x); out.bottom = std::max(out.bottom, q.y);
        }
        return out;
    }

    Gradient Gradient::Linear(const glm::vec2 from, const glm::vec2 to, std::vector<GradientStop> stops)
    {
        Gradient g;
        g.type = Type::eLinear; g.start = from; g.end = to; g.stops = std::move(stops);
        return g;
    }

    Gradient Gradient::Linear(const glm::vec2 from, const glm::vec2 to, const Color a, const Color b)
    {
        return Linear(from, to, { { 0.f, a }, { 1.f, b } });
    }

    Gradient Gradient::Radial(const glm::vec2 center, const float radius, std::vector<GradientStop> stops)
    {
        Gradient g;
        g.type = Type::eRadial; g.start = center; g.radius = radius; g.stops = std::move(stops);
        return g;
    }

    Gradient Gradient::Sweep(const glm::vec2 center, const float angle, std::vector<GradientStop> stops)
    {
        Gradient g;
        g.type = Type::eSweep; g.start = center; g.angle = angle; g.stops = std::move(stops);
        return g;
    }

    // ---- pictures and layers ------------------------------------------------------------------------

    Picture::Picture(std::unique_ptr<Data> data) : _data(std::move(data)) {}
    Picture::~Picture() = default;
    Rect Picture::Bounds() const { return _data->bounds; }
    std::size_t Picture::InstanceCount() const { return _data->instances.size(); }

    namespace { std::atomic<std::uint64_t> layerEpoch { 1 }; }

    std::uint64_t Layer::Epoch() { return layerEpoch.load(std::memory_order_relaxed); }

    std::shared_ptr<Layer> Layer::Create() { return std::shared_ptr<Layer>(new Layer()); }

    Layer& Layer::SetPicture(std::shared_ptr<const Picture> picture)
    {
        if (picture == _picture) return *this;
        _picture = std::move(picture);
        ++_contentVersion;
        layerEpoch.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    Layer& Layer::SetTransform(const Transform& transform)
    {
        if (transform == _transform) return *this;
        _transform = transform;
        ++_placementVersion;
        layerEpoch.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    Layer& Layer::SetOpacity(const float opacity)
    {
        if (opacity == _opacity) return *this;
        _opacity = opacity;
        ++_placementVersion;
        layerEpoch.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    // ---- the canvas ---------------------------------------------------------------------------------

    using detail::Instance;

    struct Canvas::State {
        struct Saved {
            Transform transform;
            std::uint32_t clip = detail::None;
        };
        Saved current;
        std::vector<Saved> stack;
        Path pen;   // what BeginPath / MoveTo / DrawLineTo build, until Fill or Stroke draw it
        bool culls = false;
        Rect cull {};   // in the canvas's own coordinates: what will be looked at, when culls is set
        std::unique_ptr<Picture::Data> data = std::make_unique<Picture::Data>();
        float tolerance = 0.25f;

        State() { data->textures.emplace_back(); }   // slot 0: the glyph atlas

        /** @brief How many local units make one logical unit, under the current transform. */
        [[nodiscard]] float UnitScale() const
        {
            const auto& t = current.transform;
            return std::sqrt(std::max(std::abs(t.a * t.d - t.b * t.c), 1e-12f));
        }

        Instance Make(const detail::Kind kind, const Rect& bounds) const
        {
            Instance it;
            it.bounds = { bounds.left, bounds.top, bounds.right, bounds.bottom };
            it.xform = detail::Pack(current.transform);
            it.translate = { current.transform.tx, current.transform.ty };
            it.SetLayerClip(0, current.clip);
            it.kindFlags = kind;
            return it;
        }

        void Extend(const Rect& localBounds) { data->bounds = data->bounds.Union(current.transform.MapRect(localBounds)); }

        std::uint32_t Push(const Instance& it, const Picture::Data::Run::Kind kind = Picture::Data::Run::Kind::ePrimitives,
                           const std::shared_ptr<ElementShader>& shader = nullptr)
        {
            const auto index = static_cast<std::uint32_t>(data->instances.size());
            data->instances.push_back(it);
            auto& runs = data->runs;
            if (kind != Picture::Data::Run::Kind::eMesh) {
                if (!runs.empty() && runs.back().kind == kind && runs.back().shader == shader && runs.back().first + runs.back().count == index)
                    ++runs.back().count;
                else
                    runs.push_back({ kind, index, 1, shader });
            }
            Extend({ it.bounds.x, it.bounds.y, it.bounds.z, it.bounds.w });
            return index;
        }

        /** @brief Sets the fill side of @p it from @p paint: colour, or a gradient. */
        void ApplyFill(Instance& it, const Paint& paint)
        {
            if (paint.gradient) {
                it.kindFlags |= (detail::eFill | detail::eGradient) << 8;
                it.paint = AddGradient(*paint.gradient);
                it.fill = Color(1.f, 1.f, 1.f, paint.opacity).Packed();
            } else if (paint.fill.Visible()) {
                it.kindFlags |= detail::eFill << 8;
                it.fill = paint.fill.WithAlpha(paint.fill.a * paint.opacity).Packed();
            }
        }

        void ApplyStroke(Instance& it, const Paint& paint)
        {
            if (!paint.HasStroke()) return;
            it.kindFlags |= detail::eStroke << 8;
            it.stroke = paint.stroke.color.WithAlpha(paint.stroke.color.a * paint.opacity).Packed();
            it.strokeWidth = paint.stroke.width;
        }

        std::uint32_t AddGradient(const Gradient& g)
        {
            detail::GpuGradient gpu;
            gpu.type = static_cast<std::uint32_t>(g.type);
            switch (g.type) {
            case Gradient::Type::eLinear: gpu.geometry = { g.start, g.end }; break;
            case Gradient::Type::eRadial: gpu.geometry = { g.start, g.radius, 0.f }; break;
            case Gradient::Type::eSweep:  gpu.geometry = { g.start, g.angle, 0.f }; break;
            }
            gpu.count = static_cast<std::uint32_t>(std::min<std::size_t>(g.stops.size(), 8));
            for (std::uint32_t i = 0; i < gpu.count; ++i) {
                gpu.colors[i] = g.stops[i].color.Packed();
                gpu.stops[i] = g.stops[i].offset;
            }
            if (gpu.count == 0) { gpu.count = 1; gpu.colors[0] = colors::Transparent.Packed(); }
            data->gradients.push_back(gpu);
            return static_cast<std::uint32_t>(data->gradients.size() - 1);
        }

        std::uint32_t AddTexture(const kor::ResourceRef<const kor::Image>& image)
        {
            for (std::size_t i = 1; i < data->textures.size(); ++i)
                if (data->textures[i].Get() == image.Get()) return static_cast<std::uint32_t>(i);
            data->textures.push_back(image);
            return static_cast<std::uint32_t>(data->textures.size() - 1);
        }

        /** @brief A tessellated mesh, filled or stroked as @p stroke says, under the current transform. */
        void PushMesh(const detail::Mesh& mesh, const Paint& paint, const bool stroke)
        {
            if (mesh.Empty()) return;
            Rect bounds { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
            for (const auto& p : mesh.positions) {
                bounds.left = std::min(bounds.left, p.x); bounds.top = std::min(bounds.top, p.y);
                bounds.right = std::max(bounds.right, p.x); bounds.bottom = std::max(bounds.bottom, p.y);
            }
            Instance it = Make(detail::eMesh, bounds);
            if (stroke) ApplyStroke(it, paint);
            else ApplyFill(it, paint);
            const std::uint32_t index = Push(it, Picture::Data::Run::Kind::eMesh);
            const auto first = static_cast<std::uint32_t>(data->vertices.size());
            for (std::size_t i = 0; i < mesh.positions.size(); ++i)
                data->vertices.push_back({ mesh.positions[i], mesh.coverage[i], index });
            auto& runs = data->runs;
            if (!runs.empty() && runs.back().kind == Picture::Data::Run::Kind::eMesh && runs.back().first + runs.back().count == first)
                runs.back().count += static_cast<std::uint32_t>(mesh.positions.size());
            else
                runs.push_back({ Picture::Data::Run::Kind::eMesh, first, static_cast<std::uint32_t>(mesh.positions.size()), nullptr });
        }

        /** @brief Fills and strokes flattened contours as meshes. */
        void DrawContours(const std::vector<Path::Contour>& contours, const FillRule rule, const Paint& paint, const bool fill)
        {
            const float pixel = 1.f / UnitScale();   // a logical unit, in local units: the fringe's width
            if (fill && paint.HasFill()) {
                detail::Mesh mesh;
                detail::Fill(contours, rule, pixel, mesh);
                PushMesh(mesh, paint, false);
            }
            if (paint.HasStroke()) {
                detail::Mesh mesh;
                detail::StrokePath(contours, paint.stroke, tolerance * pixel, pixel, mesh);
                PushMesh(mesh, paint, true);
            }
        }
    };

    Canvas::Canvas() : _state(std::make_unique<State>()) {}
    Canvas::~Canvas() = default;

    Canvas& Canvas::Save() { _state->stack.push_back(_state->current); return *this; }

    Canvas& Canvas::Restore()
    {
        if (_state->stack.empty()) return *this;
        _state->current = _state->stack.back();
        _state->stack.pop_back();
        return *this;
    }

    std::size_t Canvas::SaveCount() const { return _state->stack.size(); }
    Canvas& Canvas::Translate(const glm::vec2 by) { _state->current.transform = _state->current.transform * Transform::Translation(by); return *this; }
    Canvas& Canvas::Scale(const glm::vec2 by) { _state->current.transform = _state->current.transform * Transform::Scaling(by); return *this; }
    Canvas& Canvas::Rotate(const float radians) { _state->current.transform = _state->current.transform * Transform::Rotation(radians); return *this; }
    Canvas& Canvas::Concat(const Transform& t) { _state->current.transform = _state->current.transform * t; return *this; }
    Canvas& Canvas::SetTransform(const Transform& t) { _state->current.transform = t; return *this; }
    const Transform& Canvas::CurrentTransform() const { return _state->current.transform; }
    Canvas& Canvas::SetTolerance(const float tolerance) { _state->tolerance = std::max(tolerance, 0.01f); return *this; }

    Canvas& Canvas::ClipRect(const Rect& rect) { ClipRRect({ rect, {} }); return *this; }

    Canvas& Canvas::ClipRRect(const RRect& rrect)
    {
        auto& clips = _state->data->clips;
        if (clips.size() >= detail::None - 1) {
            kor::log::Error("[kui] a picture can hold at most {} clips", detail::None - 1);
            return *this;
        }
        clips.push_back({ _state->current.transform, rrect.rect, rrect.radii, _state->current.clip });
        _state->current.clip = static_cast<std::uint32_t>(clips.size() - 1);
        return *this;
    }

    Canvas& Canvas::SetCullRect(const Rect& rect)
    {
        _state->culls = true;
        _state->cull = rect;
        return *this;
    }

    Canvas& Canvas::Reserve(const std::size_t shapes)
    {
        _state->data->instances.reserve(shapes);
        return *this;
    }

    bool Canvas::QuickReject(const Rect& rect) const
    {
        if (!_state->culls) return false;
        const Rect r = _state->current.transform.IsIdentity() ? rect : _state->current.transform.MapRect(rect);
        const Rect& c = _state->cull;
        return r.right <= c.left || r.left >= c.right || r.bottom <= c.top || r.top >= c.bottom;
    }

    Canvas& Canvas::DrawRect(const Rect& rect, const Paint& paint) { DrawRRect({ rect, {} }, paint); return *this; }

    Canvas& Canvas::DrawRRect(const RRect& rrect, const Paint& paint)
    {
        if (!paint.HasFill() && !paint.HasStroke()) return *this;
        const float grow = paint.HasStroke() ? paint.stroke.width * 0.5f : 0.f;
        Instance it = _state->Make(detail::eRect, rrect.rect.Inflate(grow));
        it.shape0 = { rrect.rect.left, rrect.rect.top, rrect.rect.right, rrect.rect.bottom };
        it.shape1 = { rrect.radii.topLeft, rrect.radii.topRight, rrect.radii.bottomRight, rrect.radii.bottomLeft };
        _state->ApplyFill(it, paint);
        _state->ApplyStroke(it, paint);
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawCircle(const glm::vec2 center, const float radius, const Paint& paint)
    {
        DrawOval(Rect::FromCenter(center, radius * 2.f, radius * 2.f), paint);
        return *this;
    }

    Canvas& Canvas::DrawOval(const Rect& rect, const Paint& paint)
    {
        if (!paint.HasFill() && !paint.HasStroke()) return *this;
        const float grow = paint.HasStroke() ? paint.stroke.width * 0.5f : 0.f;
        Instance it = _state->Make(detail::eEllipse, rect.Inflate(grow));
        it.shape0 = { rect.Center(), rect.Size() * 0.5f };
        _state->ApplyFill(it, paint);
        _state->ApplyStroke(it, paint);
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawArc(const glm::vec2 center, const float radius, const float start, const float sweep,
                         const bool useCenter, const Paint& paint)
    {
        if (!useCenter && !paint.HasStroke()) return *this;
        if (useCenter && !paint.HasFill() && !paint.HasStroke()) return *this;
        const float grow = paint.HasStroke() ? paint.stroke.width * 0.5f : 0.f;
        Instance it = _state->Make(detail::eArc, Rect::FromCenter(center, radius * 2.f, radius * 2.f).Inflate(grow));
        it.shape0 = { center, radius, 0.f };
        it.shape1 = { start, sweep, useCenter ? 1.f : 0.f, 0.f };
        if (useCenter) _state->ApplyFill(it, paint);
        _state->ApplyStroke(it, paint);
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawLine(const glm::vec2 from, const glm::vec2 to, const Paint& paint)
    {
        Paint line = paint;
        if (!line.HasStroke()) {
            if (!paint.fill.Visible()) return *this;
            line.stroke.color = paint.fill;
            line.stroke.width = 1.f;
        }
        const float hw = line.stroke.width * 0.5f;
        Rect bounds = Rect::LTRB(std::min(from.x, to.x), std::min(from.y, to.y), std::max(from.x, to.x), std::max(from.y, to.y));
        Instance it = _state->Make(detail::eSegment, bounds.Inflate(hw * 1.5f));   // a square cap's corner reaches √2·hw
        it.shape0 = { from, to };
        it.shape1 = { static_cast<float>(line.stroke.cap), 0.f, 0.f, 0.f };
        _state->ApplyStroke(it, line);
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawTriangle(const glm::vec2 a, const glm::vec2 b, const glm::vec2 c, const Paint& paint)
    {
        if (!paint.HasFill() && !paint.HasStroke()) return *this;
        // The SDF triangle's stroke would round its corners; a joined stroke is a path's job.
        if (paint.HasStroke() && paint.stroke.join != StrokeJoin::eRound) {
            if (paint.HasFill()) {
                Paint fill = paint;
                fill.stroke.width = 0.f;
                DrawTriangle(a, b, c, fill);
            }
            const glm::vec2 points[] = { a, b, c };
            Paint stroke = paint;
            stroke.fill = colors::Transparent;
            stroke.gradient.reset();
            DrawPolygon(points, stroke);
            return *this;
        }
        const float grow = paint.HasStroke() ? paint.stroke.width * 0.5f : 0.f;
        const Rect bounds = Rect::LTRB(std::min({ a.x, b.x, c.x }), std::min({ a.y, b.y, c.y }),
                                       std::max({ a.x, b.x, c.x }), std::max({ a.y, b.y, c.y }));
        Instance it = _state->Make(detail::eTriangle, bounds.Inflate(grow));
        it.shape0 = { a, b };
        it.shape1 = { c, 0.f, 0.f };
        _state->ApplyFill(it, paint);
        _state->ApplyStroke(it, paint);
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawQuadraticBezier(const glm::vec2 from, const glm::vec2 control, const glm::vec2 to, const Paint& paint)
    {
        Paint line = paint;
        if (!line.HasStroke()) {
            if (!paint.fill.Visible()) return *this;
            line.stroke.color = paint.fill;
            line.stroke.width = 1.f;
        }
        // The distance shader's curve ends round; other caps need the tessellated stroke.
        if (line.stroke.cap != StrokeCap::eRound) {
            Path path;
            path.MoveTo(from).QuadTo(control, to);
            line.fill = colors::Transparent;
            line.gradient.reset();
            DrawPath(path, line);
            return *this;
        }
        const float hw = line.stroke.width * 0.5f;
        const Rect bounds = Rect::LTRB(std::min({ from.x, control.x, to.x }), std::min({ from.y, control.y, to.y }),
                                       std::max({ from.x, control.x, to.x }), std::max({ from.y, control.y, to.y }));
        Instance it = _state->Make(detail::eBezier, bounds.Inflate(hw));
        it.shape0 = { from, control };
        it.shape1 = { to, 0.f, 0.f };
        _state->ApplyStroke(it, line);
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawCubicBezier(const glm::vec2 from, const glm::vec2 c1, const glm::vec2 c2, const glm::vec2 to, const Paint& paint)
    {
        Path path;
        path.MoveTo(from).CubicTo(c1, c2, to);
        Paint line = paint;
        if (!line.HasStroke()) { line.stroke.color = paint.fill; line.stroke.width = 1.f; }
        line.fill = colors::Transparent;
        line.gradient.reset();
        DrawPath(path, line);
        return *this;
    }

    Canvas& Canvas::DrawPolyline(const std::span<const glm::vec2> points, const Paint& paint)
    {
        Path path;
        path.AddPolygon(points, false);
        Paint line = paint;
        line.fill = colors::Transparent;
        line.gradient.reset();
        if (!line.HasStroke()) { line.stroke.color = paint.fill; line.stroke.width = 1.f; }
        DrawPath(path, line);
        return *this;
    }

    Canvas& Canvas::DrawPolygon(const std::span<const glm::vec2> points, const Paint& paint)
    {
        Path path;
        path.AddPolygon(points, true);
        DrawPath(path, paint);
        return *this;
    }

    Canvas& Canvas::DrawPath(const Path& path, const Paint& paint)
    {
        if (path.Empty() || (!paint.HasFill() && !paint.HasStroke())) return *this;
        // Flattened finely enough for the size it is drawn at now.
        const auto contours = path.Flatten(_state->tolerance / _state->UnitScale());
        _state->DrawContours(contours, path.GetFillRule(), paint, true);
        return *this;
    }

    Canvas& Canvas::DrawShadow(const RRect& rrect, const Color color, const float blur, const glm::vec2 offset, const float spread)
    {
        if (!color.Visible()) return *this;
        const Rect box = rrect.rect.Shift(offset).Inflate(spread);
        const float sigma = std::max(blur, 0.01f);
        Instance it = _state->Make(detail::eShadow, box.Inflate(sigma * 3.f));
        it.shape0 = { box.left, box.top, box.right, box.bottom };
        it.shape1 = { std::max(0.f, rrect.radii.topLeft + spread), 0.f, 0.f, 0.f };
        it.strokeWidth = sigma;
        it.fill = color.Packed();
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawImage(const kor::ResourceRef<const kor::Image>& image, const Rect& destination, const Rect& source, const Color tint)
    {
        if (!image.Valid() || destination.Empty()) return *this;
        const glm::vec2 extent = glm::vec2(image->Extent().x, image->Extent().y);
        const Rect src = source.Empty() ? Rect::FromSize(extent) : source;
        Instance it = _state->Make(detail::eImage, destination);
        it.shape0 = { destination.left, destination.top, destination.right, destination.bottom };
        it.shape1 = { src.left / extent.x, src.top / extent.y, src.right / extent.x, src.bottom / extent.y };
        it.fill = tint.Packed();
        it.SetTexture(_state->AddTexture(image));
        _state->Push(it);
        return *this;
    }

    Canvas& Canvas::DrawParagraph(const Paragraph& paragraph, const glm::vec2 position)
    {
        const TextStyle& style = paragraph.Style();
        // Of no colour of its own, it is the colour of text in the theme this is drawn in.
        const Color color = style.color.a < 0.f ? Theme::Current().text : style.color;
        if (!color.Visible()) return *this;
        const std::uint32_t packed = color.Packed();
        // Heavier or lighter than the font is drawn: its outline moved out or in, by so much of its size.
        const float thicken = (style.weight - 400.f) / 300.f * 0.032f * style.size;
        std::uint32_t thickenBits = 0;
        static_assert(sizeof thickenBits == sizeof thicken);
        std::memcpy(&thickenBits, &thicken, sizeof thicken);
        const auto& lines = paragraph.Lines();
        std::size_t line = 0;
        bool slanted = false;
        const auto slant = [&](const std::size_t index) {
            // Sheared about the line's own baseline, so that the letters lean without leaving it.
            if (slanted) { Restore(); slanted = false; }
            if (!style.italic || index >= lines.size()) return;
            constexpr float Lean = 0.2f;
            const float baseline = position.y + lines[index].baseline;
            Save();
            Concat(Transform { 1.f, 0.f, -Lean, 1.f, Lean * baseline, 0.f });
            slanted = true;
        };
        slant(0);
        for (const auto& glyph : paragraph.Glyphs()) {
            while (line + 1 < lines.size() && glyph.byte >= lines[line].endByte) slant(++line);
            const Rect r = glyph.rect.Shift(position);
            Instance it = _state->Make(detail::eGlyph, r);
            it.shape0 = { r.left, r.top, r.right, r.bottom };
            it.shape1 = { glyph.uv.left, glyph.uv.top, glyph.uv.right, glyph.uv.bottom };
            it.strokeWidth = glyph.distanceScale;
            it.fill = packed;
            it.stroke = thickenBits;
            it.kindFlags |= detail::eFill << 8;
            it.SetTexture(0);
            _state->Push(it);
        }
        if (slanted) Restore();
        if (style.underline || style.lineThrough) {
            // A line under each line of text, or through the middle of its letters.
            const float thick = std::max(std::round(style.size / 14.f), 1.f);
            for (const auto& l : lines) {
                if (l.width <= 0.f) continue;
                const float left = position.x + l.x, right = left + l.width;
                if (style.underline) {
                    const float y = std::round(position.y + l.baseline + style.size * 0.12f);
                    DrawRect(Rect::LTRB(left, y, right, y + thick), Paint::Fill(color));
                }
                if (style.lineThrough) {
                    const float y = std::round(position.y + l.baseline - style.size * 0.28f);
                    DrawRect(Rect::LTRB(left, y, right, y + thick), Paint::Fill(color));
                }
            }
        }
        return *this;
    }

    Canvas& Canvas::DrawText(const std::string_view text, const glm::vec2 position, const TextStyle& style)
    {
        const Paragraph paragraph(std::string(text), style);
        DrawParagraph(paragraph, position);
        return *this;
    }

    Canvas& Canvas::DrawElement(const std::shared_ptr<ElementShader>& shader, const Rect& rect,
                             const std::span<const std::byte> parameters, const Radii radii, const float opacity)
    {
        if (!shader || rect.Empty()) return *this;
        Instance it = _state->Make(detail::eCustom, rect);
        it.shape0 = { rect.left, rect.top, rect.right, rect.bottom };
        it.shape1 = { radii.topLeft, radii.topRight, radii.bottomRight, radii.bottomLeft };
        it.fill = Color(1.f, 1.f, 1.f, opacity).Packed();
        it.paint = static_cast<std::uint32_t>(_state->data->parameters.size());
        _state->data->parameters.emplace_back(parameters.begin(), parameters.end());
        _state->Push(it, Picture::Data::Run::Kind::eElement, shader);
        return *this;
    }

    Canvas& Canvas::DrawLayer(const std::shared_ptr<Layer>& layer)
    {
        if (!layer) return *this;
        auto& data = *_state->data;
        data.layers.push_back({ layer, _state->current.transform, _state->current.clip });
        data.runs.push_back({ Picture::Data::Run::Kind::eLayer, static_cast<std::uint32_t>(data.layers.size() - 1), 1, nullptr });
        return *this;
    }

    Canvas& Canvas::DrawPicture(const Picture& picture)
    {
        // Copied in: its instances moved by the current transform, its indices shifted past ours.
        const auto& src = picture.Contents();
        auto& dst = *_state->data;
        const Transform& t = _state->current.transform;
        const auto instanceBase = static_cast<std::uint32_t>(dst.instances.size());
        const auto vertexBase = static_cast<std::uint32_t>(dst.vertices.size());
        const auto clipBase = static_cast<std::uint32_t>(dst.clips.size());
        const auto gradientBase = static_cast<std::uint32_t>(dst.gradients.size());
        const auto parameterBase = static_cast<std::uint32_t>(dst.parameters.size());
        const auto layerBase = static_cast<std::uint32_t>(dst.layers.size());
        const std::uint32_t inherited = _state->current.clip;
        const auto clipOf = [&](const std::uint32_t clip) { return clip == detail::None ? inherited : clip + clipBase; };

        for (const auto& clip : src.clips) dst.clips.push_back({ t * clip.transform, clip.rect, clip.radii, clipOf(clip.parent) });
        dst.gradients.insert(dst.gradients.end(), src.gradients.begin(), src.gradients.end());
        dst.parameters.insert(dst.parameters.end(), src.parameters.begin(), src.parameters.end());
        std::vector<std::uint32_t> textureMap(src.textures.size(), 0);
        for (std::size_t i = 1; i < src.textures.size(); ++i) textureMap[i] = _state->AddTexture(src.textures[i]);

        for (auto it : src.instances) {
            const Transform local { it.xform.x, it.xform.y, it.xform.z, it.xform.w, it.translate.x, it.translate.y };
            const Transform moved = t * local;
            it.xform = detail::Pack(moved);
            it.translate = { moved.tx, moved.ty };
            it.SetLayerClip(0, clipOf(it.Clip()));
            if (it.Flags() & detail::eGradient) it.paint += gradientBase;
            if (it.Kind() == detail::eCustom) it.paint += parameterBase;
            it.SetTexture(textureMap[it.Texture()]);
            dst.instances.push_back(it);
        }
        for (auto v : src.vertices) {
            v.instance += instanceBase;
            dst.vertices.push_back(v);
        }
        for (const auto& ref : src.layers) dst.layers.push_back({ ref.layer, t * ref.transform, clipOf(ref.clip) });
        for (auto run : src.runs) {
            switch (run.kind) {
            case Picture::Data::Run::Kind::eMesh: run.first += vertexBase; break;
            case Picture::Data::Run::Kind::eLayer: run.first += layerBase; break;
            default: run.first += instanceBase; break;
            }
            dst.runs.push_back(run);
        }
        dst.bounds = dst.bounds.Union(t.MapRect(src.bounds));
        return *this;
    }

    // ---- the pen ----------------------------------------------------------------------------------------

    Canvas& Canvas::BeginPath() { _state->pen = Path(); return *this; }
    Canvas& Canvas::MoveTo(const glm::vec2 point) { _state->pen.MoveTo(point); return *this; }
    Canvas& Canvas::DrawLineTo(const glm::vec2 point) { _state->pen.LineTo(point); return *this; }
    Canvas& Canvas::DrawQuadTo(const glm::vec2 control, const glm::vec2 point) { _state->pen.QuadTo(control, point); return *this; }
    Canvas& Canvas::DrawCubicTo(const glm::vec2 c1, const glm::vec2 c2, const glm::vec2 point) { _state->pen.CubicTo(c1, c2, point); return *this; }
    Canvas& Canvas::DrawArcTo(const glm::vec2 center, const float radius, const float start, const float sweep) { _state->pen.ArcTo(center, radius, start, sweep); return *this; }
    Canvas& Canvas::DrawArcTo(const glm::vec2 corner, const glm::vec2 to, const float radius) { _state->pen.ArcTo(corner, to, radius); return *this; }
    Canvas& Canvas::ClosePath() { _state->pen.Close(); return *this; }
    const Path& Canvas::CurrentPath() const { return _state->pen; }

    Canvas& Canvas::Fill(const Paint& paint)
    {
        Paint fill = paint;
        fill.stroke.width = 0.f;
        return DrawPath(_state->pen, fill);
    }

    Canvas& Canvas::Stroke(const Paint& paint)
    {
        Paint stroke = paint;
        if (!stroke.HasStroke()) { stroke.stroke.color = paint.fill; stroke.stroke.width = 1.f; }
        stroke.fill = colors::Transparent;
        stroke.gradient.reset();
        return DrawPath(_state->pen, stroke);
    }

    std::shared_ptr<const Picture> Canvas::Finish()
    {
        auto picture = std::make_shared<const Picture>(std::move(_state->data));
        _state = std::make_unique<State>();
        return picture;
    }
}
