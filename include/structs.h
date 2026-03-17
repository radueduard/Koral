//
// Created by radue on 3/6/2026.
//

#pragma once
#include <stdexcept>
#include <glm/fwd.hpp>

#include "flags.h"
#include "api.h"

namespace gfx
{
    enum class ChannelType : glm::u32
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

    enum class DescriptorType {
        eUniformBuffer,
        eStorageBuffer,
        eCombinedImageSampler,
        eStorageImage,
        eSampler,
        eSampledImage,
        eUniformTexelBuffer,
        eStorageTexelBuffer,
    };

    struct GFX_API VertexInputAttributeDescription
    {
        glm::u32 location;
        glm::u32 binding;
        glm::u32 channelCount;
        ChannelType channelType;
        glm::u32 offset;
    };

    struct GFX_API VertexInputBindingDescription
    {
        glm::u32 binding;
        glm::u32 stride;
    };



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

    enum class Topology : uint8_t
    {
        ePointList = 0,
        eLineList = 1,
        eLineStrip = 2,
        eTriangleList = 3,
        eTriangleStrip = 4,
        eTriangleFan = 5,
        eLineListAdjacency = 6,
        eLineStripAdjacency = 7,
        eTriangleListAdjacency = 8,
        eTriangleStripAdjacency = 9,
        ePatchList = 10
    };

    struct GFX_API InputAssemblyState
    {
        Topology topology = Topology::eTriangleList;
        bool primitiveRestartEnable = false;
    };

    enum class PolygonMode : uint8_t
    {
        eFill = 0,
        eLine = 1,
        ePoint = 2
    };

    enum class CullMode : uint8_t
    {
        eFront = 1,
        eBack = 2,
    };

    enum class FrontFace : bool
    {
        eCounterClockwise = false,
        eClockwise = true
    };

    struct GFX_API RasterizationState
    {
        bool depthClampEnable = false;
        bool rasterizerDiscardEnable = false;
        PolygonMode polygonMode = PolygonMode::eFill;
        Flags<CullMode> cullMode = Flags<CullMode>();
        FrontFace frontFace = FrontFace::eCounterClockwise;
        bool depthBiasEnable = false;
        float depthBiasConstantFactor = 0.f;
        float depthBiasClamp = 0.f;
        float depthBiasSlopeFactor = 0.f;
        float lineWidth = 1.f;
    };

    enum class SampleCount : uint8_t
    {
        e1 = 1,
        e2 = 2,
        e4 = 4,
        e8 = 8,
        e16 = 16,
        e32 = 32,
        e64 = 64
    };

    struct GFX_API MultisampleState
    {
        SampleCount sampleCount = SampleCount::e1;
        bool sampleShadingEnable = false;
        float minSampleShading = 1.f;
    };

    enum class CompareOp : uint8_t
    {
        eNever = 0,
        eLess = 1,
        eEqual = 2,
        eLessOrEqual = 3,
        eGreater = 4,
        eNotEqual = 5,
        eGreaterOrEqual = 6,
        eAlways = 7
    };

    enum class StencilOp : uint8_t
    {
        eKeep = 0,
        eZero = 1,
        eReplace = 2,
        eIncrementAndClamp = 3,
        eDecrementAndClamp = 4,
        eInvert = 5,
        eIncrementAndWrap = 6,
        eDecrementAndWrap = 7
    };

    struct GFX_API StencilOpState
    {
        StencilOp failOp = StencilOp::eKeep;
        StencilOp passOp = StencilOp::eKeep;
        StencilOp depthFailOp = StencilOp::eKeep;
        CompareOp compareOp = CompareOp::eAlways;
        uint32_t compareMask = 0;
        uint32_t writeMask = 0;
        uint32_t reference = 0;
    };

    struct GFX_API DepthStencilState
    {
        // Depth settings
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::eLess;
        bool depthBoundsEnable = false;

        // Stencil settings
        bool stencilEnable = false;
        StencilOpState stencilFront = {};
        StencilOpState stencilBack = {};
        float minDepth = 0.f;
        float maxDepth = 1.f;
    };

    enum class BlendMode : uint8_t
    {
        eAdd = 0,
        eSubtract = 1,
        eReverseSubtract = 2,
        eMin = 3,
        eMax = 4
    };

    enum class BlendFactor : uint8_t
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

    enum class LogicOp : uint8_t
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

    enum class BlendOp : uint8_t
    {
        eAdd = 0,
        eSubtract = 1,
        eReverseSubtract = 2,
        eMin = 3,
        eMax = 4
    };

    enum class ColorComponent : uint8_t
    {
        eR = 1,
        eG = 2,
        eB = 4,
        eA = 8
    };
}
