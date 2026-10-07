//
// koral-ui: fonts, the signed-distance glyph atlas they share, and laying text out into lines.
//

#include <algorithm>
#include <climits>
#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <buffer.h>
#include <commandBuffer.h>
#include <context.h>
#include <log.h>

#include <kui/text.h>

#include "atlas.h"

namespace kui
{
    namespace {
        // Glyphs are rendered once, at this em size, as distances from their outline: 0.5 on it, a
        // unit of the atlas per Spread / 127 pixels either side. Drawn at any size from there.
        constexpr float BakeSize = 48.f;
        constexpr int Padding = 6;
        constexpr unsigned char OnEdge = 128;
        constexpr float DistanceScale = 127.f / static_cast<float>(Padding);   // atlas units per baked pixel
        constexpr int AtlasSize = 2048;

        struct GlyphEntry {
            bool empty = true;            // a space: an advance, nothing to draw
            int x = 0, y = 0, w = 0, h = 0;   // in the atlas
            float xoff = 0.f, yoff = 0.f;     // baked pixels from the pen position to the bitmap's top-left
        };

        std::mutex& textMutex() { static std::mutex m; return m; }

        struct Atlas {
            std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(AtlasSize) * AtlasSize, 0);
            int shelfX = 1, shelfY = 1, shelfHeight = 0;
            int dirtyTop = INT_MAX, dirtyBottom = -1;
            kor::Resource<kor::Image> image;
            bool full = false;

            bool Allocate(const int w, const int h, int& x, int& y)
            {
                if (shelfX + w + 1 > AtlasSize) { shelfX = 1; shelfY += shelfHeight + 1; shelfHeight = 0; }
                if (shelfY + h + 1 > AtlasSize || w + 2 > AtlasSize) {
                    if (!full) kor::log::Error("[kui] the glyph atlas is full; text drawn from here on may be missing glyphs");
                    full = true;
                    return false;
                }
                x = shelfX; y = shelfY;
                shelfX += w + 1;
                shelfHeight = std::max(shelfHeight, h);
                dirtyTop = std::min(dirtyTop, y);
                dirtyBottom = std::max(dirtyBottom, y + h);
                return true;
            }
        };

        Atlas& atlas() { static Atlas a; return a; }

        // UTF-8 to code points, each with the byte it starts at. Malformed bytes become U+FFFD.
        struct Decoded { char32_t codepoint; std::uint32_t byte; };
        std::vector<Decoded> decode(const std::string_view text)
        {
            std::vector<Decoded> out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size();) {
                const auto c = static_cast<unsigned char>(text[i]);
                char32_t cp = 0xFFFD;
                std::size_t len = 1;
                if (c < 0x80) cp = c;
                else if ((c >> 5) == 0x6 && i + 1 < text.size()) { cp = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F); len = 2; }
                else if ((c >> 4) == 0xE && i + 2 < text.size()) { cp = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F); len = 3; }
                else if ((c >> 3) == 0x1E && i + 3 < text.size()) {
                    cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) | ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F);
                    len = 4;
                }
                out.push_back({ cp, static_cast<std::uint32_t>(i) });
                i += len;
            }
            return out;
        }
    }

    struct Font::Impl {
        std::vector<unsigned char> data;
        stbtt_fontinfo info {};
        float bakeScale = 1.f;          // font units -> baked pixels
        float ascent = 0.f, descent = 0.f, lineGap = 0.f;   // font units
        float unitsPerEm = 1.f;
        std::string name;
        std::shared_ptr<Font> fallback;
        std::unordered_map<int, GlyphEntry> glyphs;

        /** @brief The glyph, rendered into the atlas the first time it is asked for. Under textMutex. */
        const GlyphEntry& Glyph(const int index)
        {
            const auto it = glyphs.find(index);
            if (it != glyphs.end()) return it->second;
            GlyphEntry entry;
            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char* sdf = stbtt_GetGlyphSDF(&info, bakeScale, index, Padding, OnEdge, DistanceScale, &w, &h, &xoff, &yoff);
            if (sdf && w > 0 && h > 0) {
                auto& a = atlas();
                int x = 0, y = 0;
                if (a.Allocate(w, h, x, y)) {
                    for (int row = 0; row < h; ++row)
                        std::memcpy(&a.pixels[static_cast<std::size_t>(y + row) * AtlasSize + x], sdf + static_cast<std::size_t>(row) * w, w);
                    entry = { false, x, y, w, h, static_cast<float>(xoff), static_cast<float>(yoff) };
                }
            }
            if (sdf) stbtt_FreeSDF(sdf, nullptr);
            return glyphs.emplace(index, entry).first->second;
        }
    };

    Font::Font(std::unique_ptr<Impl> impl) : _impl(std::move(impl)) {}
    Font::~Font() = default;

    std::shared_ptr<Font> Font::FromMemory(const std::span<const std::byte> bytes, std::string name)
    {
        auto impl = std::make_unique<Impl>();
        impl->data.resize(bytes.size());
        std::memcpy(impl->data.data(), bytes.data(), bytes.size());
        impl->name = std::move(name);
        const int offset = stbtt_GetFontOffsetForIndex(impl->data.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&impl->info, impl->data.data(), offset)) {
            kor::log::Error("[kui] {} is not a font this can read", impl->name);
            return nullptr;
        }
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &gap);
        impl->ascent = static_cast<float>(ascent);
        impl->descent = static_cast<float>(-descent);
        impl->lineGap = static_cast<float>(gap);
        impl->bakeScale = stbtt_ScaleForMappingEmToPixels(&impl->info, BakeSize);
        impl->unitsPerEm = BakeSize / impl->bakeScale;
        return std::shared_ptr<Font>(new Font(std::move(impl)));
    }

    std::shared_ptr<Font> Font::Load(const std::filesystem::path& path)
    {
        const auto resolved = kor::AssetPath(path);
        std::ifstream file(resolved, std::ios::binary);
        if (!file) {
            kor::log::Error("[kui] the font {} could not be opened", resolved.string());
            return nullptr;
        }
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return FromMemory(std::as_bytes(std::span(bytes)), path.filename().string());
    }

    std::shared_ptr<Font> Font::Default()
    {
        static std::shared_ptr<Font> font = Load("fonts/Inter_28pt-Regular.ttf");
        return font;
    }

    void Font::SetFallback(std::shared_ptr<Font> fallback) { _impl->fallback = std::move(fallback); }
    float Font::Ascent(const float size) const { return _impl->ascent / _impl->unitsPerEm * size; }
    float Font::Descent(const float size) const { return _impl->descent / _impl->unitsPerEm * size; }
    const std::string& Font::Name() const { return _impl->name; }
    bool Font::HasGlyph(const char32_t codepoint) const { return stbtt_FindGlyphIndex(&_impl->info, static_cast<int>(codepoint)) != 0; }

    // ---- the atlas on the GPU -------------------------------------------------------------------------

    kor::ResourceRef<const kor::Image> detail::AtlasImage()
    {
        std::scoped_lock lock(textMutex());
        auto& a = atlas();
        if (!a.image.Valid()) {
            a.image = kor::Image::Builder{}
                .SetFormat(kor::Image::Format::eR8_UNORM)
                .SetExtent(kor::UVec2(AtlasSize, AtlasSize))
                .SetUsage(kor::Image::Usage::eSampled | kor::Image::Usage::eTransferDst)
                .SetData(a.pixels.data(), a.pixels.size())
                .Build();
            a.image.SetName("kui glyph atlas");
            a.dirtyTop = INT_MAX;
            a.dirtyBottom = -1;
        } else if (a.dirtyBottom > a.dirtyTop) {
            // The rows new glyphs went into, ahead of the frame that draws them.
            const int rows = a.dirtyBottom - a.dirtyTop;
            const std::size_t offset = static_cast<std::size_t>(a.dirtyTop) * AtlasSize;
            const std::size_t size = static_cast<std::size_t>(rows) * AtlasSize;
            auto staging = kor::Buffer::RawBuilder{}
                .SetRawSize(static_cast<kor::i64>(size))
                .SetUsage(kor::Buffer::Usage::eTransferSrc)
                .SetType(kor::Buffer::Type::eStaging)
                .Build();
            staging->Write(std::span<const std::uint8_t>(a.pixels.data() + offset, size), 0);
            const int top = a.dirtyTop;
            const kor::ResourceRef<const kor::Image> image(a.image);
            (void)kor::CommandBuffer::Upload([&](kor::CommandBuffer& cb) {
                cb.CopyBufferToImage(kor::ResourceRef<const kor::Buffer>(staging), image,
                                     kor::Copy { .imageOffset = { 0, top, 0 }, .imageExtent = { AtlasSize, rows, 1 } });
            });
            a.dirtyTop = INT_MAX;
            a.dirtyBottom = -1;
        }
        return kor::ResourceRef<const kor::Image>(a.image);
    }

    void detail::ReleaseAtlas()
    {
        std::scoped_lock lock(textMutex());
        auto& a = atlas();
        a.image = {};
        a.dirtyTop = INT_MAX;
        a.dirtyBottom = -1;
    }

    // ---- paragraphs -----------------------------------------------------------------------------------

    Paragraph::Paragraph(std::string text, TextStyle style, const float maxWidth, const TextAlign align)
        : _text(std::move(text)), _style(std::move(style)), _align(align)
    {
        if (!_style.font) _style.font = Font::Default();
        Layout(maxWidth);
    }

    float Paragraph::LineHeight() const { return _style.size * _style.lineHeight; }

    void Paragraph::Truncate(const std::size_t maxLines, const bool ellipsis)
    {
        if (maxLines == 0 || _lines.size() <= maxLines) return;
        const std::uint32_t end = _lines[maxLines - 1].endByte;
        if (!ellipsis) {
            _lines.resize(maxLines);
            std::erase_if(_glyphs, [end](const Glyph& g) { return g.byte >= end; });
            float widest = 0.f;
            for (const auto& line : _lines) widest = std::max(widest, line.width);
            _size = { widest, static_cast<float>(maxLines) * LineHeight() };
            return;
        }
        // As much of the text as leaves room for the mark that says there was more.
        const char* const mark = _style.font && _style.font->HasGlyph(0x2026) ? "\xE2\x80\xA6" : "...";
        std::string kept = _text.substr(0, std::min<std::size_t>(end, _text.size()));
        for (;;) {
            while (!kept.empty() && (kept.back() == ' ' || kept.back() == '\n' || kept.back() == '\t')) kept.pop_back();
            Paragraph candidate(kept + mark, _style, _maxWidth, _align);
            if (candidate.LineCount() <= maxLines || kept.empty()) { *this = std::move(candidate); return; }
            // A character less: all of its bytes.
            while (!kept.empty() && (static_cast<unsigned char>(kept.back()) & 0xC0u) == 0x80u) kept.pop_back();
            if (!kept.empty()) kept.pop_back();
        }
    }

    float Paragraph::FirstBaseline() const { return _lines.empty() ? 0.f : _lines.front().baseline; }

    void Paragraph::Layout(const float maxWidth)
    {
        if (_laidOut && maxWidth == _maxWidth) return;
        // Wider than the text already is, and not wrapping or aligning to it: nothing would move.
        if (_laidOut && _align == TextAlign::eStart && maxWidth >= _maxIntrinsic && _maxWidth >= _maxIntrinsic) { _maxWidth = maxWidth; return; }
        _maxWidth = maxWidth;
        _laidOut = true;
        _glyphs.clear();
        _lines.clear();
        _size = {};
        if (!_style.font) return;

        std::scoped_lock lock(textMutex());
        const float size = _style.size;
        const float lineHeight = size * _style.lineHeight;
        Font::Impl& primary = _style.font->Internals();
        const float ascent = primary.ascent / primary.unitsPerEm * size;
        const float descent = primary.descent / primary.unitsPerEm * size;

        // Each character: which font draws it, how far it advances.
        struct Shaped {
            char32_t cp;
            std::uint32_t byte;
            Font::Impl* font;
            int glyph;
            float advance;
        };
        const auto decoded = decode(_text);
        std::vector<Shaped> shaped;
        shaped.reserve(decoded.size());
        for (std::size_t i = 0; i < decoded.size(); ++i) {
            const char32_t cp = decoded[i].codepoint;
            Font::Impl* font = &primary;
            int glyph = stbtt_FindGlyphIndex(&font->info, static_cast<int>(cp));
            for (auto* f = primary.fallback.get(); glyph == 0 && f; f = f->Internals().fallback.get()) {
                const int g = stbtt_FindGlyphIndex(&f->Internals().info, static_cast<int>(cp));
                if (g != 0) { font = &f->Internals(); glyph = g; }
            }
            int advance = 0, bearing = 0;
            stbtt_GetGlyphHMetrics(&font->info, glyph, &advance, &bearing);
            float adv = static_cast<float>(advance) / font->unitsPerEm * size + _style.letterSpacing;
            if (i + 1 < decoded.size()) {
                const int next = stbtt_FindGlyphIndex(&font->info, static_cast<int>(decoded[i + 1].codepoint));
                if (next != 0) adv += static_cast<float>(stbtt_GetGlyphKernAdvance(&font->info, glyph, next)) / font->unitsPerEm * size;
            }
            if (cp == '\n' || cp == '\r') adv = 0.f;
            shaped.push_back({ cp, decoded[i].byte, font, glyph, adv });
        }

        const auto isSpace = [](const char32_t c) { return c == ' ' || c == '\t'; };

        // Intrinsic widths: unwrapped lines, and the widest word.
        {
            float line = 0.f, word = 0.f;
            _maxIntrinsic = _minIntrinsic = 0.f;
            for (const auto& s : shaped) {
                if (s.cp == '\n') { _maxIntrinsic = std::max(_maxIntrinsic, line); line = 0.f; word = 0.f; continue; }
                line += s.advance;
                if (isSpace(s.cp)) word = 0.f;
                else { word += s.advance; _minIntrinsic = std::max(_minIntrinsic, word); }
            }
            _maxIntrinsic = std::max(_maxIntrinsic, line);
        }

        // Greedy line breaking, at spaces, or inside a word that cannot fit on a line of its own.
        struct Range { std::size_t begin, end; float width; };
        std::vector<Range> ranges;
        std::size_t lineStart = 0;
        float width = 0.f;
        std::size_t lastBreak = SIZE_MAX;   // index just after the last space
        float widthAtBreak = 0.f;
        for (std::size_t i = 0; i < shaped.size(); ++i) {
            const auto& s = shaped[i];
            if (s.cp == '\n') {
                ranges.push_back({ lineStart, i, width });
                lineStart = i + 1; width = 0.f; lastBreak = SIZE_MAX;
                continue;
            }
            if (!isSpace(s.cp) && width + s.advance > maxWidth && i > lineStart) {
                if (lastBreak != SIZE_MAX && lastBreak > lineStart) {
                    ranges.push_back({ lineStart, lastBreak, widthAtBreak });
                    lineStart = lastBreak;
                    width = 0.f;
                    for (std::size_t j = lineStart; j < i; ++j) width += shaped[j].advance;
                } else {
                    ranges.push_back({ lineStart, i, width });
                    lineStart = i; width = 0.f;
                }
                lastBreak = SIZE_MAX;
            }
            width += s.advance;
            if (isSpace(s.cp)) {
                lastBreak = i + 1;
                // A line's width leaves out the spaces it ends in.
                widthAtBreak = width - s.advance;
                for (std::size_t j = i; j > lineStart && isSpace(shaped[j - 1].cp); --j) widthAtBreak -= shaped[j - 1].advance;
            }
        }
        ranges.push_back({ lineStart, shaped.size(), width });

        float widest = 0.f;
        for (const auto& r : ranges) {
            float w = r.width;
            for (std::size_t j = r.end; j > r.begin && isSpace(shaped[j - 1].cp); --j) w -= shaped[j - 1].advance;
            widest = std::max(widest, w);
        }
        const float alignWidth = std::isfinite(maxWidth) ? maxWidth : widest;

        // Leading split above and below, so the text sits in the middle of its line.
        const float leading = lineHeight - (ascent + descent);
        float top = 0.f;
        for (const auto& r : ranges) {
            Line line;
            line.top = top;
            line.baseline = top + leading * 0.5f + ascent;
            line.firstByte = r.begin < shaped.size() ? shaped[r.begin].byte : static_cast<std::uint32_t>(_text.size());
            line.endByte = r.end < shaped.size() ? shaped[r.end].byte : static_cast<std::uint32_t>(_text.size());
            float w = r.width;
            for (std::size_t j = r.end; j > r.begin && isSpace(shaped[j - 1].cp); --j) w -= shaped[j - 1].advance;
            line.width = w;
            line.x = _align == TextAlign::eStart ? 0.f : _align == TextAlign::eCenter ? (alignWidth - w) * 0.5f : alignWidth - w;

            float pen = line.x;
            for (std::size_t j = r.begin; j < r.end; ++j) {
                const auto& s = shaped[j];
                line.carets.push_back(pen);
                line.caretBytes.push_back(s.byte);
                if (s.cp != '\n' && !isSpace(s.cp) && s.glyph != 0) {
                    const auto& g = s.font->Glyph(s.glyph);
                    if (!g.empty) {
                        const float f = size / BakeSize;
                        Glyph glyph;
                        glyph.rect = Rect::XYWH(pen + g.xoff * f, line.baseline + g.yoff * f, static_cast<float>(g.w) * f, static_cast<float>(g.h) * f);
                        constexpr float inv = 1.f / static_cast<float>(AtlasSize);
                        glyph.uv = Rect::XYWH(static_cast<float>(g.x) * inv, static_cast<float>(g.y) * inv,
                                              static_cast<float>(g.w) * inv, static_cast<float>(g.h) * inv);
                        glyph.distanceScale = 255.f / DistanceScale * f;
                        glyph.byte = s.byte;
                        _glyphs.push_back(glyph);
                    }
                }
                pen += s.advance;
            }
            line.carets.push_back(pen);
            line.caretBytes.push_back(line.endByte);
            _lines.push_back(std::move(line));
            top += lineHeight;
        }
        _size = { widest, top };
    }

    kor::Vec2 Paragraph::CaretPosition(const std::size_t index) const
    {
        if (_lines.empty()) return {};
        for (std::size_t l = 0; l < _lines.size(); ++l) {
            const auto& line = _lines[l];
            const bool last = l + 1 == _lines.size();
            if (index < line.endByte || last || (index == line.endByte && index < _lines[l + 1].firstByte)) {
                for (std::size_t c = 0; c < line.caretBytes.size(); ++c)
                    if (line.caretBytes[c] >= index) return { line.carets[c], line.top };
                return { line.carets.back(), line.top };
            }
        }
        return { _lines.back().carets.back(), _lines.back().top };
    }

    std::size_t Paragraph::IndexAt(const kor::Vec2 point) const
    {
        if (_lines.empty()) return 0;
        const float lh = LineHeight();
        const auto l = static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(point.y / lh)), 0, static_cast<int>(_lines.size()) - 1));
        const auto& line = _lines[l];
        std::size_t best = 0;
        float bestDistance = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < line.carets.size(); ++c) {
            const float d = std::abs(line.carets[c] - point.x);
            if (d < bestDistance) { bestDistance = d; best = c; }
        }
        return line.caretBytes[best];
    }
}
