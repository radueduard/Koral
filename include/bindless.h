//
// Bindless — a resource reached from a shader by an index, rather than through a descriptor set.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "api.h"
#include "resource.h"
#include "structs.h"

namespace kor
{
    class Buffer;
    class Image;
    class ImageView;
    class Sampler;
    class AccelerationStructure;

    /**
     * @brief The descriptor set a shader declares its bindless tables in.
     *
     * A shader does not declare them itself: it imports `koral_bindless` (Slang) or includes
     * `koral_bindless.glsl`, which declare one unbounded array per kind at this set, each at its
     * @ref BindlessBinding. Under descriptor sets the set is bound for you, before every draw and
     * dispatch of a pipeline that reads it; under a descriptor heap, the arrays *are* the heap.
     * Either way the project never binds it, and a pipeline's own sets stay below it.
     */
    inline constexpr kor::u32 BindlessSet = 7;

    /**
     * @brief Which table a bindless index is into — what kind of descriptor sits at it.
     *
     * One resource can have an index of more than one kind: an image both sampled and written as a
     * storage image has two, because the device describes those two uses differently. That is why
     * a handle is asked for by kind (Bindless::Sampled, Bindless::Storage, ...), not by resource.
     */
    enum class BindlessKind : std::uint8_t {
        eNone,                    ///< No descriptor: an invalid handle.
        eSampledImage,            ///< `Texture2D`, `texture2D`: an image read through a sampler.
        eStorageImage,            ///< `RWTexture2D`, `image2D`: an image read and written texel by texel.
        eStorageBuffer,           ///< `StructuredBuffer`, `ByteAddressBuffer`, `buffer` blocks.
        eUniformBuffer,           ///< `ConstantBuffer`, `uniform` blocks.
        eSampler,                 ///< `SamplerState`, `sampler`: filtering and addressing on their own.
        eAccelerationStructure,   ///< `RaytracingAccelerationStructure`, `accelerationStructureEXT`.
    };

    /** @brief The binding of @ref BindlessSet that a kind's table is declared at. */
    [[nodiscard]] KORAL_API kor::u32 BindlessBinding(BindlessKind kind);

    /** @brief The kind's name, for a message: "sampled image". */
    [[nodiscard]] KORAL_API const char* BindlessKindName(BindlessKind kind);

    /**
     * @brief An index into one of the bindless tables: what a shader is handed to reach a resource.
     *
     * Plain data, so it goes wherever a shader can read a `uint`: a push constant, a field of a
     * uniform or storage buffer, a vertex attribute. Hand it to a shader as its @ref index:
     *
     * @code
     * const auto albedo = kor::Bindless::Sampled(texture);
     * cb.PushConstant("albedo", albedo);                 // the shader declares `uint albedo;`
     * cb.PushConstant("albedo", texture);                // the same, the index asked for on the way
     * @endcode
     *
     * The index stays the same for as long as the resource lives, so a buffer of them written once
     * stays correct. It is given back when the resource is destroyed, and not before the frames
     * still reading it have finished.
     */
    struct DescriptorIndex
    {
        kor::u32 index = ~0u;                       ///< The slot in the kind's table; what the shader indexes with.
        BindlessKind kind = BindlessKind::eNone;    ///< Which table.

        /** @brief Whether this names a slot. An invalid one was refused, and the reason logged. */
        [[nodiscard]] bool Valid() const { return kind != BindlessKind::eNone && index != ~0u; }

        bool operator==(const DescriptorIndex&) const = default;
    };

    /**
     * @brief Hands out bindless indices: the resources a shader picks among at run time.
     *
     * Under both descriptor models, with the same indices and the same shader code — only how the
     * device holds the tables differs. A descriptor set binds what a draw reads ahead of the draw;
     * a bindless index lets the shader choose, from everything the project has made an index for,
     * while it runs: a material's textures from a buffer of materials, a mesh's vertices from a
     * buffer of meshes.
     *
     * @code
     * // C++
     * const auto albedo  = kor::Bindless::Sampled(albedoTexture);
     * const auto linear  = kor::Bindless::Sampler(linearSampler);
     * cb.PushConstant("albedo", albedo).PushConstant("linear", linear);
     *
     * // Slang
     * import koral_bindless;
     * [[vk::push_constant]] cbuffer Push { uint albedo; uint linear; };
     * float4 color = kor::texture2D(albedo).Sample(kor::sampler(linear), uv);
     * @endcode
     *
     * **Barriers.** The resolver sees what a draw binds, and cannot see what a shader picks by index.
     * An index handed to the shader through CommandBuffer::PushConstant is seen anyway, because the
     * index knows its resource: the draw that follows is synchronised for it exactly as for a bound
     * one. An index the shader reads out of a buffer is not; say so with CommandBuffer::Uses, or let
     * a frame-graph pass declare the read. Nothing can check a use that was not declared: a resource
     * written and then read through an undeclared index races exactly as it would through a pointer.
     *
     * **Limits.** A per-frame resource (Buffer::Builder::SetIsPerFrame, a per-frame image) has a
     * different object each frame and so no one index; asking for one is refused. Each table holds
     * @ref Capacity indices.
     */
    class KORAL_API Bindless
    {
    public:
        /** @brief An index to read @p view through a sampler. The image needs Image::Usage::eSampled. */
        static DescriptorIndex Sampled(const ResourceRef<const ImageView>& view);
        /**
         * @brief The same, for a whole image, viewed as @p shape (an ordinary 2D texture unless told).
         * @see Image::View
         */
        static DescriptorIndex Sampled(const ResourceRef<const Image>& image, ImageShape shape = ImageShape::e2D);

        /** @brief An index to read and write @p view texel by texel. The image needs Image::Usage::eStorage. */
        static DescriptorIndex Storage(const ResourceRef<const ImageView>& view);
        /** @brief The same, for a whole image viewed as @p shape. */
        static DescriptorIndex Storage(const ResourceRef<const Image>& image, ImageShape shape = ImageShape::e2D);

        /** @brief An index to @p buffer as a storage buffer. It needs Buffer::Usage::eStorage. */
        static DescriptorIndex StorageBuffer(const ResourceRef<const Buffer>& buffer);
        /** @brief An index to @p buffer as a uniform buffer. It needs Buffer::Usage::eUniform. */
        static DescriptorIndex UniformBuffer(const ResourceRef<const Buffer>& buffer);

        /** @brief An index to @p sampler, to pair in the shader with any sampled image. */
        static DescriptorIndex Sampler(const ResourceRef<const kor::Sampler>& sampler);

        /** @brief An index to a top-level acceleration structure, to trace rays against. */
        static DescriptorIndex AccelerationStructure(const ResourceRef<const kor::AccelerationStructure>& structure);

        /**
         * @brief Gives an index back before its resource is destroyed.
         *
         * Optional: destroying the resource gives it back. The slot is reused only once the frames
         * submitted so far have finished, so a draw already recorded with it still reads what it
         * meant to.
         */
        static void Release(DescriptorIndex handle);

        /** @brief How many indices a table of @p kind holds. 0 before the device exists, or for a kind this device has no table of. */
        [[nodiscard]] static kor::u32 Capacity(BindlessKind kind);

        /** @brief How many indices are handed out, across every kind. */
        [[nodiscard]] static std::size_t Count();
    };

    namespace detail
    {
        /** @brief The resource behind a live index, for the barrier resolver: one of the two is set. */
        struct BindlessTarget {
            ResourceRef<const Buffer> buffer;
            ResourceRef<const ImageView> view;
            kor::u64 offset = 0;
            kor::u64 size = ~0ull;
        };
        [[nodiscard]] KORAL_API std::optional<BindlessTarget> BindlessResource(DescriptorIndex handle);

        /**
         * @brief Gives back the indices of resources that have been destroyed, and rewrites those of
         *        images that were reallocated (a resize). Run once a frame by the scheduler, and before
         *        each new index is handed out.
         */
        KORAL_API void CollectBindless();
    }
}
