//
// koral-ui, the widget layer: declarative building blocks, rebuilt only where their state changed.
//

#pragma once

#include <algorithm>
#include <cmath>
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

#include "kuiApi.h"
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
         * @return Whether it will be called: false for a widget that is in no tree.
         */
        bool Animate(std::function<bool(float)> tick);
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
        /** @brief Its widest child's: what holds one child, or lays its children over one another, is that wide at least. */
        [[nodiscard]] float MinIntrinsicWidth() const override;

    protected:
        /** @brief Lays the one child out under the same constraints and takes its size; the smallest size without one. */
        void PerformLayout() override;
        std::vector<RenderObject*> _children;
    };

    // ---- theme ------------------------------------------------------------------------------------

    /**
     * @brief The colours, shapes and type the built-in controls draw with.
     *
     * The default is in the manner of Samsung's One UI, dark: neutral greys on black, generous
     * corners — a control is as round as it is tall, a pill — and one accent, coral. Light() is the
     * same on a light ground.
     */
    /**
     * @brief Whose manner the built-in controls are drawn in: not only how round and how big, which a
     *        Theme's numbers say, but what a switch, a slider, a field is made of.
     */
    enum class ThemeDesign : std::uint8_t {
        eKoral,         ///< koral-ui's own, after One UI: pills, a round check, a thumb that fills its switch.
        eMaterial,      ///< Material 3: outlined and text buttons, filled fields with a line under them, a bar for a slider's handle.
        eCupertino,     ///< Apple's, after Liquid Glass: buttons, fields and segments of glass — clear or the accent's, with a bright rim — and thumbs wider than tall.
        eFluent,        ///< Windows 11's: thin outlines, a small thumb in an outlined switch, a dot in the slider's, an accent line under a field.
    };

    struct KUI_API Theme {
        Color background = Color::Hex(0x000000);
        Color surface = Color::Hex(0x171717);
        Color surfaceHover = Color::Hex(0x252525);
        Color surfacePressed = Color::Hex(0x303030);
        Color primary = Color::Hex(0xFF7F50);           ///< Coral.
        Color primaryHover = Color::Hex(0xFF946B);
        Color primaryPressed = Color::Hex(0xE86A3C);
        Color onPrimary = colors::White;
        Color text = Color::Hex(0xFAFAFA);
        Color textMuted = Color::Hex(0x8E8E8E);
        Color border = Color::Hex(0x2B2B2B);
        Color focus = Color::Hex(0xFFB59A);
        float radius = 18.f;                            ///< Half a control's height: buttons and fields are pills, cards well rounded.
        float controlHeight = 36.f;
        TextStyle textStyle { .size = 15.f };
        /// How round each kind of thing is, where it is not the theme's radius: a button, a field (a text
        /// field, a dropdown, a drag value), and a checkbox — whose negative means a circle. Negative: radius.
        float buttonRadius = -1.f, fieldRadius = -1.f;
        float checkboxRadius = -1.f;
        ThemeDesign design = ThemeDesign::eKoral;       ///< What the controls are made of. @see ThemeDesign

        /** @brief Whether it is a dark theme: by how light its background is. */
        [[nodiscard]] bool IsDark() const { return 0.2126f * background.r + 0.7152f * background.g + 0.0722f * background.b < 0.5f; }
        [[nodiscard]] float ButtonRadius() const { return buttonRadius >= 0.f ? buttonRadius : radius; }
        [[nodiscard]] float FieldRadius() const { return fieldRadius >= 0.f ? fieldRadius : radius; }

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
    /** @brief Text. @p maxLines (0: any number) keeps so many lines, the last ending in an ellipsis when @p ellipsis. */
    KUI_API Widget Text(std::string text, TextStyle style = {}, TextAlign align = TextAlign::eStart, bool wrap = true, int maxLines = 0, bool ellipsis = false);
    /**
     * @brief @p child, built, laid out and painted with @p theme in place of the view's: a panel in another
     *        family's look, a preview of a theme. Themes nest: the nearest above applies.
     */
    KUI_API Widget Themed(Theme theme, Widget child);

    /** @brief How the system itself looks: dark or light, and its accent. What a theme that follows the system is made from. */
    struct SystemAppearance {
        bool dark = true;
        Color accent = Color::Hex(0x0078D4);
        bool known = false;     ///< Whether the system said: false where it has no such thing to ask.
    };
    KUI_API SystemAppearance QuerySystemAppearance();
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
    /** @brief What a LazyList is: how many items, which way they run, how long they are, and where it is scrolled to. */
    struct LazyListOptions {
        std::size_t count = 0;
        Axis axis = Axis::eVertical;
        /// How long every item is. 0 or less: each is as long as it turns out to be, and one not yet built is
        /// taken to be @p estimatedExtent long until it is.
        float itemExtent = 0.f;
        float estimatedExtent = 40.f;
        float gap = 0.f;                                                ///< Between one item and the next.
        float paddingStart = 0.f, paddingEnd = 0.f;                     ///< Before the first item and after the last, scrolled with them.
        std::function<void(std::size_t first, std::size_t last)> onRange;   ///< The items [first, last) it keeps, as that changes.
        /// Where it is: the first item in view, and how far into it the view starts. Told when either changes.
        std::function<void(std::size_t index, float offset)> onScrolled;
        std::size_t jumpIndex = 0;                                      ///< The item to put first in view, @p jumpOffset into it,
        float jumpOffset = 0.f;                                         ///< when @p jump is not what it last was (and not 0).
        std::uint32_t jump = 0;
    };
    /**
     * @brief A list down or across of which only the items in view (and a screen either side) exist, each as
     *        long as it likes: what ListView(count, itemExtent, builder) is for items all the same.
     */
    KUI_API Widget LazyList(LazyListOptions options, std::function<Widget(std::size_t)> builder);
    /**
     * @brief @p child as wide (@p width) and as tall (@p height) as it would be with all the room there is that
     *        way, and no more: what makes a column as wide as its widest child, or a row as tall as its tallest,
     *        so that the others can be stretched to it. The child is laid out twice.
     */
    KUI_API Widget Intrinsic(bool width, bool height, Widget child);
    /** @brief Pointer events on the child. */
    KUI_API Widget GestureDetector(GestureOptions options, Widget child);

    struct ScrollOptions {
        Axis axis = Axis::eVertical;
        std::function<void(float position, float most)> onScrolled;     ///< Where it is scrolled to, and how far it can be: told when either changes.
        float jumpTo = 0.f;                                             ///< Where to scroll to, when @p jump is not what it last was.
        std::uint32_t jump = 0;
    };
    /** @brief A ScrollView that says where it is, and goes where it is told: what a scroll state is made of. */
    KUI_API Widget ScrollView(Widget child, ScrollOptions options);

    /** @brief @p child drawn, and hit, through @p transform about the point of its own box @p origin names. Its layout is unchanged. */
    KUI_API Widget TransformBox(const Transform& transform, Widget child, Alignment origin = Alignment::Center());
    /** @brief As wide as it may be, and as tall as that makes it at @p ratio (width over height). */
    KUI_API Widget AspectRatio(float ratio, Widget child);
    /** @brief @p child made that share of the width, and of the height, it is allowed. A share of 0 leaves that way alone. */
    KUI_API Widget FractionallySizedBox(float widthShare, float heightShare, Widget child);

    /** @brief What a CustomLayout's rule measures and places its children through. */
    struct LayoutContext {
        std::size_t count = 0;
        std::function<glm::vec2(std::size_t index, const BoxConstraints& constraints)> measure;    ///< Lays child @p index out; its size.
        std::function<void(std::size_t index, glm::vec2 at)> place;
    };
    /** @brief @p children laid out by @p layout: it measures each with the constraints it likes, places it, and returns its own size. */
    KUI_API Widget CustomLayout(std::function<glm::vec2(LayoutContext&, const BoxConstraints&)> layout, std::vector<Widget> children);

    /**
     * @brief Takes no room; while @p open, @p popup is shown over everything, at the left of whatever this is
     *        in — under it, or (@p below false) over its top — moved by @p offset. A press outside the popup, or
     *        Escape, closes it and calls @p onDismiss. A dropdown menu is one of these beside its button.
     */
    KUI_API Widget PopupAnchor(bool open, Widget popup, std::function<void()> onDismiss, glm::vec2 offset = {}, bool below = true);
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

    /**
     * @brief The child on glass: what is behind its box shows through, blurred, tinted and bent at the
     *        edge, with corners as round as @p radius. @see Canvas::DrawBackdrop
     *
     * @code
     * kui::BackdropFilter(kui::Backdrop{}.SetBlur(18.f).SetTint(theme.surface.WithAlpha(0.5f)), 16.f, Panel())
     * @endcode
     */
    KUI_API Widget BackdropFilter(Backdrop backdrop, Radii radius, Widget child);

    // ---- animation --------------------------------------------------------------------------------------

    /** @brief How something that goes from one value to another gets there: evenly, or easing away and in. */
    enum class Curve : std::uint8_t {
        eLinear,        ///< At one speed all the way.
        eEaseIn,        ///< Slowly at first.
        eEaseOut,       ///< Slowing as it arrives.
        eEaseInOut,     ///< Slowly at both ends. The default.
        eEaseOutBack,   ///< Past where it is going, a little, and back: what pops into place.
    };
    /** @brief Where along its way (0 to 1) something on @p curve is, @p t of the way through its time (0 to 1). */
    KUI_API float Ease(Curve curve, float t);

    struct AnimationOptions {
        float duration = 0.18f;                 ///< Seconds from one value to the next.
        Curve curve = Curve::eEaseInOut;
        std::optional<float> initial;           ///< Where it starts when first shown; there already, when not given.

        // Chainable: `kui::AnimationOptions{}.SetDuration(0.3f).SetCurve(kui::Curve::eEaseOut)`.
        AnimationOptions& SetDuration(float value) { duration = value; return *this; }
        AnimationOptions& SetCurve(Curve value) { curve = value; return *this; }
        AnimationOptions& SetInitial(float value) { initial = value; return *this; }
    };
    /**
     * @brief A number that goes where it is told to over time, and whatever is made of it: each time it is
     *        built with another @p target, the value starts for there from wherever it is, and @p builder
     *        is built with it every frame until it arrives.
     *
     * @code
     * kui::Animated(open ? 240.f : 48.f, [](const float width) { return kui::SizedBox(width, -1.f, Sidebar()); })
     * @endcode
     */
    KUI_API Widget Animated(float target, std::function<Widget(float value)> builder, AnimationOptions options = {});
    /** @brief The child, fading to @p opacity whenever that is another. */
    KUI_API Widget AnimatedOpacity(float opacity, Widget child, AnimationOptions options = {});
    /** @brief The child, fading in when it is first shown — and rising into place by @p rise as it does. */
    KUI_API Widget Appear(Widget child, AnimationOptions options = {}, float rise = 6.f);
    /**
     * @brief The child, unfolding downwards while @p open and folding away when it is not: as tall as
     *        the share of its height shown so far, and cut off there. Folded away it is not built at all;
     *        while it folds, the child it had is what is shown, so one that is gone already may be left out.
     */
    KUI_API Widget Reveal(bool open, Widget child, AnimationOptions options = {});

    // ---- drag and drop ----------------------------------------------------------------------------------

    struct DraggableOptions {
        Widget feedback;                            ///< What follows the pointer; the child itself, a little seen through, when empty.
        std::function<void()> onDragStart;
        std::function<void(bool accepted)> onDragEnd;   ///< Whether a target took it.
        bool enabled = true;
        /// The feedback is a picture of the thing itself, as big as it: held where it was taken hold of,
        /// not by its corner. (For whoever knows better than the child what the thing looks like.)
        bool feedbackInPlace = false;
        /// How round the outline drawn round what follows the pointer is — as round as the thing, for it
        /// to look right. Negative: the theme's radius. (Only the thing itself is outlined: a feedback
        /// widget of the caller's own, held by its corner, is shown as it is.)
        float feedbackRadius = -1.f;

        DraggableOptions& SetFeedbackRadius(float value) { feedbackRadius = value; return *this; }
        // Chainable: `kui::DraggableOptions{}.Set...(...).On...(...)`.
        DraggableOptions& SetFeedbackInPlace(bool value) { feedbackInPlace = value; return *this; }
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
    /** @brief A slider. @p onFinished is called when it is let go of. */
    KUI_API Widget Slider(float value, std::function<void(float)> onChanged, float min = 0.f, float max = 1.f, std::function<void()> onFinished = {},
                          Axis axis = Axis::eHorizontal);     ///< eVertical: upright, the value growing upwards.
    KUI_API Widget ProgressBar(float value);

    /** @brief How a DragValue turns a drag into a number, and shows it. */
    struct DragValueOptions {
        float speed = 0.01f;                    ///< What dragging one unit to the right adds.
        float min = -Infinity, max = Infinity;  ///< Where the value stops.
        int decimals = 2;                       ///< How many it is shown with.
        std::string label;                      ///< Shown before the value: "X", "Speed".
        float width = -1.f;                     ///< The least it is (negative: 120): it is wider where its label and its longest value need more.
        /// Which way it is dragged, and how it is laid out: across, its label before its value — or
        /// (eVertical) up and down, its label over its value, dragged up for more.
        Axis axis = Axis::eHorizontal;
        /// Double-clicked, or tabbed to, it turns into a text box for typing the value exactly: Enter, Tab
        /// or clicking elsewhere sets it (kept to its range); Escape leaves it as it was. It is in the order
        /// Tab goes through the fields.
        bool typeable = true;
        /// Past one end of its range it comes back in at the other — an angle dragged past 360 is at 0 again —
        /// rather than stopping there; a typed value is brought into the range the same way. The two ends are
        /// one place: the value is never the max, which is the min. Only where both ends are finite.
        bool wrap = false;

        // Chainable: `kui::DragValueOptions{}.SetSpeed(0.1f).SetRange(0, 10)`.
        DragValueOptions& SetSpeed(float value) { speed = value; return *this; }
        DragValueOptions& SetRange(float low, float high) { min = low; max = high; return *this; }
        DragValueOptions& SetDecimals(int value) { decimals = value; return *this; }
        DragValueOptions& SetLabel(std::string value) { label = std::move(value); return *this; }
        DragValueOptions& SetWidth(float value) { width = value; return *this; }
        DragValueOptions& SetAxis(Axis value) { axis = value; return *this; }
        DragValueOptions& SetTypeable(bool value) { typeable = value; return *this; }
        DragValueOptions& SetWrap(bool value) { wrap = value; return *this; }

        /** @brief @p value brought into the range: wrapped round it, where it wraps; kept at its ends, where not. */
        [[nodiscard]] float Keep(const float value) const
        {
            const float span = max - min;
            if (!wrap || !std::isfinite(min) || !std::isfinite(max) || !(span > 0.f)) return std::clamp(value, min, std::max(min, max));
            const float into = std::fmod(value - min, span);
            return min + (into < 0.f ? into + span : into);
        }
    };
    /**
     * @brief A number in a field, changed by dragging across it sideways. Each unit of
     *        the drag adds DragValueOptions::speed; the value stops at its range.
     */
    KUI_API Widget DragValue(float value, std::function<void(float)> onChanged, DragValueOptions options = {});

    /** @brief How a Dropdown looks. */
    struct DropdownOptions {
        float width = -1.f;                     ///< Negative: 200.
        std::string placeholder;                ///< Shown while nothing is selected (an index out of range).

        DropdownOptions& SetWidth(float value) { width = value; return *this; }
        DropdownOptions& SetPlaceholder(std::string value) { placeholder = std::move(value); return *this; }
    };
    /**
     * @brief A field showing the one of @p items that is @p selected; pressed, it opens the list of them
     *        under itself, and @p onChanged hears which was picked.
     */
    KUI_API Widget Dropdown(std::vector<std::string> items, int selected, std::function<void(int)> onChanged, DropdownOptions options = {});

    /** @brief One line of a menu: something to pick, or (separator) the line between two groups of them. */
    struct MenuItem {
        std::string label;
        std::function<void()> onSelected;
        bool enabled = true;
        bool separator = false;
    };
    /** @brief @p child, with a menu of @p items that opens where the right button is pressed on it. */
    KUI_API Widget ContextMenu(std::vector<MenuItem> items, Widget child);

    /** @brief One menu of a MenuBar: its title in the bar, and what opens under it. */
    struct Menu {
        std::string title;
        std::vector<MenuItem> items;
    };
    /** @brief A row of titles — File, Edit, View — each opening its menu under itself when pressed. */
    KUI_API Widget MenuBar(std::vector<Menu> menus);

    // ---- more controls ------------------------------------------------------------------------------------

    /** @brief A line between two things: across (as wide as it is given room for) or, eVertical, down. */
    KUI_API Widget Separator(Axis axis = Axis::eHorizontal, float thickness = 1.f);
    /** @brief @p child faded, and deaf to the pointer — while @p disabled. */
    KUI_API Widget Disabled(Widget child, bool disabled = true);
    /** @brief One of several choices: a ring, filled when it is the one @p selected. */
    KUI_API Widget RadioButton(bool selected, std::function<void()> onSelected, std::string label = {});
    /** @brief A line of a list that can be picked: lit under the pointer, in the accent while @p selected. */
    KUI_API Widget Selectable(std::string label, bool selected, std::function<void()> onTap);

    /**
     * @brief A header that folds what is under it: pressed, it tells @p onToggled what it should be now.
     *        @p child shows under it while @p open. Whoever builds it keeps whether it is open.
     */
    KUI_API Widget CollapsingHeader(std::string title, bool open, std::function<void(bool)> onToggled, Widget child = {});

    struct TreeNodeOptions {
        bool leaf = false;                  ///< Nothing under it: a dot where the arrow would be, and nothing to open.
        bool selected = false;              ///< Its label in the accent.
        float indent = 18.f;                ///< How far in what is under it starts.
        std::function<void()> onTap;        ///< Pressed — as well as being opened or shut.

        TreeNodeOptions& SetLeaf(bool value) { leaf = value; return *this; }
        TreeNodeOptions& SetSelected(bool value) { selected = value; return *this; }
        TreeNodeOptions& SetIndent(float value) { indent = value; return *this; }
        TreeNodeOptions& OnTap(std::function<void()> f) { onTap = std::move(f); return *this; }
    };
    /** @brief A node of a tree: its label after an arrow, and @p children under it, further in, while @p open. */
    KUI_API Widget TreeNode(std::string label, bool open, std::function<void(bool)> onToggled, std::vector<Widget> children = {},
                            TreeNodeOptions options = {});

    /** @brief A row of titles, the one at @p selected underlined in the accent; pressing another tells @p onSelected. */
    KUI_API Widget TabBar(std::vector<std::string> tabs, int selected, std::function<void(int)> onSelected);

    /** @brief @p child; while the pointer is over it, @p text shows by the pointer. */
    KUI_API Widget Tooltip(std::string text, Widget child);

    /**
     * @brief @p child, and @p onChanged told how big it has been laid out — in the interface's units, and
     *        in pixels — the first time and whenever that changes. What shows a texture drawn elsewhere
     *        (a viewport) makes the texture that size there, so that it is drawn at the size it is seen
     *        at. With no child it is as big as it is allowed to be made.
     *
     * Called while the interface is laid out, in Ui::Update: what it does to the interface (a SetState)
     * shows from the next frame.
     */
    KUI_API Widget SizeObserver(std::function<void(glm::vec2 size, glm::vec2 pixels)> onChanged, Widget child = {});

    /**
     * @brief @p child and, while @p open, @p dialog on a card in the middle of it, over a shade that dims
     *        the child and keeps the pointer from it. A press on the shade calls @p onDismiss.
     */
    KUI_API Widget Modal(bool open, Widget child, Widget dialog, std::function<void()> onDismiss = {});

    struct ColorPickerOptions {
        bool alpha = true;                  ///< A bar for how see-through it is.
        bool hex = true;                    ///< A swatch and the colour as #RRGGBB(AA) under the bars.
        float width = 220.f;

        ColorPickerOptions& SetAlpha(bool value) { alpha = value; return *this; }
        ColorPickerOptions& SetHex(bool value) { hex = value; return *this; }
        ColorPickerOptions& SetWidth(float value) { width = value; return *this; }
    };
    /** @brief Picks a colour: a square of every saturation and brightness of a hue, a bar of hues, and one of alpha. */
    KUI_API Widget ColorPicker(Color color, std::function<void(Color)> onChanged, ColorPickerOptions options = {});
    /** @brief A swatch of @p color (and @p label after it); pressed, it opens a ColorPicker under itself. */
    KUI_API Widget ColorEdit(Color color, std::function<void(Color)> onChanged, std::string label = {}, ColorPickerOptions options = {});

    enum class PlotKind : std::uint8_t { eLines, eHistogram };
    struct PlotOptions {
        PlotKind kind = PlotKind::eLines;
        float min = std::numeric_limits<float>::quiet_NaN();    ///< What the foot of the plot is: the least value, when not given.
        float max = std::numeric_limits<float>::quiet_NaN();    ///< What its top is: the greatest value, when not given.
        glm::vec2 size { -1.f, 60.f };                          ///< Negative: as much as it is given room for.
        std::string overlay;                                    ///< Written over it, along its top: "16.6 ms".
        Color color = colors::Transparent;                      ///< The theme's accent, when not given.

        PlotOptions& SetKind(PlotKind value) { kind = value; return *this; }
        PlotOptions& SetRange(float low, float high) { min = low; max = high; return *this; }
        PlotOptions& SetSize(glm::vec2 value) { size = value; return *this; }
        PlotOptions& SetOverlay(std::string value) { overlay = std::move(value); return *this; }
        PlotOptions& SetColor(Color value) { color = value; return *this; }
    };
    /** @brief @p values drawn as a line through them, or as bars: frame times, a histogram. */
    KUI_API Widget Plot(std::vector<float> values, PlotOptions options = {});

    struct TableColumn {
        std::string title;
        float width = -1.f;                 ///< Negative: a share of what the fixed columns leave, by @p flex.
        float flex = 1.f;
    };
    struct TableOptions {
        bool header = true;                 ///< A first row of the columns' titles.
        bool striped = true;                ///< Every other row a shade darker.
        bool borders = true;                ///< Lines between rows and columns, and round the table.
        float rowHeight = -1.f;             ///< Negative: the theme's.

        TableOptions& SetHeader(bool value) { header = value; return *this; }
        TableOptions& SetStriped(bool value) { striped = value; return *this; }
        TableOptions& SetBorders(bool value) { borders = value; return *this; }
        TableOptions& SetRowHeight(float value) { rowHeight = value; return *this; }
    };
    /** @brief Rows of cells under columns that line up: @p rows[r][c] is what row r shows in column c. */
    KUI_API Widget Table(std::vector<TableColumn> columns, std::vector<std::vector<Widget>> rows, TableOptions options = {});

    struct StepSliderOptions {
        std::vector<std::string> labels;    ///< Written in the steps, one each; a step with none shows nothing.
        float width = -1.f;                 ///< Negative: as wide as it is given room for (240 where that has no end).

        StepSliderOptions& SetLabels(std::vector<std::string> value) { labels = std::move(value); return *this; }
        StepSliderOptions& SetWidth(float value) { width = value; return *this; }
    };
    /**
     * @brief A slider that stops only at its steps: a wide rounded track of @p steps places, and in it a
     *        rounded thumb as wide as one of them, at @p value (from 0). Pressed or dragged, it tells
     *        @p onChanged the step under the pointer.
     */
    KUI_API Widget StepSlider(int value, int steps, std::function<void(int)> onChanged, StepSliderOptions options = {});

    struct GradientEditorOptions {
        float width = 260.f;
        bool picker = true;                 ///< A ColorPicker under the bar, for the stop that is picked.

        GradientEditorOptions& SetWidth(float value) { width = value; return *this; }
        GradientEditorOptions& SetPicker(bool value) { picker = value; return *this; }
    };
    /**
     * @brief Edits a gradient's stops: a bar showing it, and under the bar a handle for each stop.
     *
     * A handle is picked by pressing it and moved by dragging it; a press on the bar where there is none
     * adds a stop there, of the colour the gradient has there; the right button on a handle (or the
     * Remove button) takes its stop away, while more than two are left. The picker under the bar changes
     * the colour of the stop that is picked. @p onChanged hears the stops, in order, after every change.
     * A gradient has at most eight.
     */
    KUI_API Widget GradientEditor(std::vector<GradientStop> stops, std::function<void(std::vector<GradientStop>)> onChanged,
                                  GradientEditorOptions options = {});

    struct TitleBarOptions {
        Widget leading;                     ///< At its left end, before the title: an icon, a MenuBar.
        Widget trailing;                    ///< Before the window's buttons: a search field, an account.
        float height = 36.f;
        bool buttons = true;                ///< The window's own three: minimize, maximize or restore, close. (On macOS the system's, always, at the left.)

        TitleBarOptions& SetLeading(Widget value) { leading = std::move(value); return *this; }
        TitleBarOptions& SetTrailing(Widget value) { trailing = std::move(value); return *this; }
        TitleBarOptions& SetHeight(float value) { height = value; return *this; }
        TitleBarOptions& SetButtons(bool value) { buttons = value; return *this; }
    };
    /**
     * @brief The window's title bar, drawn by the interface in place of the system's — a row along the
     *        top of the window, in the theme's background: @p leading, the title, room that moves the
     *        window when dragged (and maximizes it when pressed twice), @p trailing, and the window's
     *        buttons.
     *
     * Showing one is what takes the system's title bar away (kor::Window::SetCustomTitleBar): put it
     * first in a Column that fills the window. The window is still resized by its edges and snapped
     * by the system. In a view with no window of its own it is only a row.
     */
    KUI_API Widget TitleBar(std::string title, TitleBarOptions options = {});

    enum class StatusLevel : std::uint8_t { eInfo, eWarning, eError };
    struct StatusBarOptions {
        Widget trailing;                    ///< At its right end: a frame rate, a progress bar.
        float height = 26.f;

        StatusBarOptions& SetTrailing(Widget value) { trailing = std::move(value); return *this; }
        StatusBarOptions& SetHeight(float value) { height = value; return *this; }
    };
    /**
     * @brief A bar along the foot of a window showing the last thing that was said — a line of text
     *        after a mark of its @p level, in the level's colour — as an editor's does. It is the colour
     *        of the window behind the docked panels (the theme's background), and nothing in it is pressed.
     */
    KUI_API Widget StatusBar(std::string message, StatusLevel level = StatusLevel::eInfo, StatusBarOptions options = {});

    struct TextFieldOptions {
        std::string text;                                   ///< What it starts with.
        std::string placeholder;
        std::function<void(const std::string&)> onChanged;
        std::function<void(const std::string&)> onSubmitted;   ///< Enter.
        float width = -1.f;                                 ///< Negative: 200 units, or as wide as a stretching parent makes it.
        /// True: it always shows `text`, as a controlled field — what is typed reaches onChanged, and shows
        /// once it comes back as `text`. False: `text` is only what it starts with.
        bool controlled = false;
        /// Several lines: Enter starts another, Up and Down move between them, and the text wraps at the field's
        /// width. It is as tall as its text, from minLines to maxLines, and scrolls past that.
        bool multiline = false;
        int minLines = 1, maxLines = 1;
        /// Takes the keyboard when this is not what it last was (and is not 0): how something else gives it the focus.
        std::uint32_t focus = 0;
        /// When it lets go of the keyboard — clicked away from, tabbed out of — with the text it has. Not
        /// after Escape: that is onEscape's.
        std::function<void(const std::string&)> onFocusLost;
        /// Escape, before it lets go of the keyboard: what an edit that can be called off is called off by.
        std::function<void()> onEscape;
        /// Everything in it is selected when it takes the keyboard, so what is typed replaces it.
        bool selectAllOnFocus = false;

        // Chainable: `kui::TextFieldOptions{}.Set...(...).Set...(...)`.
        TextFieldOptions& SetText(std::string value) { text = std::move(value); return *this; }
        TextFieldOptions& SetPlaceholder(std::string value) { placeholder = std::move(value); return *this; }
        TextFieldOptions& SetWidth(float value) { width = value; return *this; }
        TextFieldOptions& SetControlled(bool value) { controlled = value; return *this; }
        TextFieldOptions& SetMultiline(int least, int most) { multiline = true; minLines = least; maxLines = most; return *this; }
        TextFieldOptions& OnChanged(std::function<void(const std::string&)> f) { onChanged = std::move(f); return *this; }
        TextFieldOptions& OnSubmitted(std::function<void(const std::string&)> f) { onSubmitted = std::move(f); return *this; }
    };
    /**
     * @brief Editable text: one line, or (multiline) several. Shift with the arrows, or a drag, selects;
     *        Control+A, C, X and V select all, copy, cut and paste; Tab goes to the next field.
     */
    KUI_API Widget TextField(TextFieldOptions options);

    // ---- the view ---------------------------------------------------------------------------------------

    struct UiSettings {
        Theme theme {};
        float scale = 1.f;                      ///< How big the interface is drawn: 1 is its natural size, whatever the display's density.
        /// Whether the window the interface is shown in has its title bar coloured to go with it: the
        /// theme's background behind the theme's text (kor::Window::SetTitleBarColors), where the system can.
        bool titleBar = true;

        // Chainable: `kui::UiSettings{}.Set...(...).Set...(...)`.
        UiSettings& SetTitleBar(bool value) { titleBar = value; return *this; }
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
        /**
         * @brief Pixels per logical unit, as the view is drawn: its scale times its window's pixels per screen
         *        coordinate (2 on a Retina display). What turns a place in the view into pixels of an image.
         */
        [[nodiscard]] float PixelScale() const;

        /** @brief The frame, with the current scene's input and window. */
        void Update();
        /** @brief The frame, with @p input, over a target of @p viewport pixels, @p dt seconds after the last. */
        void Update(kor::Input& input, glm::vec2 viewport, float dt);

        [[nodiscard]] Renderer& GetRenderer();
        /** @brief The render tree's root, after the first Update. */
        [[nodiscard]] RenderObject* RootRenderObject() const;
        /** @brief Whether the pointer is over a widget that takes it, or one is being dragged. */
        [[nodiscard]] bool WantsPointer() const;
        /** @brief What the pointer looks like over the interface: what is under it said so. The view shows it so in its window. */
        [[nodiscard]] PointerCursor Cursor() const;
        /** @brief Takes the keyboard from whatever has it. */
        void ClearFocus();
        /** @brief The tip showing by the pointer — a Tooltip's, while the pointer is over it — or nothing. */
        [[nodiscard]] const std::string& TooltipText() const;
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

    namespace debug {
        /**
         * @brief Every text @p ui shows now — labels, values, what is typed into fields — in the order of
         *        the tree: what a test reads an interface by.
         */
        [[nodiscard]] KUI_API std::vector<std::string> Texts(const Ui& ui);
    }
}
