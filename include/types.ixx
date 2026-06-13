//
// Created by radue on 12.06.2026.
//

module;

#include <glm/glm.hpp>
#include <api.h>
#include <optional>

export module gfx:types;
import flags;
import resource;

export namespace gfx
{
    class GFX_API Window;
    class GFX_API Surface;
    class GFX_API Scene;

    class GFX_API Frame;
    class GFX_API Scheduler;
    class GFX_API Framebuffer;
    class GFX_API CommandBuffer;

    class GFX_API Buffer;
    class GFX_API Image;
    class GFX_API ImageView;
    class GFX_API Sampler;
    class GFX_API Mesh;

    template<typename Derived>
    class CustomMesh;

    template<typename... Streams>
    class ParamMesh;

    class GFX_API Shader;
    class GFX_API GraphicsPipeline;
    class GFX_API ComputePipeline;
    class GFX_API Descriptor;
    class GFX_API DescriptorSet;
    class GFX_API DescriptorSetLayout;

    class GFX_API GUI;
    class GFX_API GUI_Image;


    class GFX_API Time;
    class GFX_API Input;

    /**
     * @brief The type of texture filtering. Used to define how textures are sampled and how images are filtered during blit operations.
     */
    enum class Filter
    {
        eNearest,
        eLinear,
    };

    /**
     * @brief The type of vertex input pipe channels. Used to define the format of vertex attributes in the graphics pipeline.
     */
    enum class ChannelType : glm::u8
    {
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
     * @brief The type of descriptor. Used to define the type of resources that can be bound to a descriptor set layout binding.
     *
     * @note The exact meaning of each descriptor type may vary depending on the graphics API being used, but in general they can be categorized as follows:
     */
    enum class DescriptorType {
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
    };

    /**
     * @brief Description of a vertex input attribute. Used to define the format of vertex attributes in the graphics pipeline.
     */
    struct GFX_API VertexInputAttributeDescription
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
    struct GFX_API VertexInputBindingDescription
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
    enum class Topology : glm::u8
    {
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
    struct GFX_API InputAssemblyState
    {
        Topology topology = Topology::eTriangleList;    ///< The type of primitive topology. For example, if the topology is eTriangleList, every three vertices will form a separate triangle.
        bool primitiveRestartEnable = false;            ///< Whether primitive restart is enabled. If true, a special index value (the maximum value of the index type - 0xFFFFFFFF for 32-bit indices) can be used in the index buffer to indicate that the current primitive should be restarted, and a new primitive should
        ///< be started with the next vertex. This is only relevant for indexed draw calls, and can be useful for drawing multiple disconnected primitives with a single
        ///< draw call.
    };

    /**
     * @brief The type of polygon mode. Used to define how polygons are rasterized in the graphics pipeline.
     */
    enum class PolygonMode : glm::u8
    {
        eFill = 0,      ///< Polygons are filled in. This is the default mode and the most common one for rendering solid objects.
        eLine = 1,      ///< Polygons are rasterized as wireframes, with only the edges of the polygons being drawn. This can be useful for debugging or for rendering wireframe models.
        ePoint = 2      ///< Polygons are rasterized as points, with only the vertices of the polygons being drawn. This can be useful for debugging or for rendering point cloud data.
    };

    /**
     * @brief The type of cull mode. Used to define which faces of polygons are culled (not drawn) in the graphics pipeline.
     */
    enum class CullMode : glm::u8
    {
        eFront = 1,     ///< Front faces of polygons are culled. The definition of front and back faces is determined by the front face setting in the rasterization state.
        eBack = 2,      ///< Back faces of polygons are culled. The definition of front and back faces is determined by the front face setting in the rasterization state.
    };

    /**
     * @brief The type of front face. Used to define which faces of polygons are considered front-facing in the graphics pipeline.
     */
    enum class FrontFace : bool
    {
        eCounterClockwise = false,  ///< Polygons with vertices in counter-clockwise order are considered front-facing. This is the default setting and the most common one.
        eClockwise = true           ///< Polygons with vertices in clockwise order are considered front-facing. This can be useful if your modeling software exports models with a different winding order than the default.
    };

    /**
     * @brief Description of the rasterization state. Used to define how polygons are rasterized in the graphics pipeline.
     */
    struct GFX_API RasterizationState
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
    enum class SampleCount : glm::u8
    {
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
    struct GFX_API MultisampleState
    {
        SampleCount sampleCount = SampleCount::e1;      ///< The number of samples per pixel. For example, if the sample count is e4, each pixel will have 4 samples, which can be used for anti-aliasing or for rendering to multisampled framebuffers.
        bool sampleShadingEnable = false;               ///< If true, sample shading will be enabled, meaning that the fragment shader will be executed for each sample instead of once per pixel. This can be useful for improving the quality of anti-aliasing when using multisampling, as it allows for more accurate shading of edges and fine details.
        float minSampleShading = 1.f;                   ///< The minimum fraction of samples that must be shaded when sample shading is enabled. For example, if the min sample shading is 0.5, at least half of the samples in each pixel will be shaded. This can be used to balance the quality and performance of sample shading, as higher values will result in better quality but lower performance.
    };

    /**
     * @brief The type of comparison operation. Used to define the comparison function for depth testing and stencil testing in the graphics pipeline.
     */
    enum class CompareOp : glm::u8
    {
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
    enum class StencilOp : glm::u8
    {
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
    struct GFX_API StencilOpState
    {
        StencilOp failOp = StencilOp::eKeep;        ///< The operation to perform when the stencil test fails. For example, if the fail operation is eReplace, the stencil value will be replaced with the reference value specified in the stencil state when the stencil test fails.
        StencilOp passOp = StencilOp::eKeep;        ///< The operation to perform when the stencil test passes. For example, if the pass operation is eIncrementAndClamp, the stencil value will be incremented by one (and clamped to the maximum representable value) when the stencil test passes.
        StencilOp depthFailOp = StencilOp::eKeep;   ///< The operation to perform when the stencil test passes but the depth test fails. For example, if the depth fail operation is eDecrementAndClamp, the stencil value will be decremented by one (and clamped to zero) when the stencil test passes but the depth test fails.
        CompareOp compareOp = CompareOp::eAlways;   ///< The comparison function to use for the stencil test. For example, if the compare operation is eEqual, the stencil test will pass if the stencil value is equal to the reference value specified in the stencil state.
        uint32_t compareMask = 0;                   ///< The mask that is applied to both the stencil value and the reference value during stencil testing. This can be used to ignore certain bits of the stencil value when performing the comparison, which can be useful for rendering techniques that require partial stencil testing, such as stencil shadows or for certain shadow mapping techniques.
        uint32_t writeMask = 0;                     ///< The mask that is applied to the stencil value when writing to the stencil buffer. This can be used to ignore certain bits of the stencil value when performing stencil operations, which can be useful for rendering techniques that require partial stencil updates, such as stencil shadows or for certain shadow mapping techniques.
        uint32_t reference = 0;                     ///< The reference value that is used in stencil testing and stencil operations. This value is compared against the stencil value in the stencil buffer using the specified compare operation, and is also used in stencil operations that require a reference value (e.g., eReplace).
    };

    /**
     * @brief Description of the depth and stencil state. Used to define the depth testing and stencil testing settings in the graphics pipeline.
     */
    struct GFX_API DepthStencilState
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
    enum class BlendMode : glm::u8
    {
        eAdd = 0,               ///< The source and destination colors are added together. This is the default blending mode and is commonly used for rendering transparent objects.
        eSubtract = 1,          ///< The destination color is subtracted from the source color. This can be useful for rendering techniques that require subtractive blending, such as certain particle effects or for certain shadow mapping techniques.
        eReverseSubtract = 2,   ///< The source color is subtracted from the destination color. This can be useful for rendering techniques that require reverse subtractive blending, such as certain particle effects or for certain shadow mapping techniques.
        eMin = 3,               ///< The minimum of the source and destination colors is used. This can be useful for rendering techniques that require minimum blending, such as certain particle effects or for certain shadow mapping techniques.
        eMax = 4                ///< The maximum of the source and destination colors is used. This can be useful for rendering techniques that require maximum blending, such as certain particle effects or for certain shadow mapping techniques.
    };

    /**
     * @brief The type of blend factor. Used to define the blend factors for color blending in the graphics pipeline.
     */
    enum class BlendFactor : glm::u8
    {
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
    enum class LogicOp : glm::u8
    {
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
    enum class BlendOp : glm::u8
    {
        eAdd = 0,
        eSubtract = 1,
        eReverseSubtract = 2,
        eMin = 3,
        eMax = 4
    };

    /**
     * @brief The type of color component. Used to define which color components are affected by color blending operations in the graphics pipeline.
     */
    enum class ColorComponent : glm::u8
    {
        eR = 1,
        eG = 2,
        eB = 4,
        eA = 8
    };

    /**
     * @brief The type of resolve mode. Used to define how multisampled images are resolved to single-sampled images in the graphics pipeline.
     */
    enum class ResolveMode : glm::u8
    {
        eNone = 0,          ///< No resolve operation is performed. This can be used when you want to manually resolve multisampled images using custom shader code or compute shaders, or when you want to use multisampled images without resolving them (e.g., for certain shadow mapping techniques).
        eSampleZero = 1,    ///< The value of the first sample (sample index 0) is used as the resolved value for all samples in the pixel. This can be useful for rendering techniques that require a simple resolve operation, such as certain shadow mapping techniques or for debugging purposes.
        eAverage = 2,       ///< The average of all samples in the pixel is calculated and used as the resolved value. This is the most common resolve mode and is typically used for anti-aliasing when rendering to multisampled framebuffers.
        eMin = 3,           ///< The minimum value of all samples in the pixel is used asthe resolved value. This can be useful for rendering techniques that require a minimum resolve operation, such as certain shadow mapping techniques or for debugging purposes.
        eMax = 4            ///< The maximum value of all samples in the pixel is used as the resolved value. This can be useful for rendering techniques that require a maximum resolve operation, such as certain shadow mapping techniques or for debugging purposes.
    };

    /**
     * @brief Description of an indirect draw command. Used to define the parameters for indirect draw calls in the graphics pipeline.
     */
    struct GFX_API IndirectDrawCommand
    {
        glm::u32 vertexCount;     ///< The number of vertices to draw. This is used for non-indexed draw calls, and specifies how many vertices will be read from the vertex buffers and processed by the vertex shader.
        glm::u32 instanceCount;   ///< The number of instances to draw. This is used for instanced draw calls, and specifies how many instances of the geometry will be drawn. Each instance will have its own set of vertex attributes, which can be accessed in the vertex shader using the gl_InstanceID built-in variable.
        glm::u32 firstVertex;     ///< The index of the first vertex to draw. This is used for non-indexed draw calls, and specifies the starting point in the vertex buffers from which vertices will be read and processed by the vertex shader.
        glm::u32 firstInstance;   ///< The index of the first instance to draw. This is used for instanced draw calls, and specifies the starting point for instance data when drawing multiple instances of geometry. Each instance will have its own set of vertex attributes, which can be accessed in the vertex shader using the gl_InstanceID built-in variable.
    };

    /**
     * @brief Description of an indirect indexed draw command. Used to define the parameters for indirect indexed draw calls in the graphics pipeline.
     */
    struct GFX_API IndirectDrawIndexedCommand
    {
        glm::u32 indexCount;      ///< The number of indices to draw. This is used for indexed draw calls, and specifies how many indices will be read from the index buffer and processed by the vertex shader.
        glm::u32 instanceCount;   ///< The number of instances to draw. This is used for instanced draw calls, and specifies how many instances of the geometry will be drawn. Each instance will have its own set of vertex attributes, which can be accessed in the vertex shader using the gl_InstanceID built-in variable.
        glm::u32 firstIndex;      ///< The index of the first index to draw. This is used for indexed draw calls, and specifies the starting point in the index buffer from which indices will be read and processed by the vertex shader.
        glm::i32 vertexOffset;     ///< The value added to the vertex index before fetching vertex attributes from the vertex buffers. This can be used to specify a base vertex index when drawing indexed geometry, which can be useful for rendering techniques that require drawing subsets of a larger mesh or for certain shadow mapping techniques.
        glm::u32 firstInstance;   ///< The index of the first instance to draw. This is used for instanced draw calls, and specifies the starting point for instance data when drawing multiple instances of geometry. Each instance will have its own set of vertex attributes, which can be accessed in the vertex shader using the gl_InstanceID built-in variable.
    };

    /**
     * @brief Description of an indirect draw command for mesh tasks. Used to define the parameters for indirect draw calls that use mesh shaders in the graphics pipeline.
     */
    struct GFX_API IndirectDrawMeshTasksCommand
    {
        glm::u32 taskCountX;      ///< The number of local workgroups to dispatch in the X dimension. This is used for mesh task draw calls, and specifies how many mesh tasks will be executed in parallel across the X dimension.
        glm::u32 taskCountY;      ///< The number of local workgroups to dispatch in the Y dimension. This is used for mesh task draw calls, and specifies how many mesh tasks will be executed in parallel across the Y dimension.
        glm::u32 taskCountZ;      ///< The number of local workgroups to dispatch in the Z dimension. This is used for mesh task draw calls, and specifies how many mesh tasks will be executed in parallel across the Z dimension.
    };

    enum class PipelineStage {
        // Compute
        Compute = 1 << 0,

        // Vertex pipeline
        VertexInput = 1 << 1,
        VertexShader = 1 << 2,
        FragmentShader = 1 << 3,
        EarlyFragmentTests = 1 << 4,
        LateFragmentTests = 1 << 5,
        ColorAttachmentOutput = 1 << 6,

        // Transfer
        Transfer = 1 << 7,

        // Bottom and top of pipe
        TopOfPipe = 1 << 8,
        BottomOfPipe = 1 << 9
    };

    enum class ResourceAccess : glm::u8 {
        // Compute
        ComputeRead,         // COMPUTE_SHADER + SHADER_READ
        ComputeWrite,        // COMPUTE_SHADER + SHADER_WRITE
        ComputeReadWrite,    // COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Vertex pipeline
        VertexBuffer,        // VERTEX_INPUT + VERTEX_ATTRIBUTE_READ
        IndexBuffer,         // VERTEX_INPUT + INDEX_READ
        IndirectBuffer,      // DRAW_INDIRECT + INDIRECT_COMMAND_READ

        // Vertex shader
        VertexShaderRead,    // VERTEX_SHADER + SHADER_READ
        VertexShaderWrite,   // VERTEX_SHADER + SHADER_WRITE
        VertexShaderReadWrite, // VERTEX_SHADER + SHADER_READ | SHADER_WRITE

        // Fragment shader
        FragmentShaderRead,  // FRAGMENT_SHADER + SHADER_READ
        FragmentShaderWrite, // FRAGMENT_SHADER + SHADER_WRITE
        FragmentShaderReadWrite, // FRAGMENT_SHADER + SHADER_READ | SHADER_WRITE

        // Attachments
        ColorAttachment,     // COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE
        DepthStencilAttachment,  // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        DepthStencilRead,        // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ
        DepthAttachment,        // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        DepthRead,              // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ
        StencilAttachment,       // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        StencilRead,            // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ

        // Transfer
        TransferSrc,         // TRANSFER + TRANSFER_READ
        TransferDst,         // TRANSFER + TRANSFER_WRITE

        // General
        AllShaderRead,          // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ
        AllShaderWrite,         // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_WRITE
        AllShaderReadWrite,     // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Present
        Present,             // COLOR_ATTACHMENT_OUTPUT + 0 (no access mask needed)
    };

    class GFX_API Blit {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };
        glm::ivec3 srcExtent = { -1, -1, -1 };
        glm::ivec3 dstOffset = { 0, 0, 0 };
        glm::ivec3 dstExtent = { -1, -1, -1 };
        glm::u32 srcBaseArrayLayer = 0;
        glm::u32 dstBaseArrayLayer = 0;
        glm::u32 layerCount = 1;
        glm::u32 srcMipLevel = 0;
        glm::u32 dstMipLevel = 0;
        gfx::Filter filtering = gfx::Filter::eNearest;
    };

    class GFX_API Resolve {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };
        glm::ivec3 srcExtent = { -1, -1, -1 };
        glm::ivec3 dstOffset = { 0, 0, 0 };
        glm::ivec3 dstExtent = { -1, -1, -1 };
        glm::u32 srcBaseArrayLayer = 0;
        glm::u32 dstBaseArrayLayer = 0;
        glm::u32 layerCount = 1;
        glm::u32 srcMipLevel = 0;
        glm::u32 dstMipLevel = 0;
    };

    class GFX_API Copy {
    public:
        glm::u64 bufferOffset = 0;
        glm::u64 bufferRowLength = 0;
        glm::u64 bufferImageHeight = 0;
        glm::ivec3 imageOffset = { 0, 0, 0 };
        glm::ivec3 imageExtent = { -1, -1, -1 };
        glm::u32 imageBaseArrayLayer = 0;
        glm::u32 imageLayerCount = 1;
        glm::u32 imageMipLevel = 0;
    };

    enum class LoadOperation : glm::u8 {
        eLoad,
        eClear,
        eDontCare
    };

    enum class StoreOperation : glm::u8 {
        eStore,
        eDontCare
    };

    class GFX_API RenderParameters {
    public:
        LoadOperation colorLoadOperation = LoadOperation::eClear;
        LoadOperation depthLoadOperation = LoadOperation::eClear;
        LoadOperation stencilLoadOperation = LoadOperation::eClear;

        StoreOperation colorStoreOperation = StoreOperation::eStore;
        StoreOperation depthStoreOperation = StoreOperation::eStore;
        StoreOperation stencilStoreOperation = StoreOperation::eStore;
    };



    struct GFX_API ColorBlendState
    {
        struct GFX_API AttachmentState
        {
            bool blendEnable = false;
            BlendFactor srcColorBlendFactor = BlendFactor::eOne;
            BlendFactor dstColorBlendFactor = BlendFactor::eZero;
            BlendOp colorBlendOp = BlendOp::eAdd;
            BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;
            BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;
            BlendOp alphaBlendOp = BlendOp::eAdd;
            Flags<ColorComponent> colorWriteMask =
                Flags(ColorComponent::eR) | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
        };
        bool enableLogicOp = false;
        LogicOp logicOp = LogicOp::eCopy;
        std::vector<AttachmentState> attachments = {};
        float blendConstants[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    class GFX_API BufferBarrier {
    public:
        BufferBarrier(
            const ResourceRef<const Buffer> &buffer,
            ResourceAccess dstAccess,
            glm::u64 offset = 0,
            glm::u64 size = UINT64_MAX);

        [[nodiscard]] ResourceRef<const Buffer> getBuffer() const { return _buffer; }
        [[nodiscard]] ResourceAccess getDstAccess() const { return _dstAccess; }
        [[nodiscard]] glm::u64 getOffset() const { return _offset; }
        [[nodiscard]] glm::u64 getSize() const { return _size; }

    private:
        ResourceRef<const Buffer> _buffer;
        ResourceAccess _dstAccess;
        glm::u64 _offset;
        glm::u64 _size;
    };

    class GFX_API ImageBarrier {
    public:
        ImageBarrier(
            const ResourceRef<const Image> &image,
            ResourceAccess dstAccess,
            std::optional<glm::u32> baseMipLevel = std::nullopt,
            std::optional<glm::u32> levelCount = std::nullopt,
            std::optional<glm::u32> baseArrayLayer = std::nullopt,
            std::optional<glm::u32> layerCount = std::nullopt);

        [[nodiscard]] ResourceRef<const Image> getImage() const { return _image; }
        [[nodiscard]] ResourceAccess getDstAccess() const { return _dstAccess; }
        [[nodiscard]] std::optional<glm::u32> getBaseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] std::optional<glm::u32> getLevelCount() const { return _levelCount; }
        [[nodiscard]] std::optional<glm::u32> getBaseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] std::optional<glm::u32> getLayerCount() const { return _layerCount; }

    private:
        ResourceRef<const Image> _image;
        ResourceAccess _dstAccess;
        std::optional<glm::u32> _baseMipLevel;
        std::optional<glm::u32> _levelCount;
        std::optional<glm::u32> _baseArrayLayer;
        std::optional<glm::u32> _layerCount;
    };
}