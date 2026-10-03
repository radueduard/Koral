//
// koral-ui: render objects — the retained tree that lays out, paints and is hit, under the widgets.
//

#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <input.h>

namespace kor { class Window; }

#include "api.h"
#include "canvas.h"

namespace kui
{
    class RenderObject;
    class Owner;
    class Widget;

    inline constexpr float Infinity = std::numeric_limits<float>::infinity();

    /** @brief Space around something, per side. */
    struct EdgeInsets {
        float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;
        static constexpr EdgeInsets All(const float v) { return { v, v, v, v }; }
        static constexpr EdgeInsets Symmetric(const float horizontal, const float vertical) { return { horizontal, vertical, horizontal, vertical }; }
        static constexpr EdgeInsets Only(const float l, const float t, const float r, const float b) { return { l, t, r, b }; }
        [[nodiscard]] constexpr float Horizontal() const { return left + right; }
        [[nodiscard]] constexpr float Vertical() const { return top + bottom; }
        [[nodiscard]] constexpr glm::vec2 Total() const { return { Horizontal(), Vertical() }; }
        constexpr bool operator==(const EdgeInsets&) const = default;
    };

    /**
     * @brief The sizes a parent allows a child, as Flutter has them: constraints go down, sizes come
     *        back up, and the parent decides where the child goes.
     */
    struct KUI_API BoxConstraints {
        float minWidth = 0.f, maxWidth = Infinity, minHeight = 0.f, maxHeight = Infinity;

        static constexpr BoxConstraints Tight(const glm::vec2 size) { return { size.x, size.x, size.y, size.y }; }
        static constexpr BoxConstraints Loose(const glm::vec2 size) { return { 0.f, size.x, 0.f, size.y }; }

        [[nodiscard]] glm::vec2 Constrain(glm::vec2 size) const;
        [[nodiscard]] constexpr bool IsTight() const { return minWidth >= maxWidth && minHeight >= maxHeight; }
        [[nodiscard]] constexpr bool HasBoundedWidth() const { return maxWidth < Infinity; }
        [[nodiscard]] constexpr bool HasBoundedHeight() const { return maxHeight < Infinity; }
        [[nodiscard]] constexpr glm::vec2 Biggest() const { return { maxWidth, maxHeight }; }
        [[nodiscard]] constexpr glm::vec2 Smallest() const { return { minWidth, minHeight }; }
        [[nodiscard]] constexpr BoxConstraints Loosen() const { return { 0.f, maxWidth, 0.f, maxHeight }; }
        /** @brief Room left once @p insets are taken off every side. */
        [[nodiscard]] BoxConstraints Deflate(const EdgeInsets& insets) const;
        /** @brief These, kept within @p outer. */
        [[nodiscard]] BoxConstraints Enforce(const BoxConstraints& outer) const;
        /** @brief Tight in whichever dimensions are given (non-negative), as these allow. */
        [[nodiscard]] BoxConstraints Tighten(float width = -1.f, float height = -1.f) const;
        constexpr bool operator==(const BoxConstraints&) const = default;
    };

    /** @brief Where a pointer event happened, and what it was. */
    struct PointerEvent {
        enum class Type : std::uint8_t { eDown, eMove, eUp, eCancel, eHover, eScroll, eEnter, eExit };
        Type type = Type::eHover;
        glm::vec2 position {};          ///< In the view's logical coordinates.
        glm::vec2 local {};             ///< In the receiving render object's own coordinates.
        glm::vec2 delta {};             ///< Movement since the last event, or the wheel's turn for eScroll.
        kor::MouseButton button = kor::MouseButton::eLeft;
    };

    /**
     * @brief What a drag carries: its kind (which targets go by: "color", "asset", "file") and the thing itself.
     */
    struct DragData {
        std::string type;
        std::any payload;
        /** @brief The payload as a @p T, or null when it is something else. */
        template <typename T> [[nodiscard]] const T* As() const { return std::any_cast<T>(&payload); }
    };

    /**
     * @brief A render object a drag can be dropped on. The view asks the deepest one under the pointer
     *        that accepts the drag, tells it as the pointer comes, moves and goes, and gives it the drop.
     */
    class KUI_API DropReceiver {
    public:
        virtual ~DropReceiver() = default;
        [[nodiscard]] virtual bool AcceptsDrag(const DragData& data) const = 0;
        virtual void DragEntered(const DragData& data) {}
        virtual void DragMoved(const DragData& data, glm::vec2 local) {}
        virtual void DragLeft() {}
        virtual void Dropped(const DragData& data, glm::vec2 local) = 0;
    };

    /**
     * @brief Part of a render tree shown in a window of its own: where its pointer and keys come from,
     *        and where in @ref root 's coordinates that window's top-left is. Registered with the Owner
     *        by the render object that presents itself there (a dock area's floating panels).
     */
    struct PointerViewport {
        kor::Input* input = nullptr;
        const kor::Window* window = nullptr;
        RenderObject* root = nullptr;
        glm::vec2 origin {};
    };

    /** @brief What a pointer is over, deepest first, with where it is in each. */
    struct HitTestResult {
        struct Entry { RenderObject* target; glm::vec2 local; };
        std::vector<Entry> path;
        void Add(RenderObject* target, const glm::vec2 local) { path.push_back({ target, local }); }
    };

    /**
     * @brief A node of the render tree: something with a size, laid out by its parent under
     *        constraints, painted into a canvas, and hit by the pointer.
     *
     * A widget makes and updates its render object; projects only write one for a new kind of layout
     * or painting. Override PerformLayout (set the size, lay out and place the children), Paint, and —
     * when it has children — VisitChildren.
     *
     * Work is only redone where something changed: MarkNeedsLayout stops at the nearest *relayout
     * boundary* (a node whose size cannot change its parent's layout), and MarkNeedsPaint at the
     * nearest *repaint boundary*, which keeps its own Layer — re-recording one picture, or none when
     * only a layer moved.
     */
    class KUI_API RenderObject {
    public:
        RenderObject();
        virtual ~RenderObject();
        RenderObject(const RenderObject&) = delete;
        RenderObject& operator=(const RenderObject&) = delete;

        // -- the tree
        [[nodiscard]] RenderObject* Parent() const { return _parent; }
        /** @brief Every child, in paint order. */
        virtual void VisitChildren(const std::function<void(RenderObject&)>& visit) {}
        /** @brief For a parent: adopts @p child (the parent keeps its own list). */
        void AdoptChild(RenderObject& child);
        void DropChild(RenderObject& child);
        void Attach(Owner* owner);
        void Detach();
        /** @brief A child of it is being destroyed: forget it. */
        virtual void ChildDestroyed(RenderObject& child) {}
        [[nodiscard]] Owner* GetOwner() const { return _owner; }

        // -- layout
        /** @brief Lays out under @p constraints; nothing happens when neither they nor the subtree changed. */
        void Layout(const BoxConstraints& constraints, bool parentUsesSize = true);
        [[nodiscard]] const BoxConstraints& Constraints() const { return _constraints; }
        [[nodiscard]] glm::vec2 Size() const { return _size; }
        /** @brief Where the parent put it, in the parent's coordinates. */
        [[nodiscard]] glm::vec2 Offset() const { return _offset; }
        void SetOffset(glm::vec2 offset);
        void MarkNeedsLayout();
        [[nodiscard]] bool NeedsLayout() const { return _needsLayout; }

        // -- painting
        /** @brief Paints itself and its children at @p offset. */
        virtual void Paint(Canvas& canvas, glm::vec2 offset) {}
        /** @brief Paints @p child where it was placed — or shows its layer, if it keeps one. */
        void PaintChild(RenderObject& child, Canvas& canvas, glm::vec2 offset);
        /** @brief Paints @p child with its top-left at @p at — for a parent whose own space is not its children's (a scrolled layer). */
        void PaintChildAt(RenderObject& child, Canvas& canvas, glm::vec2 at);
        void MarkNeedsPaint();
        /** @brief Whether it keeps its own layer, so that repainting it repaints nothing else. */
        [[nodiscard]] virtual bool IsRepaintBoundary() const { return false; }
        /** @brief The layer of a repaint boundary (made on first use). */
        [[nodiscard]] const std::shared_ptr<Layer>& OwnLayer();
        /** @brief For a repaint boundary: records its picture again if it needs it. */
        void RepaintIfNeeded();

        // -- the pointer
        /** @brief Adds itself (and whatever under it is hit) to @p result when @p position, in its own coordinates, is over it. */
        virtual bool HitTest(HitTestResult& result, glm::vec2 position);
        /** @brief The children's turn, topmost first. Returns whether one was hit. */
        virtual bool HitTestChildren(HitTestResult& result, glm::vec2 position);
        /** @brief Whether it counts as hit at all at @p position — false lets the pointer through to what is below. */
        [[nodiscard]] virtual bool HitTestSelf(glm::vec2 position) const { return false; }
        /** @brief A pointer event on it (or something in it). Return true to stop it going further up. */
        virtual bool HandleEvent(const PointerEvent& event) { return false; }
        /** @brief Where a child's coordinates start, in its own: how a scroll view shifts its content. */
        [[nodiscard]] virtual glm::vec2 ChildOrigin(const RenderObject& child) const { return child.Offset(); }

        // -- the keyboard
        /** @brief Text typed while it has focus. */
        virtual void HandleText(std::u32string_view text) {}
        /** @brief A key went down (or repeated) while it has focus. Return true when used. */
        virtual bool HandleKey(kor::Key key, bool repeat) { return false; }
        /** @brief It gained or lost the keyboard focus. */
        virtual void FocusChanged(bool focused) {}
        /** @brief Each frame while it has the focus, @p dt seconds after the last: what blinks a caret. */
        virtual void FocusTick(float dt) {}

        /** @brief Where @p local, in its coordinates, is in the view's. */
        [[nodiscard]] glm::vec2 ToGlobal(glm::vec2 local) const;
        /** @brief How deep in the tree it is: the root is 0. */
        [[nodiscard]] int Depth() const;

    protected:
        /** @brief Sets the size (kept within the constraints) and lays out the children. */
        virtual void PerformLayout() = 0;
        void SetSize(glm::vec2 size);
        /** @brief Whether its size depends on nothing but its constraints (so a change below does not move its parent). */
        [[nodiscard]] virtual bool SizedByParent() const { return false; }

    private:
        friend class Owner;
        RenderObject* _parent = nullptr;
        Owner* _owner = nullptr;
        BoxConstraints _constraints {};
        glm::vec2 _size {}, _offset {};
        RenderObject* _relayoutBoundary = nullptr;
        bool _needsLayout = true, _needsPaint = true, _hasLaidOut = false;
        std::shared_ptr<Layer> _layer;
    };

    /**
     * @brief What keeps a render tree current: the objects waiting for layout and paint, flushed once a
     *        frame, and who has the keyboard.
     */
    class KUI_API Owner {
    public:
        void ScheduleLayout(RenderObject& boundary);
        void SchedulePaint(RenderObject& boundary);
        void Forget(RenderObject& object);
        /** @brief Lays out every relayout boundary waiting for it, shallowest first. */
        void FlushLayout();
        /** @brief Records every repaint boundary waiting for it. */
        void FlushPaint();
        [[nodiscard]] bool HasWork() const { return !_layout.empty() || !_paint.empty(); }

        void RequestFocus(RenderObject* object);
        [[nodiscard]] RenderObject* Focused() const { return _focused; }

        std::size_t layouts = 0, paints = 0;   ///< Counted since the last ResetCounters, for tests and stats.
        /** @brief Told of every render object leaving the tree, so whoever keeps pointers to them can let go. */
        std::function<void(RenderObject&)> onForget;
        void ResetCounters() { layouts = paints = 0; }

        /** @brief Windows other than the view's own that show part of the tree. @see PointerViewport */
        std::vector<PointerViewport> viewports;
        /** @brief Pixels per logical unit of the view: what a render object sizing a window of its own needs. */
        float scale = 1.f;
        /** @brief The window the view is drawn in, when it is updated with a scene's (Ui::Update()): null otherwise. */
        const kor::Window* window = nullptr;
        /**
         * @brief Starts a drag of @p data from @p source: @p feedback follows the pointer (held at
         *        @p hotspot inside it), drop receivers are offered it, and @p onEnd hears whether one took it.
         *        Returns false when a drag is already going. Set by the view.
         */
        std::function<bool(RenderObject& source, DragData data, const Widget& feedback, glm::vec2 hotspot,
                           std::function<void(bool accepted)> onEnd)> beginDrag;

    private:
        std::vector<RenderObject*> _layout, _paint;
        RenderObject* _focused = nullptr;
    };
}
