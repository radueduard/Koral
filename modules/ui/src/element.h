//
// koral-ui: elements — the retained instances of widgets, which decide what to rebuild.
//

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <kui/widgets.h>

namespace kui
{
    class BuildOwner;

    /**
     * @brief A widget, placed: what stays in the tree while the widgets describing it come and go.
     *
     * Updating an element with a new widget of the same type (and key) keeps it — and its state and
     * render object — and only reconfigures it; anything else replaces it.
     */
    class Element {
    public:
        virtual ~Element();

        virtual void Mount(Element* parent, BuildOwner* owner);
        virtual void Update(const Widget& widget);
        /** @brief Leaving the tree: its children first. */
        virtual void Unmount();
        /** @brief Builds again, if it was marked. */
        virtual void Rebuild() {}
        /** @brief The render object that stands for it: its own, or its child's. */
        [[nodiscard]] virtual RenderObject* RenderObjectOf() const = 0;
        /** @brief Every child element. */
        virtual void VisitChildren(const std::function<void(Element&)>& visit) {}
        /** @brief A descendant's render object changed: the nearest render element above takes its children again. */
        virtual void ChildRenderObjectChanged();

        void MarkDirty();
        /** @brief Marks itself and everything below it that builds to be built again. */
        void MarkTreeDirty();

        [[nodiscard]] const Widget& GetWidget() const { return _widget; }
        [[nodiscard]] Element* Parent() const { return _parent; }
        [[nodiscard]] BuildOwner* GetOwner() const { return _owner; }
        [[nodiscard]] int Depth() const { return _depth; }
        [[nodiscard]] bool Dirty() const { return _dirty; }
        [[nodiscard]] bool Mounted() const { return _mounted; }

        /** @brief Whether an element built for @p a can be updated to show @p b. */
        static bool CanUpdate(const Widget& a, const Widget& b);

    protected:
        /** @brief Keeps, updates, replaces or removes @p child to show @p widget. */
        std::unique_ptr<Element> UpdateChild(std::unique_ptr<Element> child, const Widget& widget);
        /** @brief The same for a list: matched by key, then by type in order. */
        std::vector<std::unique_ptr<Element>> UpdateChildren(std::vector<std::unique_ptr<Element>> old, const std::vector<Widget>& widgets);
        std::unique_ptr<Element> Inflate(const Widget& widget);

        Widget _widget;
        Element* _parent = nullptr;
        BuildOwner* _owner = nullptr;
        int _depth = 0;
        bool _dirty = false;
        bool _mounted = false;

        friend class Widget;
        friend class BuildOwner;
        template <typename> friend struct ElementFactory;
    public:
        void SetWidget(Widget widget) { _widget = std::move(widget); }
    };

    /** @brief An element built from other widgets: stateless or stateful. */
    class ComponentElement : public Element {
    public:
        void Mount(Element* parent, BuildOwner* owner) override;
        void Update(const Widget& widget) override;
        void Unmount() override;
        void Rebuild() override;
        [[nodiscard]] RenderObject* RenderObjectOf() const override { return _child ? _child->RenderObjectOf() : nullptr; }
        void VisitChildren(const std::function<void(Element&)>& visit) override { if (_child) visit(*_child); }

    protected:
        [[nodiscard]] virtual Widget Build() = 0;
        std::unique_ptr<Element> _child;
    };

    class StatelessElement final : public ComponentElement {
    protected:
        Widget Build() override;
    };

    class StatefulElement final : public ComponentElement {
    public:
        void Mount(Element* parent, BuildOwner* owner) override;
        void Update(const Widget& widget) override;
        void Unmount() override;
        [[nodiscard]] StatefulWidget& State() const { return *_state; }

    protected:
        Widget Build() override;

    private:
        std::shared_ptr<StatefulWidget> _state;   ///< The first widget placed here: it holds the state.
    };

    /** @brief An element with a render object of its own, and render children. */
    class RenderObjectElement : public Element {
    public:
        void Mount(Element* parent, BuildOwner* owner) override;
        void Update(const Widget& widget) override;
        void Unmount() override;
        [[nodiscard]] RenderObject* RenderObjectOf() const override { return _object.get(); }
        void ChildRenderObjectChanged() override;
        void VisitChildren(const std::function<void(Element&)>& visit) override { for (auto& c : _children) visit(*c); }

    protected:
        /** @brief Hands the render object its children's render objects. */
        void TakeChildren();
        std::unique_ptr<RenderObject> _object;
        std::vector<std::unique_ptr<Element>> _children;
        /// While its own children are being made or updated: their render objects are taken once, after,
        /// not once per child as each appears (which made mounting a long list cubic).
        bool _takingChildren = false;
    };

    /**
     * @brief What keeps the element tree current: the elements SetState marked, built again before the
     *        frame — shallowest first, so a parent's rebuild that replaces a child spares the child its own.
     */
    class BuildOwner {
    public:
        void Schedule(Element& element);
        void Forget(Element& element);
        void Flush();
        void AddTicker(Element& element, std::function<bool(float)> tick);
        void Tick(float dt);

        std::size_t builds = 0;

    private:
        std::vector<Element*> _dirty;
        std::vector<Element*> _pending;   ///< Being rebuilt now; an element unmounted meanwhile is nulled out.
        struct Ticker { Element* element; std::function<bool(float)> tick; };
        std::vector<Ticker> _tickers;
    };

    /** @brief The theme in effect while a view builds, lays out and paints. */
    struct ThemeScope {
        const Theme* previous;
        explicit ThemeScope(const Theme& theme);
        ~ThemeScope();
    };
}
