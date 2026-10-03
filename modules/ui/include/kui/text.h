//
// koral-ui: fonts, and text laid out into lines.
//

#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "api.h"
#include "canvas.h"

namespace kui
{
    /**
     * @brief A TrueType or OpenType font, drawn at any size from one set of signed-distance glyphs.
     *
     * Glyphs are rendered once, the first time any text uses them, into an atlas every font shares —
     * so a font costs nothing until it is drawn, and text at every size costs the same.
     *
     * @code
     * auto title = kui::Font::Load("fonts/Inter_28pt-Bold.ttf");   // resolved like any asset
     * kui::TextStyle style { .font = title, .size = 32.f, .color = kui::colors::White };
     * @endcode
     */
    class KUI_API Font {
    public:
        /** @brief The font in @p path (resolved against the asset roots), or null when it cannot be read. */
        static std::shared_ptr<Font> Load(const std::filesystem::path& path);
        /** @brief The font in @p bytes, which it copies. */
        static std::shared_ptr<Font> FromMemory(std::span<const std::byte> bytes, std::string name = "memory");
        /** @brief Inter Regular, as Koral ships it: what text without a font is drawn in. */
        static std::shared_ptr<Font> Default();

        /** @brief Where glyphs this font lacks are taken from instead — icons, other scripts. */
        void SetFallback(std::shared_ptr<Font> fallback);

        /** @brief Distance from the top of a line to its baseline, at @p size. */
        [[nodiscard]] float Ascent(float size) const;
        /** @brief Distance from the baseline to the bottom of a line (positive), at @p size. */
        [[nodiscard]] float Descent(float size) const;
        [[nodiscard]] const std::string& Name() const;
        [[nodiscard]] bool HasGlyph(char32_t codepoint) const;

        ~Font();
        struct Impl;
        [[nodiscard]] Impl& Internals() const { return *_impl; }

    private:
        explicit Font(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> _impl;
    };

    /** @brief How text looks. */
    struct TextStyle {
        std::shared_ptr<Font> font;             ///< Font::Default() when empty.
        float size = 14.f;                      ///< The em size, in logical units.
        Color color = colors::Black;
        float lineHeight = 1.25f;               ///< Line spacing, as a multiple of the size.
        float letterSpacing = 0.f;              ///< Added after every glyph, in logical units.

        // Chainable: `kui::TextStyle{}.Set...(...).Set...(...)`.
        TextStyle& SetFont(std::shared_ptr<Font> value) { font = std::move(value); return *this; }
        TextStyle& SetSize(float value) { size = std::move(value); return *this; }
        TextStyle& SetColor(Color value) { color = std::move(value); return *this; }
        TextStyle& SetLineHeight(float value) { lineHeight = std::move(value); return *this; }
        TextStyle& SetLetterSpacing(float value) { letterSpacing = std::move(value); return *this; }
    };

    enum class TextAlign : std::uint8_t { eStart, eCenter, eEnd };

    /**
     * @brief Text laid out into lines: measured once, drawn any number of times.
     *
     * Wraps at word boundaries to a width (or not at all), honours explicit line breaks, and answers
     * the questions a text field asks — where the caret goes for a given character, which character a
     * click landed on.
     */
    class KUI_API Paragraph {
    public:
        Paragraph() = default;
        Paragraph(std::string text, TextStyle style, float maxWidth = std::numeric_limits<float>::infinity(),
                  TextAlign align = TextAlign::eStart);

        /** @brief Lays the text out again for @p maxWidth; nothing is done when it would not change. */
        void Layout(float maxWidth);

        [[nodiscard]] const std::string& Text() const { return _text; }
        [[nodiscard]] const TextStyle& Style() const { return _style; }
        /** @brief Width of the widest line and height of all of them. */
        [[nodiscard]] glm::vec2 Size() const { return _size; }
        /** @brief The width it was laid out to fill (infinite when not wrapping). */
        [[nodiscard]] float MaxWidth() const { return _maxWidth; }
        /** @brief Width of the text on one line with no wrapping: the most it could ever want. */
        [[nodiscard]] float MaxIntrinsicWidth() const { return _maxIntrinsic; }
        /** @brief Width of its widest word: the least it can be squeezed to. */
        [[nodiscard]] float MinIntrinsicWidth() const { return _minIntrinsic; }
        [[nodiscard]] std::size_t LineCount() const { return _lines.size(); }
        /** @brief Distance from the top to the first line's baseline. */
        [[nodiscard]] float FirstBaseline() const;

        /** @brief Top of the caret before the character at byte @p index (the end when past it), and the line's height. */
        [[nodiscard]] glm::vec2 CaretPosition(std::size_t index) const;
        [[nodiscard]] float LineHeight() const;
        /** @brief The byte index of the caret position nearest @p point. */
        [[nodiscard]] std::size_t IndexAt(glm::vec2 point) const;

        struct Glyph {
            Rect rect;              ///< Where its quad goes, relative to the paragraph's top-left.
            Rect uv;                ///< Where it is in the atlas.
            float distanceScale;    ///< Atlas distance units to logical units.
            std::uint32_t byte;     ///< Index of its character in the text.
        };
        struct Line {
            float top = 0.f, baseline = 0.f, width = 0.f, x = 0.f;
            std::uint32_t firstByte = 0, endByte = 0;
            std::vector<float> carets;   ///< x of the caret before each character, and one past the last
            std::vector<std::uint32_t> caretBytes;
        };
        [[nodiscard]] const std::vector<Glyph>& Glyphs() const { return _glyphs; }
        [[nodiscard]] const std::vector<Line>& Lines() const { return _lines; }

    private:
        std::string _text;
        TextStyle _style;
        TextAlign _align = TextAlign::eStart;
        float _maxWidth = std::numeric_limits<float>::infinity();
        glm::vec2 _size {};
        float _maxIntrinsic = 0.f, _minIntrinsic = 0.f;
        std::vector<Glyph> _glyphs;
        std::vector<Line> _lines;
        bool _laidOut = false;
    };
}
