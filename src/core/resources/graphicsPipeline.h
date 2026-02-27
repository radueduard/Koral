//
// Created by radue on 2/22/2026.
//

#pragma once
#include <optional>
#include <glm/fwd.hpp>

#include "shader.h"
#include "utils/flags.h"


namespace gfx
{
    enum class ChannelType : uint32_t
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


    struct VertexInputAttributeDescription
    {
        glm::u32 location;
        glm::u32 binding;
        glm::u32 channelCount;
        ChannelType channelType;
        glm::u32 offset;
    };

    struct VertexInputBindingDescription
    {
        glm::u32 binding;
        glm::u32 stride;
    };

    struct VertexInputState {
        std::vector<VertexInputAttributeDescription> attributes = {};
        std::vector<VertexInputBindingDescription> bindings = {};
    };

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

    struct InputAssemblyState
    {
        Topology topology = Topology::eTriangleList;
        bool primitiveRestartEnable = false;
    };

    struct VertexState
    {
        std::reference_wrapper<const Shader> shader;
        VertexInputState vertexInputState;
        InputAssemblyState inputAssemblyState;
    };

    struct TessellationState
    {
        std::reference_wrapper<const Shader> controlShader;
        std::reference_wrapper<const Shader> evalShader;
        glm::u32 patchControlPoints = 3;

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

    struct RasterizationState
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

    struct MultisampleState
    {
        // TODO
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

    struct StencilOpState
    {
        StencilOp failOp = StencilOp::eKeep;
        StencilOp passOp = StencilOp::eKeep;
        StencilOp depthFailOp = StencilOp::eKeep;
        CompareOp compareOp = CompareOp::eAlways;
        uint32_t compareMask = 0;
        uint32_t writeMask = 0;
        uint32_t reference = 0;
    };

    struct DepthStencilState
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

    struct ColorBlendState
    {
        struct AttachmentState
        {
            bool blendEnable = false;
            BlendFactor srcColorBlendFactor = BlendFactor::eOne;
            BlendFactor dstColorBlendFactor = BlendFactor::eZero;
            BlendOp colorBlendOp = BlendOp::eAdd;
            BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;
            BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;
            BlendOp alphaBlendOp = BlendOp::eAdd;
            Flags<ColorComponent> colorWriteMask = Flags(ColorComponent::eR) | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
        };

        bool enableLogicOp = false;
        LogicOp logicOp = LogicOp::eCopy;
        std::vector<AttachmentState> attachments = {};
        float blendConstants[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    class GraphicsPipeline
    {
    public:
        struct Builder {
            std::optional<VertexState> vertexState = std::nullopt;
            std::optional<TessellationState> tessellationState = std::nullopt;
            std::optional<std::reference_wrapper<const Shader>> geometryShader = std::nullopt;
            std::optional<std::reference_wrapper<const Shader>> fragmentShader = std::nullopt;

            std::optional<std::reference_wrapper<const Shader>> taskShader = std::nullopt;
            std::optional<std::reference_wrapper<const Shader>> meshShader = std::nullopt;

            RasterizationState rasterizationState = {};
            MultisampleState multisampleState = {};
            DepthStencilState depthStencilState = {};
            ColorBlendState colorBlendState = {};

            Builder& setVertexStage(const VertexState& vertexState);
            Builder& setTessellationState(const TessellationState& tessellationState);
            Builder& setGeometryShader(const Shader& geometryShader);
            Builder& setFragmentShader(const Shader& fragmentShader);

            Builder& setTaskShader(const Shader& taskShader);
            Builder& setMeshShader(const Shader& meshShader);

            Builder& setRasterizationState(const RasterizationState& rasterizationState);
            Builder& setMultisampleState(const MultisampleState& multisampleState);
            Builder& setDepthStencilState(const DepthStencilState& depthStencilState);
            Builder& setColorBlendState(const ColorBlendState& colorBlendState);
        };

        static std::unique_ptr<GraphicsPipeline> Create(const Builder& createInfo);

        virtual ~GraphicsPipeline() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

    protected:
        explicit GraphicsPipeline(const Builder& createInfo);

        std::optional<VertexState> _vertexState;
        std::optional<TessellationState> _tessellationState;
        std::optional<std::reference_wrapper<const Shader>> _geometryShader;
        std::optional<std::reference_wrapper<const Shader>> _fragmentShader;

        std::optional<std::reference_wrapper<const Shader>> _taskShader;
        std::optional<std::reference_wrapper<const Shader>> _meshShader;

        RasterizationState _rasterizationState;
        MultisampleState _multisampleState;
        DepthStencilState _depthStencilState;
        ColorBlendState _colorBlendState;
    };
}
