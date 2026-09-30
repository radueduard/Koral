//
// What the C interface's translation units share: the handles, and nothing escaping into C.
//

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#include "koral_c.h"

#include "accelerationStructure.h"
#include "buffer.h"
#include "bufferView.h"
#include "computePipeline.h"
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include "rayTracingPipeline.h"
#include "resource.h"
#include "sampler.h"
#include "shader.h"
#include "token.h"

namespace kor::capi
{
    // ---- errors ------------------------------------------------------------------------------------

    /** The thread's last error, and the string a function returning one hands back. */
    std::string& LastError();
    const char* Keep(std::string text);

    KoralStatus Fail(std::string message);

    /** Runs @p body with nothing escaping into C: an exception becomes the thread's last error. */
    template<typename Body>
    auto Guarded(Body&& body, decltype(body()) failed) -> decltype(body())
    {
        try {
            LastError().clear();
            return body();
        } catch (const std::exception& e) {
            LastError() = e.what();
        } catch (...) {
            LastError() = "an unknown exception";
        }
        return failed;
    }

    template<typename Body>
    void GuardedVoid(Body&& body)
    {
        Guarded([&] { body(); return 0; }, 0);
    }

    template<typename E>
    Flags<E> FlagsOf(const std::uint32_t bits)
    {
        Flags<E> flags;
        for (std::uint32_t bit = 0; bit < 32; ++bit)
            if (bits & (1u << bit)) flags |= static_cast<E>(1u << bit);
        return flags;
    }

    template<typename E>
    std::uint32_t BitsOf(const Flags<E> flags) { return static_cast<std::uint32_t>(flags); }

    // ---- resources ---------------------------------------------------------------------------------

    template<typename T> constexpr KoralResourceKind KindOf();
    template<> constexpr KoralResourceKind KindOf<Buffer>() { return KORAL_RESOURCE_BUFFER; }
    template<> constexpr KoralResourceKind KindOf<Image>() { return KORAL_RESOURCE_IMAGE; }
    template<> constexpr KoralResourceKind KindOf<ImageView>() { return KORAL_RESOURCE_IMAGE_VIEW; }
    template<> constexpr KoralResourceKind KindOf<Sampler>() { return KORAL_RESOURCE_SAMPLER; }
    template<> constexpr KoralResourceKind KindOf<BufferView>() { return KORAL_RESOURCE_BUFFER_VIEW; }
    template<> constexpr KoralResourceKind KindOf<Shader>() { return KORAL_RESOURCE_SHADER; }
    template<> constexpr KoralResourceKind KindOf<GraphicsPipeline>() { return KORAL_RESOURCE_GRAPHICS_PIPELINE; }
    template<> constexpr KoralResourceKind KindOf<ComputePipeline>() { return KORAL_RESOURCE_COMPUTE_PIPELINE; }
    template<> constexpr KoralResourceKind KindOf<RayTracingPipeline>() { return KORAL_RESOURCE_RAY_TRACING_PIPELINE; }
    template<> constexpr KoralResourceKind KindOf<DescriptorSet>() { return KORAL_RESOURCE_DESCRIPTOR_SET; }
    template<> constexpr KoralResourceKind KindOf<DescriptorSetLayout>() { return KORAL_RESOURCE_DESCRIPTOR_SET_LAYOUT; }
    template<> constexpr KoralResourceKind KindOf<Framebuffer>() { return KORAL_RESOURCE_FRAMEBUFFER; }
    template<> constexpr KoralResourceKind KindOf<Mesh>() { return KORAL_RESOURCE_MESH; }
    template<> constexpr KoralResourceKind KindOf<AccelerationStructure>() { return KORAL_RESOURCE_ACCELERATION_STRUCTURE; }

    const char* KindName(KoralResourceKind kind);
}

/**
 * A resource handle: owned (a Resource<T>) or borrowed (a ResourceRef). One C type for every kind, so
 * the generic koral_resource_* functions work on any of them; the kind is checked wherever a particular
 * one is expected.
 */
struct KoralResource {
    virtual ~KoralResource() = default;
    [[nodiscard]] virtual KoralResourceKind Kind() const = 0;
    [[nodiscard]] virtual bool Owned() const = 0;
    [[nodiscard]] virtual bool Alive() const = 0;
    [[nodiscard]] virtual bool Valid() const = 0;
    [[nodiscard]] virtual bool Poisoned() const = 0;
    [[nodiscard]] virtual const kor::Error* Failure() const = 0;
    [[nodiscard]] virtual std::string Name() const = 0;
    virtual void SetName(std::string name) = 0;
    virtual bool Retry() = 0;
    [[nodiscard]] virtual const void* Identity() const = 0;
    [[nodiscard]] virtual KoralResource* Borrow() const = 0;
};

namespace kor::capi
{
    template<typename T>
    struct Handle final : KoralResource {
        Resource<T> owned;                // set for an owned handle
        ResourceRef<T> writable;          // set when the resource may be changed through this handle
        ResourceRef<const T> ref;         // always

        [[nodiscard]] KoralResourceKind Kind() const override { return KindOf<T>(); }
        [[nodiscard]] bool Owned() const override { return owned.Valid() || owned.Poisoned(); }
        [[nodiscard]] bool Alive() const override { return ref.Alive() || ref.Get() != nullptr; }
        [[nodiscard]] bool Valid() const override { return ref.Valid(); }
        [[nodiscard]] bool Poisoned() const override { return ref.Poisoned(); }
        [[nodiscard]] const Error* Failure() const override { return ref.Failure(); }
        [[nodiscard]] std::string Name() const override { return ref.Name(); }
        void SetName(std::string name) override {
            if (!Owned()) throw std::runtime_error("only the owner of a resource can name it");
            owned.SetName(std::move(name));
        }
        bool Retry() override { return ref.Retry(); }
        // The object, when there is one: a ref onto a resource's own object (an image's source, handed out
        // by its view) has no state block, and must still be the same resource as the owning handle.
        [[nodiscard]] const void* Identity() const override {
            if (const T* object = ref.Get()) return object;
            return ref.State().get();
        }
        [[nodiscard]] KoralResource* Borrow() const override {
            auto* copy = new Handle<T>();
            copy->writable = writable;
            copy->ref = ref;
            return copy;
        }
    };

    /** An owned handle onto what a builder built, poisoned or not. */
    template<typename T>
    KoralResource* Own(Resource<T> resource)
    {
        auto* handle = new Handle<T>();
        handle->owned = std::move(resource);
        handle->writable = ResourceRef<T>(handle->owned);
        handle->ref = ResourceRef<const T>(handle->owned);
        return handle;
    }

    /** A borrowed handle, or null for an empty ref (nothing there to borrow). */
    template<typename T>
    KoralResource* Borrow(const ResourceRef<const T>& ref)
    {
        if (!ref.Alive() && !ref.Get()) return nullptr;
        auto* handle = new Handle<T>();
        handle->ref = ref;
        return handle;
    }

    template<typename T>
    KoralResource* BorrowWritable(const ResourceRef<T>& ref)
    {
        if (!ref.Alive() && !ref.Get()) return nullptr;
        auto* handle = new Handle<T>();
        handle->writable = ref;
        handle->ref = ResourceRef<const T>(ref);
        return handle;
    }

    template<typename T>
    Handle<T>& HandleOf(KoralResource* resource, const char* what = nullptr)
    {
        if (!resource) throw std::runtime_error(std::string("no ") + (what ? what : KindName(KindOf<T>())) + " was given");
        auto* handle = dynamic_cast<Handle<T>*>(resource);
        if (!handle) {
            throw std::runtime_error(std::string("expected a ") + KindName(KindOf<T>()) + ", but was given a "
                                     + KindName(resource->Kind()));
        }
        return *handle;
    }

    /** What the handle refers to, for passing on: the ResourceRef the C++ API takes. */
    template<typename T>
    ResourceRef<const T> RefOf(KoralResource* resource) { return HandleOf<T>(resource).ref; }

    template<typename T>
    ResourceRef<const T> OptionalRefOf(KoralResource* resource) { return resource ? RefOf<T>(resource) : ResourceRef<const T>(); }

    [[noreturn]] void ThrowUnusable(const KoralResource& resource);

    /** The object itself, to call a member on — refused when it is gone or poisoned. */
    template<typename T>
    const T& Get(KoralResource* resource)
    {
        auto& handle = HandleOf<T>(resource);
        const T* object = handle.ref.Get();
        if (!object) ThrowUnusable(handle);
        return *object;
    }

    template<typename T>
    T& GetWritable(KoralResource* resource)
    {
        auto& handle = HandleOf<T>(resource);
        if (!handle.writable.Alive() && !handle.writable.Get()) {
            if (!handle.ref.Get()) ThrowUnusable(handle);
            throw std::runtime_error(std::string("this ") + KindName(KindOf<T>()) + " is read-only here");
        }
        T* object = handle.writable.Get();
        if (!object) ThrowUnusable(handle);
        return *object;
    }

    /** Any of the three pipelines, as the base the descriptor sets and push constants deal in. */
    ResourceRef<const Pipeline> PipelineOf(KoralResource* resource);

    // ---- builders ----------------------------------------------------------------------------------
}

/** A kor::Token of the caller's. */
struct KoralToken { kor::Token token; };

struct KoralBuilder {
    virtual ~KoralBuilder() = default;
    [[nodiscard]] virtual bool HasErrors() const = 0;
};

namespace kor::capi
{
    template<typename B>
    struct TypedBuilder final : KoralBuilder {
        template<typename... Args>
        explicit TypedBuilder(Args&&... args) : builder(std::forward<Args>(args)...) {}
        B builder;
        [[nodiscard]] bool HasErrors() const override { return builder.HasErrors(); }
    };

    template<typename B>
    B& BuilderOf(KoralBuilder* builder)
    {
        auto* typed = dynamic_cast<TypedBuilder<B>*>(builder);
        if (!typed) throw std::runtime_error(std::string("a builder of the wrong kind was given (expected ") + typeid(B).name() + ")");
        return typed->builder;
    }

    /** Runs a setter on the builder, with nothing escaping. */
    template<typename B, typename Body>
    void Set(KoralBuilder* builder, Body&& body)
    {
        GuardedVoid([&] { body(BuilderOf<B>(builder)); });
    }

    /** Builds, into an owned handle; null only when something threw at the boundary. */
    template<typename B, typename Body>
    KoralResource* Build(KoralBuilder* builder, Body&& body)
    {
        return Guarded([&]() -> KoralResource* { return Own(body(BuilderOf<B>(builder))); }, static_cast<KoralResource*>(nullptr));
    }

    // ---- values ------------------------------------------------------------------------------------

    ClearColor ClearColorOf(const KoralClearColor& color);
    KoralClearColor ClearColorOf(const ClearColor& color);
    VertexLayout VertexLayoutOf(const KoralVertexLayout& layout);
}
