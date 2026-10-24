#pragma once

/**
 * @file nodes.h
 * @brief A node editor: nodes with typed ports, wires between them, panned and zoomed, picked and moved together.
 *
 * Like the other controls it shows what it is given and says what the user did: the graph is the caller's, and
 * every change — a wire drawn, nodes moved, a selection deleted — arrives as a callback, for the caller to apply
 * and build again. What is only the view's — where it looks, how near, what is picked, a drag under way — it keeps
 * itself, so panning, zooming and dragging build nothing again.
 *
 * @code
 * kui::NodeGraph graph;
 * graph.nodes.push_back({ .id = "blur", .title = "Blur", .position = { 40, 40 },
 *                         .inputs = { { .id = "in", .label = "Image", .type = "image" } },
 *                         .outputs = { { .id = "out", .label = "Image", .type = "image" } },
 *                         .body = kui::DragValue(radius, onRadius) });
 * auto editor = kui::NodeEditor(graph, kui::NodeEditorOptions{}
 *     .OnConnect([&](const kui::GraphWire& wire) { model.Connect(wire); })
 *     .OnMove([&](const std::vector<std::string>& nodes, kor::Vec2 by) { model.Move(nodes, by); }));
 * @endcode
 *
 * - **The view:** the wheel zooms about the pointer; the middle or right button drags it about; F frames what is
 *   picked (or everything). Given a NodeEditorView (NodeEditorOptions::SetView), where it looks is the caller's to
 *   read and to move — and is kept a page at a time.
 * - **Picking:** a click picks a node, a wire or a comment, Shift or Control adds to what is picked, a drag over
 *   nothing picks what its rectangle touches; Control+A picks everything, Escape nothing.
 * - **Moving:** dragging a node moves everything picked; dragging a comment's title moves it and the nodes inside;
 *   its corner resizes it.
 * - **Wiring:** a drag from a port draws a wire, and letting go on a port it fits (by type, or canConnect) makes it.
 *   A drag from an input that has a wire picks the wire up. Let go on nothing, a wire is offered to onWireDropped.
 * - **Keys:** Delete removes what is picked; Control+C, +V and +D copy, paste at the pointer and duplicate.
 * - **Its look** is the theme's it is in, and in the manner of the theme's design (ThemeDesign): each draws cards,
 *   ports, wires, the grid and comments its own way, but puts a node's title, rows and ports where kui::nodes says.
 *   A card is a title bar tinted with its accent, then a row a port — its outputs first, then its inputs — each port's
 *   mark shaped by what it carries (PortShape), and an input given a value says it in a field across its row.
 * - **Pressed twice**, a node is told to onActivate. **The right button** let go where it was pressed opens the menu
 *   NodeEditorOptions::menu gives, for the node under it (picked, with whatever else is) or for nothing.
 */

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "canvas.h"
#include "kuiApi.h"
#include "widgets.h"

namespace kui
{
    /** @brief What a port's mark is: by what it carries, so that one is told from another without its colour. */
    enum class PortShape : std::uint8_t {
        eCircle,        ///< A value, or anything else.
        eSquare,        ///< A buffer.
        eDiamond,       ///< An image.
    };

    /** @brief One port of a node: what is wired into it (an input) or out of it (an output). */
    struct NodePort {
        std::string id;                     ///< Unique among the node's ports on its side.
        std::string label;
        std::string type;                   ///< What it carries: ports of one type wire together, unless canConnect says more.
        Color color = colors::Inherit;      ///< Its mark's, and its wires'; the theme's accent when not set.
        PortShape shape = PortShape::eCircle;
        bool multiple = false;              ///< An input that takes several wires; an output always may.
        /// An input given a value where a wire could go: said in a field across its row, its label at one end and the value
        /// at the other — as what can be changed, where a wire would otherwise come in.
        std::string value;
        /// An input's own control — a slider, a box to tick, a colour to pick — beside its label, in place of @ref value:
        /// what it is set to on the node itself, while nothing is wired to it. Its row stays one row high.
        Widget control;
    };

    /** @brief A node: a title, its ports, and its own controls under them. */
    struct GraphNode {
        std::string id;
        std::string title;
        /// A Material icon's name (`"photo"`, `"functions"`), drawn in its title bar before the title: what kind of node
        /// it is, at a glance. None when empty, or not an icon kui has.
        std::string icon;
        kor::Vec2 position {};              ///< Its top-left, in the graph's coordinates.
        std::vector<NodePort> inputs;       ///< Down its left side.
        std::vector<NodePort> outputs;      ///< Down its right side.
        Widget body;                        ///< Under the ports: the node's own controls, or nothing.
        /// What kind of node it is: its title bar is tinted with it — mixed into the card, never as bright as it is.
        /// When not set, what the theme's design puts there.
        Color accent = colors::Inherit;
        std::string error;                  ///< When not empty, it is outlined in red and says this.
        float width = 0.f;                  ///< 0: as wide as it needs, and no narrower than 140.
        /// How abstract it is, from 0 (a value) up: shown in its title bar's end as that many lit steps of five — so
        /// that what is worked at, a number or a whole technique, is told at a glance. Below 0: not shown.
        int level = -1;
        /// Made of other nodes — a function's call, a technique: drawn as a stack of cards, not one.
        bool composite = false;
    };

    /** @brief One port of one node. */
    struct PortRef {
        std::string node;
        std::string port;
        auto operator<=>(const PortRef&) const = default;
    };

    /** @brief A wire: from an output to an input. */
    struct GraphWire {
        PortRef from;
        PortRef to;
        auto operator<=>(const GraphWire&) const = default;
    };

    /** @brief A comment: a rectangle behind nodes, with a title, that moves the nodes inside it with it. */
    struct GraphComment {
        std::string id;
        std::string text;
        Rect rect;                          ///< In the graph's coordinates.
        Color color = colors::Inherit;      ///< Faint behind the nodes; the theme's accent when not set.
        /// Not empty: a frame round these nodes, wherever they are, in place of @ref rect. Dragged by its title it
        /// moves them (onMove); it has no corner to resize it by, and is never told to onCommentChanged.
        std::vector<std::string> around;
        /// Drawn as a node that holds the nodes it is round — a solid title bar in its colour, a body, a border — rather
        /// than a faint rectangle behind them: for nodes that are one thing, as a loop's are.
        bool container = false;
        /// How many frames round the same nodes it holds inside it: each further out than the one it holds. 0 for one
        /// that holds none.
        int depth = 0;
    };

    /** @brief What the editor shows. */
    struct NodeGraph {
        std::vector<GraphNode> nodes;
        std::vector<GraphWire> wires;
        std::vector<GraphComment> comments;
    };

    /**
     * @brief Where a node editor looks, shared with whoever made it: to read where a node is on the screen, to put a
     *        node made in the middle of what is seen, to frame the graph or bring a node into view.
     *
     * Kept a page at a time: whoever builds the editor says which @ref page it shows, and each page is looked at
     * where it was left — one shown for the first time, framed whole (once it has a node). What the editor is given to
 * show is that page.
     *
     * @code
     * auto view = std::make_shared<kui::NodeEditorView>();     // kept, as the graph is
     * view->page = "lighting";
     * kui::NodeEditor(graph, kui::NodeEditorOptions{}.SetView(view));
     * view->Reveal("blur");                                     // picked, and in sight, from the next layout
     * @endcode
     */
    struct KUI_API NodeEditorView {
        std::string page;                   ///< Which page the editor shows: set before it is built.
        kor::Vec2 centre {};                ///< The point of the graph in the middle of the editor.
        float zoom = 1.f;                   ///< Units of the view to one of the graph's.
        kor::Vec2 size {};                  ///< How big the editor is, as last laid out.
        kor::Vec2 origin {};                ///< Where its top-left is, in the view's coordinates, as last painted.
        std::map<std::string, kor::Vec2> sizes;     ///< How big each node's card came out, by id, as last laid out: what a layout of the page goes by.

        /** @brief Where a point of the graph is, in the view's coordinates. */
        [[nodiscard]] kor::Vec2 ToView(kor::Vec2 graph) const { return origin + size * 0.5f + (graph - centre) * zoom; }
        /** @brief Where a point of the view is, in the graph's. */
        [[nodiscard]] kor::Vec2 ToGraph(kor::Vec2 view) const { return centre + (view - origin - size * 0.5f) / (zoom > 0.f ? zoom : 1.f); }
        /** @brief Frames the whole page, from the next layout — or, while nothing is on it, once something is. */
        void Frame();
        /** @brief Picks node @p id and brings it into view, from the next layout: what goes to a node. */
        void Reveal(std::string id);

        /// The editor's own: where each page was left, and what is asked of it before it is next laid out.
        struct Kept {
            std::optional<std::string> showing;     ///< The page centre and zoom are of.
            std::map<std::string, std::pair<kor::Vec2, float>> pages;
            bool frame = false;
            std::string reveal;
            const void* editor = nullptr;   ///< The editor showing it, and how to have it laid out again.
            std::function<void()> wake;
        } kept;
    };

    /** @brief What the editor tells of what the user did. Each change is the caller's to make. */
    struct NodeEditorOptions {
        /** @brief Whether a wire may go from @p from (an output) to @p to (an input): by default, when their types are the same. */
        std::function<bool(const NodePort& from, const NodePort& to)> canConnect;
        /** @brief A wire drawn. An input that takes one wire loses the one it had first (onDisconnect). */
        std::function<void(const GraphWire& wire)> onConnect;
        /** @brief A wire taken away: picked up off an input, or replaced by another. */
        std::function<void(const GraphWire& wire)> onDisconnect;
        /** @brief Nodes dragged @p by, in the graph's units — told once, when let go. */
        std::function<void(const std::vector<std::string>& nodes, kor::Vec2 by)> onMove;
        /** @brief What is picked, deleted. */
        std::function<void(const std::vector<std::string>& nodes, const std::vector<GraphWire>& wires,
                           const std::vector<std::string>& comments)> onDelete;
        /** @brief Control+C on what is picked. */
        std::function<void(const std::vector<std::string>& nodes)> onCopy;
        /** @brief Control+V, at @p at in the graph: where the pointer is. */
        std::function<void(kor::Vec2 at)> onPaste;
        /** @brief Control+D on what is picked. */
        std::function<void(const std::vector<std::string>& nodes)> onDuplicate;
        /** @brief The right button let go without dragging, over nothing: @p at in the graph, @p viewAt in the editor. */
        std::function<void(kor::Vec2 at, kor::Vec2 viewAt)> onContextMenu;
        /** @brief A wire let go over nothing, from @p from (an output's or an input's), at @p at in the graph. */
        std::function<void(const PortRef& from, bool fromOutput, kor::Vec2 at)> onWireDropped;
        /**
         * @brief A wire let go on another node, but on no port of it that fits: from @p from (an output's or an input's)
         *        onto @p node, at @p at in the graph — for the caller to offer the node's ports that would take it.
         *        Not told, the wire is let go as over nothing.
         */
        std::function<void(const PortRef& from, bool fromOutput, const std::string& node, kor::Vec2 at)> onWireDroppedOnNode;
        /** @brief What is picked changed. */
        std::function<void(const std::vector<std::string>& nodes)> onSelectionChanged;
        /** @brief A comment moved or resized: its new rectangle. Nodes it carried are told through onMove. */
        std::function<void(const std::string& comment, Rect rect)> onCommentChanged;
        /** @brief A node pressed twice. */
        std::function<void(const std::string& node)> onActivate;
        /**
         * @brief The menu the right button opens, let go where it was pressed: for @p nodes (what is picked, once the
         *        node under the pointer is) or, over nothing, for no nodes; @p at in the graph. An empty menu is none —
         *        and over nothing, onContextMenu is told instead.
         */
        std::function<std::vector<MenuItem>(const std::vector<std::string>& nodes, kor::Vec2 at)> menu;
        /** @brief Where it looks, when the caller would know or say: see NodeEditorView. Null: its own, kept by itself. */
        std::shared_ptr<NodeEditorView> view;
        float gridSize = 20.f;              ///< Between the grid's lines, in graph units; 0 draws none.

        // Chainable: `kui::NodeEditorOptions{}.On...(...).On...(...)`.
        NodeEditorOptions& CanConnect(std::function<bool(const NodePort&, const NodePort&)> f) { canConnect = std::move(f); return *this; }
        NodeEditorOptions& OnConnect(std::function<void(const GraphWire&)> f) { onConnect = std::move(f); return *this; }
        NodeEditorOptions& OnDisconnect(std::function<void(const GraphWire&)> f) { onDisconnect = std::move(f); return *this; }
        NodeEditorOptions& OnMove(std::function<void(const std::vector<std::string>&, kor::Vec2)> f) { onMove = std::move(f); return *this; }
        NodeEditorOptions& OnDelete(std::function<void(const std::vector<std::string>&, const std::vector<GraphWire>&, const std::vector<std::string>&)> f)
        {
            onDelete = std::move(f);
            return *this;
        }
        NodeEditorOptions& OnCopy(std::function<void(const std::vector<std::string>&)> f) { onCopy = std::move(f); return *this; }
        NodeEditorOptions& OnPaste(std::function<void(kor::Vec2)> f) { onPaste = std::move(f); return *this; }
        NodeEditorOptions& OnDuplicate(std::function<void(const std::vector<std::string>&)> f) { onDuplicate = std::move(f); return *this; }
        NodeEditorOptions& OnContextMenu(std::function<void(kor::Vec2, kor::Vec2)> f) { onContextMenu = std::move(f); return *this; }
        NodeEditorOptions& OnWireDropped(std::function<void(const PortRef&, bool, kor::Vec2)> f) { onWireDropped = std::move(f); return *this; }
        NodeEditorOptions& OnWireDroppedOnNode(std::function<void(const PortRef&, bool, const std::string&, kor::Vec2)> f)
        {
            onWireDroppedOnNode = std::move(f);
            return *this;
        }
        NodeEditorOptions& OnSelectionChanged(std::function<void(const std::vector<std::string>&)> f) { onSelectionChanged = std::move(f); return *this; }
        NodeEditorOptions& OnCommentChanged(std::function<void(const std::string&, Rect)> f) { onCommentChanged = std::move(f); return *this; }
        NodeEditorOptions& OnActivate(std::function<void(const std::string&)> f) { onActivate = std::move(f); return *this; }
        NodeEditorOptions& Menu(std::function<std::vector<MenuItem>(const std::vector<std::string>&, kor::Vec2)> f) { menu = std::move(f); return *this; }
        NodeEditorOptions& SetView(std::shared_ptr<NodeEditorView> value) { view = std::move(value); return *this; }
        NodeEditorOptions& SetGridSize(const float value) { gridSize = value; return *this; }
    };

    /** @brief Shows @p graph, and tells @p options of what the user does to it. Fills what it is given. */
    KUI_API Widget NodeEditor(NodeGraph graph, NodeEditorOptions options = {});

    /**
     * @brief How a node's card is laid out: where its ports are, for whoever draws or tests against it. A port has a
     *        row of its own: the node's outputs, then its inputs under them.
     */
    namespace nodes {
        inline constexpr float TitleHeight = 30.f;
        inline constexpr float PortsTop = 4.f;      ///< Between the title and the first row of ports.
        inline constexpr float RowHeight = 26.f;    ///< A row of ports: room for a small control.
        inline constexpr float MinWidth = 140.f;
        inline constexpr float PortRadius = 5.f;
        /** @brief Which row a node's @p index th port is on, of a node with @p outputs outputs: they come first. */
        inline std::size_t PortRow(const bool output, const std::size_t index, const std::size_t outputs) { return output ? index : outputs + index; }
        /**
         * @brief Where a node's @p index th port is, from its top-left, of a node with @p outputs outputs: on its left
         *        edge for an input, its right for an output.
         */
        inline kor::Vec2 PortOffset(const bool output, const std::size_t index, const float nodeWidth, const std::size_t outputs)
        {
            return { output ? nodeWidth : 0.f, TitleHeight + PortsTop + (static_cast<float>(PortRow(output, index, outputs)) + 0.5f) * RowHeight };
        }
    }
}
