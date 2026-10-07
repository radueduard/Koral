//
// koral-ui: the built-in render objects.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <kui/widgets.h>

namespace kui
{
    void PaintDecoration(Canvas& canvas, const Decoration& decoration, const Rect& box);

    class RenderPadding final : public RenderContainer {
    public:
        void Set(const EdgeInsets& padding);
        [[nodiscard]] float MinIntrinsicWidth() const override;
    protected:
        void PerformLayout() override;
    private:
        EdgeInsets _padding {};
    };

    class RenderAlign final : public RenderContainer {
    public:
        void Set(Alignment alignment);
    protected:
        void PerformLayout() override;
    private:
        Alignment _alignment {};
    };

    class RenderConstrained final : public RenderContainer {
    public:
        void Set(const BoxConstraints& extra);
        [[nodiscard]] float MinIntrinsicWidth() const override;
    protected:
        void PerformLayout() override;
    private:
        BoxConstraints _extra {};
    };

    class RenderDecorated final : public RenderContainer {
    public:
        void Set(const Decoration& decoration);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return _decoration.Visible(); }
    private:
        Decoration _decoration {};
    };

    /**
     * @brief A Container that is more than one thing at once — a size and a colour, a colour and padding —
     *        as one box: what a margin round a sized box round a decorated one round padding round an
     *        alignment would lay out, paint and hit, without being five.
     */
    class RenderBox final : public RenderContainer {
    public:
        struct Config {
            EdgeInsets margin {}, padding {};
            float width = -1.f, height = -1.f;
            Decoration decoration {};
            std::optional<Alignment> alignment;
            bool hasMargin = false, hasPadding = false, hasSize = false, hasDecoration = false;
        };
        void Set(const Config& config);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2 position) const override;
        [[nodiscard]] float MinIntrinsicWidth() const override;
    protected:
        void PerformLayout() override;
    private:
        Config _config {};
        Rect _decorated {};     ///< Where the decoration is, in the box: inside the margin.
    };

    /** @brief A child of a flex with a share of its leftover space. */
    class RenderFlexible final : public RenderContainer {
    public:
        void Set(float flex, bool tight);
        [[nodiscard]] float Flex() const { return _flex; }
        [[nodiscard]] bool Tight() const { return _tight; }
    private:
        float _flex = 1.f;
        bool _tight = true;
    };

    class RenderFlex final : public RenderContainer {
    public:
        void Set(Axis axis, const FlexOptions& options);
        /** @brief A row's children side by side, and the gaps between them; a column's widest. */
        [[nodiscard]] float MinIntrinsicWidth() const override;
    protected:
        void PerformLayout() override;
    private:
        Axis _axis = Axis::eVertical;
        FlexOptions _options {};
    };

    class RenderPositioned final : public RenderContainer {
    public:
        void Set(const PositionedOptions& options);
        [[nodiscard]] const PositionedOptions& Options() const { return _options; }
    private:
        PositionedOptions _options {};
    };

    /** @brief A Stack's child with an alignment of its own. */
    class RenderStackAligned final : public RenderContainer {
    public:
        void Set(Alignment alignment) { if (!(alignment == _alignment)) { _alignment = alignment; if (Parent()) Parent()->MarkNeedsLayout(); } }
        [[nodiscard]] Alignment GetAlignment() const { return _alignment; }
    private:
        Alignment _alignment {};
    };

    class RenderStack final : public RenderContainer {
    public:
        void Set(Alignment alignment);
    protected:
        void PerformLayout() override;
    private:
        Alignment _alignment = Alignment::TopLeft();
    };

    class RenderParagraph final : public RenderContainer {
    public:
        void Set(const std::string& text, const TextStyle& style, TextAlign align, bool wrap, int maxLines = 0, bool ellipsis = false);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        [[nodiscard]] const Paragraph& GetParagraph() const { return _paragraph; }
        [[nodiscard]] std::string DebugText() const override { return _text; }
        /** @brief Its longest word, where it wraps; all of it on one line, where it does not; nothing, where it ends in an ellipsis. */
        [[nodiscard]] float MinIntrinsicWidth() const override;
    protected:
        void PerformLayout() override;
    private:
        Paragraph _paragraph;
        TextStyle _style;
        TextAlign _align = TextAlign::eStart;
        bool _wrap = true, _built = false, _ellipsis = false;
        int _maxLines = 0;
        std::string _text;      ///< All of it: the paragraph holds what is left once it is cut to its lines.
    };

    class RenderImage final : public RenderContainer {
    public:
        void Set(const kor::ResourceRef<const kor::Image>& image, ImageFit fit, kor::Vec2 size);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return true; }
    protected:
        void PerformLayout() override;
    private:
        kor::ResourceRef<const kor::Image> _image;
        kor::u64 _generation = 0;
        ImageFit _fit = ImageFit::eContain;
        kor::Vec2 _preferred { -1.f, -1.f };
    };

    class RenderCustomPaint final : public RenderContainer {
    public:
        void Set(std::function<void(Canvas&, kor::Vec2)> painter, kor::Vec2 size);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return true; }
    protected:
        void PerformLayout() override;
    private:
        std::function<void(Canvas&, kor::Vec2)> _painter;
        kor::Vec2 _preferred { -1.f, -1.f };
    };

    class RenderShaderBox final : public RenderContainer {
    public:
        void Set(std::shared_ptr<ElementShader> shader, std::vector<std::byte> parameters, Radii radius);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return true; }
    protected:
        void PerformLayout() override;
    private:
        std::shared_ptr<ElementShader> _shader;
        std::vector<std::byte> _parameters;
        Radii _radius {};
    };

    class RenderRepaintBoundary final : public RenderContainer {
    public:
        [[nodiscard]] bool IsRepaintBoundary() const override { return true; }
    };

    /** @brief A repaint boundary whose layer is faded: changing it moves no bytes but the layer's. */
    class RenderOpacity final : public RenderContainer {
    public:
        void Set(float opacity);
        [[nodiscard]] bool IsRepaintBoundary() const override { return true; }
    };

    /** Its child, shown down to a share of its height and cut off there: what unfolds, and folds away. */
    class RenderReveal final : public RenderContainer {
    public:
        void Set(float share);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
    protected:
        void PerformLayout() override;
    private:
        float _share = 1.f;
    };

    namespace detail {
        /** @p child, as much of its height as @p share says (0 to 1), from its top. */
        Widget RevealBox(float share, Widget child);
    }

    class RenderClip final : public RenderContainer {
    public:
        void Set(const Radii& radius);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
    private:
        Radii _radius {};
    };

    class RenderTranslate final : public RenderContainer {
    public:
        void Set(kor::Vec2 by);
        [[nodiscard]] kor::Vec2 ChildOrigin(const RenderObject& child) const override { return child.Offset() + _by; }
        /** Hit where its child is drawn, which may be outside its own box. */
        bool HitTest(HitTestResult& result, kor::Vec2 position) override;
    private:
        kor::Vec2 _by {};
    };

    class RenderScroll final : public RenderContainer {
    public:
        void Set(Axis axis);
        void Paint(Canvas& canvas, kor::Vec2 offset) override;
        bool HandleEvent(const PointerEvent& event) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return true; }
        [[nodiscard]] kor::Vec2 ChildOrigin(const RenderObject& child) const override;
        /** @brief Scrolls by @p by units; returns whether it moved. */
        bool Scroll(float by);
        /** @brief Scrolled across, any width: what does not fit is scrolled to. Up and down, its content's. */
        [[nodiscard]] float MinIntrinsicWidth() const override;
        [[nodiscard]] float Position() const { return _scroll; }
        [[nodiscard]] float MaxScroll() const;
    protected:
        void PerformLayout() override;
    private:
        [[nodiscard]] kor::Vec2 ScrollVector() const;
        void PlaceThumb();
    public:
        void Observe(std::function<void(float, float)> onScrolled, float jumpTo, std::uint32_t jump);
    private:
        std::function<void(float, float)> _onScrolled;
        float _toldAt = -1.f, _toldMost = -1.f, _jumpTo = 0.f;
        std::uint32_t _jump = 0;
        bool _jumping = false;
        /** Tells the content what part of it shows, when there is much more of it than that. */
        void Cull();
        Axis _axis = Axis::eVertical;
        float _scroll = 0.f;
        std::shared_ptr<Layer> _thumb;
    };

    class RenderIgnorePointer final : public RenderContainer {
    public:
        bool HitTest(HitTestResult&, kor::Vec2) override { return false; }
    };

    class RenderDraggable final : public RenderContainer {
    public:
        struct Config { DragData data; DraggableOptions options; Widget child; };   // the child: what follows the pointer, unless told
        void Set(const Config& config) { _config = config; }
        bool HandleEvent(const PointerEvent& event) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return true; }
    private:
        Config _config;
        bool _pressed = false;
        kor::Vec2 _down {};
    };

    class RenderDropTarget final : public RenderContainer, public DropReceiver {
    public:
        void Set(const DropTargetOptions& options) { _options = options; }
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return true; }
        [[nodiscard]] bool AcceptsDrag(const DragData& data) const override { return !_options.accepts || _options.accepts(data); }
        void DragEntered(const DragData& data) override { if (_options.onEnter) _options.onEnter(data); }
        void DragMoved(const DragData& data, const kor::Vec2 local) override { if (_options.onMove) _options.onMove(data, local); }
        void DragLeft() override { if (_options.onLeave) _options.onLeave(); }
        void Dropped(const DragData& data, const kor::Vec2 local) override { if (_options.onDrop) _options.onDrop(data, local); }
    private:
        DropTargetOptions _options;
    };

    class RenderPointerListener final : public RenderContainer {
    public:
        void Set(const GestureOptions& options) { _options = options; }
        bool HandleEvent(const PointerEvent& event) override;
        [[nodiscard]] bool HitTestSelf(kor::Vec2) const override { return _options.opaque; }
    private:
        GestureOptions _options;
        bool _pressed = false, _panning = false;
        kor::Vec2 _down {};
    };
}
