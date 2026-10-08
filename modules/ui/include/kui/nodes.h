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
 *   picked (or everything).
 * - **Picking:** a click picks a node, a wire or a comment, Shift or Control adds to what is picked, a drag over
 *   nothing picks what its rectangle touches; Control+A picks everything, Escape nothing.
 * - **Moving:** dragging a node moves everything picked; dragging a comment's title moves it and the nodes inside;
 *   its corner resizes it.
 * - **Wiring:** a drag from a port draws a wire, and letting go on a port it fits (by type, or canConnect) makes it.
 *   A drag from an input that has a wire picks the wire up. Let go on nothing, a wire is offered to onWireDropped.
 * - **Keys:** Delete removes what is picked; Control+C, +V and +D copy, paste at the pointer and duplicate.
 */

#include <functional>
#include <string>
#include <vector>

#include "canvas.h"
#include "kuiApi.h"
#include "widgets.h"

namespace kui
{
    /** @brief One port of a node: what is wired into it (an input) or out of it (an output). */
    struct NodePort {
        std::string id;                     ///< Unique among the node's ports on its side.
        std::string label;
        std::string type;                   ///< What it carries: ports of one type wire together, unless canConnect says more.
        Color color = colors::Inherit;      ///< Its dot's, and its wires'; the theme's accent when not set.
        bool multiple = false;              ///< An input that takes several wires; an output always may.
    };

    /** @brief A node: a title, its ports, and its own controls under them. */
    struct GraphNode {
        std::string id;
        std::string title;
        kor::Vec2 position {};              ///< Its top-left, in the graph's coordinates.
        std::vector<NodePort> inputs;       ///< Down its left side.
        std::vector<NodePort> outputs;      ///< Down its right side.
        Widget body;                        ///< Under the ports: the node's own controls, or nothing.
        Color accent = colors::Inherit;     ///< Its title bar; the theme's surface when not set.
        std::string error;                  ///< When not empty, it is outlined in red and says this.
        float width = 0.f;                  ///< 0: as wide as it needs, and no narrower than 140.
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
    };

    /** @brief What the editor shows. */
    struct NodeGraph {
        std::vector<GraphNode> nodes;
        std::vector<GraphWire> wires;
        std::vector<GraphComment> comments;
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
        /** @brief What is picked changed. */
        std::function<void(const std::vector<std::string>& nodes)> onSelectionChanged;
        /** @brief A comment moved or resized: its new rectangle. Nodes it carried are told through onMove. */
        std::function<void(const std::string& comment, Rect rect)> onCommentChanged;
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
        NodeEditorOptions& OnSelectionChanged(std::function<void(const std::vector<std::string>&)> f) { onSelectionChanged = std::move(f); return *this; }
        NodeEditorOptions& OnCommentChanged(std::function<void(const std::string&, Rect)> f) { onCommentChanged = std::move(f); return *this; }
        NodeEditorOptions& SetGridSize(const float value) { gridSize = value; return *this; }
    };

    /** @brief Shows @p graph, and tells @p options of what the user does to it. Fills what it is given. */
    KUI_API Widget NodeEditor(NodeGraph graph, NodeEditorOptions options = {});

    /** @brief How a node's card is laid out: where its ports are, for whoever draws or tests against it. */
    namespace nodes {
        inline constexpr float TitleHeight = 28.f;
        inline constexpr float RowHeight = 24.f;
        inline constexpr float MinWidth = 140.f;
        inline constexpr float PortRadius = 5.f;
        /** @brief Where a node's @p index th port is, from its top-left: on its left edge for an input, its right for an output. */
        inline kor::Vec2 PortOffset(const bool output, const std::size_t index, const float nodeWidth)
        {
            return { output ? nodeWidth : 0.f, TitleHeight + (static_cast<float>(index) + 0.5f) * RowHeight };
        }
    }
}
