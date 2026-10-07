//
// koral-ui: render objects — the retained tree that lays out, paints and is hit, under the widgets.
//

#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kmath/matrix.h>

#include <input.h>

namespace kor { class Window; }

#include "kuiApi.h"
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
        [[nodiscard]] constexpr kor::Vec2 Total() const { return { Horizontal(), Vertical() }; }
        constexpr bool operator==(const EdgeInsets&) const = default;
    };

    /**
     * @brief The sizes a parent allows a child, as Flutter has them: constraints go down, sizes come
     *        back up, and the parent decides where the child goes.
     */
    struct KUI_API BoxConstraints {
        float minWidth = 0.f, maxWidth = Infinity, minHeight = 0.f, maxHeight = Infinity;

        static constexpr BoxConstraints Tight(const kor::Vec2 size) { return { size.x, size.x, size.y, size.y }; }
        static constexpr BoxConstraints Loose(const kor::Vec2 size) { return { 0.f, size.x, 0.f, size.y }; }

        [[nodiscard]] kor::Vec2 Constrain(kor::Vec2 size) const;
        [[nodiscard]] constexpr bool IsTight() const { return minWidth >= maxWidth && minHeight >= maxHeight; }
        [[nodiscard]] constexpr bool HasBoundedWidth() const { return maxWidth < Infinity; }
        [[nodiscard]] constexpr bool HasBoundedHeight() const { return maxHeight < Infinity; }
        [[nodiscard]] constexpr kor::Vec2 Biggest() const { return { maxWidth, maxHeight }; }
        [[nodiscard]] constexpr kor::Vec2 Smallest() const { return { minWidth, minHeight }; }
        [[nodiscard]] constexpr BoxConstraints Loosen() const { return { 0.f, maxWidth, 0.f, maxHeight }; }
        /** @brief Room left once @p insets are taken off every side. */
        [[nodiscard]] BoxConstraints Deflate(const EdgeInsets& insets) const;
        /** @brief These, kept within @p outer. */
        [[nodiscard]] BoxConstraints Enforce(const BoxConstraints& outer) const;
        /** @brief Tight in whichever dimensions are given (non-negative), as these allow. */
        [[nodiscard]] BoxConstraints Tighten(float width = -1.f, float height = -1.f) const;
        constexpr bool operator==(const BoxConstraints&) const = default;
    };

    /** @brief Debugging aids. They are the process's: every interface in it shows them. */
    namespace debug {
        /**
         * @brief Whether every render object is outlined where it was laid out, over what it paints:
         *        what holds others in one colour, what holds nothing (a text, a control) in a fainter
         *        one, and what is painted into a layer of its own (a repaint boundary) in a third.
         *        For seeing where a container really is, and how big. Off by default.
         */
        KUI_API void SetPaintBounds(bool enabled);
        [[nodiscard]] KUI_API bool PaintBounds();
        /** @brief Changes each time a debugging aid does: what was painted before it has to be painted again. */
        [[nodiscard]] KUI_API unsigned Revision();
    }

    struct Theme;

    /** @brief What the pointer looks like: said by what is under it. @see Owner::cursor */
    enum class PointerCursor : std::uint8_t { eArrow, eResizeHorizontal, eResizeVertical, eResizeDiagonal, eHand, eText };

    /** @brief Where a pointer event happened, and what it was. */
    struct PointerEvent {
        enum class Type : std::uint8_t { eDown, eMove, eUp, eCancel, eHover, eScroll, eEnter, eExit };
        Type type = Type::eHover;
        kor::Vec2 position {};          ///< In the view's logical coordinates.
        kor::Vec2 local {};             ///< In the receiving render object's own coordinates.
        kor::Vec2 delta {};             ///< Movement since the last event, or the wheel's turn for eScroll.
        kor::MouseButton button = kor::MouseButton::eLeft;
        /// For eDown, which everything under the pointer hears, deepest first: whether something
        /// deeper has already taken this press (a button, a slider). What is behind can then leave it be.
        bool taken = false;
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
        virtual void DragMoved(const DragData& data, kor::Vec2 local) {}
        virtual void DragLeft() {}
        virtual void Dropped(const DragData& data, kor::Vec2 local) = 0;
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
        kor::Vec2 origin {};
        /// Where on the desktop @p origin is, in pixels, for a window that moves under the pointer: the
        /// pointer is then asked of the desktop (kor::Window::DesktopCursor), not of the window.
        std::optional<kor::IVec2> desktopOrigin;
    };

    /** @brief What a pointer is over, deepest first, with where it is in each. */
    struct HitTestResult {
        struct Entry { RenderObject* target; kor::Vec2 local; };
        std::vector<Entry> path;
        void Add(RenderObject* target, const kor::Vec2 local) { path.push_back({ target, local }); }
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
        /** @brief The text it shows, if it shows any: a paragraph's, a field's. What debug::Texts reads. */
        [[nodiscard]] virtual std::string DebugText() const { return {}; }
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
        [[nodiscard]] kor::Vec2 Size() const { return _size; }
        /** @brief Where the parent put it, in the parent's coordinates. */
        [[nodiscard]] kor::Vec2 Offset() const { return _offset; }
        void SetOffset(kor::Vec2 offset);
        void MarkNeedsLayout();
        [[nodiscard]] bool NeedsLayout() const { return _needsLayout; }
        /**
         * @brief The least width it can be laid out at with nothing it shows cut off — the longest word of a
         *        text, a row's children side by side, a column's widest — asked of it before it is laid out, by
         *        a parent that decides how wide it is (a dock keeps an area no narrower than its panel's).
         *        What can be any width says 0, as does anything that does not know: a leaf by default.
         */
        [[nodiscard]] virtual float MinIntrinsicWidth() const { return 0.f; }

        // -- painting
        /** @brief Paints itself and its children at @p offset. */
        virtual void Paint(Canvas& canvas, kor::Vec2 offset) {}
        /** @brief Paints @p child where it was placed — or shows its layer, if it keeps one. */
        void PaintChild(RenderObject& child, Canvas& canvas, kor::Vec2 offset);
        /** @brief Paints @p child with its top-left at @p at — for a parent whose own space is not its children's (a scrolled layer). */
        void PaintChildAt(RenderObject& child, Canvas& canvas, kor::Vec2 at);
        void MarkNeedsPaint();
        /** @brief Whether it keeps its own layer, so that repainting it repaints nothing else. */
        [[nodiscard]] virtual bool IsRepaintBoundary() const { return false; }
        /** @brief The layer of a repaint boundary (made on first use). */
        [[nodiscard]] const std::shared_ptr<Layer>& OwnLayer();
        /** @brief For a repaint boundary: records its picture again if it needs it. */
        void RepaintIfNeeded();
        /**
         * @brief For a repaint boundary: the part of it — in its own coordinates — that is looked at, so
         *        that what it holds outside that is left out of its picture. What scrolls it says so, and
         *        says again before another part comes into view. Empty: all of it is painted.
         */
        void SetPaintCull(const std::optional<Rect>& cull);
        [[nodiscard]] const std::optional<Rect>& PaintCull() const { return _cull; }

        // -- the pointer
        /** @brief Adds itself (and whatever under it is hit) to @p result when @p position, in its own coordinates, is over it. */
        virtual bool HitTest(HitTestResult& result, kor::Vec2 position);
        /** @brief The children's turn, topmost first. Returns whether one was hit. */
        virtual bool HitTestChildren(HitTestResult& result, kor::Vec2 position);
        /** @brief Whether it counts as hit at all at @p position — false lets the pointer through to what is below. */
        [[nodiscard]] virtual bool HitTestSelf(kor::Vec2 position) const { return false; }
        /** @brief A pointer event on it (or something in it). Return true to stop it going further up. */
        virtual bool HandleEvent(const PointerEvent& event) { return false; }
        /** @brief Where a child's coordinates start, in its own: how a scroll view shifts its content. */
        [[nodiscard]] virtual kor::Vec2 ChildOrigin(const RenderObject& child) const { return child.Offset(); }

        // -- the keyboard
        /** @brief Text typed while it has focus. */
        virtual void HandleText(std::u32string_view text) {}
        /** @brief A key went down (or repeated) while it has focus. Return true when used. */
        virtual bool HandleKey(kor::Key key, bool repeat) { return false; }
        /** @brief It gained or lost the keyboard focus. */
        virtual void FocusChanged(bool focused) {}
        /** @brief Each frame while it has the focus, @p dt seconds after the last: what blinks a caret. */
        virtual void FocusTick(float dt) {}
        /** @brief Whether the keyboard can be given to it: what Tab goes to, from one to the next. */
        [[nodiscard]] virtual bool Focusable() const { return false; }
        /** @brief The theme everything under it is built, laid out and painted with, when it sets one (Themed): null otherwise. */
        [[nodiscard]] virtual const Theme* ProvidedTheme() const { return nullptr; }
        /** @brief The theme set nearest above it, or null: the view's own applies. */
        [[nodiscard]] const Theme* InheritedTheme() const;

        /** @brief Where @p local, in its coordinates, is in the view's. */
        [[nodiscard]] kor::Vec2 ToGlobal(kor::Vec2 local) const;
        /** @brief Where @p global — a point of the view — is in this object's own coordinates: through whatever moves or transforms it. */
        [[nodiscard]] kor::Vec2 ToLocal(kor::Vec2 global) const;
        /** @brief A point of this object's, in @p child 's coordinates. What draws a child anywhere but at its offset says where. */
        [[nodiscard]] virtual kor::Vec2 MapToChild(const RenderObject& child, const kor::Vec2 point) const { return point - ChildOrigin(child); }
        /** @brief How deep in the tree it is: the root is 0. */
        [[nodiscard]] int Depth() const;

    protected:
        /** @brief Sets the size (kept within the constraints) and lays out the children. */
        virtual void PerformLayout() = 0;
        void SetSize(kor::Vec2 size);
        /** @brief Whether its size depends on nothing but its constraints (so a change below does not move its parent). */
        [[nodiscard]] virtual bool SizedByParent() const { return false; }

    private:
        friend class Owner;
        RenderObject* _parent = nullptr;
        Owner* _owner = nullptr;
        BoxConstraints _constraints {};
        kor::Vec2 _size {}, _offset {};
        RenderObject* _relayoutBoundary = nullptr;
        bool _needsLayout = true, _needsPaint = true, _hasLaidOut = false;
        std::shared_ptr<Layer> _layer;
        std::optional<Rect> _cull;
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
        /** @brief Screen coordinates per logical unit: what the desktop (window positions) is measured in.
         *  The same as scale except on a scaled display (Retina, Wayland), where a screen coordinate is several pixels. */
        float desktopScale = 1.f;
        /** @brief The window the view is drawn in, when it is updated with a scene's (Ui::Update()): null otherwise. */
        const kor::Window* window = nullptr;
        /**
         * @brief Starts a drag of @p data from @p source: @p feedback follows the pointer (held at
         *        @p hotspot inside it), drop receivers are offered it, and @p onEnd hears whether one took it.
         *        Returns false when a drag is already going. Set by the view.
         */
        std::function<bool(RenderObject& source, DragData data, const Widget& feedback, kor::Vec2 hotspot,
                           std::function<void(bool accepted)> onEnd)> beginDrag;
        /**
         * @brief Shows @p popup over everything else in the view — a menu, a dropdown's list — with its
         *        top-left at @p at, in the view's coordinates. @p size is how big it will be, so that it
         *        can be kept inside the view. A press anywhere outside it, or Escape, closes it; so does
         *        closePopup, which whatever is in it calls once it has been used. One at a time: showing
         *        another replaces it. Set by the view.
         */
        std::function<void(const Widget& popup, kor::Vec2 at, kor::Vec2 size)> showPopup;
        std::function<void()> closePopup;
        /// Asks that everything be built again, from the next frame: what it was built from — a theme set for
        /// part of the view — has changed. Set by the view.
        std::function<void()> reassemble;
        /**
         * @brief What the pointer should look like. The view makes it an arrow before it tells what is
         *        under the pointer that the pointer moved (eHover); whatever wants another shape there —
         *        the line between two panes, to say it can be dragged — sets it then; and the view shows
         *        the pointer so, in whichever window it is in.
         */
        PointerCursor cursor = PointerCursor::eArrow;
        /**
         * @brief Holds the pointer where it is, unseen, for a drag that has no use for where the pointer
         *        goes — a number dragged up or down — and (false) lets it go again, where it was. While it
         *        is held, moves still arrive (eMove), with how far the hand went in their delta and no
         *        end to how far that can be. The view lets go by itself when the button is. Set by the view.
         */
        std::function<void(bool)> lockPointer;
        /**
         * @brief What the view shows by the pointer: a tip about what is under it. Emptied and said
         *        afresh as the cursor is — whatever has a tip sets it when told the pointer moved over it
         *        (the innermost: one around it leaves a tip already said alone).
         */
        std::string tooltip;
        /// Told when the popup is closed by anything — a press outside it, Escape, another popup — and then
        /// forgotten: whoever showed it sets this right after, to hear that it went.
        std::function<void()> onPopupClosed;
        /// Shift and Control, as the keyboard has them while a key is handed to what has the keyboard.
        bool shift = false, control = false;
        /// The system's clipboard, for what copies and pastes. Set by the view; empty where there is none.
        std::function<std::string()> clipboardText;
        std::function<void(const std::string&)> setClipboardText;

    private:
        std::vector<RenderObject*> _layout, _paint;
        RenderObject* _focused = nullptr;
    };
}
