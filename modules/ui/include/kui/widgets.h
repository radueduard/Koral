//
// koral-ui, the widget layer: declarative building blocks, rebuilt only where their state changed.
//

#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <image.h>
#include <input.h>
#include <resource.h>

#include "api.h"
#include "canvas.h"
#include "render.h"
#include "rendering.h"
#include "text.h"

namespace kui
{
    class Element;
    class WidgetBase;
    class Ui;
    struct Alignment;
    struct Decoration;
    struct GestureOptions;
    struct PositionedOptions;
    struct DraggableOptions;
    struct DropTargetOptions;
    enum class Axis : std::uint8_t { eHorizontal, eVertical };

    /**
     * @brief A description of part of the interface: cheap to make, immutable once given away, and
     *        compared against the one before it to decide what to rebuild.
     *
     * A handle; widgets are made by the functions below (`kui::Column`, `kui::Text`, ...) or with
     * `kui::Make<MyWidget>(...)` for a widget of the project's own.
     */
    class KUI_API Widget {
    public:
        Widget() = default;
        template <typename T> requires std::derived_from<T, WidgetBase>
        Widget(std::shared_ptr<T> widget) : _widget(std::move(widget)) {}   // NOLINT(*-explicit-constructor)

        [[nodiscard]] explicit operator bool() const { return _widget != nullptr; }
        [[nodiscard]] const WidgetBase* Get() const { return _widget.get(); }
        [[nodiscard]] const std::shared_ptr<const WidgetBase>& Shared() const { return _widget; }

        /**
         * @brief Gives the widget a key: among its siblings, it is matched to the one from the last
         *        build with the same key, wherever it moved to — so a reordered list keeps each item's state.
         */
        Widget Key(std::string key) &&;

        // ---- modifiers -------------------------------------------------------------------------------
        // Each wraps the widget in one of the building blocks and returns the result, so a chain reads
        // inside out: `Text("Hi").Padding(8).Background(surface, 12).OnTap(go)` is a clickable, rounded,
        // filled box with eight units around the text. Order matters, as nesting does: padding before a
        // background is inside it, padding after is outside (a margin).

        [[nodiscard]] Widget Padding(float all) const;
        [[nodiscard]] Widget Padding(float horizontal, float vertical) const;
        [[nodiscard]] Widget Padding(EdgeInsets insets) const;
        /** @brief A filled box behind it, with rounded corners. */
        [[nodiscard]] Widget Background(Color color, Radii radius = {}) const;
        /** @brief An outline, drawn inside its box. */
        [[nodiscard]] Widget Border(float width, Color color, Radii radius = {}) const;
        /** @brief A shadow under its box. */
        [[nodiscard]] Widget Shadow(Color color, float blur, glm::vec2 offset = {}, Radii radius = {}) const;
        /** @brief Any decoration behind it. */
        [[nodiscard]] Widget Decorated(const Decoration& decoration) const;
        /** @brief An exact size; a negative dimension is left to the widget. */
        [[nodiscard]] Widget Size(float width, float height) const;
        [[nodiscard]] Widget Width(float width) const;
        [[nodiscard]] Widget Height(float height) const;
        [[nodiscard]] Widget Constrained(BoxConstraints constraints) const;
        /** @brief A weighted share of a Row's or Column's leftover space, filled. */
        [[nodiscard]] Widget Expanded(float flex = 1.f) const;
        /** @brief A weighted share it may leave part of. */
        [[nodiscard]] Widget Flexible(float flex = 1.f) const;
        /** @brief Fills what it is given, the widget in the middle. */
        [[nodiscard]] Widget Center() const;
        [[nodiscard]] Widget Align(const Alignment& alignment) const;
        /** @brief Its own place in a Stack, or across a Row or Column. */
        [[nodiscard]] Widget AlignSelf(const Alignment& alignment) const;
        [[nodiscard]] Widget Positioned(const PositionedOptions& options) const;
        [[nodiscard]] Widget Opacity(float opacity) const;
        /** @brief Cut to its box, with rounded corners. */
        [[nodiscard]] Widget Clip(Radii radius = {}) const;
        /** @brief Moved where it is drawn and hit; its layout unchanged. */
        [[nodiscard]] Widget Offset(glm::vec2 by) const;
        [[nodiscard]] Widget Scrollable(Axis axis = Axis::eVertical) const;
        [[nodiscard]] Widget RepaintBoundary() const;
        /** @brief Calls @p onTap when clicked, and nothing else: no hover or press look (see Button for those). */
        [[nodiscard]] Widget OnTap(std::function<void()> onTap) const;
        [[nodiscard]] Widget Gestures(GestureOptions options) const;
        /** @brief Can be picked up and dragged, carrying @p data. @see Draggable */
        [[nodiscard]] Widget Draggable(DragData data) const;
        [[nodiscard]] Widget Draggable(DragData data, DraggableOptions options) const;
        /** @brief Takes drags of @p type dropped on it. @see DropTarget */
        [[nodiscard]] Widget OnDrop(std::string type, std::function<void(const DragData&)> onDrop) const;
        [[nodiscard]] Widget DropTarget(DropTargetOptions options) const;

    private:
        std::shared_ptr<const WidgetBase> _widget;
    };

    template <typename T, typename... Args> requires std::derived_from<T, WidgetBase>
    Widget Make(Args&&... args) { return Widget(std::make_shared<T>(std::forward<Args>(args)...)); }

    /** @brief What every widget is. Projects derive from StatelessWidget or StatefulWidget instead. */
    class KUI_API WidgetBase {
    public:
        virtual ~WidgetBase();
        [[nodiscard]] virtual std::unique_ptr<Element> CreateElement() const = 0;
        [[nodiscard]] const std::string& WidgetKey() const { return _key; }
        /**
         * @brief What it is, beyond its C++ type: widgets written in another language share one C++
         *        type, and tell their own types apart by this. Null for a C++ widget.
         */
        [[nodiscard]] virtual const void* TypeTag() const { return nullptr; }

    private:
        friend class Widget;
        std::string _key;
    };

    /**
     * @brief A widget made of other widgets, with no state of its own: Build describes it from what it
     *        was given.
     *
     * @code
     * struct Card : kui::StatelessWidget {
     *     std::string title;
     *     explicit Card(std::string t) : title(std::move(t)) {}
     *     kui::Widget Build() const override {
     *         return kui::Container({.padding = kui::EdgeInsets::All(12), .decoration = {.color = kui::Theme::Current().surface, .radius = 8}},
     *                               kui::Text(title));
     *     }
     * };
     * // ... kui::Make<Card>("Settings")
     * @endcode
     */
    class KUI_API StatelessWidget : public WidgetBase {
    public:
        [[nodiscard]] virtual Widget Build() const = 0;
        [[nodiscard]] std::unique_ptr<Element> CreateElement() const override;
    };

    /**
     * @brief A widget that keeps state: its members *are* the state, Build describes the widget from
     *        them, and SetState says they changed.
     *
     * The instance first built at a place in the tree is the one kept there. When the parent builds
     * again it makes a new instance — what it would give a fresh one — and that is offered to
     * DidUpdateWidget, where the widget takes whatever configuration it accepts from its parent.
     *
     * @code
     * struct Counter : kui::StatefulWidget {
     *     int n = 0;
     *     kui::Widget Build() override {
     *         return kui::Column({
     *             kui::Text(std::format("{}", n)),
     *             kui::Button("+", [this] { SetState([&] { ++n; }); }),
     *         });
     *     }
     * };
     * @endcode
     */
    class KUI_API StatefulWidget : public WidgetBase {
    public:
        [[nodiscard]] virtual Widget Build() = 0;
        /** @brief Once, after it is first placed in the tree. */
        virtual void InitState() {}
        /** @brief Once, before it leaves the tree. */
        virtual void Dispose() {}
        /** @brief The parent built again, describing it as @p newer (of the same type). Copy what configuration you accept. */
        virtual void DidUpdateWidget(const StatefulWidget& newer) {}
        [[nodiscard]] std::unique_ptr<Element> CreateElement() const override;

    protected:
        /** @brief Applies @p change and builds the widget again before the next frame is drawn. */
        void SetState(const std::function<void()>& change = {});
        /**
         * @brief Calls @p tick each frame with the seconds since the last one, for as long as it returns
         *        true and the widget is in the tree — what an animation runs on.
         */
        void Animate(std::function<bool(float)> tick);
        [[nodiscard]] bool Mounted() const { return _element != nullptr; }

    private:
        friend class StatefulElement;
        Element* _element = nullptr;
    };

    /**
     * @brief A widget backed by a render object of its own: how layout and painting are added. Make the
     *        render object, keep it in step with the widget, and say which widgets are its children.
     */
    class KUI_API RenderObjectWidget : public WidgetBase {
    public:
        [[nodiscard]] virtual std::unique_ptr<RenderObject> CreateRenderObject() const = 0;
        virtual void UpdateRenderObject(RenderObject& object) const {}
        [[nodiscard]] virtual const std::vector<Widget>& Children() const;
        [[nodiscard]] std::unique_ptr<Element> CreateElement() const override;
    };

    /**
     * @brief A render object that holds any number of children — what a RenderObjectWidget with
     *        children makes. Single-child ones are the same, with one.
     */
    class KUI_API RenderContainer : public RenderObject {
    public:
        void VisitChildren(const std::function<void(RenderObject&)>& visit) override;
        /** @brief Replaces the children, adopting the new and dropping the gone. */
        void SetChildren(const std::vector<RenderObject*>& children);
        [[nodiscard]] const std::vector<RenderObject*>& Children() const { return _children; }
        [[nodiscard]] RenderObject* Child() const { return _children.empty() ? nullptr : _children.front(); }
        void Paint(Canvas& canvas, glm::vec2 offset) override;
        void ChildDestroyed(RenderObject& child) override;

    protected:
        /** @brief Lays the one child out under the same constraints and takes its size; the smallest size without one. */
        void PerformLayout() override;
        std::vector<RenderObject*> _children;
    };

    // ---- theme ------------------------------------------------------------------------------------

    /** @brief The colours, shapes and type the built-in controls draw with. */
    struct KUI_API Theme {
        Color background = Color::Hex(0x15161B);
        Color surface = Color::Hex(0x22242C);
        Color surfaceHover = Color::Hex(0x2C2F39);
        Color surfacePressed = Color::Hex(0x353946);
        Color primary = Color::Hex(0x5B7CFA);
        Color primaryHover = Color::Hex(0x7090FF);
        Color primaryPressed = Color::Hex(0x4A68DD);
        Color onPrimary = colors::White;
        Color text = Color::Hex(0xE8E9EE);
        Color textMuted = Color::Hex(0x9A9DAA);
        Color border = Color::Hex(0x3A3D49);
        Color focus = Color::Hex(0x8FA6FF);
        float radius = 6.f;
        float controlHeight = 32.f;
        TextStyle textStyle { .size = 14.f };

        static Theme Dark();
        static Theme Light();
        /** @brief The theme of the Ui being built. */
        static const Theme& Current();
    };

    // ---- layout widgets -----------------------------------------------------------------------------

    enum class MainAxisAlignment : std::uint8_t { eStart, eEnd, eCenter, eSpaceBetween, eSpaceAround, eSpaceEvenly };
    enum class CrossAxisAlignment : std::uint8_t { eStart, eEnd, eCenter, eStretch };
    /**
     * @brief How long a Row or Column is along its axis.
     *
     * Everything fits its contents unless asked otherwise. What fills the space it is given: Expanded
     * (along a Row or Column), CrossAxisAlignment::eStretch (across one), Align and Center (to place
     * their child), and anything a parent gives an exact size.
     */
    enum class MainAxisSize : std::uint8_t { eMin, eMax };

    /** @brief Where in a box something goes: (-1, -1) is the top-left, (0, 0) the centre, (1, 1) the bottom-right. */
    struct Alignment {
        float x = 0.f, y = 0.f;
        static constexpr Alignment TopLeft() { return { -1.f, -1.f }; }
        static constexpr Alignment TopCenter() { return { 0.f, -1.f }; }
        static constexpr Alignment TopRight() { return { 1.f, -1.f }; }
        static constexpr Alignment CenterLeft() { return { -1.f, 0.f }; }
        static constexpr Alignment Center() { return { 0.f, 0.f }; }
        static constexpr Alignment CenterRight() { return { 1.f, 0.f }; }
        static constexpr Alignment BottomLeft() { return { -1.f, 1.f }; }
        static constexpr Alignment BottomCenter() { return { 0.f, 1.f }; }
        static constexpr Alignment BottomRight() { return { 1.f, 1.f }; }
        /** @brief Where something of @p inner size goes in @p outer. */
        [[nodiscard]] glm::vec2 Place(const glm::vec2 inner, const glm::vec2 outer) const { return (outer - inner) * 0.5f * glm::vec2(x + 1.f, y + 1.f); }
        constexpr bool operator==(const Alignment&) const = default;
    };

    struct FlexOptions {
        MainAxisAlignment mainAxisAlignment = MainAxisAlignment::eStart;
        CrossAxisAlignment crossAxisAlignment = CrossAxisAlignment::eCenter;
        /// eMin: as long as the children (the default — everything fits its contents). eMax: as long as
        /// allowed. Expanded children take their share of the space either way.
        MainAxisSize mainAxisSize = MainAxisSize::eMin;
        float gap = 0.f;                 ///< Between neighbouring children.

        // Chainable: `kui::FlexOptions{}.Set...(...).Set...(...)`.
        FlexOptions& SetMainAxisAlignment(MainAxisAlignment value) { mainAxisAlignment = std::move(value); return *this; }
        FlexOptions& SetCrossAxisAlignment(CrossAxisAlignment value) { crossAxisAlignment = std::move(value); return *this; }
        FlexOptions& SetMainAxisSize(MainAxisSize value) { mainAxisSize = std::move(value); return *this; }
        FlexOptions& SetGap(float value) { gap = std::move(value); return *this; }
    };

    /** @brief How a box looks behind its content. */
    struct Decoration {
        Color color = colors::Transparent;
        std::shared_ptr<const Gradient> gradient;   ///< In the box's own coordinates.
        float borderWidth = 0.f;                    ///< Drawn inside the box.
        Color borderColor = colors::Transparent;
        Radii radius {};
        Color shadowColor = colors::Transparent;
        float shadowBlur = 0.f;
        glm::vec2 shadowOffset {};
        [[nodiscard]] bool Visible() const { return color.Visible() || gradient || (borderWidth > 0.f && borderColor.Visible()) || shadowColor.Visible(); }
        bool operator==(const Decoration&) const = default;

        // Chainable: `kui::Decoration{}.Set...(...).Set...(...)`.
        Decoration& SetColor(Color value) { color = std::move(value); return *this; }
        Decoration& SetGradient(std::shared_ptr<const Gradient> value) { gradient = std::move(value); return *this; }
        Decoration& SetBorderWidth(float value) { borderWidth = std::move(value); return *this; }
        Decoration& SetBorderColor(Color value) { borderColor = std::move(value); return *this; }
        Decoration& SetRadius(Radii value) { radius = std::move(value); return *this; }
        Decoration& SetShadowColor(Color value) { shadowColor = std::move(value); return *this; }
        Decoration& SetShadowBlur(float value) { shadowBlur = std::move(value); return *this; }
        Decoration& SetShadowOffset(glm::vec2 value) { shadowOffset = std::move(value); return *this; }
    };

    struct ContainerOptions {
        float width = -1.f, height = -1.f;          ///< Negative: sized by the child (just its padding, without one).
        EdgeInsets padding {};
        EdgeInsets margin {};
        Decoration decoration {};
        std::optional<Alignment> alignment;          ///< Set: the box fills what it is given, the child aligned inside — how to centre something.

        // Chainable: `kui::ContainerOptions{}.Set...(...).Set...(...)`.
        ContainerOptions& SetWidth(float value) { width = value; return *this; }
        ContainerOptions& SetHeight(float value) { height = value; return *this; }
        ContainerOptions& SetSize(float w, float h) { width = w; height = h; return *this; }
        ContainerOptions& SetPadding(EdgeInsets value) { padding = std::move(value); return *this; }
        ContainerOptions& SetMargin(EdgeInsets value) { margin = std::move(value); return *this; }
        ContainerOptions& SetDecoration(Decoration value) { decoration = std::move(value); return *this; }
        ContainerOptions& SetAlignment(Alignment value) { alignment = std::move(value); return *this; }
    };

    struct PositionedOptions {
        std::optional<float> left, top, right, bottom, width, height;

        // Chainable: `kui::PositionedOptions{}.Set...(...).Set...(...)`.
        PositionedOptions& SetLeft(float value) { left = std::move(value); return *this; }
        PositionedOptions& SetTop(float value) { top = std::move(value); return *this; }
        PositionedOptions& SetRight(float value) { right = std::move(value); return *this; }
        PositionedOptions& SetBottom(float value) { bottom = std::move(value); return *this; }
        PositionedOptions& SetWidth(float value) { width = std::move(value); return *this; }
        PositionedOptions& SetHeight(float value) { height = std::move(value); return *this; }
    };

    struct GestureOptions {
        std::function<void()> onTap;
        std::function<void(glm::vec2 local)> onTapDown;
        std::function<void()> onTapUp;
        std::function<void(glm::vec2 local)> onPanStart;
        std::function<void(glm::vec2 delta, glm::vec2 local)> onPanUpdate;
        std::function<void()> onPanEnd;
        std::function<void()> onEnter;
        std::function<void()> onExit;
        std::function<void(glm::vec2 local)> onHover;
        std::function<bool(glm::vec2 delta)> onScroll;   ///< Return true when used.
        /** Hit even where nothing it holds is drawn — a transparent area that still takes clicks. */
        bool opaque = true;

        // Chainable: `kui::GestureOptions{}.Set...(...).Set...(...)`.
        GestureOptions& SetOpaque(bool value) { opaque = value; return *this; }
        GestureOptions& OnTap(std::function<void()> f) { onTap = std::move(f); return *this; }
        GestureOptions& OnTapDown(std::function<void(glm::vec2)> f) { onTapDown = std::move(f); return *this; }
        GestureOptions& OnTapUp(std::function<void()> f) { onTapUp = std::move(f); return *this; }
        GestureOptions& OnPanStart(std::function<void(glm::vec2)> f) { onPanStart = std::move(f); return *this; }
        GestureOptions& OnPanUpdate(std::function<void(glm::vec2, glm::vec2)> f) { onPanUpdate = std::move(f); return *this; }
        GestureOptions& OnPanEnd(std::function<void()> f) { onPanEnd = std::move(f); return *this; }
        GestureOptions& OnEnter(std::function<void()> f) { onEnter = std::move(f); return *this; }
        GestureOptions& OnExit(std::function<void()> f) { onExit = std::move(f); return *this; }
        GestureOptions& OnHover(std::function<void(glm::vec2)> f) { onHover = std::move(f); return *this; }
        GestureOptions& OnScroll(std::function<bool(glm::vec2)> f) { onScroll = std::move(f); return *this; }
    };

    enum class ImageFit : std::uint8_t { eFill, eContain, eCover, eNone };

    /** @brief Text, wrapped to the width it is given. */
    KUI_API Widget Text(std::string text, TextStyle style = {}, TextAlign align = TextAlign::eStart, bool wrap = true);
    /** @brief Children side by side. */
    KUI_API Widget Row(std::vector<Widget> children, FlexOptions options = {});
    /** @brief Children one above the other. */
    KUI_API Widget Column(std::vector<Widget> children, FlexOptions options = {});
    KUI_API Widget Flex(Axis axis, std::vector<Widget> children, FlexOptions options = {});
    /** @brief In a Row or Column: takes a share (@p flex) of the space left over, filling it. */
    KUI_API Widget Expanded(Widget child, float flex = 1.f);
    /** @brief In a Row or Column: may take up to a share of the space left over. */
    KUI_API Widget Flexible(Widget child, float flex = 1.f);
    KUI_API Widget Padding(EdgeInsets padding, Widget child);
    KUI_API Widget Align(Alignment alignment, Widget child);
    KUI_API Widget Center(Widget child);
    /** @brief A box of a fixed size (negative: whatever the child is), or empty space. */
    KUI_API Widget SizedBox(float width, float height, Widget child = {});
    /** @brief Constraints of its own, within its parent's. */
    KUI_API Widget ConstrainedBox(BoxConstraints constraints, Widget child);
    /** @brief Padding, a decoration, a size and an alignment: the everyday box. */
    KUI_API Widget Container(ContainerOptions options, Widget child = {});
    KUI_API Widget DecoratedBox(Decoration decoration, Widget child);
    /** @brief Children on top of each other: the first at the back. Positioned ones are placed; the rest aligned. */
    KUI_API Widget Stack(std::vector<Widget> children, Alignment alignment = Alignment::TopLeft());
    KUI_API Widget Positioned(PositionedOptions options, Widget child);
    /**
     * @brief @p child placed by its own @p alignment: in a Stack, still counting towards its size; in a Row
     *        or Column, across it (a Row uses the vertical part, a Column the horizontal).
     */
    KUI_API Widget StackAlign(Alignment alignment, Widget child);
    /** @brief A child that may be larger than the box, scrolled by the wheel or by dragging. */
    KUI_API Widget ScrollView(Widget child, Axis axis = Axis::eVertical);
    /**
     * @brief A scrolling list whose items each keep a layer of their own: an item that changes repaints
     *        and uploads only itself, however long the list.
     */
    KUI_API Widget ListView(std::vector<Widget> children, Axis axis = Axis::eVertical, float gap = 0.f);
    /**
     * @brief A list of @p count items @p itemExtent tall, of which only those in view (and a screen
     *        either side) exist: @p builder makes item i when it scrolls into view. A million rows cost
     *        what a screenful does. @p onRange, if given, is told the items [first, last) the list keeps
     *        whenever that changes: what lies outside it will be built again when it comes back.
     */
    KUI_API Widget ListView(std::size_t count, float itemExtent, std::function<Widget(std::size_t)> builder,
                            std::function<void(std::size_t first, std::size_t last)> onRange = {});
    /** @brief Pointer events on the child. */
    KUI_API Widget GestureDetector(GestureOptions options, Widget child);
    /** @brief Draws with a canvas, in a box of @p size (negative: as large as the child, or nothing without one — unless the parent sets its size). */
    KUI_API Widget CustomPaint(std::function<void(Canvas&, glm::vec2 size)> painter, glm::vec2 size = { -1.f, -1.f }, Widget child = {});
    /** @brief An element shader filling the box. @see ElementShader */
    KUI_API Widget ShaderBox(std::shared_ptr<ElementShader> shader, std::vector<std::byte> parameters = {}, Radii radius = {}, Widget child = {});
    template <typename T> requires std::is_trivially_copyable_v<T>
    Widget ShaderBox(std::shared_ptr<ElementShader> shader, const T& parameters, const Radii radius = {}, Widget child = {}) {
        const auto bytes = std::as_bytes(std::span(&parameters, 1));
        return ShaderBox(std::move(shader), std::vector<std::byte>(bytes.begin(), bytes.end()), radius, std::move(child));
    }
    KUI_API Widget Image(kor::ResourceRef<const kor::Image> image, ImageFit fit = ImageFit::eContain, glm::vec2 size = { -1.f, -1.f });
    /** @brief Keeps the child in a layer of its own: repainting it repaints nothing around it, and the other way round. */
    KUI_API Widget RepaintBoundary(Widget child);
    /** @brief The child, faded. Changing it re-records nothing. */
    KUI_API Widget Opacity(float opacity, Widget child);
    /** @brief The child, cut to its box with rounded corners: nothing it paints shows outside. */
    KUI_API Widget ClipRRect(Radii radius, Widget child);
    /** @brief The child, moved by @p offset where it paints and is hit — its layout, and its parent's, unchanged. */
    KUI_API Widget Translate(glm::vec2 offset, Widget child);

    /** @brief The child, which the pointer goes through as if it were not there. */
    KUI_API Widget IgnorePointer(Widget child);

    // ---- drag and drop ----------------------------------------------------------------------------------

    struct DraggableOptions {
        Widget feedback;                            ///< What follows the pointer; a ghost of the child's size when empty.
        std::function<void()> onDragStart;
        std::function<void(bool accepted)> onDragEnd;   ///< Whether a target took it.
        bool enabled = true;

        // Chainable: `kui::DraggableOptions{}.Set...(...).On...(...)`.
        DraggableOptions& SetFeedback(Widget value) { feedback = std::move(value); return *this; }
        DraggableOptions& SetEnabled(bool value) { enabled = value; return *this; }
        DraggableOptions& OnDragStart(std::function<void()> f) { onDragStart = std::move(f); return *this; }
        DraggableOptions& OnDragEnd(std::function<void(bool)> f) { onDragEnd = std::move(f); return *this; }
    };
    /**
     * @brief The child, which can be picked up and dragged: moving the pressed pointer a little starts a
     *        drag carrying @p data, shown by the feedback widget under the pointer until it is dropped on
     *        a DropTarget that accepts it, or let go (or Esc pressed) anywhere else.
     *
     * @code
     * kui::Draggable({ "color", kui::colors::Red }, Swatch(kui::colors::Red))
     * kui::DropTarget({ .accepts = [](const kui::DragData& d) { return d.type == "color"; },
     *                   .onDrop = [this](const kui::DragData& d, glm::vec2) { SetState([&] { fill = *d.As<kui::Color>(); }); } },
     *                 Well(fill))
     * @endcode
     */
    KUI_API Widget Draggable(DragData data, Widget child, DraggableOptions options = {});

    struct DropTargetOptions {
        std::function<bool(const DragData&)> accepts;                       ///< Empty: anything.
        std::function<void(const DragData&, glm::vec2 local)> onDrop;
        std::function<void(const DragData&)> onEnter;                       ///< An accepted drag came over it: show it.
        std::function<void()> onLeave;                                      ///< It left, was dropped, or was cancelled.
        std::function<void(const DragData&, glm::vec2 local)> onMove;

        // Chainable: `kui::DropTargetOptions{}.Accepts(...).OnDrop(...)`.
        DropTargetOptions& Accepts(std::function<bool(const DragData&)> f) { accepts = std::move(f); return *this; }
        /** @brief Accepts drags of this type only. */
        DropTargetOptions& AcceptsType(std::string type) { accepts = [type = std::move(type)](const DragData& d) { return d.type == type; }; return *this; }
        DropTargetOptions& OnDrop(std::function<void(const DragData&, glm::vec2)> f) { onDrop = std::move(f); return *this; }
        DropTargetOptions& OnEnter(std::function<void(const DragData&)> f) { onEnter = std::move(f); return *this; }
        DropTargetOptions& OnLeave(std::function<void()> f) { onLeave = std::move(f); return *this; }
        DropTargetOptions& OnMove(std::function<void(const DragData&, glm::vec2)> f) { onMove = std::move(f); return *this; }
    };
    /** @brief The child, as somewhere drags can be dropped. Of nested targets, the deepest that accepts gets it. */
    KUI_API Widget DropTarget(DropTargetOptions options, Widget child);

    // ---- controls -------------------------------------------------------------------------------------

    /** @brief How a button looks. */
    enum class ButtonStyle : std::uint8_t {
        ePrimary,       ///< The theme's primary colour.
        eSecondary,     ///< The theme's surface, with a border.
        ePlain,         ///< Nothing of its own: the child alone, made clickable.
    };

    struct ButtonOptions {
        ButtonStyle style = ButtonStyle::ePrimary;
        std::optional<float> width;
        std::optional<EdgeInsets> padding;      ///< Around the child; the style's own when not given (none for ePlain).
        bool enabled = true;

        // Chainable: `kui::ButtonOptions{}.Set...(...).Set...(...)`.
        ButtonOptions& SetStyle(ButtonStyle value) { style = value; return *this; }
        ButtonOptions& SetWidth(float value) { width = value; return *this; }
        ButtonOptions& SetPadding(EdgeInsets value) { padding = value; return *this; }
        ButtonOptions& SetEnabled(bool value) { enabled = value; return *this; }
    };
    /**
     * @brief Any widget, made a button: @p child in a container (styled as @p options say) that calls
     *        @p onPressed when clicked, and shows when it is hovered and pressed.
     *
     * @code
     * kui::Button(kui::Row({ kui::Image(icon), kui::Text("Save") }), save)
     * kui::Button(kui::Image(thumbnail), open, { .style = kui::ButtonStyle::ePlain })   // just clickable
     * @endcode
     */
    KUI_API Widget Button(Widget child, std::function<void()> onPressed, ButtonOptions options = {});
    /** @brief A button holding @p label, in the style's text colour. */
    KUI_API Widget Button(std::string label, std::function<void()> onPressed, ButtonOptions options = {});
    /** @brief A box with a tick, and a label beside it. */
    KUI_API Widget Checkbox(bool value, std::function<void(bool)> onChanged, std::string label = {});
    KUI_API Widget Switch(bool value, std::function<void(bool)> onChanged);
    KUI_API Widget Slider(float value, std::function<void(float)> onChanged, float min = 0.f, float max = 1.f);
    KUI_API Widget ProgressBar(float value);

    struct TextFieldOptions {
        std::string text;                                   ///< What it starts with.
        std::string placeholder;
        std::function<void(const std::string&)> onChanged;
        std::function<void(const std::string&)> onSubmitted;   ///< Enter.
        float width = -1.f;                                 ///< Negative: 200 units, or as wide as a stretching parent makes it.
        /// True: it always shows `text`, as a controlled field — what is typed reaches onChanged, and shows
        /// once it comes back as `text`. False: `text` is only what it starts with.
        bool controlled = false;

        // Chainable: `kui::TextFieldOptions{}.Set...(...).Set...(...)`.
        TextFieldOptions& SetText(std::string value) { text = std::move(value); return *this; }
        TextFieldOptions& SetPlaceholder(std::string value) { placeholder = std::move(value); return *this; }
        TextFieldOptions& SetWidth(float value) { width = value; return *this; }
        TextFieldOptions& SetControlled(bool value) { controlled = value; return *this; }
        TextFieldOptions& OnChanged(std::function<void(const std::string&)> f) { onChanged = std::move(f); return *this; }
        TextFieldOptions& OnSubmitted(std::function<void(const std::string&)> f) { onSubmitted = std::move(f); return *this; }
    };
    /** @brief One line of editable text. */
    KUI_API Widget TextField(TextFieldOptions options);

    // ---- the view ---------------------------------------------------------------------------------------

    struct UiSettings {
        Theme theme {};
        float scale = 1.f;                      ///< Pixels per logical unit.

        // Chainable: `kui::UiSettings{}.Set...(...).Set...(...)`.
        UiSettings& SetTheme(Theme value) { theme = std::move(value); return *this; }
        UiSettings& SetScale(float value) { scale = std::move(value); return *this; }
    };

    /**
     * @brief A widget tree, live: builds it, lays it out, paints it, and feeds it the scene's input.
     *
     * @code
     * class Menu : public kor::Scene {
     *     kui::Ui _ui { kui::Make<MainMenu>() };
     *     void Initialize() override { Graph().Add<kui::UiPass>(_ui); }
     *     void Update() override { _ui.Update(); }
     * };
     * @endcode
     *
     * Each Update hands the pointer and keyboard to the widgets, rebuilds what SetState marked, lays
     * out what changed size and records what changed look — and when nothing did, does nothing. While
     * the pointer is over a widget, or text is being typed, the scene's input says the interface wants
     * it (kor::Input::InterfaceWantsMouse), so a camera stays put.
     */
    class KUI_API Ui {
    public:
        explicit Ui(Widget root = {}, UiSettings settings = {});
        ~Ui();
        Ui(const Ui&) = delete;
        Ui& operator=(const Ui&) = delete;

        void SetRoot(Widget root);
        /** @brief A new theme: every widget built again with it, keeping its state. */
        void SetTheme(const Theme& theme);
        /**
         * @brief Every widget built again, keeping its state — what a hot reload calls, so code that
         *        changed runs. Flutter's reassemble.
         */
        void Reassemble();
        /** @brief Every live Ui: what a hot reload reassembles. Main thread. */
        static void ReassembleAll();
        [[nodiscard]] const Theme& GetTheme() const;
        void SetScale(float scale);

        /** @brief The frame, with the current scene's input and window. */
        void Update();
        /** @brief The frame, with @p input, over a target of @p viewport pixels, @p dt seconds after the last. */
        void Update(kor::Input& input, glm::vec2 viewport, float dt);

        [[nodiscard]] Renderer& GetRenderer();
        /** @brief The render tree's root, after the first Update. */
        [[nodiscard]] RenderObject* RootRenderObject() const;
        /** @brief Whether the pointer is over a widget that takes it, or one is being dragged. */
        [[nodiscard]] bool WantsPointer() const;
        /** @brief Whether a widget has the keyboard. */
        [[nodiscard]] bool WantsKeyboard() const;

        struct Statistics {
            std::size_t builds = 0;     ///< Widgets built in the last Update.
            std::size_t layouts = 0;    ///< Render objects laid out in it.
            std::size_t paints = 0;     ///< Layers recorded in it.
            double inputMs = 0.0, buildMs = 0.0, layoutMs = 0.0, paintMs = 0.0;   ///< Where the last Update's time went.
        };
        [[nodiscard]] const Statistics& Stats() const;

        struct Impl;
    private:
        std::unique_ptr<Impl> _impl;
    };

}
