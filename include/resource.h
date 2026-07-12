//
// Created by radue on 4/26/2026.
//

/**
 * @file resource.h
 * @brief Owning handles (@ref kor::Resource) and non-owning views (@ref kor::ResourceRef)
 *        for every GPU object the API hands out.
 *
 * A Resource is one of three things:
 *  - **valid**    — it owns the object, and everything works;
 *  - **poisoned** — construction failed, and it owns a @ref kor::Error explaining why;
 *  - **empty**    — default-constructed, owning nothing.
 *
 * A poisoned Resource is not an exception and not a null pointer: it is a first-class
 * value that flows through the API. Passing one into a builder poisons the resource that
 * builder produces (linking the original error as the cause); passing one into a command
 * buffer fails that recording. Nothing throws, and nothing is silently ignored.
 *
 * This exists so failures that are *fixable at runtime* can actually be fixed. A pipeline
 * whose shader failed to compile is poisoned rather than absent, so it keeps its identity:
 * when the shader is repaired the pipeline is rebuilt in place and every ResourceRef handed
 * out earlier — to a scene, a renderer, a recording lambda — observes the new object without
 * being re-issued. That only works because a ResourceRef points at a stable state block and
 * re-reads the object on every access, rather than caching the pointer at construction.
 */

#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "error.h"
#include "log.h"
#include "task.h"

namespace kor {
    // Marker base for resources the Repository drives once per frame. Inheriting
    // it (rather than just declaring automaticUpdate()) is what RefStorage keys
    // on, so the auto-update can never be silently disabled by an inaccessible
    // override — a non-public automaticUpdate() becomes a compile error.
    struct AutoUpdatable {
        virtual ~AutoUpdatable() = default;
        virtual void automaticUpdate() = 0;
    };

    template<typename ResourceType>
    class ResourceRef;

    /**
     * @brief Type-erased half of a Resource's state, and the thing ResourceRefs point at.
     *
     * Its address is stable for as long as the owning Resource lives, even across a repair
     * that replaces the underlying object. Refs therefore hold this, never the object.
     */
    struct ResourceStateBase {
        virtual ~ResourceStateBase() = default;

        std::shared_ptr<const Error> error;  ///< Non-null exactly when poisoned.
        std::uint64_t generation = 0;        ///< Bumped on every successful (re)build.
        std::string name;                    ///< Human label for logs, e.g. "shaders/forward.frag.glsl".

        // Every resource this one was built from, paired with the generation it had at build time.
        // A repair is only worth attempting once one of these has moved on — which is what stops a
        // broken pipeline from recompiling itself sixty times a second while you edit its shader.
        std::vector<std::pair<std::weak_ptr<ResourceStateBase>, std::uint64_t>> dependencies;

        // Set from *outside* the frame — a file-watcher thread noticing that a shader's source
        // changed. It is only a request: the repair itself happens on the main thread, in
        // Repository::update(), because it destroys and replaces the underlying object and must not
        // race a command buffer that is recording with it.
        std::atomic<bool> repairRequested{false};

        // Opaque data whose lifetime is tied to the resource *slot* rather than to the object in
        // it. A shader's file-watch state lives here: a shader that failed to compile has no
        // object to hang it on, yet it is precisely then that the watch must stay registered —
        // it is the thing that will notice the fix and trigger the retry.
        std::shared_ptr<void> attachment;

        [[nodiscard]] bool poisoned() const noexcept { return error != nullptr; }

        /**
         * @brief Re-run the stored builder, hoping the cause has been fixed.
         * @return true if the resource is valid after the attempt.
         *
         * Driven by the Repository once per frame for resources that need it. A resource with no
         * stored builder can never be repaired and always returns false.
         */
        virtual bool retry() = 0;

        /** @brief Whether this resource could ever be repaired at runtime. */
        [[nodiscard]] virtual bool recoverable() const noexcept = 0;

        /** @brief Ask for a repair on the next frame. Safe to call from any thread. */
        void requestRepair() noexcept { repairRequested.store(true, std::memory_order_relaxed); }

        /** @brief Whether any input has been rebuilt (or destroyed) since we last read it. */
        [[nodiscard]] bool dependenciesChanged() const {
            for (const auto& [weak, seen] : dependencies) {
                const auto dependency = weak.lock();
                if (!dependency) return true;  // destroyed: worth one attempt, to report it properly
                if (dependency->generation != seen) return true;
            }
            return false;
        }

        /**
         * @brief Whether this resource is broken and something has changed that might fix it.
         *
         * Consumes the repair request. Only poisoned resources are candidates: a *working* resource
         * whose shader changed is hot-reloaded in place (Shader::OnReload), which keeps its object
         * identity and its subscribers — a rebuild would throw those away.
         */
        [[nodiscard]] bool needsRepair() {
            if (!poisoned() || !recoverable()) return false;
            const bool requested = repairRequested.exchange(false, std::memory_order_relaxed);
            return requested || dependenciesChanged();
        }
    };

    template<typename T>
    struct ResourceState final : ResourceStateBase {
        std::unique_ptr<T> value;  ///< Null exactly when poisoned or empty.

        // The builder that produced (or failed to produce) `value`, captured by value so it
        // can be re-run later. Without this a poisoned resource has nothing to retry with,
        // and the "fix the shader, the pipeline comes back" story is impossible.
        std::function<Result<std::unique_ptr<T>>()> rebuild;

        // Reads back which inputs that builder consumed, and at which generation, after an attempt.
        // The builder re-adopts its inputs on every run, so this is how the dependency list stays
        // honest across a repair.
        std::function<std::vector<std::pair<std::weak_ptr<ResourceStateBase>, std::uint64_t>>()> collectDependencies;

        [[nodiscard]] bool recoverable() const noexcept override { return static_cast<bool>(rebuild); }

        bool retry() override {
            if (!rebuild) return false;

            auto result = rebuild();

            // Re-read the dependencies after *every* attempt, successful or not. A failed attempt
            // must still record the generations it just saw — otherwise dependenciesChanged() would
            // stay true forever and we would rebuild a still-broken pipeline on every single frame.
            if (collectDependencies) dependencies = collectDependencies();

            if (!result) {
                // Still broken. Keep the newest explanation — the user is iterating, and the
                // error they just caused is more useful than the one they already fixed.
                error = std::make_shared<const Error>(std::move(result.error()));
                value.reset();
                return false;
            }

            value = std::move(*result);
            error.reset();
            ++generation;  // un-gates everything downstream of us on the next frame
            log::info("Resource '{}' recovered.", name.empty() ? "<unnamed>" : name);
            return true;
        }
    };

    /**
     * @brief Owning handle to a GPU object, an error explaining its absence, or nothing.
     *
     * Move-only, like the unique_ptr it replaced. The shared_ptr inside is an implementation
     * detail: it exists so ResourceRefs can detect destruction, not to share ownership.
     */
    template<typename ResourceType>
    class Resource {
        template<typename>
        friend class ResourceRef;

        template<typename>
        friend class Resource;

        friend class Repository;

    public:
        /** @brief An empty resource: no object, no error. */
        Resource() : _state(std::make_shared<ResourceState<ResourceType>>()) {}
        Resource(std::nullptr_t) : Resource() {}

        /** @brief A valid resource owning @p ptr. */
        explicit Resource(std::unique_ptr<ResourceType> ptr, std::string name = {})
            : _state(std::make_shared<ResourceState<ResourceType>>())
        {
            _state->value = std::move(ptr);
            _state->name = std::move(name);
            _state->generation = 1;
        }

        /**
         * @brief A poisoned resource: construction failed, and this is why.
         *
         * The resource is still a live, addressable thing with a stable identity — refs may
         * be taken from it and will come good if it is ever repaired.
         */
        static Resource failed(Error error, std::string name = {}) {
            Resource r;
            r._state->error = std::make_shared<const Error>(std::move(error));
            r._state->name = std::move(name);
            return r;
        }

        Resource(Resource&&) noexcept = default;
        Resource& operator=(Resource&&) noexcept = default;

        Resource(const Resource&) = delete;
        Resource& operator=(const Resource&) = delete;

        // Dereference. The framework never reaches these on a poisoned resource: every path
        // into a backend goes through an API entry point (a builder's adopt(), a command
        // buffer's validation) that rejects poison before the core→backend cast that would
        // dereference it. Dereferencing a poisoned resource is therefore a bug in *calling*
        // code, and is treated as one: it is reported loudly rather than papered over.
        ResourceType& operator*() { checkUsable(); return *get(); }
        const ResourceType& operator*() const { checkUsable(); return *get(); }
        ResourceType* operator->() const { checkUsable(); return get(); }

        /** @brief The object, or nullptr if this resource is empty or poisoned. */
        [[nodiscard]] ResourceType* get() const noexcept {
            return valid() ? _state->value.get() : nullptr;
        }

        /** @brief True iff the object exists and is usable. */
        [[nodiscard]] bool valid() const noexcept {
            return _state && _state->value != nullptr && !_state->poisoned();
        }

        /** @brief True iff construction failed and an explanation is attached. */
        [[nodiscard]] bool poisoned() const noexcept { return _state && _state->poisoned(); }

        explicit operator bool() const noexcept { return valid(); }

        /** @brief Why this resource is unusable, or nullptr if it is fine. */
        [[nodiscard]] const Error* error() const noexcept {
            return _state ? _state->error.get() : nullptr;
        }

        /** @brief Shared handle to the error, for linking as another error's cause. */
        [[nodiscard]] std::shared_ptr<const Error> errorPtr() const noexcept {
            return _state ? _state->error : nullptr;
        }

        [[nodiscard]] std::uint64_t generation() const noexcept { return _state ? _state->generation : 0; }
        [[nodiscard]] const std::string& name() const noexcept {
            static const std::string empty;
            return _state ? _state->name : empty;
        }

        /** @brief Attempt to rebuild a poisoned resource. @see ResourceStateBase::retry */
        bool retry() { return _state && _state->retry(); }

        /** @brief Whether a repair is worth attempting right now. @see ResourceStateBase::needsRepair */
        [[nodiscard]] bool needsRepair() const { return _state && _state->needsRepair(); }

        /** @brief Ask for a repair on the next frame. Safe to call from any thread. */
        void requestRepair() const noexcept { if (_state) _state->requestRepair(); }

        /** @brief Install the builder used to repair this resource if it is ever broken. */
        void setRebuild(std::function<Result<std::unique_ptr<ResourceType>>()> rebuild) {
            if (_state) _state->rebuild = std::move(rebuild);
        }

        /** @brief Install the probe that re-reads the builder's inputs after each attempt. */
        void setDependencyProbe(
            std::function<std::vector<std::pair<std::weak_ptr<ResourceStateBase>, std::uint64_t>>()> probe) {
            if (_state) _state->collectDependencies = std::move(probe);
        }

        /** @brief Record a dependency and the generation it had when we consumed it. */
        void addDependency(const std::shared_ptr<ResourceStateBase>& dep, const std::uint64_t generation) {
            if (_state && dep) _state->dependencies.emplace_back(dep, generation);
        }

        /** @brief Destroy the object. Existing refs expire, exactly as before. */
        void reset() { _state.reset(); }

    private:
        void checkUsable() const {
            if (!_state || _state->value) return;

            if (_state->poisoned()) {
                log::error("Dereferenced the unusable resource '{}':\n{}",
                           _state->name.empty() ? "<unnamed>" : _state->name,
                           _state->error->history());
            } else {
                log::error("Dereferenced an empty resource.");
            }
        }

        std::shared_ptr<ResourceState<ResourceType>> _state;
    };

    /** @brief Construct a valid Resource<T> holding a T. */
    template<typename ResourceType, typename... Args>
    Resource<ResourceType> MakeResource(Args&&... args) {
        return Resource<ResourceType>(std::make_unique<ResourceType>(std::forward<Args>(args)...));
    }

    /**
     * @brief Construct a Resource<Public> whose object is a backend Impl.
     *
     * The state block is typed on the *public* type, so a `Resource<Buffer>` holding a
     * `vk::Buffer` needs no conversion afterwards. This replaces the old
     * `Resource<Buffer>(MakeResource<vk::Buffer>(...))` two-step, which cannot work once
     * refs point at a typed state block: converting the Resource would have to move the
     * state block, invalidating any ref already taken from it.
     */
    template<typename Public, typename Impl, typename... Args>
        requires std::is_base_of_v<Public, Impl>
    Resource<Public> MakeBackendResource(Args&&... args) {
        return Resource<Public>(std::unique_ptr<Public>(new Impl(std::forward<Args>(args)...)));
    }

    /**
     * @brief Construct a backend Impl behind a unique_ptr to its Public base.
     *
     * The unwrapped counterpart of @ref MakeBackendResource, for a builder's `create()`, which
     * works one level below the Resource: it produces the object (or an error), and the Resource
     * that owns it is assembled around that by Builder::materialize.
     */
    template<typename Public, typename Impl, typename... Args>
        requires std::is_base_of_v<Public, Impl>
    std::unique_ptr<Public> MakeBackendPtr(Args&&... args) {
        return std::unique_ptr<Public>(new Impl(std::forward<Args>(args)...));
    }

    /**
     * @brief Non-owning, lifetime-tracked view of a Resource.
     *
     * Holds the owner's state block, never the object itself, and re-reads the object on
     * every access. A ref handed out while its resource was poisoned starts working the
     * moment that resource is repaired — which is the whole point.
     */
    template<typename ResourceType>
    class ResourceRef {
        friend class Repository;

        template<typename U>
        friend class Resource;

        template<typename>
        friend class ResourceRef;

        using Raw = std::remove_const_t<ResourceType>;

        // Reads the object out of a state block, adjusting the pointer from the type actually
        // stored (which may be derived from ours) to ours. Instantiated per stored type at
        // construction, so an upcast ref keeps working across a repair that reseats the object.
        // It yields Raw* rather than ResourceType* so that a ResourceRef<T> and a
        // ResourceRef<const T> share one thunk type and can convert into each other.
        template<typename Stored>
        static Raw* readThunk(ResourceStateBase* state) {
            auto* typed = static_cast<ResourceState<std::remove_const_t<Stored>>*>(state);
            return typed->value.get();  // Stored* → Raw* applies any base-class adjustment
        }

    public:
        ResourceRef() = default;

        // From an owning Resource<Stored>, where Stored is our type or derives from it.
        // Covers the plain case (Resource<Buffer> → ResourceRef<const Buffer>) and the upcast
        // (Resource<StandardMesh> → ResourceRef<const Mesh>) in one constructor.
        template<typename Stored>
            requires (std::is_base_of_v<Raw, std::remove_const_t<Stored>> &&
                      (std::is_const_v<ResourceType> || !std::is_const_v<Stored>))
        ResourceRef(const Resource<Stored>& resource)
            : _life(resource._state),
              _state(resource._state.get()),
              _get(&readThunk<Stored>) {}

        // Const-qualifying conversion: ResourceRef<T> → ResourceRef<const T>. The thunk type
        // is identical (both yield Raw*), so it carries across unchanged.
        ResourceRef(const ResourceRef<Raw>& other)
            requires (std::is_const_v<ResourceType>)
            : _life(other._life), _state(other._state), _get(other._get),
              _direct(other._direct), _unsafe(other._unsafe) {}

        // Unsafe refs: a view onto an object this ref does not track the lifetime of, used
        // where a backend hands out `*this` or a raw pointer. No state block, so no poison
        // and no repair — the caller guarantees the object outlives the ref.
        explicit ResourceRef(const ResourceType& resource)
            : _direct(const_cast<Raw*>(&resource)), _unsafe(true) {}

        template<typename P>
        explicit ResourceRef(P* ptr)
            requires (
                std::is_same_v<std::remove_const_t<P>, Raw> &&
                (std::is_const_v<ResourceType> || !std::is_const_v<P>)
            )
            : _direct(const_cast<Raw*>(ptr)), _unsafe(true) {}

        ResourceRef(const ResourceRef&) = default;
        ResourceRef& operator=(const ResourceRef&) = default;
        ResourceRef(ResourceRef&&) = default;
        ResourceRef& operator=(ResourceRef&&) = default;
        ~ResourceRef() = default;

        ResourceType& operator*() const { return *checkedGet(); }
        ResourceType* operator->() const { return checkedGet(); }

        /** @brief The object, or nullptr if this ref is expired, empty or poisoned. */
        [[nodiscard]] ResourceType* get() const noexcept {
            if (_unsafe) return _direct;
            if (!alive() || _state->poisoned()) return nullptr;
            return _get ? _get(_state) : nullptr;
        }

        /** @brief Whether the owning Resource still exists. */
        [[nodiscard]] bool alive() const noexcept {
            return _unsafe ? _direct != nullptr : !_life.expired();
        }

        /** @brief Whether the resource this refers to failed to build. */
        [[nodiscard]] bool poisoned() const noexcept {
            return !_unsafe && alive() && _state->poisoned();
        }

        /** @brief True iff the object exists and is usable right now. */
        [[nodiscard]] bool valid() const noexcept { return get() != nullptr; }

        explicit operator bool() const noexcept { return valid(); }

        [[nodiscard]] const Error* error() const noexcept {
            return (!_unsafe && alive()) ? _state->error.get() : nullptr;
        }

        [[nodiscard]] std::shared_ptr<const Error> errorPtr() const noexcept {
            return (!_unsafe && alive()) ? _state->error : nullptr;
        }

        [[nodiscard]] std::uint64_t generation() const noexcept {
            return (!_unsafe && alive()) ? _state->generation : 0;
        }

        [[nodiscard]] std::string name() const {
            return (!_unsafe && alive()) ? _state->name : std::string{};
        }

        /** @brief The owner's state block, for a consumer to record as a dependency. */
        [[nodiscard]] std::shared_ptr<ResourceStateBase> state() const noexcept {
            return _unsafe ? nullptr : _life.lock();
        }

        /**
         * @brief Attempt to rebuild the resource behind this ref. @see ResourceStateBase::retry
         *
         * Available through a ref (not just the owner) because the thing that notices a fix — a
         * file-watch callback — only ever holds a ref, never the Resource.
         */
        bool retry() const { return !_unsafe && alive() && _state->retry(); }

        /** @brief Whether a repair is worth attempting right now. @see ResourceStateBase::needsRepair */
        [[nodiscard]] bool needsRepair() const { return !_unsafe && alive() && _state->needsRepair(); }

        /**
         * @brief Ask for a repair on the next frame. Safe to call from any thread.
         *
         * This is what a file-watcher callback calls. It must not do the repair itself: that swaps
         * out the underlying object, and would race whatever thread is recording with it.
         */
        void requestRepair() const noexcept { if (!_unsafe && alive()) _state->requestRepair(); }

        /** @brief Attach data whose lifetime follows the resource slot. @see ResourceStateBase::attachment */
        void attach(std::shared_ptr<void> data) const {
            if (!_unsafe && alive()) _state->attachment = std::move(data);
        }

    private:
        ResourceType* checkedGet() const {
            // An expired ref is a lifetime bug, not a build failure, and there is no valid
            // value to hand back — this keeps throwing, as it always has.
            if (!_unsafe && _life.expired()) {
                throw std::runtime_error("Attempted to dereference a ResourceRef whose Resource has been destroyed!");
            }
            if (poisoned()) {
                log::error("Dereferenced the unusable resource '{}':\n{}",
                           _state->name.empty() ? "<unnamed>" : _state->name,
                           _state->error->history());
                return nullptr;
            }
            return get();
        }

        // The state block's address is stable for the owner's whole life (unlike the object's,
        // which moves on repair), so caching it here is safe; _life is what detects destruction.
        std::weak_ptr<ResourceStateBase> _life;
        ResourceStateBase* _state = nullptr;
        Raw* (*_get)(ResourceStateBase*) = nullptr;

        Raw* _direct = nullptr;  // unsafe refs only
        bool _unsafe = false;
    };

    // Deduction guide: `ResourceRef ref = someResource;` deduces ResourceRef<T> from
    // Resource<T>. The constructor deduces `Stored`, not `ResourceType`, so without this
    // CTAD from a Resource would fail.
    template<typename T>
    ResourceRef(const Resource<T>&) -> ResourceRef<T>;

    class Repository {
        struct IStorage {
            virtual ~IStorage() = default;
            virtual void update() = 0;
            /** @return true if anything was actually brought back this pass. */
            virtual bool repair() = 0;
        };

        template<typename T>
        struct Storage final : IStorage {
            std::unordered_map<std::string, Resource<T>> items{};

            bool repair() override {
                bool recovered = false;
                for (auto& resource : items | std::views::values) {
                    if (resource.needsRepair()) recovered |= resource.retry();
                }
                return recovered;
            }

            void update() override {
                if constexpr (requires(T t) { t.update(); }) {
                    for (auto& [id, resource] : items) {
                        if (resource) {
                            resource->update();
                        }
                    }
                }
            }
        };

        template<typename T>
        struct RefStorage final : IStorage {
            std::vector<ResourceRef<T>> items{};

            bool repair() override {
                bool recovered = false;
                for (auto& item : items) {
                    if (item.needsRepair()) recovered |= item.retry();
                }
                return recovered;
            }

            void update() override {
                if constexpr (std::is_base_of_v<AutoUpdatable, T>) {
                    for (auto& item : items) {
                        if (item.valid()) {
                            const_cast<std::remove_const_t<T>&>(*item).automaticUpdate();
                        }
                    }
                }
            }
        };

    public:
        void update() {
            repair();

            for (const auto &storage: _refStorages | std::views::values) {
                storage->update();
            }
        }

        /**
         * @brief Bring back every resource that is broken and whose cause may have been fixed.
         *
         * Runs at the top of the frame, on the main thread — never on the file-watcher thread that
         * *noticed* the fix, because a repair destroys and replaces the underlying object.
         *
         * Poison flows one level at a time (a shader comes back, and only then can the pipelines
         * built from it), so a single pass repairs only the shallowest layer. Iterate until nothing
         * more comes back, bounded so a dependency cycle cannot spin the frame forever.
         */
        void repair() {
            constexpr int maxPasses = 8;
            for (int pass = 0; pass < maxPasses; ++pass) {
                bool recovered = false;
                for (const auto& storage : _storages | std::views::values)    recovered |= storage->repair();
                for (const auto& storage : _refStorages | std::views::values) recovered |= storage->repair();
                if (!recovered) return;
            }
            kor::log::warn("Resource repair did not settle after {} passes; giving up this frame.", maxPasses);
        }

        template<typename T>
        T& get(std::string_view id) {
            auto& s = storage<T>();
            if (!s) {
                kor::log::error("Resource of type '{}' not found in repository!", typeid(T).name());
                throw std::runtime_error("Resource of type '" + std::string(typeid(T).name()) + "' not found in repository!");
            }
            auto it = s.items.find(std::string(id));
            if (it == s.items.end()) {
                kor::log::error("Resource with id '{}' not found in repository!", id);
                throw std::runtime_error("Resource with id '" + std::string(id) + "' not found in repository!");
            }
            return *it->second;
        }

        template<typename T>
        const T& get(std::string_view id) const {
            const auto& s = tryStorage<T>();
            if (!s) {
                kor::log::error("Resource of type '{}' not found in repository!", typeid(T).name());
                throw std::runtime_error("Resource of type '" + std::string(typeid(T).name()) + "' not found in repository!");
            }
            const auto it = s->items.find(std::string(id));
            if (it == s->items.end()) {
                kor::log::error("Resource with id '{}' not found in repository!", id);
                throw std::runtime_error("Resource with id '" + std::string(id) + "' not found in repository!");
            }
            return *it->second;
        }

        template<typename T>
        ResourceRef<T> add(std::string_view id, Resource<T> resource) {
            auto& s = storage<T>();
            if (s.items.contains(std::string(id))) {
                kor::log::error("Resource with id '{}' already exists in repository!", id);
            }
            auto [it, inserted] = s.items.emplace(std::string(id), std::move(resource));
            return ResourceRef<T>(it->second);
        }

        template<typename T>
        void addRef(ResourceRef<T> resource) {
            auto& s = refStorage<T>();
            s.items.emplace_back(std::move(resource));
        }

        // Whether a resource of type T is registered under `id`.
        template<typename T>
        bool contains(const std::string_view id) {
            const auto* s = tryStorage<T>();
            return s && s->items.contains(std::string(id));
        }

        // A lifetime-tracked reference to the resource registered under `id`. Unlike
        // tryGet (which returns a raw pointer / "unsafe" ref), this carries the owning
        // Resource's lifetime stamp, so it is safe to store and pass around.
        template<typename T>
        ResourceRef<T> getRef(const std::string_view id) {
            auto& s = storage<T>();
            const auto it = s.items.find(std::string(id));
            if (it == s.items.end()) {
                kor::log::error("Resource with id '{}' not found in repository!", id);
                throw std::runtime_error("Resource with id '" + std::string(id) + "' not found in repository!");
            }
            return ResourceRef<T>(it->second);
        }

        template<typename T>
        T* tryGet(const std::string_view id) noexcept {
            auto* s = tryStorage<T>();
            if (!s) {
                return nullptr;
            }
            auto it = s->items.find(std::string(id));
            if (it == s->items.end()) {
                return nullptr;
            }
            return it->second ? &*it->second : nullptr;
        }

        template<typename T>
        const T* tryGet(const std::string_view id) const noexcept {
            const auto* s = tryStorage<T>();
            if (!s) {
                return nullptr;
            }
            const auto it = s->items.find(std::string(id));
            if (it == s->items.end()) {
                return nullptr;
            }
            return it->second ? &*it->second : nullptr;
        }

        // Defined in repository.cpp, not here: they drive the FileWatcher, which is an internal
        // header. A public header that includes one cannot be shipped — the installed SDK has no
        // src/ tree to resolve it against.
        Repository();
        ~Repository();

    private:
        template<typename T>
        Storage<T>& storage() {
            const std::type_index key(typeid(T));
            const auto it = _storages.find(key);
            if (it == _storages.end()) {
                auto ptr = std::make_unique<Storage<T>>();
                auto* raw = ptr.get();
                _storages.emplace(key, std::move(ptr));
                return *raw;
            }
            return *static_cast<Storage<T>*>(it->second.get());
        }

        template<typename T>
        RefStorage<T>& refStorage() {
            const std::type_index key(typeid(T));
            const auto it = _refStorages.find(key);
            if (it == _refStorages.end()) {
                auto ptr = std::make_unique<RefStorage<T>>();
                auto* raw = ptr.get();
                _refStorages.emplace(key, std::move(ptr));
                return *raw;
            }
            return *static_cast<RefStorage<T>*>(it->second.get());
        }

        template<typename T>
        Storage<T>* tryStorage() {
            const std::type_index key(typeid(T));
            const auto it = _storages.find(key);
            return it == _storages.end() ? nullptr : static_cast<Storage<T>*>(it->second.get());
        }

        template<typename T>
        const Storage<T>* tryStorage() const {
            const std::type_index key(typeid(T));
            const auto it = _storages.find(key);
            return it == _storages.end() ? nullptr : static_cast<const Storage<T>*>(it->second.get());
        }

        std::unordered_map<std::type_index, std::unique_ptr<IStorage>> _storages{};
        std::unordered_map<std::type_index, std::unique_ptr<IStorage>> _refStorages{};

        Task<void> _fileWatcherTask {};
    };
}
