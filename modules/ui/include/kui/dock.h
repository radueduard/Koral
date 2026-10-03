//
// koral-ui: docking — panels in tabs, split and resized by dragging, floated, and pulled out into
// windows of their own.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "widgets.h"

namespace kui
{
    /** @brief Where a panel goes beside another: a half of its space, or (eCenter) a tab in its group. */
    enum class DockSide : std::uint8_t { eLeft, eRight, eTop, eBottom, eCenter };

    /** @brief A panel of a dock space: what one tab shows. */
    struct DockPanel {
        std::string id;         ///< What the layout knows it by: the same from build to build.
        std::string title;      ///< On its tab, and on its window when it has one.
        Widget content;
        bool closable = true;   ///< Whether its tab has a close button.
    };

    /**
     * @brief How a dock space is arranged: which panels are tabbed together, how the space is split
     *        between the groups, what floats and what is closed. It lives outside the widgets — make
     *        one, keep it (a member of the scene or of a stateful widget), and give it to DockSpace
     *        each build; dragging changes it, and Save/Load carry it between runs.
     *
     * @code
     * auto layout = std::make_shared<kui::DockLayout>();
     * layout->Dock("scene")
     *        .Dock("inspector", kui::DockSide::eRight, "scene", 0.25f)
     *        .Dock("log", kui::DockSide::eBottom, "scene", 0.3f)
     *        .Dock("assets", kui::DockSide::eCenter, "log");        // a tab beside the log
     * @endcode
     */
    class KUI_API DockLayout {
    public:
        DockLayout();
        ~DockLayout();
        DockLayout(const DockLayout&) = delete;
        DockLayout& operator=(const DockLayout&) = delete;

        /**
         * @brief Puts @p panel on @p side of @p relativeTo — of the whole space when that is empty —
         *        taking @p fraction of it; eCenter makes it a tab of that group. A panel the layout has
         *        not placed yet goes there when the dock space first shows it; one already placed moves now.
         */
        DockLayout& Dock(std::string panel, DockSide side = DockSide::eCenter, std::string relativeTo = {}, float fraction = 0.25f);
        /** @brief Floats @p panel over the dock space, at @p rect in the space's coordinates. */
        DockLayout& Float(std::string panel, Rect rect);

        /**
         * @brief Gives @p panel a window of its own, @p size big, where the dock space can open windows
         *        (DockOptions::multiViewport); it floats inside the space where it cannot.
         */
        DockLayout& PopOut(std::string panel, glm::vec2 size = { 480.f, 360.f });

        /** @brief Hides @p panel; it keeps its place for when it is opened again. */
        void Close(const std::string& panel);
        void Open(const std::string& panel);
        [[nodiscard]] bool IsOpen(const std::string& panel) const;
        /** @brief Brings @p panel 's tab to the front of its group. */
        void Activate(const std::string& panel);
        /** @brief Whether @p panel is in a window of its own, or floating over the space. */
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

    struct DockOptions {
        /// Whether a panel dragged out of the window gets an OS window of its own, where there is an
        /// application to open one in. Otherwise it floats inside the space.
        bool multiViewport = true;
        std::function<void(const std::string& panel)> onClosed;   ///< A panel's close button was clicked.
        std::function<void()> onChanged;                          ///< The arrangement changed: when to save it.

        // Chainable: `kui::DockOptions{}.SetMultiViewport(false).OnChanged(...)`.
        DockOptions& SetMultiViewport(bool value) { multiViewport = value; return *this; }
        DockOptions& OnClosed(std::function<void(const std::string&)> f) { onClosed = std::move(f); return *this; }
        DockOptions& OnChanged(std::function<void()> f) { onChanged = std::move(f); return *this; }
    };

    /**
     * @brief A space of docked panels, filling what it is given.
     *
     * Each panel is a tab. Drag a tab onto another group's tab bar to tab it there, onto an edge of a
     * group to split it, into the middle of the space to float it, and out of the window to give it a
     * window of its own; drag the line between two groups to resize them. A panel keeps its state
     * wherever it goes — it is the same widget, shown somewhere else.
     *
     * @code
     * kui::DockSpace(_layout, {
     *     { "scene", "Scene", SceneView() },
     *     { "inspector", "Inspector", kui::Make<Inspector>() },
     *     { "log", "Log", LogView(), false },
     * })
     * @endcode
     *
     * Pulling a panel out into a window needs the platform to place windows (not Wayland) for the
     * window to appear under the pointer and to be dragged back in; where it cannot, the window opens
     * where the compositor puts it and its tab bar has a button that docks it back.
     */
    KUI_API Widget DockSpace(std::shared_ptr<DockLayout> layout, std::vector<DockPanel> panels, DockOptions options = {});
}
