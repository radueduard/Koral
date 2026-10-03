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
    protected:
        void PerformLayout() override;
    private:
        BoxConstraints _extra {};
    };

    class RenderDecorated final : public RenderContainer {
    public:
        void Set(const Decoration& decoration);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return _decoration.Visible(); }
    private:
        Decoration _decoration {};
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
        void Set(const std::string& text, const TextStyle& style, TextAlign align, bool wrap);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        [[nodiscard]] const Paragraph& GetParagraph() const { return _paragraph; }
    protected:
        void PerformLayout() override;
    private:
        Paragraph _paragraph;
        TextStyle _style;
        TextAlign _align = TextAlign::eStart;
        bool _wrap = true, _built = false;
    };

    class RenderImage final : public RenderContainer {
    public:
        void Set(const kor::ResourceRef<const kor::Image>& image, ImageFit fit, glm::vec2 size);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
    protected:
        void PerformLayout() override;
    private:
        kor::ResourceRef<const kor::Image> _image;
        ImageFit _fit = ImageFit::eContain;
        glm::vec2 _preferred { -1.f, -1.f };
    };

    class RenderCustomPaint final : public RenderContainer {
    public:
        void Set(std::function<void(Canvas&, glm::vec2)> painter, glm::vec2 size);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
    protected:
        void PerformLayout() override;
    private:
        std::function<void(Canvas&, glm::vec2)> _painter;
        glm::vec2 _preferred { -1.f, -1.f };
    };

    class RenderShaderBox final : public RenderContainer {
    public:
        void Set(std::shared_ptr<ElementShader> shader, std::vector<std::byte> parameters, Radii radius);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
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

    class RenderClip final : public RenderContainer {
    public:
        void Set(const Radii& radius);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
    private:
        Radii _radius {};
    };

    class RenderTranslate final : public RenderContainer {
    public:
        void Set(glm::vec2 by);
        [[nodiscard]] glm::vec2 ChildOrigin(const RenderObject& child) const override { return child.Offset() + _by; }
        /** Hit where its child is drawn, which may be outside its own box. */
        bool HitTest(HitTestResult& result, glm::vec2 position) override;
    private:
        glm::vec2 _by {};
    };

    class RenderScroll final : public RenderContainer {
    public:
        void Set(Axis axis);
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        bool HandleEvent(const PointerEvent& event) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
        [[nodiscard]] glm::vec2 ChildOrigin(const RenderObject& child) const override;
        /** @brief Scrolls by @p by units; returns whether it moved. */
        bool Scroll(float by);
        [[nodiscard]] float Position() const { return _scroll; }
        [[nodiscard]] float MaxScroll() const;
    protected:
        void PerformLayout() override;
    private:
        [[nodiscard]] glm::vec2 ScrollVector() const;
        void PlaceThumb();
        Axis _axis = Axis::eVertical;
        float _scroll = 0.f;
        std::shared_ptr<Layer> _thumb;
    };

    class RenderIgnorePointer final : public RenderContainer {
    public:
        bool HitTest(HitTestResult&, glm::vec2) override { return false; }
    };

    class RenderDraggable final : public RenderContainer {
    public:
        struct Config { DragData data; DraggableOptions options; };
        void Set(const Config& config) { _config = config; }
        bool HandleEvent(const PointerEvent& event) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
    private:
        Config _config;
        bool _pressed = false;
        glm::vec2 _down {};
    };

    class RenderDropTarget final : public RenderContainer, public DropReceiver {
    public:
        void Set(const DropTargetOptions& options) { _options = options; }
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return true; }
        [[nodiscard]] bool AcceptsDrag(const DragData& data) const override { return !_options.accepts || _options.accepts(data); }
        void DragEntered(const DragData& data) override { if (_options.onEnter) _options.onEnter(data); }
        void DragMoved(const DragData& data, const glm::vec2 local) override { if (_options.onMove) _options.onMove(data, local); }
        void DragLeft() override { if (_options.onLeave) _options.onLeave(); }
        void Dropped(const DragData& data, const glm::vec2 local) override { if (_options.onDrop) _options.onDrop(data, local); }
    private:
        DropTargetOptions _options;
    };

    class RenderPointerListener final : public RenderContainer {
    public:
        void Set(const GestureOptions& options) { _options = options; }
        bool HandleEvent(const PointerEvent& event) override;
        [[nodiscard]] bool HitTestSelf(glm::vec2) const override { return _options.opaque; }
    private:
        GestureOptions _options;
        bool _pressed = false, _panning = false;
        glm::vec2 _down {};
    };
}
