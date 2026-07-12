//
// Created by radue on 2/28/2026.
//

#pragma once
#include <image.h>
#include <framebuffer.h>

#include "image.h"
#include "imageView.h"
#include "graphicsPipeline.h"
#include "sampler.h"

#include <vulkan/vulkan.hpp>


namespace kor
{
    inline ::vk::AttachmentLoadOp getVkLoadOp(const LoadOperation loadOperation) {
        switch (loadOperation)
        {
        case LoadOperation::eLoad: return ::vk::AttachmentLoadOp::eLoad;
        case LoadOperation::eClear: return ::vk::AttachmentLoadOp::eClear;
        case LoadOperation::eDontCare: return ::vk::AttachmentLoadOp::eDontCare;
        default: throw std::runtime_error("Unknown load operation type!");
        }
    }

    inline ::vk::AttachmentStoreOp getVkStoreOp(const StoreOperation storeOperation) {
        switch (storeOperation)
        {
        case StoreOperation::eStore: return ::vk::AttachmentStoreOp::eStore;
        case StoreOperation::eDontCare: return ::vk::AttachmentStoreOp::eDontCare;
        default: throw std::runtime_error("Unknown store operation type!");
        }
    }

    inline ::vk::ResolveModeFlagBits getVkResolveMode(const ResolveMode resolveMode) {
        switch (resolveMode)
        {
        case ResolveMode::eNone: return ::vk::ResolveModeFlagBits::eNone;
        case ResolveMode::eAverage: return ::vk::ResolveModeFlagBits::eAverage;
        case ResolveMode::eMin: return ::vk::ResolveModeFlagBits::eMin;
        case ResolveMode::eMax: return ::vk::ResolveModeFlagBits::eMax;
        case ResolveMode::eSampleZero: return ::vk::ResolveModeFlagBits::eSampleZero;
        default: throw std::runtime_error("Unknown resolve mode type!");
        }
    }

    inline ::vk::AccessFlags getVkAccessFlags(const ResourceAccess access) {
        switch (access)
        {
        case ResourceAccess::ComputeRead: return ::vk::AccessFlagBits::eShaderRead;
        case ResourceAccess::ComputeWrite: return ::vk::AccessFlagBits::eShaderWrite;
        case ResourceAccess::ComputeReadWrite: return ::vk::AccessFlagBits::eShaderRead | ::vk::AccessFlagBits::eShaderWrite;
        case ResourceAccess::VertexBuffer: return ::vk::AccessFlagBits::eVertexAttributeRead;
        case ResourceAccess::IndexBuffer: return ::vk::AccessFlagBits::eIndexRead;
        case ResourceAccess::IndirectBuffer: return ::vk::AccessFlagBits::eIndirectCommandRead;
        case ResourceAccess::ColorAttachment: return ::vk::AccessFlagBits::eColorAttachmentWrite;
        case ResourceAccess::DepthStencilAttachment:
        case ResourceAccess::DepthAttachment:
        case ResourceAccess::StencilAttachment:
            return ::vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        case ResourceAccess::DepthStencilRead:
        case ResourceAccess::DepthRead:
        case ResourceAccess::StencilRead:
            return ::vk::AccessFlagBits::eDepthStencilAttachmentRead;
        case ResourceAccess::TransferSrc: return ::vk::AccessFlagBits::eTransferRead;
        case ResourceAccess::TransferDst: return ::vk::AccessFlagBits::eTransferWrite;

        case ResourceAccess::VertexShaderRead:
        case ResourceAccess::FragmentShaderRead:
        case ResourceAccess::AllShaderRead:
            return ::vk::AccessFlagBits::eShaderRead;
        case ResourceAccess::VertexShaderWrite:
        case ResourceAccess::FragmentShaderWrite:
        case ResourceAccess::AllShaderWrite:
            return ::vk::AccessFlagBits::eShaderWrite;
        case ResourceAccess::VertexShaderReadWrite:
        case ResourceAccess::FragmentShaderReadWrite:
        case ResourceAccess::AllShaderReadWrite:
            return ::vk::AccessFlagBits::eShaderRead | ::vk::AccessFlagBits::eShaderWrite;

        case ResourceAccess::Present: return {}; // no access mask needed for present
        default: throw std::runtime_error("Unknown resource access type!");
        }
    }

    inline ::vk::PipelineStageFlags getVkPipelineStageFlags(const ResourceAccess access) {
        switch (access)
        {
        case ResourceAccess::ComputeRead:
        case ResourceAccess::ComputeWrite:
        case ResourceAccess::ComputeReadWrite:
            return ::vk::PipelineStageFlagBits::eComputeShader;
        case ResourceAccess::VertexBuffer:
        case ResourceAccess::IndexBuffer:
            return ::vk::PipelineStageFlagBits::eVertexInput;
        case ResourceAccess::IndirectBuffer:
            return ::vk::PipelineStageFlagBits::eDrawIndirect;
        case ResourceAccess::VertexShaderRead:
        case ResourceAccess::VertexShaderWrite:
        case ResourceAccess::VertexShaderReadWrite:
            return ::vk::PipelineStageFlagBits::eVertexShader;
        case ResourceAccess::FragmentShaderRead:
        case ResourceAccess::FragmentShaderWrite:
        case ResourceAccess::FragmentShaderReadWrite:
            return ::vk::PipelineStageFlagBits::eFragmentShader;
        case ResourceAccess::ColorAttachment:
            return ::vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case ResourceAccess::DepthStencilAttachment:
        case ResourceAccess::DepthStencilRead:
        case ResourceAccess::DepthAttachment:
        case ResourceAccess::DepthRead:
        case ResourceAccess::StencilAttachment:
        case ResourceAccess::StencilRead:
            return ::vk::PipelineStageFlagBits::eEarlyFragmentTests | ::vk::PipelineStageFlagBits::eLateFragmentTests;
        case ResourceAccess::TransferSrc:
        case ResourceAccess::TransferDst:
            return ::vk::PipelineStageFlagBits::eTransfer;
        case ResourceAccess::AllShaderRead:
        case ResourceAccess::AllShaderWrite:
            case ResourceAccess::AllShaderReadWrite:
            return ::vk::PipelineStageFlagBits::eAllCommands;
        case ResourceAccess::Present:
            return ::vk::PipelineStageFlagBits::eBottomOfPipe;
        default: throw std::runtime_error("Unknown resource access type!");
        }
    }

    inline ::vk::ImageAspectFlags getVkImageAspectFlags(const Image::Format format) {
        switch (format)
        {
        case Image::Format::eD16_UNORM:
        case Image::Format::eD32_SFLOAT:
            return ::vk::ImageAspectFlagBits::eDepth;
        case Image::Format::eD24_UNORM_S8_UINT:
        case Image::Format::eD32_SFLOAT_S8_UINT:
            return ::vk::ImageAspectFlagBits::eDepth | ::vk::ImageAspectFlagBits::eStencil;
        default: return ::vk::ImageAspectFlagBits::eColor;
        }
    }

    inline ::vk::ImageLayout getVkImageLayout(const ResourceAccess access) {
        switch (access)
        {
        case ResourceAccess::ComputeWrite:
        case ResourceAccess::ComputeReadWrite:
        case ResourceAccess::VertexShaderWrite:
        case ResourceAccess::VertexShaderReadWrite:
        case ResourceAccess::FragmentShaderWrite:
        case ResourceAccess::FragmentShaderReadWrite:
        case ResourceAccess::AllShaderWrite:
        case ResourceAccess::AllShaderReadWrite:
            return ::vk::ImageLayout::eGeneral;
        case ResourceAccess::ComputeRead:
        case ResourceAccess::VertexShaderRead:
        case ResourceAccess::FragmentShaderRead:
        case ResourceAccess::AllShaderRead:
            return ::vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceAccess::ColorAttachment:
            return ::vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceAccess::DepthStencilAttachment:
            return ::vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceAccess::DepthStencilRead:
            return ::vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case ResourceAccess::DepthAttachment:
            return ::vk::ImageLayout::eDepthAttachmentOptimal;
        case ResourceAccess::DepthRead:
            return ::vk::ImageLayout::eDepthReadOnlyOptimal;
        case ResourceAccess::StencilAttachment:
            return ::vk::ImageLayout::eStencilAttachmentOptimal;
        case ResourceAccess::StencilRead:
            return ::vk::ImageLayout::eStencilReadOnlyOptimal;
        case ResourceAccess::TransferSrc:
            return ::vk::ImageLayout::eTransferSrcOptimal;
        case ResourceAccess::TransferDst:
            return ::vk::ImageLayout::eTransferDstOptimal;
        case ResourceAccess::Present:
            return ::vk::ImageLayout::ePresentSrcKHR;
        default: throw std::runtime_error("Unknown resource access type!");
        }
    }

    inline ::vk::IndexType getVkIndexType(const ChannelType indexType) {
        switch (indexType)
        {
        case ChannelType::eUByte: return ::vk::IndexType::eUint8;
        case ChannelType::eUShort: return ::vk::IndexType::eUint16;
        case ChannelType::eUInt: return ::vk::IndexType::eUint32;
        default: throw std::runtime_error("Unknown index type!");
        }
    }

    inline ::vk::ShaderStageFlags getVkShaderStageFlags(const Flags<Shader::Stage> stages)
    {
        ::vk::ShaderStageFlags flags = {};
        if (stages & Shader::Stage::eVertex) flags |= ::vk::ShaderStageFlagBits::eVertex;
        if (stages & Shader::Stage::eTessellationControl) flags |= ::vk::ShaderStageFlagBits::eTessellationControl;
        if (stages & Shader::Stage::eTessellationEvaluation) flags |= ::vk::ShaderStageFlagBits::eTessellationEvaluation;
        if (stages & Shader::Stage::eGeometry) flags |= ::vk::ShaderStageFlagBits::eGeometry;
        if (stages & Shader::Stage::eFragment) flags |= ::vk::ShaderStageFlagBits::eFragment;
        if (stages & Shader::Stage::eCompute) flags |= ::vk::ShaderStageFlagBits::eCompute;
        if (stages & Shader::Stage::eTask) flags |= ::vk::ShaderStageFlagBits::eTaskEXT;
        if (stages & Shader::Stage::eMesh) flags |= ::vk::ShaderStageFlagBits::eMeshEXT;
        if (stages & Shader::Stage::eRaygen) flags |= ::vk::ShaderStageFlagBits::eRaygenKHR;
        if (stages & Shader::Stage::eAnyHit) flags |= ::vk::ShaderStageFlagBits::eAnyHitKHR;
        if (stages & Shader::Stage::eClosestHit) flags |= ::vk::ShaderStageFlagBits::eClosestHitKHR;
        if (stages & Shader::Stage::eMiss) flags |= ::vk::ShaderStageFlagBits::eMissKHR;
        if (stages & Shader::Stage::eIntersection) flags |= ::vk::ShaderStageFlagBits::eIntersectionKHR;
        if (stages & Shader::Stage::eCallable) flags |= ::vk::ShaderStageFlagBits::eCallableKHR;
        return flags;
    }

    inline ::vk::PolygonMode getVkPolygonMode(const PolygonMode polygonMode)
    {
        switch (polygonMode)
        {
        case PolygonMode::eFill: return ::vk::PolygonMode::eFill;
        case PolygonMode::eLine: return ::vk::PolygonMode::eLine;
        case PolygonMode::ePoint: return ::vk::PolygonMode::ePoint;
        default: throw std::runtime_error("Unknown polygon mode!");
        }
    }

    inline ::vk::CullModeFlags getVkCullMode(const Flags<CullMode> cullMode)
    {
        ::vk::CullModeFlags flags = {};
        if (cullMode & CullMode::eFront) flags |= ::vk::CullModeFlagBits::eFront;
        if (cullMode & CullMode::eBack) flags |= ::vk::CullModeFlagBits::eBack;
        return flags;
    }

    inline ::vk::FrontFace getVkFrontFace(const FrontFace frontFace) {
        switch (frontFace)
        {
        case FrontFace::eClockwise: return ::vk::FrontFace::eClockwise;
        case FrontFace::eCounterClockwise: return ::vk::FrontFace::eCounterClockwise;
        default: throw std::runtime_error("Unknown front face!");
        }
    }

    inline ::vk::PrimitiveTopology getVkPrimitiveTopology(const Topology topology)
    {
        switch (topology)
        {
        case Topology::ePointList: return ::vk::PrimitiveTopology::ePointList;
        case Topology::eLineList: return ::vk::PrimitiveTopology::eLineList;
        case Topology::eLineStrip: return ::vk::PrimitiveTopology::eLineStrip;
        case Topology::eTriangleList: return ::vk::PrimitiveTopology::eTriangleList;
        case Topology::eTriangleStrip: return ::vk::PrimitiveTopology::eTriangleStrip;
        case Topology::eTriangleFan: return ::vk::PrimitiveTopology::eTriangleFan;
        case Topology::eLineListAdjacency: return ::vk::PrimitiveTopology::eLineListWithAdjacency;
        case Topology::eLineStripAdjacency: return ::vk::PrimitiveTopology::eLineStripWithAdjacency;
        case Topology::eTriangleListAdjacency: return ::vk::PrimitiveTopology::eTriangleListWithAdjacency;
        case Topology::eTriangleStripAdjacency: return ::vk::PrimitiveTopology::eTriangleStripWithAdjacency;
        case Topology::ePatchList: return ::vk::PrimitiveTopology::ePatchList;
        default: throw std::runtime_error("Unknown topology!");
        }
    }

    inline ::vk::SampleCountFlagBits getVkSampleCount(const SampleCount sampleCount)
    {
        switch (sampleCount)
        {
        case SampleCount::e1: return ::vk::SampleCountFlagBits::e1;
        case SampleCount::e2: return ::vk::SampleCountFlagBits::e2;
        case SampleCount::e4: return ::vk::SampleCountFlagBits::e4;
        case SampleCount::e8: return ::vk::SampleCountFlagBits::e8;
        case SampleCount::e16: return ::vk::SampleCountFlagBits::e16;
        case SampleCount::e32: return ::vk::SampleCountFlagBits::e32;
        case SampleCount::e64: return ::vk::SampleCountFlagBits::e64;
        default: throw std::runtime_error("Unknown sample count!");
        }
    }

    inline ::vk::DescriptorType getVkDescriptorType(const kor::DescriptorType type)
    {
        switch (type)
        {
        case DescriptorType::eSampler: return ::vk::DescriptorType::eSampler;
        case DescriptorType::eCombinedImageSampler: return ::vk::DescriptorType::eCombinedImageSampler;
        case DescriptorType::eStorageImage: return ::vk::DescriptorType::eStorageImage;
        case DescriptorType::eUniformBuffer: return ::vk::DescriptorType::eUniformBuffer;
        case DescriptorType::eUniformTexelBuffer: return ::vk::DescriptorType::eUniformTexelBuffer;
        case DescriptorType::eSampledImage: return ::vk::DescriptorType::eSampledImage;
        case DescriptorType::eStorageTexelBuffer: return ::vk::DescriptorType::eStorageTexelBuffer;
        case DescriptorType::eStorageBuffer: return ::vk::DescriptorType::eStorageBuffer;
        case DescriptorType::eAccelerationStructure: return ::vk::DescriptorType::eAccelerationStructureKHR;
        default: throw std::runtime_error("Unknown descriptor type!");
        }
    }

    inline DescriptorType getDescriptorType(const ::vk::DescriptorType type)
    {
        switch (type)
        {
        case ::vk::DescriptorType::eSampler: return DescriptorType::eSampler;
        case ::vk::DescriptorType::eCombinedImageSampler: return DescriptorType::eCombinedImageSampler;
        case ::vk::DescriptorType::eSampledImage: return DescriptorType::eSampledImage;
        case ::vk::DescriptorType::eStorageImage: return DescriptorType::eStorageImage;
        case ::vk::DescriptorType::eUniformTexelBuffer: return DescriptorType::eUniformTexelBuffer;
        case ::vk::DescriptorType::eStorageTexelBuffer: return DescriptorType::eStorageTexelBuffer;
        case ::vk::DescriptorType::eUniformBuffer: return DescriptorType::eUniformBuffer;
        case ::vk::DescriptorType::eStorageBuffer: return DescriptorType::eStorageBuffer;
        case ::vk::DescriptorType::eAccelerationStructureKHR: return DescriptorType::eAccelerationStructure;

        default: throw std::runtime_error("Unknown descriptor type!");
        }
    }

    inline ::vk::Format getVkFormat(ChannelType channelType, glm::u32 channelCount)
    {
        switch (channelType)
        {
        case ChannelType::eFloat:
            switch (channelCount)
            {
            case 1: return ::vk::Format::eR32Sfloat;
            case 2: return ::vk::Format::eR32G32Sfloat;
            case 3: return ::vk::Format::eR32G32B32Sfloat;
            case 4: return ::vk::Format::eR32G32B32A32Sfloat;
            default: throw std::runtime_error("Unsupported channel count for float type!");
            }
        case ChannelType::eInt:
            switch (channelCount)
            {
            case 1: return ::vk::Format::eR32Sint;
            case 2: return ::vk::Format::eR32G32Sint;
            case 3: return ::vk::Format::eR32G32B32Sint;
            case 4: return ::vk::Format::eR32G32B32A32Sint;
            default: throw std::runtime_error("Unsupported channel count for int type!");
            }
        case ChannelType::eUInt:
            switch (channelCount)
            {
            case 1: return ::vk::Format::eR32Uint;
            case 2: return ::vk::Format::eR32G32Uint;
            case 3: return ::vk::Format::eR32G32B32Uint;
            case 4: return ::vk::Format::eR32G32B32A32Uint;
            default: throw std::runtime_error("Unsupported channel count for uint type!");
            }
        default:
            throw std::runtime_error("Unsupported channel type for vertex input attribute!");
        }
    }

    inline ::vk::Format getVkFormat(const Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eR8_UNORM: return ::vk::Format::eR8Unorm;
        case Image::Format::eR8_SNORM: return ::vk::Format::eR8Snorm;
        case Image::Format::eR8_UINT: return ::vk::Format::eR8Uint;
        case Image::Format::eR8_SINT: return ::vk::Format::eR8Sint;
        case Image::Format::eRG8_UNORM: return ::vk::Format::eR8G8Unorm;
        case Image::Format::eRG8_SNORM: return ::vk::Format::eR8G8Snorm;
        case Image::Format::eRG8_UINT: return ::vk::Format::eR8G8Uint;
        case Image::Format::eRG8_SINT: return ::vk::Format::eR8G8Sint;
        case Image::Format::eRGB8_UNORM: return ::vk::Format::eR8G8B8Unorm;
        case Image::Format::eRGB8_SNORM: return ::vk::Format::eR8G8B8Snorm;
        case Image::Format::eRGB8_UINT: return ::vk::Format::eR8G8B8Uint;
        case Image::Format::eRGB8_SINT: return ::vk::Format::eR8G8B8Sint;
        case Image::Format::eRGB8_SRGB: return ::vk::Format::eR8G8B8Srgb;
        case Image::Format::eRGBA8_UNORM: return ::vk::Format::eR8G8B8A8Unorm;
        case Image::Format::eRGBA8_SNORM: return ::vk::Format::eR8G8B8A8Snorm;
        case Image::Format::eRGBA8_UINT: return ::vk::Format::eR8G8B8A8Uint;
        case Image::Format::eRGBA8_SINT: return ::vk::Format::eR8G8B8A8Sint;
        case Image::Format::eRGBA8_SRGB: return ::vk::Format::eR8G8B8A8Srgb;
        case Image::Format::eR16_UNORM: return ::vk::Format::eR16Unorm;
        case Image::Format::eR16_SNORM: return ::vk::Format::eR16Snorm;
        case Image::Format::eR16_UINT: return ::vk::Format::eR16Uint;
        case Image::Format::eR16_SINT: return ::vk::Format::eR16Sint;
        case Image::Format::eR16_SFLOAT: return ::vk::Format::eR16Sfloat;
        case Image::Format::eRG16_UNORM: return ::vk::Format::eR16G16Unorm;
        case Image::Format::eRG16_SNORM: return ::vk::Format::eR16G16Snorm;
        case Image::Format::eRG16_UINT: return ::vk::Format::eR16G16Uint;
        case Image::Format::eRG16_SINT: return ::vk::Format::eR16G16Sint;
        case Image::Format::eRG16_SFLOAT: return ::vk::Format::eR16G16Sfloat;
        case Image::Format::eRGB16_UNORM: return ::vk::Format::eR16G16B16Unorm;
        case Image::Format::eRGB16_SNORM: return ::vk::Format::eR16G16B16Snorm;
        case Image::Format::eRGB16_UINT: return ::vk::Format::eR16G16B16Uint;
        case Image::Format::eRGB16_SINT: return ::vk::Format::eR16G16B16Sint;
        case Image::Format::eRGB16_SFLOAT: return ::vk::Format::eR16G16B16Sfloat;
        case Image::Format::eRGBA16_UNORM: return ::vk::Format::eR16G16B16A16Unorm;
        case Image::Format::eRGBA16_SNORM: return ::vk::Format::eR16G16B16A16Snorm;
        case Image::Format::eRGBA16_UINT: return ::vk::Format::eR16G16B16A16Uint;
        case Image::Format::eRGBA16_SINT: return ::vk::Format::eR16G16B16A16Sint;
        case Image::Format::eRGBA16_SFLOAT: return ::vk::Format::eR16G16B16A16Sfloat;
        case Image::Format::eR32_UINT: return ::vk::Format::eR32Uint;
        case Image::Format::eR32_SINT: return ::vk::Format::eR32Sint;
        case Image::Format::eR32_SFLOAT: return ::vk::Format::eR32Sfloat;
        case Image::Format::eRG32_UINT: return ::vk::Format::eR32G32Uint;
        case Image::Format::eRG32_SINT: return ::vk::Format::eR32G32Sint;
        case Image::Format::eRG32_SFLOAT: return ::vk::Format::eR32G32Sfloat;
        case Image::Format::eRGB32_UINT: return ::vk::Format::eR32G32B32Uint;
        case Image::Format::eRGB32_SINT: return ::vk::Format::eR32G32B32Sint;
        case Image::Format::eRGB32_SFLOAT: return ::vk::Format::eR32G32B32Sfloat;
        case Image::Format::eRGBA32_UINT: return ::vk::Format::eR32G32B32A32Uint;
        case Image::Format::eRGBA32_SINT: return ::vk::Format::eR32G32B32A32Sint;
        case Image::Format::eRGBA32_SFLOAT: return ::vk::Format::eR32G32B32A32Sfloat;
        case Image::Format::eD16_UNORM: return ::vk::Format::eD16Unorm;
        case Image::Format::eD24_UNORM_S8_UINT: return ::vk::Format::eD24UnormS8Uint;
        case Image::Format::eD32_SFLOAT: return ::vk::Format::eD32Sfloat;
        case Image::Format::eD32_SFLOAT_S8_UINT: return ::vk::Format::eD32SfloatS8Uint;

        case Image::Format::eBGRA8_UNORM : return ::vk::Format::eB8G8R8A8Unorm;
        case Image::Format::eBGRA8_SRGB : return ::vk::Format::eB8G8R8A8Srgb;
        case Image::Format::eBC1_RGB_UNORM: return ::vk::Format::eBc1RgbUnormBlock;
        case Image::Format::eBC1_RGB_SRGB: return ::vk::Format::eBc1RgbSrgbBlock;
        case Image::Format::eBC1_RGBA_UNORM: return ::vk::Format::eBc1RgbaUnormBlock;
        case Image::Format::eBC1_RGBA_SRGB: return ::vk::Format::eBc1RgbaSrgbBlock;
        case Image::Format::eBC2_UNORM: return ::vk::Format::eBc2UnormBlock;
        case Image::Format::eBC2_SRGB: return ::vk::Format::eBc2SrgbBlock;
        case Image::Format::eBC3_UNORM: return ::vk::Format::eBc3UnormBlock;
        case Image::Format::eBC3_SRGB: return ::vk::Format::eBc3SrgbBlock;
        case Image::Format::eBC7_UNORM: return ::vk::Format::eBc7UnormBlock;
        case Image::Format::eBC7_SRGB: return ::vk::Format::eBc7SrgbBlock;
        default: throw std::runtime_error("Unsupported image format!");
        }
    }

    inline kor::Image::Format getFormat(const ::vk::Format format)
    {
        switch (format)
        {
        case ::vk::Format::eR8Unorm: return Image::Format::eR8_UNORM;
        case ::vk::Format::eR8Snorm: return Image::Format::eR8_SNORM;
        case ::vk::Format::eR8Uint: return Image::Format::eR8_UINT;
        case ::vk::Format::eR8Sint: return Image::Format::eR8_SINT;
        case ::vk::Format::eR8G8Unorm: return Image::Format::eRG8_UNORM;
        case ::vk::Format::eR8G8Snorm: return Image::Format::eRG8_SNORM;
        case ::vk::Format::eR8G8Uint: return Image::Format::eRG8_UINT;
        case ::vk::Format::eR8G8Sint: return Image::Format::eRG8_SINT;
        case ::vk::Format::eR8G8B8Unorm: return Image::Format::eRGB8_UNORM;
        case ::vk::Format::eR8G8B8Snorm: return Image::Format::eRGB8_SNORM;
        case ::vk::Format::eR8G8B8Uint: return Image::Format::eRGB8_UINT;
        case ::vk::Format::eR8G8B8Sint: return Image::Format::eRGB8_SINT;
        case ::vk::Format::eR8G8B8Srgb: return Image::Format::eRGB8_SRGB;
        case ::vk::Format::eR8G8B8A8Unorm: return Image::Format::eRGBA8_UNORM;
        case ::vk::Format::eR8G8B8A8Snorm: return Image::Format::eRGBA8_SNORM;
        case ::vk::Format::eR8G8B8A8Uint: return Image::Format::eRGBA8_UINT;
        case ::vk::Format::eR8G8B8A8Sint: return Image::Format::eRGBA8_SINT;
        case ::vk::Format::eR8G8B8A8Srgb: return Image::Format::eRGBA8_SRGB;
        case ::vk::Format::eR16Unorm: return Image::Format::eR16_UNORM;
        case ::vk::Format::eR16Snorm: return Image::Format::eR16_SNORM;
        case ::vk::Format::eR16Uint: return Image::Format::eR16_UINT;
        case ::vk::Format::eR16Sint: return Image::Format::eR16_SINT;
        case ::vk::Format::eR16Sfloat: return Image::Format::eR16_SFLOAT;
        case ::vk::Format::eR16G16Unorm: return Image::Format::eRG16_UNORM;
        case ::vk::Format::eR16G16Snorm: return Image::Format::eRG16_SNORM;
        case ::vk::Format::eR16G16Uint: return Image::Format::eRG16_UINT;
        case ::vk::Format::eR16G16Sint: return Image::Format::eRG16_SINT;
        case ::vk::Format::eR16G16Sfloat: return Image::Format::eRG16_SFLOAT;
        case ::vk::Format::eR16G16B16Unorm: return Image::Format::eRGB16_UNORM;
        case ::vk::Format::eR16G16B16Snorm: return Image::Format::eRGB16_SNORM;
        case ::vk::Format::eR16G16B16Uint: return Image::Format::eRGB16_UINT;
        case ::vk::Format::eR16G16B16Sint: return Image::Format::eRGB16_SINT;
        case ::vk::Format::eR16G16B16Sfloat: return Image::Format::eRGB16_SFLOAT;
        case ::vk::Format::eR16G16B16A16Unorm: return Image::Format::eRGBA16_UNORM;
        case ::vk::Format::eR16G16B16A16Snorm: return Image::Format::eRGBA16_SNORM;
        case ::vk::Format::eR16G16B16A16Uint: return Image::Format::eRGBA16_UINT;
        case ::vk::Format::eR16G16B16A16Sint: return Image::Format::eRGBA16_SINT;
        case ::vk::Format::eR16G16B16A16Sfloat: return Image::Format::eRGBA16_SFLOAT;
        case ::vk::Format::eR32Uint: return Image::Format::eR32_UINT;
        case ::vk::Format::eR32Sint: return Image::Format::eR32_SINT;
        case ::vk::Format::eR32Sfloat: return Image::Format::eR32_SFLOAT;
        case ::vk::Format::eR32G32Uint: return Image::Format::eRG32_UINT;
        case ::vk::Format::eR32G32Sint: return Image::Format::eRG32_SINT;
        case ::vk::Format::eR32G32Sfloat: return Image::Format::eRG32_SFLOAT;
        case ::vk::Format::eR32G32B32Uint: return Image::Format::eRGB32_UINT;
        case ::vk::Format::eR32G32B32Sint: return Image::Format::eRGB32_SINT;
        case ::vk::Format::eR32G32B32Sfloat: return Image::Format::eRGB32_SFLOAT;
        case ::vk::Format::eR32G32B32A32Uint: return Image::Format::eRGBA32_UINT;
        case ::vk::Format::eR32G32B32A32Sint: return Image::Format::eRGBA32_SINT;
        case ::vk::Format::eR32G32B32A32Sfloat: return Image::Format::eRGBA32_SFLOAT;
        case ::vk::Format::eD16Unorm: return Image::Format::eD16_UNORM;
        case ::vk::Format::eD24UnormS8Uint: return Image::Format::eD24_UNORM_S8_UINT;
        case ::vk::Format::eD32Sfloat: return Image::Format::eD32_SFLOAT;
        case ::vk::Format::eD32SfloatS8Uint: return Image::Format::eD32_SFLOAT_S8_UINT;

        case ::vk::Format::eB8G8R8A8Unorm: return Image::Format::eBGRA8_UNORM;
        case ::vk::Format::eB8G8R8A8Srgb: return Image::Format::eBGRA8_SRGB;
        default: throw std::runtime_error("Unsupported image format!");
        }
    }

    inline ::vk::ImageType getVkImageType(const kor::Image::Type type)
    {
        switch (type) {
        case kor::Image::Type::e1D: return ::vk::ImageType::e1D;
        case kor::Image::Type::e2D: return ::vk::ImageType::e2D;
        case kor::Image::Type::e3D: return ::vk::ImageType::e3D;
        default: throw std::runtime_error("Unknown image type");
        }
    }

    inline ::vk::ImageViewType getVkImageViewType(const kor::ImageView::Type type)
    {
        switch (type) {
        case kor::ImageView::Type::e1D: return ::vk::ImageViewType::e1D;
        case kor::ImageView::Type::e2D: return ::vk::ImageViewType::e2D;
        case kor::ImageView::Type::e3D: return ::vk::ImageViewType::e3D;
        case kor::ImageView::Type::eCube: return ::vk::ImageViewType::eCube;
        case kor::ImageView::Type::e1DArray: return ::vk::ImageViewType::e1DArray;
        case kor::ImageView::Type::e2DArray: return ::vk::ImageViewType::e2DArray;
        case kor::ImageView::Type::eCubeArray: return ::vk::ImageViewType::eCubeArray;
        default: throw std::runtime_error("Unknown image type");
        }
    }

    inline ::vk::ComponentSwizzle getVkComponentSwizzle(const kor::ImageView::Swizzle swizzle)
    {
        switch (swizzle)
        {
        case kor::ImageView::Swizzle::eIdentity: return ::vk::ComponentSwizzle::eIdentity;
        case kor::ImageView::Swizzle::eZero: return ::vk::ComponentSwizzle::eZero;
        case kor::ImageView::Swizzle::eOne: return ::vk::ComponentSwizzle::eOne;
        case kor::ImageView::Swizzle::eR: return ::vk::ComponentSwizzle::eR;
        case kor::ImageView::Swizzle::eG: return ::vk::ComponentSwizzle::eG;
        case kor::ImageView::Swizzle::eB: return ::vk::ComponentSwizzle::eB;
        case kor::ImageView::Swizzle::eA: return ::vk::ComponentSwizzle::eA;
        default: throw std::runtime_error("Unknown component swizzle!");
        }
    }

    inline ::vk::ImageUsageFlags getVkUsage(const Flags<kor::Image::Usage> usage)
    {
        ::vk::ImageUsageFlags usageFlags = ::vk::ImageUsageFlags();
        if (usage & kor::Image::Usage::eTransferSrc) usageFlags |= ::vk::ImageUsageFlagBits::eTransferSrc;
        if (usage & kor::Image::Usage::eTransferDst) usageFlags |= ::vk::ImageUsageFlagBits::eTransferDst;
        if (usage & kor::Image::Usage::eSampled) usageFlags |= ::vk::ImageUsageFlagBits::eSampled;
        if (usage & kor::Image::Usage::eStorage) usageFlags |= ::vk::ImageUsageFlagBits::eStorage;
        if (usage & kor::Image::Usage::eColorAttachment) usageFlags |= ::vk::ImageUsageFlagBits::eColorAttachment;
        if (usage & kor::Image::Usage::eDepthStencilAttachment) usageFlags |= ::vk::ImageUsageFlagBits::eDepthStencilAttachment;
        return usageFlags;
    }

    inline ::vk::BufferUsageFlags getVkBufferUsageFlags(const kor::Flags<Buffer::Usage> usage)
    {
        auto vkUsage = ::vk::BufferUsageFlags();
        if (usage & Buffer::Usage::eVertex)
            vkUsage |= ::vk::BufferUsageFlagBits::eVertexBuffer;
        if (usage & Buffer::Usage::eIndex)
            vkUsage |= ::vk::BufferUsageFlagBits::eIndexBuffer;
        if (usage & Buffer::Usage::eIndirect)
            vkUsage |= ::vk::BufferUsageFlagBits::eIndirectBuffer;
        if (usage & Buffer::Usage::eStorage)
            vkUsage |= ::vk::BufferUsageFlagBits::eStorageBuffer;
        if (usage & Buffer::Usage::eTransferSrc)
            vkUsage |= ::vk::BufferUsageFlagBits::eTransferSrc;
        if (usage & Buffer::Usage::eTransferDst)
            vkUsage |= ::vk::BufferUsageFlagBits::eTransferDst;
        if (usage & Buffer::Usage::eUniform)
            vkUsage |= ::vk::BufferUsageFlagBits::eUniformBuffer;
        if (usage & Buffer::Usage::eTexel)
            vkUsage |= ::vk::BufferUsageFlagBits::eUniformTexelBuffer | ::vk::BufferUsageFlagBits::eStorageTexelBuffer;
        if (usage & Buffer::Usage::eShaderDeviceAddress)
            vkUsage |= ::vk::BufferUsageFlagBits::eShaderDeviceAddress;
        if (usage & Buffer::Usage::eAccelerationStructureInput)
            vkUsage |= ::vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | ::vk::BufferUsageFlagBits::eShaderDeviceAddress;
        return vkUsage;
    }

    inline ::vk::SampleCountFlagBits getVkSampleCount(const MSAA msaa)
    {
        switch (msaa) {
        case MSAA::eNone: return ::vk::SampleCountFlagBits::e1;
        case MSAA::e2x: return ::vk::SampleCountFlagBits::e2;
        case MSAA::e4x: return ::vk::SampleCountFlagBits::e4;
        case MSAA::e8x: return ::vk::SampleCountFlagBits::e8;
        case MSAA::e16x: return ::vk::SampleCountFlagBits::e16;
        default: throw std::runtime_error("Unknown MSAA level");
        }
    }

    inline ::vk::Filter getVkFilter(const kor::Filter filter)
    {
        switch (filter) {
        case kor::Filter::eNearest: return ::vk::Filter::eNearest;
        case kor::Filter::eLinear: return ::vk::Filter::eLinear;
        default: throw std::runtime_error("Unknown filter type");
        }
    }

    inline ::vk::SamplerMipmapMode getVkMipmapMode(const kor::Sampler::MipmapMode mode)
    {
        switch (mode) {
        case kor::Sampler::MipmapMode::eNearest: return ::vk::SamplerMipmapMode::eNearest;
        case kor::Sampler::MipmapMode::eLinear: return ::vk::SamplerMipmapMode::eLinear;
        default: throw std::runtime_error("Unknown mipmap mode");
        }
    }

    inline ::vk::SamplerAddressMode getVkSamplerAddressMode(const kor::Sampler::AddressMode mode)
    {
        switch (mode) {
        case kor::Sampler::AddressMode::eRepeat: return ::vk::SamplerAddressMode::eRepeat;
        case kor::Sampler::AddressMode::eMirroredRepeat: return ::vk::SamplerAddressMode::eMirroredRepeat;
        case kor::Sampler::AddressMode::eClampToEdge: return ::vk::SamplerAddressMode::eClampToEdge;
        case kor::Sampler::AddressMode::eClampToBorder: return ::vk::SamplerAddressMode::eClampToBorder;
        default: throw std::runtime_error("Unknown address mode");
        }
    }

    inline ::vk::CompareOp getVkCompareOp(const kor::CompareOp op)
    {
        switch (op) {
        case kor::CompareOp::eNever: return ::vk::CompareOp::eNever;
        case kor::CompareOp::eLess: return ::vk::CompareOp::eLess;
        case kor::CompareOp::eEqual: return ::vk::CompareOp::eEqual;
        case kor::CompareOp::eLessOrEqual: return ::vk::CompareOp::eLessOrEqual;
        case kor::CompareOp::eGreater: return ::vk::CompareOp::eGreater;
        case kor::CompareOp::eNotEqual: return ::vk::CompareOp::eNotEqual;
        case kor::CompareOp::eGreaterOrEqual: return ::vk::CompareOp::eGreaterOrEqual;
        case kor::CompareOp::eAlways: return ::vk::CompareOp::eAlways;
        default: throw std::runtime_error("Unknown compare operation");
        }
    }

    inline ::vk::StencilOp getVkStencilOp(const kor::StencilOp op)
    {
        switch (op) {
        case kor::StencilOp::eKeep: return ::vk::StencilOp::eKeep;
        case kor::StencilOp::eZero: return ::vk::StencilOp::eZero;
        case kor::StencilOp::eReplace: return ::vk::StencilOp::eReplace;
        case kor::StencilOp::eIncrementAndClamp: return ::vk::StencilOp::eIncrementAndClamp;
        case kor::StencilOp::eDecrementAndClamp: return ::vk::StencilOp::eDecrementAndClamp;
        case kor::StencilOp::eInvert: return ::vk::StencilOp::eInvert;
        case kor::StencilOp::eIncrementAndWrap: return ::vk::StencilOp::eIncrementAndWrap;
        case kor::StencilOp::eDecrementAndWrap: return ::vk::StencilOp::eDecrementAndWrap;
        default: throw std::runtime_error("Unknown stencil operation");
        }
    }

    inline ::vk::StencilFaceFlags getVkStencilFace(const kor::StencilFace face)
    {
        switch (face) {
        case kor::StencilFace::eFront: return ::vk::StencilFaceFlagBits::eFront;
        case kor::StencilFace::eBack: return ::vk::StencilFaceFlagBits::eBack;
        case kor::StencilFace::eFrontAndBack: return ::vk::StencilFaceFlagBits::eFrontAndBack;
        default: throw std::runtime_error("Unknown stencil face");
        }
    }

    inline ::vk::SamplerMipmapMode getVkSamplerMipmapMode(const kor::Sampler::MipmapMode mode)
    {
        switch (mode) {
        case kor::Sampler::MipmapMode::eNearest: return ::vk::SamplerMipmapMode::eNearest;
        case kor::Sampler::MipmapMode::eLinear: return ::vk::SamplerMipmapMode::eLinear;
        default: throw std::runtime_error("Unknown mipmap mode");
        }
    }

    inline ::vk::LogicOp getVkLogicOp(const kor::LogicOp op)
    {
        switch (op) {
        case kor::LogicOp::eClear: return ::vk::LogicOp::eClear;
        case kor::LogicOp::eAnd: return ::vk::LogicOp::eAnd;
        case kor::LogicOp::eAndReverse: return ::vk::LogicOp::eAndReverse;
        case kor::LogicOp::eCopy: return ::vk::LogicOp::eCopy;
        case kor::LogicOp::eAndInverted: return ::vk::LogicOp::eAndInverted;
        case kor::LogicOp::eNoOp: return ::vk::LogicOp::eNoOp;
        case kor::LogicOp::eXor: return ::vk::LogicOp::eXor;
        case kor::LogicOp::eOr: return ::vk::LogicOp::eOr;
        case kor::LogicOp::eNor: return ::vk::LogicOp::eNor;
        case kor::LogicOp::eEquivalent: return ::vk::LogicOp::eEquivalent;
        case kor::LogicOp::eInvert: return ::vk::LogicOp::eInvert;
        case kor::LogicOp::eOrReverse: return ::vk::LogicOp::eOrReverse;
        case kor::LogicOp::eCopyInverted: return ::vk::LogicOp::eCopyInverted;
        case kor::LogicOp::eOrInverted: return ::vk::LogicOp::eOrInverted;
        case kor::LogicOp::eNand: return ::vk::LogicOp::eNand;
        case kor::LogicOp::eSet: return ::vk::LogicOp::eSet;
        default: throw std::runtime_error("Unknown logic operation");
        }
    }

    inline ::vk::BlendFactor getVkBlendFactor(const kor::BlendFactor factor)
    {
        switch (factor) {
        case kor::BlendFactor::eZero: return ::vk::BlendFactor::eZero;
        case kor::BlendFactor::eOne: return ::vk::BlendFactor::eOne;
        case kor::BlendFactor::eSrcColor: return ::vk::BlendFactor::eSrcColor;
        case kor::BlendFactor::eOneMinusSrcColor: return ::vk::BlendFactor::eOneMinusSrcColor;
        case kor::BlendFactor::eDstColor: return ::vk::BlendFactor::eDstColor;
        case kor::BlendFactor::eOneMinusDstColor: return ::vk::BlendFactor::eOneMinusDstColor;
        case kor::BlendFactor::eSrcAlpha: return ::vk::BlendFactor::eSrcAlpha;
        case kor::BlendFactor::eOneMinusSrcAlpha: return ::vk::BlendFactor::eOneMinusSrcAlpha;
        case kor::BlendFactor::eDstAlpha: return ::vk::BlendFactor::eDstAlpha;
        case kor::BlendFactor::eOneMinusDstAlpha: return ::vk::BlendFactor::eOneMinusDstAlpha;
        case kor::BlendFactor::eConstantColor: return ::vk::BlendFactor::eConstantColor;
        case kor::BlendFactor::eOneMinusConstantColor: return ::vk::BlendFactor::eOneMinusConstantColor;
        case kor::BlendFactor::eSrcAlphaSaturate: return ::vk::BlendFactor::eSrcAlphaSaturate;
        default: throw std::runtime_error("Unknown blend factor");
        }
    }

    inline ::vk::BlendOp getVkBlendOp(const kor::BlendOp op)
    {
        switch (op) {
        case kor::BlendOp::eAdd: return ::vk::BlendOp::eAdd;
        case kor::BlendOp::eSubtract: return ::vk::BlendOp::eSubtract;
        case kor::BlendOp::eReverseSubtract: return ::vk::BlendOp::eReverseSubtract;
        case kor::BlendOp::eMin: return ::vk::BlendOp::eMin;
        case kor::BlendOp::eMax: return ::vk::BlendOp::eMax;
        default: throw std::runtime_error("Unknown blend operation");
        }
    }

    inline ::vk::ColorComponentFlags getVkColorComponentFlags(const Flags<kor::ColorComponent> flags)
    {
        auto vkFlags = ::vk::ColorComponentFlags();
        if (flags & kor::ColorComponent::eR) vkFlags |= ::vk::ColorComponentFlagBits::eR;
        if (flags & kor::ColorComponent::eG) vkFlags |= ::vk::ColorComponentFlagBits::eG;
        if (flags & kor::ColorComponent::eB) vkFlags |= ::vk::ColorComponentFlagBits::eB;
        if (flags & kor::ColorComponent::eA) vkFlags |= ::vk::ColorComponentFlagBits::eA;
        return vkFlags;
    }

    inline ::vk::ClearValue getVkClearValue(const kor::ClearColor& clearValue)
    {
        return std::visit([](auto&& value) -> ::vk::ClearValue {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, float>)
                return ::vk::ClearColorValue{ std::array<float,4>{ value, 0.f, 0.f, 0.f } };
            else if constexpr (std::is_same_v<T, glm::vec2>)
                return ::vk::ClearColorValue{ std::array<float,4>{ value.x, value.y, 0.f, 0.f } };
            else if constexpr (std::is_same_v<T, glm::vec3>)
                return ::vk::ClearColorValue{ std::array<float,4>{ value.x, value.y, value.z, 0.f } };
            else if constexpr (std::is_same_v<T, glm::vec4>)
                return ::vk::ClearColorValue{ std::array<float,4>{ value.x, value.y, value.z, value.w } };
            else if constexpr (std::is_same_v<T, glm::i32>)
                return ::vk::ClearColorValue{ std::array<int32_t,4>{ value, 0, 0, 0 } };
            else if constexpr (std::is_same_v<T, glm::ivec2>)
                return ::vk::ClearColorValue{ std::array<int32_t,4>{ value.x, value.y, 0, 0 } };
            else if constexpr (std::is_same_v<T, glm::ivec3>)
                return ::vk::ClearColorValue{ std::array<int32_t,4>{ value.x, value.y, value.z, 0 } };
            else if constexpr (std::is_same_v<T, glm::ivec4>)
                return ::vk::ClearColorValue{ std::array<int32_t,4>{ value.x, value.y, value.z, value.w } };
            else if constexpr (std::is_same_v<T, glm::u32>)
                return ::vk::ClearColorValue{ std::array<uint32_t,4>{ value, 0u, 0u, 0u } };
            else if constexpr (std::is_same_v<T, glm::uvec2>)
                return ::vk::ClearColorValue{ std::array<uint32_t,4>{ value.x, value.y, 0u, 0u } };
            else if constexpr (std::is_same_v<T, glm::uvec3>)
                return ::vk::ClearColorValue{ std::array<uint32_t,4>{ value.x, value.y, value.z, 0u } };
            else if constexpr (std::is_same_v<T, glm::uvec4>)
                return ::vk::ClearColorValue{ std::array<uint32_t,4>{ value.x, value.y, value.z, value.w } };
            else
                return ::vk::ClearColorValue{ std::array<float,4>{ 0.f, 0.f, 0.f, 0.f } };
        }, clearValue);
    }

}


