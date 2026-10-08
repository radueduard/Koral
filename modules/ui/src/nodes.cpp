//
// koral-ui: the node editor. One render object holds the view — where it looks, how near, what is picked, a drag
// under way — draws the grid, comments, wires, ports and highlights itself, and holds each node's card (built from
// widgets, so a node's controls are ordinary ones) as a child, under the view's transform.
//

#include <kui/nodes.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

#include <input.h>

#include "element.h"

namespace kui
{
    namespace {
        using namespace nodes;

        constexpr Color ErrorColor = Color::Hex(0xE5484D);
        constexpr float MinZoom = 0.15f, MaxZoom = 3.f;
        constexpr float CommentTitle = 26.f;   // the strip along a comment's top that drags it
        constexpr float CommentCorner = 14.f;  // the square in its bottom-right that resizes it
        constexpr float DragThreshold = 3.f;   // in the view's units: less than this, a press is a click

        Color Resolve(const Color color, const Color fallback) { return color.a < 0.f ? fallback : color; }

        /** Distance from @p p to a cubic Bezier, by sampling: what picks a wire. */
        float DistanceToCubic(const kor::Vec2 p, const kor::Vec2 a, const kor::Vec2 b, const kor::Vec2 c, const kor::Vec2 d)
        {
            float best = std::numeric_limits<float>::max();
            kor::Vec2 previous = a;
            for (int i = 1; i <= 24; ++i) {
                const float t = static_cast<float>(i) / 24.f, u = 1.f - t;
                const kor::Vec2 q = a * (u * u * u) + b * (3.f * u * u * t) + c * (3.f * u * t * t) + d * (t * t * t);
                const kor::Vec2 e = q - previous;
                const float l2 = kor::Dot(e, e);
                const float s = l2 > 0.f ? std::clamp(kor::Dot(p - previous, e) / l2, 0.f, 1.f) : 0.f;
                best = std::min(best, kor::Length(p - (previous + e * s)));
                previous = q;
            }
            return best;
        }

        /** The two control points of a wire from @p from (an output) to @p to (an input): out of the one, into the other. */
        std::pair<kor::Vec2, kor::Vec2> WireControls(const kor::Vec2 from, const kor::Vec2 to)
        {
            const float reach = std::max(40.f, std::abs(to.x - from.x) * 0.5f);
            return { from + kor::Vec2(reach, 0.f), to - kor::Vec2(reach, 0.f) };
        }

        class RenderNodeCanvas final : public RenderContainer {
        public:
            struct Config {
                NodeGraph graph;
                NodeEditorOptions options;
            };

            void Set(const Config& config)
            {
                _config = config;
                // What was picked and has gone since is not picked.
                std::erase_if(_pickedNodes, [&](const std::string& id) { return !FindNode(id); });
                std::erase_if(_pickedComments, [&](const std::string& id) { return !FindComment(id); });
                std::erase_if(_pickedWires, [&](const GraphWire& w) { return std::ranges::find(_config.graph.wires, w) == _config.graph.wires.end(); });
                MarkNeedsLayout();
                MarkNeedsPaint();
            }

            [[nodiscard]] bool Focusable() const override { return true; }

            // ---- layout ---------------------------------------------------------------------------------

            void PerformLayout() override
            {
                const auto& c = Constraints();
                SetSize(c.Constrain({ c.HasBoundedWidth() ? c.maxWidth : 800.f, c.HasBoundedHeight() ? c.maxHeight : 600.f }));
                for (std::size_t i = 0; i < _children.size() && i < _config.graph.nodes.size(); ++i) {
                    const auto& node = _config.graph.nodes[i];
                    // As wide as it asks to be, or as its content needs, and never narrower than MinWidth: laid out at
                    // exactly that, so that its rows stretch across it.
                    const float width = node.width > 0.f ? node.width : std::max(MinWidth, std::ceil(_children[i]->MinIntrinsicWidth()));
                    _children[i]->Layout(BoxConstraints { width, width, 0.f, kui::Infinity });
                    _children[i]->SetOffset(node.position);
                }
            }

            // ---- where things are -----------------------------------------------------------------------

            [[nodiscard]] kor::Vec2 ToGraph(const kor::Vec2 view) const { return (view - _pan) / _zoom; }
            [[nodiscard]] kor::Vec2 ToView(const kor::Vec2 graph) const { return graph * _zoom + _pan; }

            [[nodiscard]] kor::Vec2 ChildOrigin(const RenderObject& child) const override { return ToView(child.Offset() + ShiftOf(child)); }
            [[nodiscard]] kor::Vec2 MapToChild(const RenderObject& child, const kor::Vec2 point) const override
            {
                return ToGraph(point) - child.Offset() - ShiftOf(child);
            }

            bool HitTest(HitTestResult& result, const kor::Vec2 position) override
            {
                if (!Rect::FromSize(Size()).Contains(position)) return false;
                HitTestChildren(result, position);
                result.Add(this, position);
                return true;
            }

            bool HitTestChildren(HitTestResult& result, const kor::Vec2 position) override
            {
                for (auto it = _children.rbegin(); it != _children.rend(); ++it)
                    if ((*it)->HitTest(result, MapToChild(**it, position))) return true;
                return false;
            }

            // ---- painting -------------------------------------------------------------------------------

            void Paint(Canvas& canvas, const kor::Vec2 offset) override
            {
                const Theme& t = Theme::Current();
                const Rect bounds = Rect::FromSize(Size()).Shift(offset);
                canvas.Save();
                canvas.ClipRect(bounds);
                canvas.DrawRect(bounds, Paint::Fill(t.background));
                PaintGrid(canvas, offset, t);

                canvas.Save();
                canvas.Translate(offset + _pan);
                canvas.Scale(_zoom);
                const float pixel = 1.f / _zoom;
                // What is in view, in the graph: what lies wholly outside it is not drawn at all.
                const Rect seen = Rect::LTRB(ToGraph({}).x, ToGraph({}).y, ToGraph(Size()).x, ToGraph(Size()).y).Inflate(20.f);

                for (const auto& comment : _config.graph.comments)
                    if (!comment.rect.Intersect(seen).Empty()) PaintComment(canvas, comment, t, pixel);
                for (const auto& wire : _config.graph.wires) PaintWire(canvas, wire, t, pixel, seen);
                for (std::size_t i = 0; i < _children.size() && i < _config.graph.nodes.size(); ++i) {
                    const auto& node = _config.graph.nodes[i];
                    const Rect card = Rect::XYWH(node.position.x, node.position.y, _children[i]->Size().x, _children[i]->Size().y).Shift(Shift(node.id));
                    if (card.Inflate(PortRadius + 12.f).Intersect(seen).Empty()) continue;
                    canvas.DrawShadow({ card, 6.f }, colors::Black.WithAlpha(0.35f), 10.f, { 0.f, 3.f });
                    PaintChild(*_children[i], canvas, Shift(node.id));
                    const bool picked = _pickedNodes.contains(node.id);
                    if (!node.error.empty() || picked)
                        canvas.DrawRRect({ card.Inflate(1.5f * pixel), 7.f }, Paint::Stroked(node.error.empty() ? t.primary : ErrorColor, 2.f * pixel));
                    PaintPorts(canvas, node, card, t, pixel);
                }
                if (_drag == Drag::eWire) PaintLooseWire(canvas, t, pixel);
                canvas.Restore();

                if (_drag == Drag::eBox) {
                    const Rect box = BoxRect().Shift(offset);
                    canvas.DrawRect(box, Paint::Fill(t.primary.WithAlpha(0.12f)).SetStroke(1.f, t.primary.WithAlpha(0.8f)));
                }
                canvas.Restore();
            }

            // ---- the pointer ------------------------------------------------------------------------------

            bool HandleEvent(const PointerEvent& event) override
            {
                using Type = PointerEvent::Type;
                const kor::Vec2 graph = ToGraph(event.local);
                switch (event.type) {
                case Type::eScroll: {
                    // Zoomed about the pointer: what is under it stays under it.
                    const float zoom = std::clamp(_zoom * std::pow(1.15f, event.delta.y), MinZoom, MaxZoom);
                    _pan = event.local - graph * zoom;
                    _zoom = zoom;
                    MarkNeedsPaint();
                    return true;
                }
                case Type::eHover:
                    _pointer = graph;
                    if (auto* owner = GetOwner(); owner && HoveredPort(graph)) owner->cursor = PointerCursor::eHand;
                    return false;
                case Type::eDown:
                    if (auto* owner = GetOwner()) owner->RequestFocus(this);
                    if (event.taken) return false;     // a control in a node had it
                    _pressAt = event.local;
                    _pointer = graph;
                    _moved = false;
                    if (event.button != kor::MouseButton::eLeft) {
                        _drag = Drag::ePan;
                        _panButton = event.button;
                        return true;
                    }
                    Press(graph);
                    return true;
                case Type::eMove:
                    _pointer = graph;
                    if (kor::Length(event.local - _pressAt) > DragThreshold) _moved = true;
                    Drag_(event, graph);
                    return _drag != Drag::eNone;
                case Type::eUp:
                    _pointer = graph;
                    Release(event, graph);
                    return true;
                case Type::eCancel:
                    _drag = Drag::eNone;
                    MarkNeedsPaint();
                    return true;
                default:
                    return false;
                }
            }

            // ---- the keyboard ------------------------------------------------------------------------------

            bool HandleKey(const kor::Key key, bool) override
            {
                const auto* owner = GetOwner();
                const bool control = owner && owner->control;
                const auto& o = _config.options;
                switch (key) {
                case kor::Key::eDelete:
                case kor::Key::eBackspace:
                    if (o.onDelete && (!_pickedNodes.empty() || !_pickedWires.empty() || !_pickedComments.empty()))
                        o.onDelete({ _pickedNodes.begin(), _pickedNodes.end() }, { _pickedWires.begin(), _pickedWires.end() },
                                   { _pickedComments.begin(), _pickedComments.end() });
                    return true;
                case kor::Key::eEsc:
                    if (_drag != Drag::eNone) { _drag = Drag::eNone; MarkNeedsPaint(); return true; }
                    Pick({}, false);
                    return true;
                case kor::Key::eA:
                    if (!control) return false;
                    {
                        std::set<std::string> all;
                        for (const auto& n : _config.graph.nodes) all.insert(n.id);
                        Pick(all, false);
                    }
                    return true;
                case kor::Key::eC:
                    if (!control || !o.onCopy) return false;
                    o.onCopy({ _pickedNodes.begin(), _pickedNodes.end() });
                    return true;
                case kor::Key::eV:
                    if (!control || !o.onPaste) return false;
                    o.onPaste(_pointer);
                    return true;
                case kor::Key::eD:
                    if (!control || !o.onDuplicate) return false;
                    o.onDuplicate({ _pickedNodes.begin(), _pickedNodes.end() });
                    return true;
                case kor::Key::eF:
                    Frame();
                    return true;
                default:
                    return false;
                }
            }

            // ---- for tests, and the view's own use --------------------------------------------------------

            [[nodiscard]] float Zoom() const { return _zoom; }
            [[nodiscard]] kor::Vec2 Pan() const { return _pan; }

        private:
            enum class Drag : std::uint8_t { eNone, ePan, eNodes, eBox, eWire, eComment, eCommentResize };

            [[nodiscard]] const GraphNode* FindNode(const std::string_view id) const
            {
                const auto it = std::ranges::find(_config.graph.nodes, id, &GraphNode::id);
                return it == _config.graph.nodes.end() ? nullptr : &*it;
            }
            [[nodiscard]] const GraphComment* FindComment(const std::string_view id) const
            {
                const auto it = std::ranges::find(_config.graph.comments, id, &GraphComment::id);
                return it == _config.graph.comments.end() ? nullptr : &*it;
            }
            [[nodiscard]] float NodeWidth(const GraphNode& node) const
            {
                const auto index = static_cast<std::size_t>(&node - _config.graph.nodes.data());
                return index < _children.size() ? _children[index]->Size().x : std::max(node.width, MinWidth);
            }
            [[nodiscard]] Rect NodeRect(const GraphNode& node) const
            {
                const auto index = static_cast<std::size_t>(&node - _config.graph.nodes.data());
                const kor::Vec2 size = index < _children.size() ? _children[index]->Size() : kor::Vec2(MinWidth, TitleHeight);
                return Rect::XYWH(node.position.x, node.position.y, size.x, size.y).Shift(Shift(node.id));
            }

            /** How far a node is drawn from where it is: what is picked, while it is dragged. */
            [[nodiscard]] kor::Vec2 Shift(const std::string& node) const
            {
                if ((_drag == Drag::eNodes || _drag == Drag::eComment) && _carried.contains(node)) return _dragBy;
                return {};
            }
            [[nodiscard]] kor::Vec2 ShiftOf(const RenderObject& child) const
            {
                for (std::size_t i = 0; i < _children.size() && i < _config.graph.nodes.size(); ++i)
                    if (_children[i] == &child) return Shift(_config.graph.nodes[i].id);
                return {};
            }

            struct PortHit { const GraphNode* node; bool output; std::size_t index; };

            [[nodiscard]] std::optional<PortHit> HoveredPort(const kor::Vec2 graph) const
            {
                const float reach = (PortRadius + 4.f) / std::min(_zoom, 1.f);
                for (auto it = _config.graph.nodes.rbegin(); it != _config.graph.nodes.rend(); ++it) {
                    const Rect card = NodeRect(*it);
                    for (const bool output : { false, true }) {
                        const auto& ports = output ? it->outputs : it->inputs;
                        for (std::size_t i = 0; i < ports.size(); ++i)
                            if (kor::Length(graph - (card.TopLeft() + PortOffset(output, i, card.Width()))) <= reach) return PortHit { &*it, output, i };
                    }
                }
                return std::nullopt;
            }

            [[nodiscard]] std::optional<kor::Vec2> PortPosition(const PortRef& ref, const bool output) const
            {
                const GraphNode* node = FindNode(ref.node);
                if (!node) return std::nullopt;
                const auto& ports = output ? node->outputs : node->inputs;
                const auto it = std::ranges::find(ports, ref.port, &NodePort::id);
                if (it == ports.end()) return std::nullopt;
                const Rect card = NodeRect(*node);
                return card.TopLeft() + PortOffset(output, static_cast<std::size_t>(it - ports.begin()), card.Width());
            }

            [[nodiscard]] const NodePort* Port(const PortRef& ref, const bool output) const
            {
                const GraphNode* node = FindNode(ref.node);
                if (!node) return nullptr;
                const auto& ports = output ? node->outputs : node->inputs;
                const auto it = std::ranges::find(ports, ref.port, &NodePort::id);
                return it == ports.end() ? nullptr : &*it;
            }

            [[nodiscard]] bool Fits(const NodePort& from, const NodePort& to) const
            {
                return _config.options.canConnect ? _config.options.canConnect(from, to) : from.type == to.type;
            }

            [[nodiscard]] Rect BoxRect() const
            {
                const kor::Vec2 a = ToView(_boxFrom), b = ToView(_boxTo);
                return Rect::LTRB(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
            }

            void Pick(std::set<std::string> nodes, const bool add, std::set<GraphWire> wires = {}, std::set<std::string> comments = {})
            {
                if (add) {
                    // Adding what is picked already takes it out again.
                    for (const auto& n : nodes) if (!_pickedNodes.erase(n)) _pickedNodes.insert(n);
                    for (const auto& w : wires) if (!_pickedWires.erase(w)) _pickedWires.insert(w);
                    for (const auto& c : comments) if (!_pickedComments.erase(c)) _pickedComments.insert(c);
                } else {
                    _pickedNodes = std::move(nodes);
                    _pickedWires = std::move(wires);
                    _pickedComments = std::move(comments);
                }
                if (_config.options.onSelectionChanged) _config.options.onSelectionChanged({ _pickedNodes.begin(), _pickedNodes.end() });
                MarkNeedsPaint();
            }

            void Press(const kor::Vec2 graph)
            {
                const auto* owner = GetOwner();
                const bool add = owner && (owner->shift || owner->control);
                _dragBy = {};

                // A port: a wire starts here — or, from an input that has one, that wire is picked up.
                if (const auto port = HoveredPort(graph)) {
                    const auto& p = (port->output ? port->node->outputs : port->node->inputs)[port->index];
                    _wireFrom = { port->node->id, p.id };
                    _wireFromOutput = port->output;
                    _pickedUp.reset();
                    if (!port->output) {
                        const auto existing = std::ranges::find_if(_config.graph.wires, [&](const GraphWire& w) { return w.to == _wireFrom; });
                        if (existing != _config.graph.wires.end()) {
                            _pickedUp = *existing;
                            _wireFrom = existing->from;   // carried on from its output
                            _wireFromOutput = true;
                        }
                    }
                    _wireTo = graph;
                    _drag = Drag::eWire;
                    MarkNeedsPaint();
                    return;
                }

                // A node: picked, and what is picked moves with it.
                for (auto it = _config.graph.nodes.rbegin(); it != _config.graph.nodes.rend(); ++it) {
                    if (!NodeRect(*it).Contains(graph)) continue;
                    if (add) Pick({ it->id }, true);
                    else if (!_pickedNodes.contains(it->id)) Pick({ it->id }, false);
                    _carried = _pickedNodes;
                    _carriedComments.clear();
                    _dragFrom = graph;
                    _drag = _carried.empty() ? Drag::eNone : Drag::eNodes;
                    return;
                }

                // A wire, picked by clicking near it.
                const float near = 6.f / _zoom;
                for (const auto& wire : _config.graph.wires) {
                    const auto from = PortPosition(wire.from, true), to = PortPosition(wire.to, false);
                    if (!from || !to) continue;
                    const auto [c1, c2] = WireControls(*from, *to);
                    if (DistanceToCubic(graph, *from, c1, c2, *to) <= near) {
                        Pick({}, add, { wire });
                        return;
                    }
                }

                // A comment: its corner resizes it, its title moves it and what it holds.
                for (auto it = _config.graph.comments.rbegin(); it != _config.graph.comments.rend(); ++it) {
                    const Rect& r = it->rect;
                    if (!r.Contains(graph)) continue;
                    const bool corner = graph.x > r.right - CommentCorner / _zoom && graph.y > r.bottom - CommentCorner / _zoom;
                    const bool title = graph.y < r.top + CommentTitle;
                    if (!corner && !title) break;   // its inside is the canvas: a box is drawn there
                    Pick({}, add, {}, { it->id });
                    _comment = it->id;
                    _commentRect = r;
                    _dragFrom = graph;
                    if (corner) {
                        _drag = Drag::eCommentResize;
                    } else {
                        _drag = Drag::eComment;
                        _carried.clear();
                        for (const auto& node : _config.graph.nodes)
                            if (r.Contains(NodeRect(node).TopLeft()) && r.Contains(kor::Vec2(NodeRect(node).right, NodeRect(node).bottom) - kor::Vec2(1.f)))
                                _carried.insert(node.id);
                    }
                    return;
                }

                // Nothing: a box is drawn to pick what it touches.
                if (!add) Pick({}, false);
                _boxFrom = _boxTo = graph;
                _drag = Drag::eBox;
            }

            void Drag_(const PointerEvent& event, const kor::Vec2 graph)
            {
                switch (_drag) {
                case Drag::ePan:
                    _pan += event.delta;
                    break;
                case Drag::eNodes:
                case Drag::eComment:
                    _dragBy = graph - _dragFrom;   // drawn and hit where they are dragged to (Shift): nothing is laid out again
                    break;
                case Drag::eCommentResize:
                    _dragBy = graph - _dragFrom;
                    break;
                case Drag::eBox:
                    _boxTo = graph;
                    break;
                case Drag::eWire:
                    _wireTo = graph;
                    break;
                default:
                    return;
                }
                MarkNeedsPaint();
            }

            void Release(const PointerEvent& event, const kor::Vec2 graph)
            {
                const auto& o = _config.options;
                const Drag drag = std::exchange(_drag, Drag::eNone);
                switch (drag) {
                case Drag::ePan:
                    if (!_moved && event.button == kor::MouseButton::eRight && o.onContextMenu) o.onContextMenu(graph, event.local);
                    break;
                case Drag::eNodes:
                    if (_moved && o.onMove && _dragBy != kor::Vec2(0.f)) o.onMove({ _carried.begin(), _carried.end() }, _dragBy);
                    break;
                case Drag::eComment:
                    if (_moved && _dragBy != kor::Vec2(0.f)) {
                        if (o.onCommentChanged) o.onCommentChanged(_comment, _commentRect.Shift(_dragBy));
                        if (o.onMove && !_carried.empty()) o.onMove({ _carried.begin(), _carried.end() }, _dragBy);
                    }
                    break;
                case Drag::eCommentResize:
                    if (_moved && o.onCommentChanged) o.onCommentChanged(_comment, ResizedComment());
                    break;
                case Drag::eBox: {
                    if (!_moved) break;
                    const Rect box = Rect::LTRB(std::min(_boxFrom.x, _boxTo.x), std::min(_boxFrom.y, _boxTo.y),
                                                std::max(_boxFrom.x, _boxTo.x), std::max(_boxFrom.y, _boxTo.y));
                    std::set<std::string> touched;
                    for (const auto& node : _config.graph.nodes)
                        if (!NodeRect(node).Intersect(box).Empty()) touched.insert(node.id);
                    const auto* owner = GetOwner();
                    const bool add = owner && (owner->shift || owner->control);
                    if (add) {
                        for (const auto& id : _pickedNodes) touched.insert(id);
                        Pick(std::move(touched), false, _pickedWires, _pickedComments);
                    } else {
                        Pick(std::move(touched), false);
                    }
                    break;
                }
                case Drag::eWire:
                    Connect(graph);
                    break;
                default:
                    break;
                }
                _dragBy = {};
                _carried.clear();
                MarkNeedsPaint();
            }

            void Connect(const kor::Vec2 graph)
            {
                const auto& o = _config.options;
                const auto target = HoveredPort(graph);
                const NodePort* from = Port(_wireFrom, _wireFromOutput);
                if (target && from && target->output != _wireFromOutput) {
                    const NodePort& to = (target->output ? target->node->outputs : target->node->inputs)[target->index];
                    const PortRef other { target->node->id, to.id };
                    const GraphWire wire = _wireFromOutput ? GraphWire { _wireFrom, other } : GraphWire { other, _wireFrom };
                    const NodePort& out = _wireFromOutput ? *from : to;
                    const NodePort& in = _wireFromOutput ? to : *from;
                    if (wire.from.node != wire.to.node && Fits(out, in)) {
                        if (_pickedUp && *_pickedUp == wire) return;   // put back where it was
                        if (_pickedUp && o.onDisconnect) o.onDisconnect(*_pickedUp);
                        // An input that takes one wire lets go of the one it had.
                        if (!in.multiple && o.onDisconnect)
                            for (const auto& existing : _config.graph.wires)
                                if (existing.to == wire.to && existing != wire && (!_pickedUp || existing != *_pickedUp)) o.onDisconnect(existing);
                        if (o.onConnect && std::ranges::find(_config.graph.wires, wire) == _config.graph.wires.end()) o.onConnect(wire);
                        return;
                    }
                }
                // Let go over nothing it fits: a picked-up wire is taken away; a new one is offered.
                if (_pickedUp) {
                    if (o.onDisconnect) o.onDisconnect(*_pickedUp);
                } else if (!target && o.onWireDropped) {
                    o.onWireDropped(_wireFrom, _wireFromOutput, graph);
                }
            }

            [[nodiscard]] Rect ResizedComment() const
            {
                return Rect::LTRB(_commentRect.left, _commentRect.top, std::max(_commentRect.right + _dragBy.x, _commentRect.left + 80.f),
                                  std::max(_commentRect.bottom + _dragBy.y, _commentRect.top + CommentTitle + 20.f));
            }

            void Frame()
            {
                std::optional<Rect> bounds;
                for (const auto& node : _config.graph.nodes) {
                    if (!_pickedNodes.empty() && !_pickedNodes.contains(node.id)) continue;
                    bounds = bounds ? bounds->Union(NodeRect(node)) : NodeRect(node);
                }
                if (!bounds || Size().x <= 0.f || Size().y <= 0.f) return;
                const Rect b = bounds->Inflate(40.f);
                _zoom = std::clamp(std::min(Size().x / b.Width(), Size().y / b.Height()), MinZoom, 1.f);
                _pan = Size() * 0.5f - b.Center() * _zoom;
                MarkNeedsPaint();
            }

            // ---- drawing ----------------------------------------------------------------------------------

            void PaintGrid(Canvas& canvas, const kor::Vec2 offset, const Theme& t) const
            {
                const float grid = _config.options.gridSize;
                if (grid <= 0.f) return;
                const float step = grid * _zoom;
                const Color minor = t.border.WithAlpha(0.35f), major = t.border.WithAlpha(0.9f);
                const auto lines = [&](const float spacing, const Color color) {
                    if (spacing < 6.f) return;   // too dense to read
                    const kor::Vec2 start = { std::fmod(_pan.x, spacing), std::fmod(_pan.y, spacing) };
                    for (float x = start.x < 0.f ? start.x + spacing : start.x; x < Size().x; x += spacing)
                        canvas.DrawLine(offset + kor::Vec2(x, 0.f), offset + kor::Vec2(x, Size().y), Paint::Stroked(color, 1.f));
                    for (float y = start.y < 0.f ? start.y + spacing : start.y; y < Size().y; y += spacing)
                        canvas.DrawLine(offset + kor::Vec2(0.f, y), offset + kor::Vec2(Size().x, y), Paint::Stroked(color, 1.f));
                };
                lines(step, minor);
                lines(step * 5.f, major);
            }

            void PaintComment(Canvas& canvas, const GraphComment& comment, const Theme& t, const float pixel) const
            {
                Rect r = comment.rect;
                if (_drag == Drag::eComment && _comment == comment.id) r = r.Shift(_dragBy);
                if (_drag == Drag::eCommentResize && _comment == comment.id) r = ResizedComment();
                const Color color = Resolve(comment.color, t.primary);
                canvas.DrawRRect({ r, 8.f }, Paint::Fill(color.WithAlpha(0.03f)));
                canvas.DrawRRect({ Rect::LTRB(r.left, r.top, r.right, r.top + CommentTitle), Radii { 8.f, 8.f, 0.f, 0.f } }, Paint::Fill(color.WithAlpha(0.22f)));
                canvas.DrawText(comment.text, { r.left + 10.f, r.top + 4.f }, { .size = 14.f, .color = t.text });
                const bool picked = _pickedComments.contains(comment.id);
                canvas.DrawRRect({ r, 8.f }, Paint::Stroked(picked ? t.primary : color.WithAlpha(0.5f), (picked ? 2.f : 1.f) * pixel));
                // The corner that resizes it.
                const kor::Vec2 c { r.right - 4.f, r.bottom - 4.f };
                canvas.DrawLine(c - kor::Vec2(8.f, 0.f), c - kor::Vec2(0.f, 8.f), Paint::Stroked(color.WithAlpha(0.7f), 1.5f * pixel));
            }

            void PaintWire(Canvas& canvas, const GraphWire& wire, const Theme& t, const float pixel, const Rect& seen) const
            {
                if (_drag == Drag::eWire && _pickedUp && *_pickedUp == wire) return;   // in hand
                const auto from = PortPosition(wire.from, true), to = PortPosition(wire.to, false);
                if (!from || !to) return;
                {
                    // A curve stays inside the box of its ends and control points.
                    const auto [a, b] = WireControls(*from, *to);
                    const Rect hull = Rect::LTRB(std::min({ from->x, to->x, a.x, b.x }), std::min({ from->y, to->y, a.y, b.y }),
                                                 std::max({ from->x, to->x, a.x, b.x }), std::max({ from->y, to->y, a.y, b.y })).Inflate(4.f);
                    if (hull.Intersect(seen).Empty()) return;
                }
                const NodePort* port = Port(wire.from, true);
                const Color color = Resolve(port ? port->color : colors::Inherit, t.primary);
                const bool picked = _pickedWires.contains(wire);
                const auto [c1, c2] = WireControls(*from, *to);
                if (picked) canvas.DrawCubicBezier(*from, c1, c2, *to, Paint::Stroked(t.text.WithAlpha(0.5f), 6.f * pixel));
                canvas.DrawCubicBezier(*from, c1, c2, *to, Paint::Stroked(color, 2.5f * pixel));
            }

            void PaintLooseWire(Canvas& canvas, const Theme& t, const float pixel) const
            {
                const auto anchor = PortPosition(_wireFrom, _wireFromOutput);
                if (!anchor) return;
                const NodePort* port = Port(_wireFrom, _wireFromOutput);
                const Color color = Resolve(port ? port->color : colors::Inherit, t.primary);
                const kor::Vec2 from = _wireFromOutput ? *anchor : _wireTo, to = _wireFromOutput ? _wireTo : *anchor;
                const auto [c1, c2] = WireControls(from, to);
                canvas.DrawCubicBezier(from, c1, c2, to, Paint::Stroked(color.WithAlpha(0.8f), 2.5f * pixel));
            }

            void PaintPorts(Canvas& canvas, const GraphNode& node, const Rect& card, const Theme& t, const float pixel) const
            {
                const NodePort* dragged = _drag == Drag::eWire ? Port(_wireFrom, _wireFromOutput) : nullptr;
                for (const bool output : { false, true }) {
                    const auto& ports = output ? node.outputs : node.inputs;
                    for (std::size_t i = 0; i < ports.size(); ++i) {
                        const auto& port = ports[i];
                        const kor::Vec2 at = card.TopLeft() + PortOffset(output, i, card.Width());
                        Color color = Resolve(port.color, t.primary);
                        // While a wire is drawn, what it cannot go to fades.
                        if (dragged && (output == _wireFromOutput || !(output ? Fits(port, *dragged) : Fits(*dragged, port))))
                            color = color.WithAlpha(0.25f);
                        const bool wired = std::ranges::any_of(_config.graph.wires, [&](const GraphWire& w) {
                            return output ? w.from == PortRef { node.id, port.id } : w.to == PortRef { node.id, port.id };
                        });
                        canvas.DrawCircle(at, PortRadius, Paint::Fill(wired ? color : t.surface).SetStroke(1.5f * pixel, color));
                    }
                }
            }

            Config _config;
            kor::Vec2 _pan { 0.f, 0.f };
            float _zoom = 1.f;
            std::set<std::string> _pickedNodes, _pickedComments;
            std::set<GraphWire> _pickedWires;

            Drag _drag = Drag::eNone;
            kor::MouseButton _panButton = kor::MouseButton::eMiddle;
            kor::Vec2 _pressAt {};       // in the view
            bool _moved = false;
            kor::Vec2 _pointer {};       // in the graph: where a paste goes
            kor::Vec2 _dragFrom {}, _dragBy {};
            std::set<std::string> _carried, _carriedComments;
            kor::Vec2 _boxFrom {}, _boxTo {};
            PortRef _wireFrom;
            bool _wireFromOutput = true;
            kor::Vec2 _wireTo {};
            std::optional<GraphWire> _pickedUp;
            std::string _comment;
            Rect _commentRect {};
        };

        struct NodeCanvasWidget final : RenderObjectWidget {
            RenderNodeCanvas::Config config;
            std::vector<Widget> cards;
            NodeCanvasWidget(RenderNodeCanvas::Config c, std::vector<Widget> k) : config(std::move(c)), cards(std::move(k)) {}
            [[nodiscard]] std::unique_ptr<RenderObject> CreateRenderObject() const override { return std::make_unique<RenderNodeCanvas>(); }
            void UpdateRenderObject(RenderObject& object) const override { static_cast<RenderNodeCanvas&>(object).Set(config); }
            [[nodiscard]] const std::vector<Widget>& Children() const override { return cards; }
        };

        /** A node's card: its title, a row a port pair, and its own controls under them. */
        Widget Card(const GraphNode& node)
        {
            const Theme& t = Theme::Current();
            TextStyle title = t.textStyle;
            title.size = 13.f;
            title.weight = 600.f;
            TextStyle label = t.textStyle;
            label.size = 12.f;
            label.color = t.textMuted;

            std::vector<Widget> rows;
            rows.push_back(Container({ .height = TitleHeight, .padding = EdgeInsets::Symmetric(10.f, 0.f),
                                       .decoration = { .color = Resolve(node.accent, t.surfaceHover), .radius = Radii { 6.f, 6.f, 0.f, 0.f } } },
                                     Align(Alignment::CenterLeft(), Text(node.title, title, TextAlign::eStart, false))));
            const std::size_t count = std::max(node.inputs.size(), node.outputs.size());
            for (std::size_t i = 0; i < count; ++i) {
                Widget in = i < node.inputs.size() ? Text(node.inputs[i].label, label, TextAlign::eStart, false) : SizedBox(0.f, 0.f);
                Widget out = i < node.outputs.size() ? Text(node.outputs[i].label, label, TextAlign::eEnd, false) : SizedBox(0.f, 0.f);
                rows.push_back(Container({ .height = RowHeight, .padding = EdgeInsets::Symmetric(12.f, 0.f) },
                                         Row({ std::move(in), SizedBox(16.f, 0.f).Expanded(), std::move(out) },
                                             { .crossAxisAlignment = CrossAxisAlignment::eCenter })));
            }
            if (node.body) rows.push_back(node.body.Padding(EdgeInsets::All(8.f)));
            if (!node.error.empty()) {
                TextStyle error = label;
                error.color = ErrorColor;
                rows.push_back(Text(node.error, error).Padding(EdgeInsets::Symmetric(10.f, 6.f)));
            }
            if (rows.size() == 1) rows.push_back(SizedBox(0.f, 6.f));
            return Container({ .decoration = { .color = t.surface, .borderWidth = 1.f, .borderColor = t.border, .radius = 6.f } },
                             Column(std::move(rows), { .crossAxisAlignment = CrossAxisAlignment::eStretch, .mainAxisSize = MainAxisSize::eMin }))
                .Key(node.id);
        }
    }

    Widget NodeEditor(NodeGraph graph, NodeEditorOptions options)
    {
        std::vector<Widget> cards;
        cards.reserve(graph.nodes.size());
        for (const auto& node : graph.nodes) cards.push_back(Card(node));
        return Make<NodeCanvasWidget>(RenderNodeCanvas::Config { std::move(graph), std::move(options) }, std::move(cards));
    }
}
