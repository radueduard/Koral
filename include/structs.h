//
// Created by radue on 3/6/2026.
//

#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <variant>
#include <string>
#include <glm/glm.hpp>

#include "flags.h"
#include "api.h"
#include "resource.h"

namespace kor
{
    class Buffer;
    class Image;
    class Framebuffer;

    /**
     * @brief The type of vertex input pipe channels. Used to define the format of vertex attributes in the graphics pipeline.
     */
    /**
     * @brief "All of it, from here on" — the whole of a buffer, or every remaining element.
     *
     * The one sentinel the API uses for this. A count of 0 means zero, everywhere, so asking for
     * nothing and asking for everything cannot be confused; leaving the parameter defaulted asks
     * for everything, which is what it is defaulted to.
     *
     * @code
     * cb.CopyBuffer(src, dst);                  // the whole buffer
     * const auto all  = buffer->Read<T>();      // every element
     * const auto some = buffer->Read<T>(16);    // sixteen of them
     * const auto none = buffer->Read<T>(0);     // none
     * @endcode
     */
    inline constexpr glm::u64 WholeSize = std::numeric_limits<glm::u64>::max();

    enum class ChannelType : std::uint8_t {
        eFloat = 0,
        eInt = 1,
        eUInt = 2,
        eShort = 3,
        eUShort = 4,
        eByte = 5,
        eUByte = 6,
        eDouble = 7
    };

    /**
     * @brief What a resource is about to be used for, as one value pairing a pipeline stage with an
     *        access kind.
     *
     * The vocabulary barriers are written in: `BufferBarrier(buf, ResourceAccess::eComputeWrite)`
     * says "a compute shader is about to write this", and the backend turns that into the stage and
     * access masks its API wants. One enum rather than two independent masks, because the pairs that
     * make sense are few and the ones that do not are a common source of silent stalls.
     *
     * The command buffer also tracks the last access of every resource it touches, so most barriers
     * are inserted for you; naming one by hand is for the cases it cannot infer.
     *
     * The trailing comment on each is the Vulkan stage + access it maps to.
     */
    enum class ResourceAccess : std::uint8_t {
        // Compute
        eComputeRead,         // COMPUTE_SHADER + SHADER_READ
        eComputeWrite,        // COMPUTE_SHADER + SHADER_WRITE
        eComputeReadWrite,    // COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Vertex pipeline
        eVertexBuffer,        // VERTEX_INPUT + VERTEX_ATTRIBUTE_READ
        eIndexBuffer,         // VERTEX_INPUT + INDEX_READ
        eIndirectBuffer,      // DRAW_INDIRECT + INDIRECT_COMMAND_READ

        // Vertex shader
        eVertexShaderRead,    // VERTEX_SHADER + SHADER_READ
        eVertexShaderWrite,   // VERTEX_SHADER + SHADER_WRITE
        eVertexShaderReadWrite, // VERTEX_SHADER + SHADER_READ | SHADER_WRITE

        // Fragment shader
        eFragmentShaderRead,  // FRAGMENT_SHADER + SHADER_READ
        eFragmentShaderWrite, // FRAGMENT_SHADER + SHADER_WRITE
        eFragmentShaderReadWrite, // FRAGMENT_SHADER + SHADER_READ | SHADER_WRITE

        // Attachments
        eColorAttachment,         // COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE
        eDepthStencilAttachment,  // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        eDepthStencilRead,        // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ
        eDepthAttachment,         // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        eDepthRead,               // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ
        eStencilAttachment,       // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        eStencilRead,             // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ

        // Transfer
        eTransferSrc,         // TRANSFER + TRANSFER_READ
        eTransferDst,         // TRANSFER + TRANSFER_WRITE

        // General
        eAllShaderRead,          // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ
        eAllShaderWrite,         // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_WRITE
        eAllShaderReadWrite,     // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Present
        ePresent,             // COLOR_ATTACHMENT_OUTPUT + 0 (no access mask needed)
    };

    /**
     * @brief How an image binding is shaped, as the shader itself declared it.
     *
     * `sampler2D`, `samplerCube`, `image3D`, `sampler2DArray` — the shader says which, and this is
     * where that survives reflection. It matters because an image alone cannot always answer the
     * question: six array layers are equally a cube map and a six-layer 2D array, and only the
     * shader knows which one it means to sample. Handing an Image straight to a descriptor set or a
     * framebuffer relies on this to build the view the binding actually wants.
     *
     * Mirrors ImageView::Type, plus @ref eBuffer for a texel buffer and @ref eUnknown for a binding
     * that is not an image at all. Lives here rather than on Shader so that image.h can name it
     * without pulling in the whole of shader.h. @see Shader::ImageShape, Image::view
     */
    enum class ImageShape : std::uint8_t {
        eUnknown,   ///< Not an image binding, or a shape reflection could not name.
        e1D,        ///< `sampler1D`, `image1D`.
        e2D,        ///< `sampler2D`, `image2D`. The ordinary case.
        e3D,        ///< `sampler3D`, `image3D`.
        eCube,      ///< `samplerCube`, `imageCube`.
        e1DArray,   ///< `sampler1DArray`.
        e2DArray,   ///< `sampler2DArray`.
        eCubeArray, ///< `samplerCubeArray`.
        eBuffer,    ///< `samplerBuffer`, `imageBuffer` — a texel buffer, viewed through a BufferView.
    };

    enum class DescriptorType : std::uint8_t {
        eUniformBuffer,             ///< This type of descriptor is used to bind a buffer that contains uniform data,
                                    ///< which is read-only data that is accessed by shaders. Uniform buffers are
                                    ///< typically used to store data that is shared across multiple draw calls,
                                    ///< such as transformation matrices, lighting parameters, or material properties.
                                    ///< In shader code it would appear like this:
                                    ///< - glsl: layout(set = X, binding = Y) uniform MyUniformBuffer { ... } myUniformBuffer;
                                    ///< - hlsl: cbuffer MyUniformBuffer : register(bY, spaceX) { ... } myUniformBuffer;
                                    ///< - slang: [[vk::binding(Y, X)]] ConstantBuffer<MyUniformBuffer> myUniformBuffer;
        eStorageBuffer,             ///< This type of descriptor is used to bind a buffer that contains storage data, which is
                                    /// read-write data that is accessed by shaders. Storage buffers are typically used to store
                                    /// data that is modified by shaders, such as particle positions, compute shader output, or indirect draw command parameters.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) buffer MyStorageBuffer { ... } myStorageBuffer;
                                    /// - hlsl: RWStructuredBuffer<MyStorageBuffer> myStorageBuffer : register(uY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] RWStructuredBuffer<MyStorageBuffer> myStorageBuffer;
        eCombinedImageSampler,      ///< This type of descriptor is used to bind a combined image sampler, which is a combination of an image view and a sampler.
                                    /// It is used to sample textures in shaders. Combined image samplers are typically used to bind textures that are accessed by shaders, such as diffuse maps, normal maps, or shadow maps.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform sampler2D myCombinedImageSampler;
                                    /// - hlsl: Texture2D myCombinedImageSampler : register(tY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] Texture2D myCombinedImageSampler;
        eStorageImage,              ///< This type of descriptor is used to bind an image view that can be read from and written to in shaders. Storage images are typically used to store data that is modified by shaders, such as render targets, compute shader output, or image load/store operations.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform image2D myStorageImage;
                                    /// - hlsl: RWTexture2D<float4> myStorageImage : register(uY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] RWTexture2D<float4> myStorageImage;
        eSampler,                   ///< This type of descriptor is used to bind a sampler, which is an object that defines how textures are sampled in shaders. Samplers are typically used to bind samplers that are accessed by shaders, such as linear filtering, anisotropic filtering, or comparison samplers.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform sampler mySampler;
                                    /// - hlsl: SamplerState mySampler : register(sY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] SamplerState mySampler;
        eSampledImage,              ///< This type of descriptor is used to bind an image view that can only be read from in shaders. Sampled images are typically used to store textures that are accessed by shaders, such as diffuse maps, normal maps, or shadow maps.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform texture2D mySampledImage;
                                    /// - hlsl: Texture2D mySampledImage : register(tY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] Texture2D mySampledImage;
        eUniformTexelBuffer,        ///< This type of descriptor is used to bind a buffer that contains uniform texel data, which is read-only data that is accessed by shaders. Uniform texel buffers are typically used to store data that is shared across multiple draw calls, such as transformation matrices, lighting parameters, or material properties, but in a format that allows for more efficient sampling in shaders.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform samplerBuffer myUniformTexelBuffer;
                                    /// - hlsl: Buffer<float4> myUniformTexelBuffer : register(tY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] Buffer<float4> myUniformTexelBuffer;
        eStorageTexelBuffer,        ///< This type of descriptor is used to bind a buffer that contains storage texel data, which is read-write data that is accessed by shaders. Storage texel buffers are typically used to store data that is modified by shaders, such as particle positions, compute shader output, or indirect draw command parameters, but in a format that allows for more efficient sampling in shaders.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform samplerBuffer myStorageTexelBuffer;
                                    /// - hlsl: RWBuffer<float4> myStorageTexelBuffer : register(uY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] RWBuffer<float4> myStorageTexelBuffer;
        eAccelerationStructure,     ///< This type of descriptor is used to bind a ray-tracing acceleration structure (a TLAS), which is traversed by ray queries or trace calls in shaders.
                                    /// In shader code it would appear like this:
                                    /// - glsl: layout(set = X, binding = Y) uniform accelerationStructureEXT myTLAS;
                                    /// - hlsl: RaytracingAccelerationStructure myTLAS : register(tY, spaceX);
                                    /// - slang: [[vk::binding(Y, X)]] RaytracingAccelerationStructure myTLAS;
    };

    /**
     * @brief Description of a vertex input attribute. Used to define the format of vertex attributes in the graphics pipeline.
     */
    struct KORAL_API VertexInputAttributeDescription
    {
        glm::u32 location;          ///< The location of the vertex attribute in the shader. Must match the location specified in the shader code.
        glm::u32 binding;           ///< The binding index of the vertex buffer that contains this attribute. Must match the binding index specified in the vertex input binding description.
        glm::u32 channelCount;      ///< The number of channels in the vertex attribute. For example, a vec3 would have a channel count of 3.
        ChannelType channelType;    ///< The type of the channels in the vertex attribute. For example, a vec3 of floats would have a channel type of eFloat.
        glm::u32 offset;            ///< The byte offset of this attribute from the start of the vertex. For example, if the vertex has a vec3 position followed by a vec2 texCoord, the offset of the texCoord attribute would be sizeof(float) * 3 = 12 bytes. If a struct is available, it is recommended to use the offsetof macro to calculate the offset of each attribute within the vertex struct, as this will ensure correct offsets even if the struct is modified in the future.
    };

    /**
     * @brief Description of a vertex input binding. Used to define the format of vertex buffers in the graphics pipeline.
     */
    struct KORAL_API VertexInputBindingDescription
    {
        glm::u32 binding;   ///< The binding index of the vertex buffer. Must match the binding index specified in the vertex input attribute descriptions that reference this binding.
        glm::u32 stride;    ///< The byte stride between consecutive vertices in the vertex buffer. For example, if the vertex struct has
                            ///< a size of 32 bytes, the stride would be 32. It is recommended to use the sizeof operator on the vertex struct to calculate
                            ///< the stride, as this will ensure correct stride even if the struct is modified in the future.
    };

    /**
     * @brief Returns the size in bytes of a single channel of the given channel type. For example, if the channel type is eFloat,
     * this function will return 4, since a single float channel is 4 bytes. This is useful for calculating the offset of vertex
     * attributes within a vertex struct, as well as for calculating the stride of vertex buffers.
     *
     * @param channelType The type of the channel. For example, if the vertex attribute is a vec3 of floats, the channel type would be eFloat.
     * @return The size in bytes of a single channel of the given channel type. For example, if the channel type is eFloat, this function will return 4.
     */
    inline glm::u32 sizeofChannelType(const ChannelType channelType) {
        switch (channelType) {
        case ChannelType::eFloat: return sizeof(float);
        case ChannelType::eInt: return sizeof(int);
        case ChannelType::eUInt: return sizeof(unsigned int);
        case ChannelType::eShort: return sizeof(short);
        case ChannelType::eUShort: return sizeof(unsigned short);
        case ChannelType::eByte: return sizeof(char);
        case ChannelType::eUByte: return sizeof(unsigned char);
        case ChannelType::eDouble: return sizeof(double);
        default: throw std::runtime_error("Unknown channel type!");
        }
    }

    /**
     * @brief The type of primitive topology. Used to define how the vertices are assembled into primitives in the graphics pipeline.
     */
    enum class Topology : std::uint8_t {
        ePointList = 0,                 ///< Each vertex represents a single point.
        eLineList = 1,                  ///< Every two vertices form a separate line segment.
        eLineStrip = 2,                 ///< The first two vertices form the first line segment, and each subsequent vertex forms a new line segment with the previous vertex.
        eTriangleList = 3,              ///< Every three vertices form a separate triangle.
        eTriangleStrip = 4,             ///< The first three vertices form the first triangle, and each subsequent vertex forms a new triangle with the previous two vertices.
        eTriangleFan = 5,               ///< The first vertex is the center of the fan, and each subsequent pair of vertices forms a new triangle with the center vertex.
        eLineListAdjacency = 6,         ///< Every two vertices form a separate line segment, and each line segment is adjacent to two additional vertices that provide connectivity information for geometry shaders.
        eLineStripAdjacency = 7,        ///< The first two vertices form the first line segment, and each subsequent vertex forms a new line segment with the previous vertex. Each line segment is adjacent to two additional vertices that provide connectivity information for geometry shaders.
        eTriangleListAdjacency = 8,     ///< Every three vertices form a separate triangle, and each triangle is adjacent to three additional vertices that provide connectivity information for geometry shaders.
        eTriangleStripAdjacency = 9,    ///< The first three vertices form the first triangle, and each subsequent vertex forms a new triangle with the previous two vertices. Each triangle is adjacent to three additional vertices that provide connectivity information for geometry shaders.
        ePatchList = 10                 ///< Every N vertices form a separate patch, where N is specified by the patch control points state in the graphics pipeline. Patches are used for tessellation.
    };

    /**
     * @brief Description of the input assembly state. Used to define how the vertices are assembled into primitives in the graphics pipeline.
     */
    struct KORAL_API InputAssemblyState
    {
        Topology topology = Topology::eTriangleList;    ///< The type of primitive topology. For example, if the topology is eTriangleList, every three vertices will form a separate triangle.
        bool primitiveRestartEnable = false;            ///< Whether primitive restart is enabled. If true, a special index value (the maximum value of the index type - 0xFFFFFFFF for 32-bit indices) can be used in the index buffer to indicate that the current primitive should be restarted, and a new primitive should
                                                        ///< be started with the next vertex. This is only relevant for indexed draw calls, and can be useful for drawing multiple disconnected primitives with a single
                                                        ///< draw call.
    };

    /**
     * @brief The type of polygon mode. Used to define how polygons are rasterized in the graphics pipeline.
     */
    enum class PolygonMode : std::uint8_t {
        eFill = 0,      ///< Polygons are filled in. This is the default mode and the most common one for rendering solid objects.
        eLine = 1,      ///< Polygons are rasterized as wireframes, with only the edges of the polygons being drawn. This can be useful for debugging or for rendering wireframe models.
        ePoint = 2      ///< Polygons are rasterized as points, with only the vertices of the polygons being drawn. This can be useful for debugging or for rendering point cloud data.
    };

    /**
     * @brief The type of cull mode. Used to define which faces of polygons are culled (not drawn) in the graphics pipeline.
     */
    enum class CullMode : std::uint8_t {
        eFront = 1,     ///< Front faces of polygons are culled. The definition of front and back faces is determined by the front face setting in the rasterization state.
        eBack = 2,      ///< Back faces of polygons are culled. The definition of front and back faces is determined by the front face setting in the rasterization state.
    };
    template<> struct enable_flags<CullMode> : std::true_type {};

    /**
     * @brief The type of front face. Used to define which faces of polygons are considered front-facing in the graphics pipeline.
     */
    enum class FrontFace : std::uint8_t {
        eCounterClockwise = false,  ///< Polygons with vertices in counter-clockwise order are considered front-facing. This is the default setting and the most common one.
        eClockwise = true           ///< Polygons with vertices in clockwise order are considered front-facing. This can be useful if your modeling software exports models with a different winding order than the default.
    };

    /**
     * @brief Description of the rasterization state. Used to define how polygons are rasterized in the graphics pipeline.
     */
    struct KORAL_API RasterizationState
    {
        bool depthClampEnable = false;                          ///< If true, fragments that are outside the near and far planes will be clamped to the respective plane instead of being discarded. This can be useful for rendering shadow maps or for rendering objects that are partially behind the camera.
        bool rasterizerDiscardEnable = false;                   ///< If true, primitives will be discarded before the rasterization stage, meaning that no fragments will be generated. This can be useful for rendering techniques that only need to process vertex data without actually drawing anything, such as occlusion culling or transform feedback.
        PolygonMode polygonMode = PolygonMode::eFill;           ///< The polygon mode determines how polygons are rasterized. For example, if the polygon mode is eLine, polygons will be rasterized as wireframes, with only the edges of the polygons being drawn.
        Flags<CullMode> cullMode = Flags<CullMode>();           ///< The cull mode determines which faces of polygons are culled (not drawn). For example, if the cull mode is eBack, back faces of polygons will be culled. The definition of front and back faces is determined by the front face setting in the rasterization state. By default, no faces are culled.
        FrontFace frontFace = FrontFace::eCounterClockwise;     ///< The front face setting determines which faces of polygons are considered front-facing. For example, if the front face is eCounterClockwise, polygons with vertices in counter-clockwise order will be considered front-facing. This is the default setting and the most common one.
        bool depthBiasEnable = false;                           ///< If true, depth bias will be applied to fragments during rasterization. Depth bias can be used to prevent z-fighting when rendering coplanar geometry, such as decals or shadow maps. The depth bias settings (constant factor, clamp, and slope factor) determine how the depth bias is calculated and applied.
        float depthBiasConstantFactor = 0.f;                    ///< The constant factor is added to the depth value of each fragment. This can be used to apply a fixed depth bias to all fragments, regardless of their slope or distance from the camera.
        float depthBiasClamp = 0.f;                             ///< The clamp value is the maximum (or minimum, if negative) depth bias that can be applied to a fragment. This can be used to prevent excessively large depth bias values that could cause rendering artifacts.
        float depthBiasSlopeFactor = 0.f;                       ///< The slope factor is multiplied by the maximum depth slope of the fragment and added to the depth bias. This can be used to apply a variable depth bias that increases with the slope of the geometry, which can help to prevent z-fighting on steep surfaces.
        float lineWidth = 1.f;                                  ///< The width of lines when the polygon mode is set to eLine. This can be used to make wireframe rendering more visible. Note that wide lines may not be supported on all hardware, and may have performance implications.
    };

    /**
     * @brief The type of sample count for multisampling. Used to define the number of samples per pixel for multisampled images and framebuffers in the graphics pipeline.
     */
    enum class SampleCount : std::uint8_t {
        e1 = 1,
        e2 = 2,
        e4 = 4,
        e8 = 8,
        e16 = 16,
        e32 = 32,
        e64 = 64
    };

    /**
     * @brief Description of the multisampling state. Used to define the multisampling settings for multisampled images and framebuffers in the graphics pipeline.
     */
    struct KORAL_API MultisampleState
    {
        SampleCount sampleCount = SampleCount::e1;      ///< The number of samples per pixel. For example, if the sample count is e4, each pixel will have 4 samples, which can be used for anti-aliasing or for rendering to multisampled framebuffers.
        bool sampleShadingEnable = false;               ///< If true, sample shading will be enabled, meaning that the fragment shader will be executed for each sample instead of once per pixel. This can be useful for improving the quality of anti-aliasing when using multisampling, as it allows for more accurate shading of edges and fine details.
        float minSampleShading = 1.f;                   ///< The minimum fraction of samples that must be shaded when sample shading is enabled. For example, if the min sample shading is 0.5, at least half of the samples in each pixel will be shaded. This can be used to balance the quality and performance of sample shading, as higher values will result in better quality but lower performance.
    };

    /**
     * @brief The type of comparison operation. Used to define the comparison function for depth testing and stencil testing in the graphics pipeline.
     */
    enum class CompareOp : std::uint8_t {
        eNever = 0,             ///< The comparison always fails, meaning that the fragment will be discarded. This can be useful for rendering techniques that require manual control over which fragments are drawn, such as stencil masking or depth pre-pass.
        eLess = 1,              ///< The comparison passes if the fragment's depth value is less than the existing depth value in the depth buffer. This is the default depth comparison function and is commonly used for rendering solid objects.
        eEqual = 2,             ///< The comparison passes if the fragment's depth value is equal to the existing depth value in the depth buffer. This can be useful for rendering techniques that require exact depth matches, such as decals or shadow maps.
        eLessOrEqual = 3,       ///< The comparison passes if the fragment's depth value is less than or equal to the existing depth value in the depth buffer. This can be useful for rendering techniques that require inclusive depth testing, such as rendering transparent objects or for certain shadow mapping techniques.
        eGreater = 4,           ///< The comparison passes if the fragment's depth value is greater than the existing depth value in the depth buffer. This can be useful for rendering techniques that require reverse depth testing, such as rendering skyboxes or for certain shadow mapping techniques.
        eNotEqual = 5,          ///< The comparison passes if the fragment's depth value is not equal to the existing depth value in the depth buffer. This can be useful for rendering techniques that require non-equal depth testing, such as rendering outlines or for certain shadow mapping techniques.
        eGreaterOrEqual = 6,    ///< The comparison passes if the fragment's depth value is greater than or equal to the existing depth value in the depth buffer. This can be useful for rendering techniques that require inclusive reverse depth testing, such as rendering transparent objects or for certain shadow mapping techniques.
        eAlways = 7             ///< The comparison always passes, meaning that the fragment will always be drawn. This can be useful for rendering techniques that require manual control over which fragments are drawn, such as stencil masking or depth pre-pass.
    };

    /**
     * @brief The type of stencil operation. Used to define the operations that are performed on the stencil buffer during stencil testing in the graphics pipeline.
     */
    enum class StencilOp : std::uint8_t {
        eKeep = 0,                  ///< Keep the existing stencil value. This is the default operation and is commonly used when you want to preserve the current stencil buffer contents.
        eZero = 1,                  ///< Set the stencil value to zero. This can be useful for clearing the stencil buffer or for rendering techniques that require resetting the stencil value, such as stencil shadows or outlining.
        eReplace = 2,               ///< Replace the stencil value with a reference value specified in the stencil state. This can be useful for rendering techniques that require setting specific stencil values, such as stencil masking or for certain shadow mapping techniques.
        eIncrementAndClamp = 3,     ///< Increment the stencil value by one, but clamp it to the maximum representable value (e.g., 255 for an 8-bit stencil buffer). This can be useful for rendering techniques that require counting stencil hits, such as stencil shadows or for certain shadow mapping techniques.
        eDecrementAndClamp = 4,     ///< Decrement the stencil value by one, but clamp it to zero. This can be useful for rendering techniques that require counting stencil hits, such as stencil shadows or for certain shadow mapping techniques.
        eInvert = 5,                ///< Bitwise invert the stencil value. This can be useful for rendering techniques that require toggling stencil values, such as stencil masking or for certain shadow mapping techniques.
        eIncrementAndWrap = 6,      ///< Increment the stencil value by one, and wrap it to zero if it exceeds the maximum representable value. This can be useful for rendering techniques that require counting stencil hits without clamping, such as stencil shadows or for certain shadow mapping techniques.
        eDecrementAndWrap = 7       ///< Decrement the stencil value by one, and wrap it to the maximum representable value if it goes below zero. This can be useful for rendering techniques that require counting stencil hits without clamping, such as stencil shadows or for certain shadow mapping techniques.
    };

    /**
     * @brief Description of the stencil operation state. Used to define the operations that are performed on the stencil buffer during stencil testing in the graphics pipeline.
     */
    struct KORAL_API StencilOpState
    {
        StencilOp failOp = StencilOp::eKeep;        ///< The operation to perform when the stencil test fails. For example, if the fail operation is eReplace, the stencil value will be replaced with the reference value specified in the stencil state when the stencil test fails.
        StencilOp passOp = StencilOp::eKeep;        ///< The operation to perform when the stencil test passes. For example, if the pass operation is eIncrementAndClamp, the stencil value will be incremented by one (and clamped to the maximum representable value) when the stencil test passes.
        StencilOp depthFailOp = StencilOp::eKeep;   ///< The operation to perform when the stencil test passes but the depth test fails. For example, if the depth fail operation is eDecrementAndClamp, the stencil value will be decremented by one (and clamped to zero) when the stencil test passes but the depth test fails.
        CompareOp compareOp = CompareOp::eAlways;   ///< The comparison function to use for the stencil test. For example, if the compare operation is eEqual, the stencil test will pass if the stencil value is equal to the reference value specified in the stencil state.
        glm::u32 compareMask = 0;                   ///< The mask that is applied to both the stencil value and the reference value during stencil testing. This can be used to ignore certain bits of the stencil value when performing the comparison, which can be useful for rendering techniques that require partial stencil testing, such as stencil shadows or for certain shadow mapping techniques.
        glm::u32 writeMask = 0;                     ///< The mask that is applied to the stencil value when writing to the stencil buffer. This can be used to ignore certain bits of the stencil value when performing stencil operations, which can be useful for rendering techniques that require partial stencil updates, such as stencil shadows or for certain shadow mapping techniques.
        glm::u32 reference = 0;                     ///< The reference value that is used in stencil testing and stencil operations. This value is compared against the stencil value in the stencil buffer using the specified compare operation, and is also used in stencil operations that require a reference value (e.g., eReplace).
    };

    /**
     * @brief Description of the depth and stencil state. Used to define the depth testing and stencil testing settings in the graphics pipeline.
     */
    struct KORAL_API DepthStencilState
    {
        // Depth settings
        bool depthTestEnable = true;                    ///< If true, depth testing will be enabled, meaning that fragments will be tested against the existing depth values in the depth buffer to determine whether they should be drawn. This is the default setting and is commonly used for rendering solid objects.
        bool depthWriteEnable = true;                   ///< If true, depth values will be written to the depth buffer when fragments are drawn. This is the default setting and is commonly used for rendering solid objects. Disabling depth writes can be useful for rendering transparent objects or for certain shadow mapping techniques.
        CompareOp depthCompareOp = CompareOp::eLess;    ///< The comparison function to use for depth testing. For example, if the depth compare operation is eLess, a fragment will pass the depth test if its depth value is less than the existing depth value in the depth buffer. This is the default depth comparison function and is commonly used for rendering solid objects.
        bool depthBoundsEnable = false;                 ///< If true, depth bounds testing will be enabled, meaning that fragments will be tested against the specified minimum and maximum depth bounds to determine whether they should be drawn. This can be useful for rendering techniques that require depth-based culling, such as occlusion culling or for certain shadow mapping techniques.

        // Stencil settings
        bool stencilEnable = false;                     ///< If true, stencil testing will be enabled, meaning that fragments will be tested against the stencil buffer to determine whether they should be drawn, and stencil operations will be performed on the stencil buffer based on the results of the stencil test and depth test. This can be useful for rendering techniques that require stencil testing, such as stencil shadows, outlining, or for certain shadow mapping techniques.
        StencilOpState stencilFront = {};               ///< The stencil operation state for front-facing polygons. This defines the operations that are performed on the stencil buffer when rendering front-facing polygons, based on the results of the stencil test and depth test.
        StencilOpState stencilBack = {};                ///< The stencil operation state for back-facing polygons. This defines the operations that are performed on the stencil buffer when rendering back-facing polygons, based on the results of the stencil test and depth test.
        float minDepth = 0.f;                           ///< The minimum depth bound for depth bounds testing. Fragments with depth values less than this value will fail the depth bounds test and will be discarded. This can be used for rendering techniques that require depth-based culling, such as occlusion culling or for certain shadow mapping techniques.
        float maxDepth = 1.f;                           ///< The maximum depth bound for depth bounds testing. Fragments with depth values greater than this value will fail the depth bounds test and will be discarded. This can be used for rendering techniques that require depth-based culling, such as occlusion culling or for certain shadow mapping techniques.
    };

    /**
     * @brief The type of blend mode. Used to define the blending operation for color blending in the graphics pipeline.
     */
    enum class BlendMode : std::uint8_t {
        eAdd = 0,               ///< The source and destination colors are added together. This is the default blending mode and is commonly used for rendering transparent objects.
        eSubtract = 1,          ///< The destination color is subtracted from the source color. This can be useful for rendering techniques that require subtractive blending, such as certain particle effects or for certain shadow mapping techniques.
        eReverseSubtract = 2,   ///< The source color is subtracted from the destination color. This can be useful for rendering techniques that require reverse subtractive blending, such as certain particle effects or for certain shadow mapping techniques.
        eMin = 3,               ///< The minimum of the source and destination colors is used. This can be useful for rendering techniques that require minimum blending, such as certain particle effects or for certain shadow mapping techniques.
        eMax = 4                ///< The maximum of the source and destination colors is used. This can be useful for rendering techniques that require maximum blending, such as certain particle effects or for certain shadow mapping techniques.
    };

    /**
     * @brief The type of blend factor. Used to define the blend factors for color blending in the graphics pipeline.
     */
    enum class BlendFactor : std::uint8_t {
        eZero = 0,
        eOne = 1,
        eSrcColor = 2,
        eOneMinusSrcColor = 3,
        eDstColor = 4,
        eOneMinusDstColor = 5,
        eSrcAlpha = 6,
        eOneMinusSrcAlpha = 7,
        eDstAlpha = 8,
        eOneMinusDstAlpha = 9,
        eConstantColor = 10,
        eOneMinusConstantColor = 11,
        eSrcAlphaSaturate = 12
    };

    /**
     * @brief The type of logic operation. Used to define the logic operation for color blending in the graphics pipeline when logic operations are enabled.
     */
    enum class LogicOp : std::uint8_t {
        eClear = 0,
        eAnd = 1,
        eAndReverse = 2,
        eCopy = 3,
        eAndInverted = 4,
        eNoOp = 5,
        eXor = 6,
        eOr = 7,
        eNor = 8,
        eEquivalent = 9,
        eInvert = 10,
        eOrReverse = 11,
        eCopyInverted = 12,
        eOrInverted = 13,
        eNand = 14,
        eSet = 15
    };

    /**
     * @brief The type of blend operation. Used to define the blend operation for color blending in the graphics pipeline when blending is enabled.
     */
    enum class BlendOp : std::uint8_t {
        eAdd = 0,
        eSubtract = 1,
        eReverseSubtract = 2,
        eMin = 3,
        eMax = 4
    };

    /**
     * @brief The type of color component. Used to define which color components are affected by color blending operations in the graphics pipeline.
     */
    enum class ColorComponent : std::uint8_t {
        eR = 1,
        eG = 2,
        eB = 4,
        eA = 8
    };
    template<> struct enable_flags<ColorComponent> : std::true_type {};

    /**
     * @brief The type of resolve mode. Used to define how multisampled images are resolved to single-sampled images in the graphics pipeline.
     */
    enum class ResolveMode : std::uint8_t {
        eNone = 0,          ///< No resolve operation is performed.
        eSampleZero = 1,    ///< The value of the first sample (sample index 0) is used as the resolved value.
        eAverage = 2,       ///< The average of all samples in the pixel is calculated and used as the resolved value.
        eMin = 3,           ///< The minimum value of all samples is used as the resolved value.
        eMax = 4            ///< The maximum value of all samples is used as the resolved value.
    };

    /**
     * @brief How texels are chosen when an image is sampled or rescaled.
     *
     * Used both by samplers (how a shader reads a texture) and by CommandBuffer::Blit (how the
     * source rectangle is stretched onto a destination of a different size).
     */
    enum class Filter : std::uint8_t {
        eNearest,   ///< Take the single nearest texel. Exact and blocky; the right choice when the source and destination are the same size, or for data that must not be interpolated (IDs, indices, masks).
        eLinear,    ///< Interpolate between the neighbouring texels. Smooth, and what you want when scaling a colour image up or down.
    };

    /**
     * @brief Description of an indirect draw command.
     */
    struct KORAL_API IndirectDrawCommand
    {
        glm::u32 vertexCount;
        glm::u32 instanceCount;
        glm::u32 firstVertex;
        glm::u32 firstInstance;
    };

    /**
     * @brief Description of an indirect indexed draw command.
     */
    struct KORAL_API IndirectDrawIndexedCommand
    {
        glm::u32 indexCount;
        glm::u32 instanceCount;
        glm::u32 firstIndex;
        int32_t vertexOffset;
        glm::u32 firstInstance;
    };

    /**
     * @brief Description of an indirect draw command for mesh tasks.
     */
    struct KORAL_API IndirectDrawMeshTasksCommand
    {
        glm::u32 taskCountX;
        glm::u32 taskCountY;
        glm::u32 taskCountZ;
    };

    // =========================================================================
    //  Command recording
    //
    //  The parameter types of CommandBuffer. They are declared here, with the rest of the API's
    //  vocabulary, rather than in commandBuffer.h, which is left holding only the interface that
    //  consumes them.
    // =========================================================================

    /**
     * @brief The access a buffer must be usable for, as demanded of CommandBuffer::Barrier.
     *
     * You rarely need one. The command buffer inserts barriers itself: every recorded command
     * declares the resources it touches and how, and the recording is resolved as a whole at
     * End(), which is what lets a transition be placed *before* the render pass that needs it
     * rather than illegally inside it.
     *
     * Write one for the accesses that analysis cannot see — chiefly a buffer a shader reaches
     * through a raw device address, which appears in no descriptor set. A barrier you write is
     * also taken as authoritative for the range it covers: the resolver advances its tracking
     * past it and does not emit a second one, so the hand-written barrier replaces the automatic
     * barrier rather than doubling it.
     */
    class KORAL_API BufferBarrier {
    public:
        /**
         * @param buffer The buffer to transition.
         * @param dstAccess The access the buffer must support once the barrier has executed. Everything recorded before the barrier is made visible to it.
         * @param offset Byte offset of the range being transitioned.
         * @param size Length of that range in bytes. The default covers everything from @p offset to the end of the buffer.
         */
        BufferBarrier(
            const kor::ResourceRef<const kor::Buffer> &buffer,
            ResourceAccess dstAccess,
            glm::u64 offset = 0,
            glm::u64 size = WholeSize);

        [[nodiscard]] kor::ResourceRef<const kor::Buffer> buffer() const { return _buffer; }
        [[nodiscard]] ResourceAccess dstAccess() const { return _dstAccess; }
        [[nodiscard]] glm::u64 offset() const { return _offset; }
        [[nodiscard]] glm::u64 size() const { return _size; }

    private:
        kor::ResourceRef<const kor::Buffer> _buffer;
        ResourceAccess _dstAccess;
        glm::u64 _offset;
        glm::u64 _size;
    };

    /**
     * @brief The access an image must be usable for, as demanded of CommandBuffer::Barrier.
     *
     * The image counterpart of BufferBarrier, and the same advice applies: the command buffer
     * does this for you, and a barrier you write yourself suppresses the automatic one for the
     * subresources it names. Unlike a buffer, an image also carries a *layout* the driver picks
     * from the access — transitioning to ResourceAccess::eTransferDst, for instance, is what makes
     * the image a legal copy destination.
     *
     * Every subresource argument defaults to nullopt, meaning the whole image. Give them to
     * transition one mip level or one array layer of a texture on its own, which is how a mip
     * chain is built (each level is read as a transfer source while the next is written).
     */
    class KORAL_API ImageBarrier {
    public:
        /**
         * @param image The image to transition.
         * @param dstAccess The access the image must support once the barrier has executed, and with it the layout the image is put into.
         * @param baseMipLevel First mip level of the range; nullopt starts at level 0.
         * @param levelCount Number of mip levels; nullopt covers every level from @p baseMipLevel to the last.
         * @param baseArrayLayer First array layer of the range; nullopt starts at layer 0.
         * @param layerCount Number of array layers; nullopt covers every layer from @p baseArrayLayer to the last.
         */
        ImageBarrier(
            const kor::ResourceRef<const kor::Image> &image,
            ResourceAccess dstAccess,
            std::optional<glm::u32> baseMipLevel = std::nullopt,
            std::optional<glm::u32> levelCount = std::nullopt,
            std::optional<glm::u32> baseArrayLayer = std::nullopt,
            std::optional<glm::u32> layerCount = std::nullopt);

        [[nodiscard]] kor::ResourceRef<const kor::Image> image() const { return _image; }
        [[nodiscard]] ResourceAccess dstAccess() const { return _dstAccess; }
        [[nodiscard]] std::optional<glm::u32> baseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] std::optional<glm::u32> levelCount() const { return _levelCount; }
        [[nodiscard]] std::optional<glm::u32> baseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] std::optional<glm::u32> layerCount() const { return _layerCount; }

    private:
        kor::ResourceRef<const kor::Image> _image;
        ResourceAccess _dstAccess;
        std::optional<glm::u32> _baseMipLevel;
        std::optional<glm::u32> _levelCount;
        std::optional<glm::u32> _baseArrayLayer;
        std::optional<glm::u32> _layerCount;
    };

    /**
     * @brief Which region of which image CommandBuffer::Blit reads, and where it writes it.
     *
     * A blit copies a rectangle between images and rescales it on the way, so the two extents are
     * independent — that is the difference between it and a copy. Both images are named by the
     * Blit() call itself; everything else about the transfer is here. Defaults blit the whole of
     * mip 0, layer 0 onto the whole of the destination's mip 0, layer 0.
     */
    class KORAL_API Blit {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };     ///< Texel coordinate the source rectangle starts at.
        glm::ivec3 srcExtent = { -1, -1, -1 };  ///< Size of the source rectangle in texels. The default (-1) means the source image's full extent.
        glm::ivec3 dstOffset = { 0, 0, 0 };     ///< Texel coordinate the destination rectangle starts at.
        glm::ivec3 dstExtent = { -1, -1, -1 };  ///< Size of the destination rectangle in texels. The default (-1) means the destination image's full extent. Differing from @ref srcExtent is what scales the image.
        glm::u32 srcBaseArrayLayer = 0;         ///< First array layer read from the source.
        glm::u32 dstBaseArrayLayer = 0;         ///< First array layer written on the destination.
        glm::u32 layerCount = 1;                ///< How many array layers to blit, starting from the two base layers above.
        glm::u32 srcMipLevel = 0;               ///< Mip level read from the source.
        glm::u32 dstMipLevel = 0;               ///< Mip level written on the destination.
        kor::Filter filtering = kor::Filter::eNearest;  ///< How texels are sampled when the two extents differ. Only meaningful when they do — a same-size blit reads each texel exactly once either way.
    };

    /**
     * @brief Which region of which image CommandBuffer::Resolve reads, and where it writes it.
     *
     * A resolve collapses a multisampled image into a single-sampled one — the step that turns
     * an SampleCount render target into something that can be sampled or presented. It is a Blit without
     * the filter: the samples of each pixel are combined by the resolve mode, not interpolated,
     * so the two extents are expected to match.
     */
    class KORAL_API Resolve {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };     ///< Texel coordinate the source rectangle starts at.
        glm::ivec3 srcExtent = { -1, -1, -1 };  ///< Size of the source rectangle in texels. The default (-1) means the source image's full extent.
        glm::ivec3 dstOffset = { 0, 0, 0 };     ///< Texel coordinate the destination rectangle starts at.
        glm::ivec3 dstExtent = { -1, -1, -1 };  ///< Size of the destination rectangle in texels. The default (-1) means the destination image's full extent.
        glm::u32 srcBaseArrayLayer = 0;         ///< First array layer read from the source.
        glm::u32 dstBaseArrayLayer = 0;         ///< First array layer written on the destination.
        glm::u32 layerCount = 1;                ///< How many array layers to resolve, starting from the two base layers above.
        glm::u32 srcMipLevel = 0;               ///< Mip level read from the source.
        glm::u32 dstMipLevel = 0;               ///< Mip level written on the destination.
    };

    /**
     * @brief How buffer memory is laid out against image texels, for the copies in both directions.
     *
     * Shared by CommandBuffer::CopyBufferToImage and CommandBuffer::CopyImageToBuffer; the buffer
     * is the source in the first and the destination in the second, but the layout it describes is
     * the same either way. Defaults copy the whole of mip 0, layer 0 to or from tightly packed
     * memory at the start of the buffer.
     */
    class KORAL_API Copy {
    public:
        glm::u64 bufferOffset = 0;              ///< Byte offset into the buffer where the texel data begins.
        glm::u64 bufferRowLength = 0;           ///< Row pitch in *texels*, for buffer memory with padding between rows. The default (0) means rows are tightly packed, i.e. equal to @ref imageExtent.x.
        glm::u64 bufferImageHeight = 0;         ///< Slice pitch in *rows*, for buffer memory with padding between 2D slices. The default (0) means slices are tightly packed, i.e. equal to @ref imageExtent.y.
        glm::ivec3 imageOffset = { 0, 0, 0 };   ///< Texel coordinate in the image the copied region starts at.
        glm::ivec3 imageExtent = { -1, -1, -1 };///< Size of the copied region in texels. The default (-1) means the image's full extent.
        glm::u32 imageBaseArrayLayer = 0;       ///< First array layer copied.
        glm::u32 imageLayerCount = 1;           ///< How many array layers to copy, starting from @ref imageBaseArrayLayer.
        glm::u32 imageMipLevel = 0;             ///< Mip level copied.
    };

    /**
     * @brief What happens to an attachment's existing contents when a render pass opens.
     */
    enum class LoadOperation : std::uint8_t {
        eLoad,      ///< Keep what is already in the attachment and draw over it. Costs the bandwidth of reading it back, and is what you want when adding to an image rendered earlier in the frame.
        eClear,     ///< Fill the attachment with its clear value first. Usually the cheapest way to start a pass, because the hardware never has to read the old contents.
        eDontCare   ///< Leave the contents undefined. Only correct when the pass writes every pixel it will later read; anything else reads garbage that differs between GPUs.
    };

    /**
     * @brief What happens to an attachment's contents when a render pass closes.
     */
    enum class StoreOperation : std::uint8_t {
        eStore,     ///< Write the results back to memory, so a later pass — or the display — can read them.
        eDontCare   ///< Discard them. Right for a depth buffer nothing reads after the pass, and it saves the bandwidth of writing it out.
    };

    /**
     * @brief The value a colour attachment is cleared to, in whatever type its format holds.
     *
     * Pick the alternative that matches the attachment: a float vector for UNORM/SFLOAT formats, an
     * integer one for UINT/SINT. Clearing a float format with an integer value is a mismatch the
     * backend cannot fix.
     */
    using ClearColor = std::variant<
        float,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        glm::i32,
        glm::ivec2,
        glm::ivec3,
        glm::ivec4,
        glm::u32,
        glm::uvec2,
        glm::uvec3,
        glm::uvec4
    >;

    /**
     * @brief What one render pass renders into, and what happens to it on the way in and out.
     *
     * Everything CommandBuffer::BeginRendering needs: the framebuffer, a load op per attachment
     * kind deciding what the pass starts from, a store op deciding what survives it, and — for the
     * attachments the load op clears — what they are cleared *to*. Which of them apply depends on
     * the framebuffer being rendered to; settings for an attachment it does not have are ignored.
     * Defaults clear everything on entry and keep everything on exit, which is the
     * correct-but-conservative choice — a depth buffer nothing samples afterwards is worth
     * switching to StoreOperation::eDontCare.
     *
     * @code
     * commandBuffer.BeginRendering(gBuffer);                       // clear to the framebuffer's own values
     *
     * commandBuffer.BeginRendering(kor::RenderInfo(gBuffer)        // or override them, this pass only
     *     .setClearColor(0, glm::vec4{0.1f, 0.1f, 0.12f, 1.f})
     *     .setClearColor(2, glm::uvec4{~0u})                       // an integer attachment's sentinel
     *     .setDepthStoreOperation(kor::StoreOperation::eDontCare));
     *
     * commandBuffer.BeginRendering();                              // the screen, with its own values
     * @endcode
     *
     * A clear value left unset falls back to the one the framebuffer was built with, so a pass that
     * wants the usual thing says nothing. The fallback is taken when the pass is *recorded*, not
     * when it runs, which is what makes two passes over one framebuffer able to clear it to two
     * different colours in the same frame.
     */
    class KORAL_API RenderInfo {
    public:
        /** @brief Renders to the window's default framebuffer — the screen. */
        RenderInfo();

        // Not explicit: `BeginRendering(myFramebuffer)` is the common case by a wide margin, and
        // there is nothing for the conversion to be confused with — BeginRendering takes one
        // argument, and nothing else in the API takes a RenderInfo.
        RenderInfo(const kor::ResourceRef<const kor::Framebuffer>& framebuffer);
        RenderInfo(const kor::ResourceRef<kor::Framebuffer>& framebuffer);
        RenderInfo(const kor::Resource<kor::Framebuffer>& framebuffer);

        RenderInfo& setColorLoadOperation(const kor::LoadOperation op) { _colorLoadOperation = op; return *this; }
        RenderInfo& setDepthLoadOperation(const kor::LoadOperation op) { _depthLoadOperation = op; return *this; }
        RenderInfo& setStencilLoadOperation(const kor::LoadOperation op) { _stencilLoadOperation = op; return *this; }
        RenderInfo& setColorStoreOperation(const kor::StoreOperation op) { _colorStoreOperation = op; return *this; }
        RenderInfo& setDepthStoreOperation(const kor::StoreOperation op) { _depthStoreOperation = op; return *this; }
        RenderInfo& setStencilStoreOperation(const kor::StoreOperation op) { _stencilStoreOperation = op; return *this; }

        /**
         * @brief What colour attachment @p index is cleared to, for this pass only.
         * @param index Which colour attachment, in the order the framebuffer declares them.
         * @param color The value, in the type the attachment's format holds. @see ClearColor
         *
         * Attachments not named here keep the framebuffer's own clear value, so overriding one of
         * five means writing one line, not five.
         */
        RenderInfo& setClearColor(const glm::u32 index, const ClearColor &color)
        {
            if (index >= _clearColors.size()) {
                _clearColors.resize(index + 1, std::nullopt);
            }
            _clearColors[index] = color;
            return *this;
        }
        RenderInfo& setClearDepth(const float depth) { _clearDepth = depth; return *this; }
        RenderInfo& setClearStencil(const glm::i32 stencil) { _clearStencil = stencil; return *this; }

        [[nodiscard]] kor::ResourceRef<const kor::Framebuffer> framebuffer() const { return _framebuffer; }

        [[nodiscard]] kor::LoadOperation colorLoadOperation() const { return _colorLoadOperation; }
        [[nodiscard]] kor::LoadOperation depthLoadOperation() const { return _depthLoadOperation; }
        [[nodiscard]] kor::LoadOperation stencilLoadOperation() const { return _stencilLoadOperation; }
        [[nodiscard]] kor::StoreOperation colorStoreOperation() const { return _colorStoreOperation; }
        [[nodiscard]] kor::StoreOperation depthStoreOperation() const { return _depthStoreOperation; }
        [[nodiscard]] kor::StoreOperation stencilStoreOperation() const { return _stencilStoreOperation; }

        /**
         * @brief What colour attachment @p index will be cleared to.
         *
         * The value this pass was given, or the framebuffer's own once resolveClearValues() has
         * run. Black for an attachment neither of them describes, which cannot happen for a pass
         * recorded through BeginRendering.
         */
        [[nodiscard]] const ClearColor& clearColor(glm::u32 index) const;

        /** @brief What the depth attachment will be cleared to; the far plane if nothing said. */
        [[nodiscard]] float clearDepth() const { return _clearDepth.value_or(1.f); }

        /** @brief What the stencil attachment will be cleared to; 0 if nothing said. */
        [[nodiscard]] glm::i32 clearStencil() const { return _clearStencil.value_or(0); }

        /**
         * @brief Fills in every clear value this pass did not set from @p framebuffer's own.
         *
         * Called by CommandBuffer::BeginRendering while it records, and the reason a clear value is
         * a property of the *record* rather than of the framebuffer: OpenGL replays its records
         * after the fact, so a value read at replay time would be whatever the framebuffer holds
         * then — the last one written, for every pass in the frame — rather than what each pass was
         * recorded with. Resolving here makes both backends agree.
         */
        void resolveClearValues(const kor::Framebuffer& framebuffer);

    private:
        kor::ResourceRef<const kor::Framebuffer> _framebuffer;        ///< The framebuffer the pass will render to. Its attachments determine which of the load/store ops below are used.

        kor::LoadOperation _colorLoadOperation = kor::LoadOperation::eClear;         ///< What the color attachments start from.
        kor::LoadOperation _depthLoadOperation = kor::LoadOperation::eClear;         ///< What the depth attachment starts from.
        kor::LoadOperation _stencilLoadOperation = kor::LoadOperation::eClear;       ///< What the stencil attachment starts from.

        kor::StoreOperation _colorStoreOperation = kor::StoreOperation::eStore;      ///< Whether the color results survive the pass.
        kor::StoreOperation _depthStoreOperation = kor::StoreOperation::eStore;      ///< Whether the depth results survive the pass.
        kor::StoreOperation _stencilStoreOperation = kor::StoreOperation::eStore;    ///< Whether the stencil results survive the pass.

        std::vector<std::optional<ClearColor>> _clearColors {};                      ///< Clear values for the color attachments, in the order they are bound. Only used if @ref colorLoadOperation is LoadOperation::eClear.
        std::optional<float> _clearDepth = std::nullopt;                             ///< Clear value for the depth attachment. Only used if @ref depthLoadOperation is LoadOperation::eClear.
        std::optional<glm::i32> _clearStencil = std::nullopt;                        ///< Clear value for the stencil attachment.
    };

    /**
     * @brief What one CommandBuffer::BeginTimer / EndTimer scope cost on the GPU.
     *
     * Produced by CommandBuffer::timings(), in the order the scopes were opened. The time is
     * measured on the device, so it is what the GPU spent, not what the recording thread did.
     *
     * @see CommandBuffer::BeginTimer
     */
    struct TimerResult {
        std::string label;          ///< The name given to BeginTimer.
        double milliseconds = 0.0;  ///< GPU time between the scope's two timestamps.
        glm::u32 depth = 0;         ///< Nesting depth; 0 for an outermost scope, 1 for one opened inside it, and so on.
    };

    /**
     * @brief Selects which polygon face(s) a stencil setter affects.
     *
     * Mirrors the per-face stencil model, so front and back can carry different masks, references
     * and operations — which is how single-pass techniques like stencil shadow volumes count
     * front and back faces against the same buffer.
     */
    enum class StencilFace : std::uint8_t {
        eFront = 1,         ///< Front-facing polygons only, as decided by FrontFace.
        eBack = 2,          ///< Back-facing polygons only.
        eFrontAndBack = 3,  ///< Both, with the same value. The usual choice.
    };

    /**
     * @brief One flag per pipeline-derived dynamic state the command buffer tracks.
     *
     * A bit is set once the state has a current value on the GPU — either applied from the bound
     * pipeline's default or overridden by an explicit CommandBuffer::Set* call — and cleared when a
     * new graphics pipeline is bound. What the tracking buys is that a pipeline's baked defaults are
     * applied lazily, before the first draw that needs them, instead of being re-sent on every bind.
     *
     * @see CommandBuffer::applyDynamicDefaults
     */
    enum class DynamicState : std::uint16_t {
        eLineWidth              = 1 << 0,   ///< @see CommandBuffer::SetLineWidth
        eDepthBias              = 1 << 1,   ///< @see CommandBuffer::SetDepthBias
        eBlendConstants         = 1 << 2,   ///< @see CommandBuffer::SetBlendConstants
        eStencilCompareMask     = 1 << 3,   ///< @see CommandBuffer::SetStencilCompareMask
        eStencilWriteMask       = 1 << 4,   ///< @see CommandBuffer::SetStencilWriteMask
        eStencilReference       = 1 << 5,   ///< @see CommandBuffer::SetStencilReference
        eCullMode               = 1 << 6,   ///< @see CommandBuffer::SetCullMode
        eFrontFace              = 1 << 7,   ///< @see CommandBuffer::SetFrontFace
        eDepthTestEnable        = 1 << 8,   ///< @see CommandBuffer::SetDepthTestEnable
        eDepthWriteEnable       = 1 << 9,   ///< @see CommandBuffer::SetDepthWriteEnable
        eDepthCompareOp         = 1 << 10,  ///< @see CommandBuffer::SetDepthCompareOp
        eStencilTestEnable      = 1 << 11,  ///< @see CommandBuffer::SetStencilTestEnable
        eStencilOp              = 1 << 12,  ///< @see CommandBuffer::SetStencilOp
        eDepthBiasEnable        = 1 << 13,  ///< @see CommandBuffer::SetDepthBiasEnable
        eRasterizerDiscardEnable = 1 << 14, ///< @see CommandBuffer::SetRasterizerDiscardEnable
        ePrimitiveRestartEnable = 1 << 15,  ///< @see CommandBuffer::SetPrimitiveRestartEnable
    };
    template<> struct enable_flags<DynamicState> : std::true_type {};
}
