//
// koral-ui: vector images read from SVG, the Material icons, and the widget that shows them.
//

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "kui/icons.h"
#include "materialIcons.h"

namespace kui
{
    // ---- reading SVG -----------------------------------------------------------------------------------

    namespace {
        bool space(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ','; }

        /** @brief Numbers as SVG writes them: `-.5`, `1e-3`, `1.5.5` (two of them), run together without spaces. */
        class Numbers {
        public:
            explicit Numbers(const std::string_view text) : _text(text) {}

            void SkipSpace() { while (_at < _text.size() && space(_text[_at])) ++_at; }
            [[nodiscard]] bool Done() { SkipSpace(); return _at >= _text.size(); }
            [[nodiscard]] char Peek() { SkipSpace(); return _at < _text.size() ? _text[_at] : '\0'; }
            char Take() { SkipSpace(); return _at < _text.size() ? _text[_at++] : '\0'; }
            [[nodiscard]] bool AtNumber()
            {
                const char c = Peek();
                return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
            }

            std::optional<float> Number()
            {
                // By hand, not strtof: that one reads a comma as the decimal point where the locale says so.
                SkipSpace();
                std::size_t i = _at;
                double sign = 1.;
                if (i < _text.size() && (_text[i] == '-' || _text[i] == '+')) sign = _text[i++] == '-' ? -1. : 1.;
                double value = 0.;
                bool digits = false;
                while (i < _text.size() && _text[i] >= '0' && _text[i] <= '9') { value = value * 10. + (_text[i++] - '0'); digits = true; }
                if (i < _text.size() && _text[i] == '.') {
                    ++i;
                    double scale = 0.1;
                    while (i < _text.size() && _text[i] >= '0' && _text[i] <= '9') { value += (_text[i++] - '0') * scale; scale *= 0.1; digits = true; }
                }
                if (!digits) return std::nullopt;
                if (i < _text.size() && (_text[i] == 'e' || _text[i] == 'E')) {
                    std::size_t j = i + 1;
                    int expSign = 1, exponent = 0;
                    if (j < _text.size() && (_text[j] == '-' || _text[j] == '+')) expSign = _text[j++] == '-' ? -1 : 1;
                    if (j < _text.size() && _text[j] >= '0' && _text[j] <= '9') {
                        while (j < _text.size() && _text[j] >= '0' && _text[j] <= '9') exponent = exponent * 10 + (_text[j++] - '0');
                        value *= std::pow(10., expSign * exponent);
                        i = j;
                    }
                }
                _at = i;
                return static_cast<float>(sign * value);
            }

            /** @brief An arc's flag: one character, 0 or 1, which may touch the next number (`a1 1 0 01-2 0`). */
            std::optional<bool> Flag()
            {
                const char c = Peek();
                if (c != '0' && c != '1') return std::nullopt;
                ++_at;
                return c == '1';
            }

        private:
            std::string_view _text;
            std::size_t _at = 0;
        };

        /** @brief Builds a Path from points given in the SVG's units, mapped through the transform of where they are. */
        struct Pen {
            Path& path;
            Transform transform;

            void MoveTo(const kor::Vec2 p) const { path.MoveTo(transform.Apply(p)); }
            void LineTo(const kor::Vec2 p) const { path.LineTo(transform.Apply(p)); }
            void QuadTo(const kor::Vec2 c, const kor::Vec2 p) const { path.QuadTo(transform.Apply(c), transform.Apply(p)); }
            void CubicTo(const kor::Vec2 c1, const kor::Vec2 c2, const kor::Vec2 p) const
            {
                path.CubicTo(transform.Apply(c1), transform.Apply(c2), transform.Apply(p));
            }
            void Close() const { path.Close(); }

            /** @brief An ellipse, as four cubics: under a transform it may be any ellipse at all, which only curves can follow. */
            void Ellipse(const kor::Vec2 c, const kor::Vec2 r) const
            {
                constexpr float k = 0.5522847498f;   // 4/3 (√2 − 1): a quarter circle's control arm
                MoveTo({ c.x + r.x, c.y });
                CubicTo({ c.x + r.x, c.y + r.y * k }, { c.x + r.x * k, c.y + r.y }, { c.x, c.y + r.y });
                CubicTo({ c.x - r.x * k, c.y + r.y }, { c.x - r.x, c.y + r.y * k }, { c.x - r.x, c.y });
                CubicTo({ c.x - r.x, c.y - r.y * k }, { c.x - r.x * k, c.y - r.y }, { c.x, c.y - r.y });
                CubicTo({ c.x + r.x * k, c.y - r.y }, { c.x + r.x, c.y - r.y * k }, { c.x + r.x, c.y });
                Close();
            }

            /** @brief SVG's arc, from @p from to @p to: the endpoint form turned into its centre (SVG 1.1, F.6.5) and drawn as cubics. */
            void Arc(const kor::Vec2 from, kor::Vec2 radii, const float rotationDegrees, const bool large, const bool sweep, const kor::Vec2 to) const
            {
                if (from == to) return;
                radii = kor::Abs(radii);
                if (radii.x < 1e-6f || radii.y < 1e-6f) { LineTo(to); return; }
                const float phi = rotationDegrees * std::numbers::pi_v<float> / 180.f;
                const float cosPhi = std::cos(phi), sinPhi = std::sin(phi);
                const kor::Vec2 half = (from - to) * 0.5f;
                const kor::Vec2 p { cosPhi * half.x + sinPhi * half.y, -sinPhi * half.x + cosPhi * half.y };
                // Radii too small to reach are scaled up until they just do.
                const float lambda = p.x * p.x / (radii.x * radii.x) + p.y * p.y / (radii.y * radii.y);
                if (lambda > 1.f) radii *= std::sqrt(lambda);
                const float rx2 = radii.x * radii.x, ry2 = radii.y * radii.y;
                const float numerator = rx2 * ry2 - rx2 * p.y * p.y - ry2 * p.x * p.x;
                const float denominator = rx2 * p.y * p.y + ry2 * p.x * p.x;
                float factor = denominator > 0.f ? std::sqrt(std::max(0.f, numerator / denominator)) : 0.f;
                if (large == sweep) factor = -factor;
                const kor::Vec2 cp { factor * radii.x * p.y / radii.y, -factor * radii.y * p.x / radii.x };
                const kor::Vec2 center = kor::Vec2(cosPhi * cp.x - sinPhi * cp.y, sinPhi * cp.x + cosPhi * cp.y) + (from + to) * 0.5f;
                const auto angle = [](const kor::Vec2 u, const kor::Vec2 v) {
                    return std::atan2(u.x * v.y - u.y * v.x, u.x * v.x + u.y * v.y);
                };
                const kor::Vec2 u { (p.x - cp.x) / radii.x, (p.y - cp.y) / radii.y };
                const kor::Vec2 v { (-p.x - cp.x) / radii.x, (-p.y - cp.y) / radii.y };
                const float start = angle({ 1.f, 0.f }, u);
                float delta = angle(u, v);
                constexpr float tau = 2.f * std::numbers::pi_v<float>;
                if (!sweep && delta > 0.f) delta -= tau;
                else if (sweep && delta < 0.f) delta += tau;

                // On the unit circle, a quarter turn at most per cubic, then out to the ellipse where it is.
                const auto onEllipse = [&](const kor::Vec2 unit) {
                    const kor::Vec2 s = unit * radii;
                    return center + kor::Vec2(cosPhi * s.x - sinPhi * s.y, sinPhi * s.x + cosPhi * s.y);
                };
                const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (tau / 4.f) - 1e-3f)));
                const float step = delta / static_cast<float>(pieces);
                const float k = 4.f / 3.f * std::tan(step / 4.f);
                float a = start;
                for (int i = 0; i < pieces; ++i) {
                    const kor::Vec2 d0 { std::cos(a), std::sin(a) }, d1 { std::cos(a + step), std::sin(a + step) };
                    const kor::Vec2 end = i + 1 == pieces ? to : onEllipse(d1);
                    CubicTo(onEllipse(d0 + k * kor::Vec2(-d0.y, d0.x)), onEllipse(d1 - k * kor::Vec2(-d1.y, d1.x)), end);
                    a += step;
                }
            }
        };

        /** @brief A `d` attribute, drawn: every command, relative or not, with the shorthands' reflected controls. */
        void PathData(const std::string_view d, const Pen& pen)
        {
            Numbers in(d);
            kor::Vec2 current {}, start {}, lastControl {};
            char previous = 0;
            char command = 0;
            while (!in.Done()) {
                // A number where a command would be repeats the last one — a moveto's repeats as a lineto.
                if (!in.AtNumber()) command = in.Take();
                else if (command == 0) return;
                else if (command == 'M') command = 'L';
                else if (command == 'm') command = 'l';

                const bool relative = command >= 'a' && command <= 'z';
                const kor::Vec2 base = relative ? current : kor::Vec2 {};
                const auto point = [&]() -> std::optional<kor::Vec2> {
                    const auto x = in.Number();
                    const auto y = x ? in.Number() : std::nullopt;
                    if (!y) return std::nullopt;
                    return base + kor::Vec2(*x, *y);
                };
                const char upper = static_cast<char>(relative ? command - 'a' + 'A' : command);
                switch (upper) {
                case 'M': {
                    const auto p = point(); if (!p) return;
                    pen.MoveTo(*p);
                    current = start = *p;
                    break;
                }
                case 'L': {
                    const auto p = point(); if (!p) return;
                    pen.LineTo(*p);
                    current = *p;
                    break;
                }
                case 'H': {
                    const auto x = in.Number(); if (!x) return;
                    current.x = (relative ? current.x : 0.f) + *x;
                    pen.LineTo(current);
                    break;
                }
                case 'V': {
                    const auto y = in.Number(); if (!y) return;
                    current.y = (relative ? current.y : 0.f) + *y;
                    pen.LineTo(current);
                    break;
                }
                case 'C': {
                    const auto c1 = point(), c2 = c1 ? point() : std::nullopt, p = c2 ? point() : std::nullopt;
                    if (!p) return;
                    pen.CubicTo(*c1, *c2, *p);
                    lastControl = *c2;
                    current = *p;
                    break;
                }
                case 'S': {
                    const bool follows = previous == 'C' || previous == 'S';
                    const kor::Vec2 c1 = follows ? 2.f * current - lastControl : current;
                    const auto c2 = point(), p = c2 ? point() : std::nullopt;
                    if (!p) return;
                    pen.CubicTo(c1, *c2, *p);
                    lastControl = *c2;
                    current = *p;
                    break;
                }
                case 'Q': {
                    const auto c = point(), p = c ? point() : std::nullopt;
                    if (!p) return;
                    pen.QuadTo(*c, *p);
                    lastControl = *c;
                    current = *p;
                    break;
                }
                case 'T': {
                    const bool follows = previous == 'Q' || previous == 'T';
                    const kor::Vec2 c = follows ? 2.f * current - lastControl : current;
                    const auto p = point(); if (!p) return;
                    pen.QuadTo(c, *p);
                    lastControl = c;
                    current = *p;
                    break;
                }
                case 'A': {
                    const auto rx = in.Number(), ry = rx ? in.Number() : std::nullopt, rotation = ry ? in.Number() : std::nullopt;
                    const auto large = rotation ? in.Flag() : std::nullopt, sweep = large ? in.Flag() : std::nullopt;
                    const auto p = sweep ? point() : std::nullopt;
                    if (!p) return;
                    pen.Arc(current, { *rx, *ry }, *rotation, *large, *sweep, *p);
                    current = *p;
                    break;
                }
                case 'Z':
                    pen.Close();
                    current = start;
                    break;
                default:
                    return;   // not SVG: what was read so far stands
                }
                previous = upper;
            }
        }

        // -- the document

        struct Attributes {
            std::string_view element;
            std::map<std::string_view, std::string_view> values;
            bool selfClosing = false;
            bool closing = false;

            [[nodiscard]] std::optional<std::string_view> Get(const std::string_view name) const
            {
                const auto it = values.find(name);
                return it == values.end() ? std::nullopt : std::optional(it->second);
            }
            [[nodiscard]] float Number(const std::string_view name, const float otherwise = 0.f) const
            {
                const auto v = Get(name);
                if (!v) return otherwise;
                Numbers in(*v);
                return in.Number().value_or(otherwise);
            }
        };

        /** @brief The next tag from @p at on, its attributes read; comments, declarations and text skipped. */
        std::optional<Attributes> NextTag(const std::string_view xml, std::size_t& at)
        {
            while (true) {
                at = xml.find('<', at);
                if (at == std::string_view::npos) return std::nullopt;
                if (xml.substr(at, 4) == "<!--") {
                    const std::size_t end = xml.find("-->", at);
                    if (end == std::string_view::npos) return std::nullopt;
                    at = end + 3;
                    continue;
                }
                if (at + 1 < xml.size() && (xml[at + 1] == '?' || xml[at + 1] == '!')) {
                    at = xml.find('>', at);
                    if (at == std::string_view::npos) return std::nullopt;
                    continue;
                }
                break;
            }
            Attributes tag;
            std::size_t i = at + 1;
            if (i < xml.size() && xml[i] == '/') { tag.closing = true; ++i; }
            const auto nameChar = [](const char c) { return c != '>' && c != '/' && c != '=' && !space(c) && c != '"' && c != '\''; };
            std::size_t begin = i;
            while (i < xml.size() && nameChar(xml[i])) ++i;
            tag.element = xml.substr(begin, i - begin);
            while (i < xml.size()) {
                while (i < xml.size() && space(xml[i])) ++i;
                if (i >= xml.size()) break;
                if (xml[i] == '>') { ++i; break; }
                if (xml[i] == '/') { tag.selfClosing = true; ++i; continue; }
                begin = i;
                while (i < xml.size() && nameChar(xml[i])) ++i;
                const std::string_view name = xml.substr(begin, i - begin);
                if (name.empty()) { ++i; continue; }
                while (i < xml.size() && space(xml[i])) ++i;
                if (i < xml.size() && xml[i] == '=') {
                    ++i;
                    while (i < xml.size() && space(xml[i])) ++i;
                    if (i < xml.size() && (xml[i] == '"' || xml[i] == '\'')) {
                        const char quote = xml[i++];
                        const std::size_t end = xml.find(quote, i);
                        if (end == std::string_view::npos) return std::nullopt;
                        tag.values[name] = xml.substr(i, end - i);
                        i = end + 1;
                    }
                }
            }
            at = i;
            return tag;
        }

        /** @brief A `transform` attribute: its list of matrix, translate, scale, rotate and skews, composed left to right. */
        Transform ParseTransform(const std::string_view text)
        {
            Transform result;
            std::size_t at = 0;
            while (at < text.size()) {
                while (at < text.size() && space(text[at])) ++at;
                const std::size_t open = text.find('(', at), close = text.find(')', at);
                if (open == std::string_view::npos || close == std::string_view::npos || close < open) break;
                std::string_view name = text.substr(at, open - at);
                while (!name.empty() && space(name.back())) name.remove_suffix(1);
                Numbers in(text.substr(open + 1, close - open - 1));
                float v[6] {};
                int count = 0;
                while (count < 6) { const auto n = in.Number(); if (!n) break; v[count++] = *n; }
                Transform t;
                constexpr float degrees = std::numbers::pi_v<float> / 180.f;
                if (name == "matrix" && count == 6) t = { v[0], v[1], v[2], v[3], v[4], v[5] };
                else if (name == "translate") t = Transform::Translation({ v[0], count > 1 ? v[1] : 0.f });
                else if (name == "scale") t = Transform::Scaling({ v[0], count > 1 ? v[1] : v[0] });
                else if (name == "rotate") {
                    t = Transform::Rotation(v[0] * degrees);
                    if (count == 3) t = Transform::Translation({ v[1], v[2] }) * t * Transform::Translation({ -v[1], -v[2] });
                }
                else if (name == "skewX") t = { 1.f, 0.f, std::tan(v[0] * degrees), 1.f, 0.f, 0.f };
                else if (name == "skewY") t = { 1.f, std::tan(v[0] * degrees), 0.f, 1.f, 0.f, 0.f };
                result = result * t;
                at = close + 1;
            }
            return result;
        }

        /** @brief What a shape takes from the groups it is in. */
        struct Inherited {
            Transform transform;
            float opacity = 1.f;
            bool filled = true;
            FillRule rule = FillRule::eNonZero;
        };

        Inherited Apply(Inherited state, const Attributes& tag)
        {
            if (const auto t = tag.Get("transform")) state.transform = state.transform * ParseTransform(*t);
            state.opacity *= std::clamp(tag.Number("opacity", 1.f), 0.f, 1.f);
            state.opacity *= std::clamp(tag.Number("fill-opacity", 1.f), 0.f, 1.f);
            if (const auto fill = tag.Get("fill")) state.filled = *fill != "none" && *fill != "transparent";
            if (const auto rule = tag.Get("fill-rule")) state.rule = *rule == "evenodd" ? FillRule::eEvenOdd : FillRule::eNonZero;
            // An inline style says the same as the attributes do, for the few that matter here.
            if (const auto style = tag.Get("style")) {
                std::size_t at = 0;
                while (at < style->size()) {
                    std::size_t end = style->find(';', at);
                    if (end == std::string_view::npos) end = style->size();
                    const std::string_view decl = style->substr(at, end - at);
                    const std::size_t colon = decl.find(':');
                    if (colon != std::string_view::npos) {
                        const auto trim = [](std::string_view s) {
                            while (!s.empty() && space(s.front())) s.remove_prefix(1);
                            while (!s.empty() && space(s.back())) s.remove_suffix(1);
                            return s;
                        };
                        const std::string_view key = trim(decl.substr(0, colon)), value = trim(decl.substr(colon + 1));
                        Numbers in(value);
                        if (key == "fill") state.filled = value != "none" && value != "transparent";
                        else if (key == "fill-rule") state.rule = value == "evenodd" ? FillRule::eEvenOdd : FillRule::eNonZero;
                        else if (key == "opacity" || key == "fill-opacity") state.opacity *= std::clamp(in.Number().value_or(1.f), 0.f, 1.f);
                    }
                    at = end + 1;
                }
            }
            return state;
        }

        /** @brief Points as `<polygon points="…">` lists them. */
        std::vector<kor::Vec2> PointList(const std::string_view text)
        {
            std::vector<kor::Vec2> points;
            Numbers in(text);
            while (true) {
                const auto x = in.Number(), y = x ? in.Number() : std::nullopt;
                if (!y) break;
                points.emplace_back(*x, *y);
            }
            return points;
        }
    }

    VectorImage VectorImage::FromSvg(const std::string_view svg)
    {
        VectorImage image;
        std::vector<Inherited> groups { Inherited {} };
        // How deep inside something not drawn — a <defs>, a <clipPath>, a display="none" — the reading is:
        // nothing there is drawn where it stands, however deeply nested.
        int skipping = 0;
        std::size_t at = 0;
        while (const auto tag = NextTag(svg, at)) {
            if (skipping > 0) {
                if (tag->closing) --skipping;
                else if (!tag->selfClosing) ++skipping;
                continue;
            }
            const std::string_view element = tag->element;
            const bool container = element == "g" || element == "svg" || element == "a" || element == "switch";
            if (tag->closing) {
                if (container && groups.size() > 1) groups.pop_back();
                continue;
            }
            const auto style = tag->Get("style");
            const bool hidden = element == "defs" || element == "clipPath" || element == "mask" || element == "symbol"
                             || element == "pattern" || element == "linearGradient" || element == "radialGradient"
                             || element == "marker" || element == "style" || element == "title" || element == "desc"
                             || tag->Get("display") == "none" || (style && style->find("display:none") != std::string_view::npos);
            if (hidden) { if (!tag->selfClosing) skipping = 1; continue; }

            if (element == "svg" && groups.size() == 1) {
                // The outermost one says what box the drawing is on.
                if (const auto box = tag->Get("viewBox")) {
                    Numbers in(*box);
                    const auto x = in.Number(), y = in.Number(), w = in.Number(), h = in.Number();
                    if (x && y && w && h && *w > 0.f && *h > 0.f) image._viewBox = Rect::XYWH(*x, *y, *w, *h);
                } else {
                    const float w = tag->Number("width", 24.f), h = tag->Number("height", 24.f);
                    if (w > 0.f && h > 0.f) image._viewBox = Rect::XYWH(0.f, 0.f, w, h);
                }
            }
            const Inherited state = Apply(groups.back(), *tag);
            if (container) {
                if (!tag->selfClosing) groups.push_back(state);
                continue;
            }
            if (!state.filled || state.opacity <= 0.f) continue;

            Shape shape;
            shape.opacity = state.opacity;
            shape.path.SetFillRule(state.rule);
            const Pen pen { shape.path, state.transform };
            if (element == "path") {
                if (const auto d = tag->Get("d")) PathData(*d, pen);
            } else if (element == "circle") {
                const float r = tag->Number("r");
                if (r > 0.f) pen.Ellipse({ tag->Number("cx"), tag->Number("cy") }, { r, r });
            } else if (element == "ellipse") {
                const kor::Vec2 r { tag->Number("rx"), tag->Number("ry") };
                if (r.x > 0.f && r.y > 0.f) pen.Ellipse({ tag->Number("cx"), tag->Number("cy") }, r);
            } else if (element == "rect") {
                const float x = tag->Number("x"), y = tag->Number("y"), w = tag->Number("width"), h = tag->Number("height");
                if (w > 0.f && h > 0.f) {
                    // A missing radius is the other one; neither is more than half the side.
                    float rx = tag->Number("rx", -1.f), ry = tag->Number("ry", -1.f);
                    if (rx < 0.f) rx = std::max(ry, 0.f);
                    if (ry < 0.f) ry = rx;
                    rx = std::min(rx, w * 0.5f);
                    ry = std::min(ry, h * 0.5f);
                    if (rx <= 0.f || ry <= 0.f) {
                        pen.MoveTo({ x, y }); pen.LineTo({ x + w, y }); pen.LineTo({ x + w, y + h }); pen.LineTo({ x, y + h }); pen.Close();
                    } else {
                        pen.MoveTo({ x + rx, y });
                        pen.LineTo({ x + w - rx, y });
                        pen.Arc({ x + w - rx, y }, { rx, ry }, 0.f, false, true, { x + w, y + ry });
                        pen.LineTo({ x + w, y + h - ry });
                        pen.Arc({ x + w, y + h - ry }, { rx, ry }, 0.f, false, true, { x + w - rx, y + h });
                        pen.LineTo({ x + rx, y + h });
                        pen.Arc({ x + rx, y + h }, { rx, ry }, 0.f, false, true, { x, y + h - ry });
                        pen.LineTo({ x, y + ry });
                        pen.Arc({ x, y + ry }, { rx, ry }, 0.f, false, true, { x + rx, y });
                        pen.Close();
                    }
                }
            } else if (element == "polygon" || element == "polyline") {
                // Filled either way: a polyline's fill closes it as a polygon's does.
                if (const auto points = tag->Get("points")) {
                    const auto list = PointList(*points);
                    if (list.size() >= 2) {
                        pen.MoveTo(list.front());
                        for (std::size_t i = 1; i < list.size(); ++i) pen.LineTo(list[i]);
                        pen.Close();
                    }
                }
            }
            if (!shape.path.Empty()) image._shapes.push_back(std::move(shape));
        }
        return image;
    }

    void VectorImage::Draw(Canvas& canvas, const Rect& rect, const Color tint) const
    {
        if (_shapes.empty() || _viewBox.Width() <= 0.f || _viewBox.Height() <= 0.f || !tint.Visible()) return;
        canvas.Save();
        canvas.Translate({ rect.left, rect.top });
        canvas.Scale({ rect.Width() / _viewBox.Width(), rect.Height() / _viewBox.Height() });
        canvas.Translate({ -_viewBox.left, -_viewBox.top });
        for (const Shape& shape : _shapes)
            canvas.DrawPath(shape.path, Paint::Fill(tint.WithAlpha(tint.a * shape.opacity)));
        canvas.Restore();
    }

    // ---- the Material icons ------------------------------------------------------------------------------

    namespace {
        constexpr std::string_view StyleFolder(const IconStyle style)
        {
            switch (style) {
            case IconStyle::eOutlined: return "outlined";
            case IconStyle::eRounded: return "rounded";
            case IconStyle::eSharp: return "sharp";
            case IconStyle::eTwoTone: return "twotone";
            default: return "filled";
            }
        }

        /** @brief The name Compose gives the icon Material calls @p snake: `arrow_back` is `ArrowBack`, `3d_rotation` `_3dRotation`. */
        std::string ComposeName(const std::string_view snake)
        {
            std::string out;
            bool upper = true;
            for (const char c : snake) {
                if (c == '_') { upper = true; continue; }
                out += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
                upper = false;
            }
            if (!out.empty() && out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
            return out;
        }

        /** @brief Every icon's SVG by style and Material's name, and Material's name by Compose's: made once. */
        struct Index {
            std::unordered_map<std::string, std::string_view> svgs;          // "filled/arrow_back"
            std::unordered_map<std::string, std::string_view> byCompose;     // "ArrowBack" -> "arrow_back"
            std::vector<std::string_view> names;                             // Material's, in order

            Index()
            {
                const auto data = detail::MaterialIconData();
                svgs.reserve(data.size());
                for (const auto& icon : data) {
                    svgs.emplace(std::string(icon.style) + '/' + std::string(icon.name), icon.svg);
                    if (icon.style == "filled") {
                        names.push_back(icon.name);
                        byCompose.emplace(ComposeName(icon.name), icon.name);
                    }
                }
            }
        };

        const Index& Icons()
        {
            static const Index index;
            return index;
        }
    }

    std::span<const std::string_view> MaterialIconNames() { return Icons().names; }

    std::shared_ptr<const VectorImage> MaterialIcon(const std::string_view name, const IconStyle style)
    {
        // Read the first time each is asked for, then kept: a few hundred bytes of path apiece.
        static std::mutex mutex;
        static std::unordered_map<std::string, std::shared_ptr<const VectorImage>> cache;
        const Index& index = Icons();
        const std::string_view folder = StyleFolder(style);
        // Material's name, or else Compose's for it: either way, the one icon.
        std::string_view material = name;
        if (!index.svgs.contains(std::string(folder) + '/' + std::string(name)))
            if (const auto it = index.byCompose.find(std::string(name)); it != index.byCompose.end()) material = it->second;
        std::string key = std::string(folder) + '/' + std::string(material);
        const std::scoped_lock lock(mutex);
        if (const auto it = cache.find(key); it != cache.end()) return it->second;
        std::shared_ptr<const VectorImage> image;
        if (const auto svg = index.svgs.find(key); svg != index.svgs.end()) image = std::make_shared<const VectorImage>(VectorImage::FromSvg(svg->second));
        else spdlog::warn("kui: there is no Material icon named \"{}\"", name);
        cache.emplace(std::move(key), image);
        return image;
    }

    // ---- the widget --------------------------------------------------------------------------------------

    namespace {
        class RenderIcon final : public RenderContainer {
        public:
            void Set(std::shared_ptr<const VectorImage> icon, const Color tint)
            {
                if (icon == _icon && tint == _tint) return;
                _icon = std::move(icon);
                _tint = tint;
                MarkNeedsPaint();
            }

            void Paint(Canvas& canvas, const kor::Vec2 offset) override
            {
                if (!_icon) return;
                // Inheriting, it is the theme's text colour — read now, under whatever theme is over it.
                const Color tint = _tint.a < 0.f ? Theme::Current().text : _tint;
                // Fitted, as an image with ImageFit::eContain is: a box of other proportions does not stretch it.
                const Rect& box = _icon->ViewBox();
                const kor::Vec2 size = Size();
                const float scale = std::min(size.x / box.Width(), size.y / box.Height());
                const kor::Vec2 drawn { box.Width() * scale, box.Height() * scale };
                _icon->Draw(canvas, Rect::XYWH(offset.x + (size.x - drawn.x) * 0.5f, offset.y + (size.y - drawn.y) * 0.5f, drawn.x, drawn.y), tint);
            }

        protected:
            void PerformLayout() override { SetSize(Constraints().Constrain({ 24.f, 24.f })); }

        private:
            std::shared_ptr<const VectorImage> _icon;
            Color _tint = colors::Inherit;
        };

        struct IconWidget final : RenderObjectWidget {
            std::shared_ptr<const VectorImage> icon;
            Color tint;
            IconWidget(std::shared_ptr<const VectorImage> i, const Color t) : icon(std::move(i)), tint(t) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderIcon>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderIcon&>(object).Set(icon, tint); }
        };
    }

    Widget Icon(std::shared_ptr<const VectorImage> icon, const Color tint)
    {
        return Widget(std::make_shared<IconWidget>(std::move(icon), tint));
    }

    Widget Icon(const std::string_view name, const IconStyle style, const Color tint)
    {
        return Icon(MaterialIcon(name, style), tint);
    }
}
