//
// Created by radue on 4/26/2026.
//

module;
#include "log.h"

#include "context.h"

export module gfx.resource;
import std;

namespace gfx {
    export template<typename ResourceType>
    class ResourceRef;

    export template<typename ResourceType>
    class Resource {
        template<typename>
        friend class Resource;

        template<typename>
        friend class ResourceRef;
    public:
        Resource() = default;
        Resource(std::nullptr_t) : resource(nullptr) {}
        explicit Resource(std::unique_ptr<ResourceType> ptr) : resource(std::move(ptr)) {
            AddToRepository();
        }

        template<typename U, typename = std::enable_if_t<std::is_base_of_v<ResourceType, U>>>
        Resource(Resource<U>&& other) noexcept : resource(std::move(other.resource)),
                                                 lifetimeStamp(std::move(other.lifetimeStamp)) {}

        Resource(Resource&&) noexcept = default;
        Resource& operator=(Resource&&) noexcept = default;

        Resource(const Resource&) = delete;
        Resource& operator=(const Resource&) = delete;

        ResourceType& operator*() { return *resource; }
        const ResourceType& operator*() const { return *resource; }

        ResourceType* operator->() const { return resource.get(); }

        explicit operator bool() const noexcept { return static_cast<bool>(resource); }

        void reset() { resource.reset(); }

        void AddToRepository();

    private:
        std::unique_ptr<ResourceType> resource{};
        std::shared_ptr<bool> lifetimeStamp{std::make_shared<bool>(true)};
    };

    export template<typename ResourceType, typename... Args>
    Resource<ResourceType> MakeResource(Args&&... args) {
        return Resource<ResourceType>(std::make_unique<ResourceType>(std::forward<Args>(args)...));
    }

    export template<typename ResourceType>
    class ResourceRef {
        friend class Repository;

        template<typename U>
        friend class Resource;

    public:
        ResourceRef() = default;

        ResourceRef(const Resource<std::remove_const_t<ResourceType>>& resource)
        requires (!std::is_const_v<ResourceType>)
            : resource(*resource.resource), lifetimeStamp(resource.lifetimeStamp) {}

        ResourceRef(const Resource<std::remove_const_t<ResourceType>>& resource)
            requires (std::is_const_v<ResourceType>)
            : resource(*resource.resource), lifetimeStamp(resource.lifetimeStamp) {}

        ResourceRef(const Resource<const std::remove_const_t<ResourceType>>& resource)
            requires (std::is_const_v<ResourceType>)
            : resource(*resource.resource), lifetimeStamp(resource.lifetimeStamp) {}

        // Implicit upcast: Resource<Derived> → ResourceRef<Base> / ResourceRef<const Base>
        template<typename Derived>
            requires ((!std::is_const_v<ResourceType>) &&
                     std::is_base_of_v<ResourceType, std::remove_const_t<Derived>> &&
                     (!std::is_same_v<std::remove_const_t<Derived>, std::remove_const_t<ResourceType>>))
        ResourceRef(const Resource<Derived>& resource)
            : resource(*resource.resource), lifetimeStamp(resource.lifetimeStamp) {}

        template<typename Derived>
            requires (std::is_const_v<ResourceType> &&
                     std::is_base_of_v<std::remove_const_t<ResourceType>, std::remove_const_t<Derived>> &&
                     (!std::is_same_v<std::remove_const_t<Derived>, std::remove_const_t<ResourceType>>))
        ResourceRef(const Resource<Derived>& resource)
            : resource(*resource.resource), lifetimeStamp(resource.lifetimeStamp) {}

        template<typename P>
        explicit ResourceRef(P* ptr)
            requires (
                std::is_same_v<std::remove_const_t<P>, std::remove_const_t<ResourceType>> &&
                (std::is_const_v<ResourceType> || !std::is_const_v<P>)
            )
            : resource(*ptr), unsafe(true) {}

        ResourceRef(const ResourceRef&) = default;
        ResourceRef& operator=(const ResourceRef&) = default;

        ResourceRef(ResourceRef&&) = default;
        ResourceRef& operator=(ResourceRef&&) = default;

        ~ResourceRef() = default;

        const ResourceType& operator*() const {
            if (lifetimeStamp.expired() && !unsafe) {
                throw std::runtime_error("Attempted to dereference a ResourceRef whose Resource has been destroyed!");
            }
            return resource.get();
        }
        ResourceType* operator->() const {
            if (lifetimeStamp.expired() && !unsafe) {
                throw std::runtime_error("Attempted to dereference a ResourceRef whose Resource has been destroyed!");
            }
            return &resource.get();
        }

        explicit operator bool() const noexcept { return !lifetimeStamp.expired(); }

    private:
        std::reference_wrapper<ResourceType> resource;
        std::weak_ptr<bool> lifetimeStamp;
        bool unsafe {false};
    };

    class Repository {
        struct IStorage {
            virtual ~IStorage() = default;
            virtual void update() = 0;
        };

        template<typename T>
        struct Storage final : IStorage {
            std::unordered_map<std::string, Resource<T>> items{};

            void update() override {
                if constexpr (requires(T& t) { t.update(); }) {
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

            void update() override {
                if constexpr (requires(T& t) { t.automaticUpdate(); }) {
                    for (auto& item : items) {
                        if (!item.lifetimeStamp.expired()) {
                            item->automaticUpdate();
                        }
                    }
                }
            }
        };

    public:
        void update() {
            for (const auto &storage: _storages | std::views::values) {
                storage->update();
            }
            for (const auto &storage: _refStorages | std::views::values) {
                storage->update();
            }
        }

        template<typename T>
        T& get(std::string_view id) {
            auto& s = storage<T>();
            if (!s) {
                gfx::log::error("Resource of type '{}' not found in repository!", typeid(T).name());
                throw std::runtime_error("Resource of type '" + std::string(typeid(T).name()) + "' not found in repository!");
            }
            auto it = s.items.find(std::string(id));
            if (it == s.items.end()) {
                gfx::log::error("Resource with id '{}' not found in repository!", id);
                throw std::runtime_error("Resource with id '" + std::string(id) + "' not found in repository!");
            }
            return *it->second;
        }

        template<typename T>
        const T& get(std::string_view id) const {
            const auto& s = tryStorage<T>();
            if (!s) {
                gfx::log::error("Resource of type '{}' not found in repository!", typeid(T).name());
                throw std::runtime_error("Resource of type '" + std::string(typeid(T).name()) + "' not found in repository!");
            }
            const auto it = s->items.find(std::string(id));
            if (it == s->items.end()) {
                gfx::log::error("Resource with id '{}' not found in repository!", id);
                throw std::runtime_error("Resource with id '" + std::string(id) + "' not found in repository!");
            }
            return *it->second;
        }

        template<typename T>
        ResourceRef<const T> add(std::string_view id, Resource<T> resource) {
            auto& s = storage<T>();
            if (s.items.contains(std::string(id))) {
                gfx::log::error("Resource with id '{}' already exists in repository!", id);
                return ResourceRef<const T>(s.items.at(std::string(id)));
            }
            s.items.emplace(std::string(id), std::move(resource));
            return ResourceRef<const T>(s.items.at(std::string(id)));
        }

        template<typename T>
        void addRef(ResourceRef<T> resource) {
            auto& s = refStorage<T>();
            s.items.emplace_back(std::move(resource));
        }

        template<typename T>
        void addRef(ResourceRef<const T> resource) {
            auto& s = refStorage<const T>();
            s.items.emplace_back(std::move(resource));
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

    private:
        template<typename>
        friend class Resource;

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
    };

    template<typename ResourceType>
    void Resource<ResourceType>::AddToRepository() {
        gfx::Context::Repository().addRef(ResourceRef<ResourceType>(*this));
    }

}
