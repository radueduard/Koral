//
// koral-ui: docking, in the manner of the JetBrains IDEs' tool windows — panels in areas round the
// edge of a space, opened from buttons in a stripe down each side; floated; and pulled out of the
// window, over the desktop.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "icons.h"
#include "widgets.h"

namespace kui
{
    /**
     * @brief Where a docked panel is: one of the areas round the edge of the space, or its middle.
     *
     * Down each side an area that can be in several parts, one over the other; along the bottom, from
     * one side to the other and under both, an area in two parts, left and right; and the middle,
     * which is whatever is left — the scene, where nothing is docked in it.
     */
    enum class DockArea : std::uint8_t { eLeft, eRight, eBottomLeft, eBottomRight, eCenter };

    /** @brief A side of a panel or of the space, for DockLayout::Dock(panel, side, relativeTo, fraction). */
    enum class DockSide : std::uint8_t { eLeft, eRight, eTop, eBottom, eCenter };

    /** @brief A panel of a dock space. */
    struct DockPanel {
        std::string id;         ///< What the layout knows it by: the same from build to build.
        std::string title;      ///< In its title bar; what follows the pointer while it is dragged; and where its button's letter comes from.
        Widget content;
        bool closable = true;   ///< Whether its title bar has a button that closes it.
        /// False: it never docks. It floats, wherever the layout says and whatever is dragged where.
        bool dockable = true;
        /// Open, a panel has a title bar: a sliver along its top with its title on the left and its
        /// buttons on the right. False: while it floats it has no title bar, frame or surface — only
        /// its content, at the size the layout gave it. It is moved by dragging its content: the
        /// padding, and whatever else of it takes no press (a button or a slider in it still works as one).
        bool showTitleBar = true;
        /// What its button in the stripe shows, in the colour the button's state gives it: `kui::MaterialIcon("tune")`,
        /// or any SVG's. None: the first letter of its title.
        std::shared_ptr<const VectorImage> icon;
        /// What its title bar shows between its title and its buttons — a toolbar, say: given that room's width,
        /// and as tall as it likes (the bar grows to fit it). Where it takes no press, pressing it picks the panel
        /// up as the rest of the bar does. Empty: nothing there.
        Widget titleBar;
    };

    /**
     * @brief How a dock space is arranged: which panels are in which area, which of them are open, how
     *        big the areas are, what floats and what is closed. It lives outside the widgets — make
     *        one, keep it (a member of the scene or of a stateful widget), and give it to DockSpace
     *        each build; dragging changes it, and Save/Load carry it between runs.
     *
     * @code
     * auto layout = std::make_shared<kui::DockLayout>();
     * layout->Dock("project", kui::DockArea::eLeft)
     *        .Dock("structure", kui::DockArea::eLeft, 1)       // a second part of the left side, under the first
     *        .Dock("inspector", kui::DockArea::eRight)
     *        .Dock("log", kui::DockArea::eBottomLeft)
     *        .Dock("problems", kui::DockArea::eBottomRight);
     * @endcode
     */
    class KUI_API DockLayout {
    public:
        DockLayout();
        ~DockLayout();
        DockLayout(const DockLayout&) = delete;
        DockLayout& operator=(const DockLayout&) = delete;

        /**
         * @brief Docks @p panel in @p area, and opens it there — in front of whatever of that area was
         *        open. Down a side, @p part says which part of it, counted from the top; a part that is
         *        not there yet is made. A panel the layout has not placed yet goes there when the dock
         *        space first shows it; one already placed moves now.
         */
        DockLayout& Dock(std::string panel, DockArea area, int part = 0);
        /**
         * @brief Docks @p panel by a side: eLeft (and eTop) down the left, eRight down the right, eBottom
         *        along the bottom — its right part when @p relativeTo is on the right, its left
         *        otherwise — and eCenter in the middle, or in the same group as @p relativeTo when that
         *        is given. @p fraction is the share of the space the area takes, when the panel is the
         *        first in it.
         */
        DockLayout& Dock(std::string panel, DockSide side = DockSide::eCenter, std::string relativeTo = {}, float fraction = 0.25f);
        /** @brief Floats @p panel over the dock space, at @p rect in the space's coordinates. */
        DockLayout& Float(std::string panel, Rect rect);
        /**
         * @brief Floats @p panel over the dock space at @p at, as big as what it shows: its content says
         *        how big it wants to be (with as much room as the space has), and the float is that — and
         *        its bar and frame, when it has them. It follows its content when that changes size, and
         *        has no corner to resize it by.
         */
        DockLayout& Float(std::string panel, glm::vec2 at = { 40.f, 40.f });

        /**
         * @brief Floats @p panel outside the window, over the desktop, @p size big, where the dock space
         *        can (DockOptions::multiViewport); it floats inside the space where it cannot.
         */
        DockLayout& PopOut(std::string panel, glm::vec2 size = { 480.f, 360.f });

        /** @brief Takes @p panel away, button and all; it keeps its place for when it is opened again. */
        void Close(const std::string& panel);
        void Open(const std::string& panel);
        [[nodiscard]] bool IsOpen(const std::string& panel) const;
        /** @brief Shows @p panel in its area, in front of whatever of that area was shown. */
        void Activate(const std::string& panel);
        /** @brief Folds @p panel 's area away, when it is the one shown there: its button stays in the stripe. */
        void Hide(const std::string& panel);
        /** @brief Whether @p panel is to be seen: floating, or the one its area shows — and not closed. */
        [[nodiscard]] bool IsShown(const std::string& panel) const;
        /** @brief Whether @p panel floats: over the space, or outside the window over the desktop. */
        [[nodiscard]] bool IsFloating(const std::string& panel) const;

        /** @brief The arrangement as text, for a settings file. */
        [[nodiscard]] std::string Save() const;
        /** @brief Takes an arrangement Save gave. False (and unchanged) when @p text is not one. */
        bool Load(std::string_view text);

        struct Impl;
        [[nodiscard]] Impl& Internal() const { return *_impl; }

    private:
        std::unique_ptr<Impl> _impl;
    };

    /** @brief The sizes a dock space is drawn and handled with. Every one has the value it always had. */
    struct DockStyle {
        float titleBarHeight = 28.f;    ///< A panel's title bar, and the row of tabs in the middle.
        float stripeWidth = 38.f;       ///< A stripe of buttons down a side, before DockOptions::stripeGap is added to it.
        float buttonSize = 30.f;        ///< A stripe's square buttons. They shrink when a stripe has more than fit.
        float buttonGap = 4.f;          ///< Between one button of a stripe and the next.
        float separatorGap = 9.f;       ///< Between the buttons of one level of a side and the next, where the line is.
        float tabPadding = 10.f;        ///< Either side of a title, in a title bar and in a tab.
        float resizeGrip = 14.f;        ///< The corner of a float that resizes it.
        float minFloatSize = 120.f;     ///< The least a float can be dragged down to, each way.
        float minAreaSize = 24.f;       ///< The least an area round the edge can be dragged down to.
        float radius = 10.f;            ///< The corners of a docked panel: each is an island.
        float edgeDropMargin = 0.15f;   ///< How much of the space, from its left, right and bottom edges, docks a panel dropped there on that side.
        float centerDropSize = 0.30f;   ///< How much of the middle, about its centre, docks a panel dropped there in the middle.
        float underDropStart = 0.60f;   ///< From how far down a side's panel a drop goes under it, as a level of its own, rather than joining it.
    };

    struct DockOptions {
        /// Whether a panel can leave the window: it floats over the desktop, in one see-through window
        /// with no frame that covers every monitor, stays above everything, and lets the pointer through
        /// to what is behind wherever no panel is. A float is drawn there while it is being moved, in
        /// front of everything; put down, it is in the space when all of it is inside the window, and
        /// stays over the desktop otherwise. The window over the desktop opens when it is first needed
        /// and closes a while after the last panel has left it. Where there is no application to open it
        /// in, where windows cannot be placed (Wayland), or where the display shows no see-through
        /// window, panels stay in the space.
        bool multiViewport = true;
        /// The space between two areas that are next to each other — which is also the line that is
        /// dragged to resize them: over it the pointer shows which way it goes.
        float gap = 6.f;
        /// Space added round a stripe's buttons, half on each side of them: they are as far from the edge of
        /// the window as from the panels beside them.
        float stripeGap = 3.f;
        DockStyle style {};
        std::function<void(const std::string& panel)> onClosed;   ///< A panel's close button was clicked.
        std::function<void()> onChanged;                          ///< The arrangement changed: when to save it.

        // Chainable: `kui::DockOptions{}.SetMultiViewport(false).OnChanged(...)`.
        DockOptions& SetMultiViewport(bool value) { multiViewport = value; return *this; }
        DockOptions& SetGap(float value) { gap = value; return *this; }
        DockOptions& SetStripeGap(float value) { stripeGap = value; return *this; }
        DockOptions& SetStyle(DockStyle value) { style = value; return *this; }
        DockOptions& OnClosed(std::function<void(const std::string&)> f) { onClosed = std::move(f); return *this; }
        DockOptions& OnChanged(std::function<void()> f) { onChanged = std::move(f); return *this; }
    };

    /**
     * @brief A space of docked panels, filling what it is given — arranged as the tool windows of the
     *        JetBrains IDEs are.
     *
     * Down each side of the space is a stripe of square buttons, one glyph each: a docked panel's.
     * Pressing a button opens its panel in its area — in front of whichever of that area was open —
     * and pressing the button of the panel that is open folds the area away. The button of each open
     * panel is in the accent.
     *
     *  - The buttons from the top of a stripe are that side's panels: they open down that side of the
     *    space. A side can be in several parts, one over the other, each showing one panel; a line
     *    between the buttons separates one part's from the next's.
     *  - The buttons at the foot of a stripe are the bottom's: they open along the bottom of the space,
     *    which runs from one stripe to the other, under both sides — the left stripe's in its left
     *    part, the right stripe's in its right.
     *  - The middle is whatever the sides and the bottom leave. With nothing docked in it it is empty:
     *    it shows what is behind the space, the scene, and lets the pointer through. Panels docked
     *    there (DockArea::eCenter, or dragged into it) are tabs, with no buttons in the stripes: one is
     *    brought to the front by its title, and taken out by dragging its title away.
     *
     * An open panel has a title bar along its top: its title on the left; on the right a button that
     * folds it away and, when it is closable, one that closes it; between them, whatever the panel's
     * DockPanel::titleBar is. Between two areas is a gap
     * (DockOptions::gap): drag it to resize them — over it the pointer turns into the arrows that say
     * which way.
     *
     * A panel is moved by dragging its button, or its title:
     *  - onto a stripe, among the buttons where it is let go: between a part's, into that part; on the
     *    line between two parts or under the last, into a part of its own; at the stripe's foot, into
     *    that end of the bottom;
     *  - onto a panel that is docked round the edge, into that panel's group, in front of it — or,
     *    down a side and over the lower part of the panel, under it, as a part of its own;
     *  - into a margin of the space — within 15% of its left, its right or its bottom, between the
     *    stripes — into the area on that side. A side's margin is in bands, one for each of the side's
     *    levels and one more: over a level's band the panel joins that level, over the last it is a
     *    level of its own under them all. The bottom's margin is its two parts, left and right;
     *  - into the middle — onto its title bar, or within the 30% about its centre — to dock it there, a
     *    tab among whatever else is in the middle;
     *  - anywhere else in the space, to float there; and out of the window, to float over the desktop.
     *
     * A float is moved by its title bar, in the window and out of it alike, and its bar ends in a
     * button that docks it back. A panel keeps its state wherever it goes — it is the same widget,
     * shown somewhere else.
     *
     * @code
     * kui::DockSpace(_layout, {
     *     { "project", "Project", ProjectTree() },
     *     { "inspector", "Inspector", kui::Make<Inspector>() },
     *     { "log", "Log", LogView(), false },
     * })
     * @endcode
     */
    KUI_API Widget DockSpace(std::shared_ptr<DockLayout> layout, std::vector<DockPanel> panels, DockOptions options = {});
}
