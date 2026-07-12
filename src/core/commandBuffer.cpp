//
// Created by radue on 2/21/2026.
//

#include <commandBuffer.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/commandBuffer.h"
#include "../backends/vulkan/commandBuffer.h"

#include "image.h"
#include "mesh.h"
#include "graphicsPipeline.h"
#include "../../include/log.h"
#include "../backends/vulkan/device.h"
#include "../backends/vulkan/vulkanContext.h"
#include "../../include/window.h"

namespace kor
{
    ::vk::QueueFlags getQueueFlagsFromUsage(const Flags<CommandBuffer::Usage> usage)
    {
        ::vk::QueueFlags queueFlags;
        if (usage & CommandBuffer::Usage::eCompute)
            queueFlags |= ::vk::QueueFlagBits::eCompute;
        if (usage & CommandBuffer::Usage::eGraphics)
            queueFlags |= ::vk::QueueFlagBits::eGraphics;
        if (usage & CommandBuffer::Usage::eTransfer)
            queueFlags |= ::vk::QueueFlagBits::eTransfer;
        return queueFlags;
    }

    BufferBarrier::BufferBarrier(
        const kor::ResourceRef<const kor::Buffer> &buffer, const ResourceAccess dstAccess,
        const glm::u64 offset, const glm::u64 size)
      : _buffer(buffer), _dstAccess(dstAccess),
        _offset(offset), _size(size) {}

    ImageBarrier::ImageBarrier(
        const kor::ResourceRef<const kor::Image> &image,
        const ResourceAccess dstAccess,
        const std::optional<glm::u32> baseMipLevel, const std::optional<glm::u32> levelCount,
        const std::optional<glm::u32> baseArrayLayer, const std::optional<glm::u32> layerCount)
        : _image(image), _dstAccess(dstAccess),
          _baseMipLevel(baseMipLevel), _levelCount(levelCount),
          _baseArrayLayer(baseArrayLayer), _layerCount(layerCount) {}


    CommandBuffer& CommandBuffer::record(const ErrorCode code, std::string message)
    {
        return record(Error{ .code = code, .message = std::move(message) });
    }

    CommandBuffer& CommandBuffer::record(Error error)
    {
        // history() rather than toString(): when a command fails because a resource is unusable,
        // the line the user needs is the root cause (the shader that would not compile), not the
        // symptom (the pipeline that could not be bound).
        kor::log::error("[command] {}", error.history());
        _errors.push_back(std::move(error));
        _failed = true;
        return *this;
    }

    VoidResult CommandBuffer::result() const
    {
        if (_errors.empty()) return {};
        return std::unexpected(_errors.front());
    }

    CommandBuffer & CommandBuffer::BeginRendering(RenderParameters renderParameters) {
        _state.boundFramebuffer = Context::Window().getFramebuffer();
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
        _state.viewportSet = false;
        _state.scissorSet = false;
        return *this;
    }

    void CommandBuffer::stateBeginRendering(const kor::ResourceRef<const Framebuffer>& framebuffer)
    {
        _state.boundFramebuffer = framebuffer;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
        _state.viewportSet = false;
        _state.scissorSet = false;
    }

    CommandBuffer& CommandBuffer::BeginRendering(kor::ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters)
    {
        if (_failed) return *this;
        if (reject(framebuffer, "framebuffer")) return *this;

        return doBeginRendering(framebuffer, renderParameters);
    }

    CommandBuffer& CommandBuffer::EndRendering()
    {
        _state.boundFramebuffer = std::nullopt;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
        return *this;
    }

    CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot set the viewport without a graphics pipeline bound.");
        _state.viewportSet = true;
        return *this;
    }

    CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot set the scissor without a graphics pipeline bound.");
        _state.scissorSet = true;
        return *this;
    }

    // ---- Dynamic state --------------------------------------------------
    // The base implementations only validate and record intent (which state the
    // caller has established); backends override to emit the GPU command and then
    // chain up to these to mark the tracking bit.
#define KORAL_DYNAMIC_STATE_SETTER_GUARD(bit, name)                                            \
        if (!_state.boundGraphicsPipeline.has_value())                                       \
            return record(ErrorCode::eNoGraphicsPipelineBound,                               \
                "Cannot set " name " without a graphics pipeline bound.");                   \
        _state.dynamicStateSet |= DynamicState::bit;

    CommandBuffer& CommandBuffer::SetLineWidth(float)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eLineWidth, "line width") return *this; }

    CommandBuffer& CommandBuffer::SetDepthBias(float, float, float)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthBias, "depth bias") return *this; }

    CommandBuffer& CommandBuffer::SetBlendConstants(glm::vec4)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eBlendConstants, "blend constants") return *this; }

    CommandBuffer& CommandBuffer::SetStencilCompareMask(StencilFace, glm::u32)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilCompareMask, "stencil compare mask") return *this; }

    CommandBuffer& CommandBuffer::SetStencilWriteMask(StencilFace, glm::u32)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilWriteMask, "stencil write mask") return *this; }

    CommandBuffer& CommandBuffer::SetStencilReference(StencilFace, glm::u32)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilReference, "stencil reference") return *this; }

    CommandBuffer& CommandBuffer::SetCullMode(Flags<CullMode>)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eCullMode, "cull mode") return *this; }

    CommandBuffer& CommandBuffer::SetFrontFace(FrontFace)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eFrontFace, "front face") return *this; }

    CommandBuffer& CommandBuffer::SetDepthTestEnable(bool)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthTestEnable, "depth test enable") return *this; }

    CommandBuffer& CommandBuffer::SetDepthWriteEnable(bool)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthWriteEnable, "depth write enable") return *this; }

    CommandBuffer& CommandBuffer::SetDepthCompareOp(CompareOp)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthCompareOp, "depth compare op") return *this; }

    CommandBuffer& CommandBuffer::SetStencilTestEnable(bool)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilTestEnable, "stencil test enable") return *this; }

    CommandBuffer& CommandBuffer::SetStencilOp(StencilFace, StencilOp, StencilOp, StencilOp, CompareOp)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilOp, "stencil op") return *this; }

    CommandBuffer& CommandBuffer::SetDepthBiasEnable(bool)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthBiasEnable, "depth bias enable") return *this; }

    CommandBuffer& CommandBuffer::SetRasterizerDiscardEnable(bool)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eRasterizerDiscardEnable, "rasterizer discard enable") return *this; }

    CommandBuffer& CommandBuffer::SetPrimitiveRestartEnable(bool)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(ePrimitiveRestartEnable, "primitive restart enable") return *this; }

#undef KORAL_DYNAMIC_STATE_SETTER_GUARD

    void CommandBuffer::applyDynamicDefaults()
    {
        if (!_state.boundGraphicsPipeline.has_value()) return;
        const auto& pipeline = *_state.boundGraphicsPipeline.value();
        const RasterizationState& rs = pipeline.getRasterizationState();
        const DepthStencilState&  ds = pipeline.getDepthStencilState();
        const ColorBlendState&    cb = pipeline.getColorBlendState();
        const InputAssemblyState& ia = pipeline.getInputAssemblyState();

        // Snapshot the mask so setters marking their own bit don't affect sibling
        // decisions (e.g. the front/back pair below).
        const Flags<DynamicState> set = _state.dynamicStateSet;

        if (!(set & DynamicState::eLineWidth))
            SetLineWidth(rs.lineWidth);
        if (!(set & DynamicState::eDepthBias))
            SetDepthBias(rs.depthBiasConstantFactor, rs.depthBiasClamp, rs.depthBiasSlopeFactor);
        if (!(set & DynamicState::eBlendConstants))
            SetBlendConstants({ cb.blendConstants[0], cb.blendConstants[1], cb.blendConstants[2], cb.blendConstants[3] });
        if (!(set & DynamicState::eStencilCompareMask)) {
            SetStencilCompareMask(StencilFace::eFront, ds.stencilFront.compareMask);
            SetStencilCompareMask(StencilFace::eBack,  ds.stencilBack.compareMask);
        }
        if (!(set & DynamicState::eStencilWriteMask)) {
            SetStencilWriteMask(StencilFace::eFront, ds.stencilFront.writeMask);
            SetStencilWriteMask(StencilFace::eBack,  ds.stencilBack.writeMask);
        }
        if (!(set & DynamicState::eStencilReference)) {
            SetStencilReference(StencilFace::eFront, ds.stencilFront.reference);
            SetStencilReference(StencilFace::eBack,  ds.stencilBack.reference);
        }
        if (!(set & DynamicState::eCullMode))
            SetCullMode(rs.cullMode);
        if (!(set & DynamicState::eFrontFace))
            SetFrontFace(rs.frontFace);
        if (!(set & DynamicState::eDepthTestEnable))
            SetDepthTestEnable(ds.depthTestEnable);
        if (!(set & DynamicState::eDepthWriteEnable))
            SetDepthWriteEnable(ds.depthWriteEnable);
        if (!(set & DynamicState::eDepthCompareOp))
            SetDepthCompareOp(ds.depthCompareOp);
        if (!(set & DynamicState::eStencilTestEnable))
            SetStencilTestEnable(ds.stencilEnable);
        if (!(set & DynamicState::eStencilOp)) {
            SetStencilOp(StencilFace::eFront, ds.stencilFront.failOp, ds.stencilFront.passOp, ds.stencilFront.depthFailOp, ds.stencilFront.compareOp);
            SetStencilOp(StencilFace::eBack,  ds.stencilBack.failOp,  ds.stencilBack.passOp,  ds.stencilBack.depthFailOp,  ds.stencilBack.compareOp);
        }
        if (!(set & DynamicState::eDepthBiasEnable))
            SetDepthBiasEnable(rs.depthBiasEnable);
        if (!(set & DynamicState::eRasterizerDiscardEnable))
            SetRasterizerDiscardEnable(rs.rasterizerDiscardEnable);
        if (!(set & DynamicState::ePrimitiveRestartEnable))
            SetPrimitiveRestartEnable(ia.primitiveRestartEnable);
    }

    void CommandBuffer::stateBindComputePipeline(const kor::ResourceRef<const ComputePipeline>& pipeline)
    {
        // get(), not &*: comparing identity must not dereference.
        if (!_state.boundComputePipeline.has_value() || _state.boundComputePipeline->get() != pipeline.get())
            _state.boundComputeDescriptorSets.clear();
        _state.boundComputePipeline = pipeline;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
    }

    CommandBuffer& CommandBuffer::BindComputePipeline(kor::ResourceRef<const ComputePipeline> pipeline)
    {
        if (_failed) return *this;
        if (reject(pipeline, "compute pipeline")) return *this;

        return doBindComputePipeline(pipeline);
    }

    void CommandBuffer::stateBindGraphicsPipeline(const kor::ResourceRef<const GraphicsPipeline>& pipeline)
    {
        // get(), not &*: comparing identity must not dereference.
        if (!_state.boundGraphicsPipeline.has_value() || _state.boundGraphicsPipeline->get() != pipeline.get())
            _state.boundGraphicsDescriptorSets.clear();
        _state.boundGraphicsPipeline = pipeline;
        _state.boundComputePipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
        // A new pipeline brings its own dynamic-state defaults; forget whatever the
        // previous pipeline established so the next draw re-applies them.
        _state.dynamicStateSet = Flags<DynamicState>{};
    }

    CommandBuffer& CommandBuffer::BindGraphicsPipeline(kor::ResourceRef<const GraphicsPipeline> pipeline)
    {
        if (_failed) return *this;
        if (reject(pipeline, "graphics pipeline")) return *this;

        return doBindGraphicsPipeline(pipeline);
    }

    void CommandBuffer::stateBindRayTracingPipeline(const kor::ResourceRef<const RayTracingPipeline>& pipeline)
    {
        // get(), not &*: comparing identity must not dereference.
        if (!_state.boundRayTracingPipeline.has_value() || _state.boundRayTracingPipeline->get() != pipeline.get())
            _state.boundRayTracingDescriptorSets.clear();
        _state.boundRayTracingPipeline = pipeline;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
    }

    CommandBuffer& CommandBuffer::BindRayTracingPipeline(kor::ResourceRef<const RayTracingPipeline> pipeline)
    {
        if (_failed) return *this;
        if (reject(pipeline, "ray tracing pipeline")) return *this;

        return doBindRayTracingPipeline(pipeline);
    }

    CommandBuffer& CommandBuffer::TraceRays(glm::u32 width, glm::u32 height, glm::u32 depth)
    {
        return record(ErrorCode::eRayTracingUnsupported, "Ray tracing is not supported on this backend.");
    }

    CommandBuffer& CommandBuffer::doBindRayTracingPipeline(kor::ResourceRef<const RayTracingPipeline>)
    {
        return record(ErrorCode::eRayTracingUnsupported, "Ray tracing is not supported on this backend.");
    }

    // Default no-ops: a backend without debug-marker support simply ignores labels.
    CommandBuffer& CommandBuffer::BeginDebugLabel(const std::string&, glm::vec4) { return *this; }
    CommandBuffer& CommandBuffer::EndDebugLabel() { return *this; }
    CommandBuffer& CommandBuffer::InsertDebugLabel(const std::string&, glm::vec4) { return *this; }

    void CommandBuffer::stateBindMesh(const kor::ResourceRef<const Mesh>& mesh)
    {
        _state.boundMesh = mesh;
    }

    CommandBuffer& CommandBuffer::BindMesh(kor::ResourceRef<const Mesh> mesh) {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot bind a mesh without a graphics pipeline bound.");
        if (reject(mesh, "mesh")) return *this;

        return doBindMesh(mesh);
    }

    CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        return *this;
    }

    CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (!_state.boundMesh.has_value())
            return record(ErrorCode::eNoMeshBound, "Cannot draw indexed without a mesh bound.");
        if (!_state.boundMesh.value()->hasIndexBuffer())
            return record(ErrorCode::eMeshHasNoIndexBuffer, "Cannot draw indexed: the bound mesh has no index buffer.");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        return *this;
    }

    CommandBuffer & CommandBuffer::DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ) {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw mesh tasks without a graphics pipeline bound.");
        if (!_state.viewportSet)
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        if (!_state.scissorSet)
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        return *this;
    }


    CommandBuffer& CommandBuffer::GenerateMipmaps(kor::ResourceRef<const Image> image)
    {
        if (_failed) return *this;
        if (reject(image, "image")) return *this;
        return doGenerateMipmaps(image);
    }

    // Default: blit each mip from the one above it. Vulkan uses this; GL overrides it with
    // glGenerateMipmap. Reached only through the wrapper above, so `image` is always usable.
    CommandBuffer& CommandBuffer::doGenerateMipmaps(ResourceRef<const Image> image) {
        const auto& extent = image->getExtent();
        const auto mipLevels = image->getMipLevels();
        const auto arrayLayers = image->getArrayLayers();

        auto mipWidth = static_cast<glm::i32>(extent.x);
        auto mipHeight = static_cast<glm::i32>(extent.y);
        auto mipDepth = static_cast<glm::i32>(extent.z);

        for (uint32_t i = 1; i < mipLevels; i++) {
            Blit(
                image, image,
                kor::Blit {
                    .srcOffset = { 0, 0, 0 },
                    .srcExtent = { mipWidth, mipHeight, mipDepth },
                    .dstOffset = { 0, 0, 0 },
                    .dstExtent = { std::max(mipWidth / 2, 1), std::max(mipHeight / 2, 1), std::max(mipDepth / 2, 1) },
                    .srcBaseArrayLayer = 0,
                    .dstBaseArrayLayer = 0,
                    .layerCount = arrayLayers,
                    .srcMipLevel = i - 1,
                    .dstMipLevel = i,
                    .filtering = Filter::eLinear
                });

            if (mipWidth > 1) {
                mipWidth /= 2;
            }
            if (mipHeight > 1) {
                mipHeight /= 2;
            }
        }
        return *this;
    }

    void CommandBuffer::SingleTimeCommand(const std::function<void(kor::CommandBuffer &)> &command, const Usage usage) {
        const auto commandBuffer = Create(usage);
        commandBuffer->Begin();
        command(*commandBuffer);
        commandBuffer->End();
        if (auto submitted = commandBuffer->Submit(); !submitted) {
            kor::log::error("[command] single-time command failed: {}", submitted.error().toString());
        }
        commandBuffer->WaitForFence();
    }

    CommandBuffer& CommandBuffer::DrawMesh(kor::ResourceRef<const Mesh> mesh, const glm::u32 instanceCount, const glm::u32 baseInstance)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw a mesh without a graphics pipeline bound.");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        BindMesh(mesh);
        DrawIndexed(UINT64_MAX, instanceCount, 0, 0, baseInstance);
        return *this;
    }

    CommandBuffer & CommandBuffer::DrawSubMesh(kor::ResourceRef<const Mesh> mesh, glm::u32 baseIndex, glm::u32 indexCount) {
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw a mesh without a graphics pipeline bound.");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        BindMesh(mesh);
        DrawIndexed(indexCount, 1, baseIndex, 0, 0);
        return *this;
    }

    std::unique_ptr<CommandBuffer> CommandBuffer::Create(const Flags<Usage> usage)
    {
        switch (Context::activeAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::CommandBuffer>(usage);
        case API::eVulkan:
            {
                const auto& queue = vk::Context::Device().requestQueue(getQueueFlagsFromUsage(usage));
                return vk::Context::Device().requestCommandBuffer(queue, std::hash<std::thread::id>{}(std::this_thread::get_id()));
            }
        default:
            throw std::runtime_error("Unknown API");
        }
    }

    // ---- Non-virtual interface: the gate ---------------------------------
    // Each of these validates, refuses unusable resources, and only then reaches the backend.
    // The do* implementations below them may assume every resource they receive is alive and
    // usable — which is what the dynamic_cast at every backend deref has always assumed.

    CommandBuffer& CommandBuffer::BindDescriptorSet(const glm::u32 index, kor::ResourceRef<const DescriptorSet> descriptorSet, const bool debug)
    {
        if (_failed) return *this;
        if (reject(descriptorSet, "descriptor set")) return *this;
        return doBindDescriptorSet(index, descriptorSet, debug);
    }

    CommandBuffer& CommandBuffer::Barrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers)
    {
        if (_failed) return *this;
        for (const auto& barrier : bufferBarriers)
            if (reject(barrier.getBuffer(), "barrier's buffer")) return *this;
        for (const auto& barrier : imageBarriers)
            if (reject(barrier.getImage(), "barrier's image")) return *this;
        return doBarrier(std::move(bufferBarriers), std::move(imageBarriers));
    }

    CommandBuffer& CommandBuffer::DispatchIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset)
    {
        if (_failed) return *this;
        if (!_state.boundComputePipeline.has_value())
            return record(ErrorCode::eNoComputePipelineBound, "Cannot dispatch without a compute pipeline bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;
        return doDispatchIndirect(indirectBuffer, offset);
    }

    CommandBuffer& CommandBuffer::DrawIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        return doDrawIndirect(indirectBuffer, offset, drawCount, stride);
    }

    CommandBuffer& CommandBuffer::DrawIndexedIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (!_state.boundMesh.has_value())
            return record(ErrorCode::eNoMeshBound, "Cannot draw indexed without a mesh bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        return doDrawIndexedIndirect(indirectBuffer, offset, drawCount, stride);
    }

    CommandBuffer& CommandBuffer::DrawMeshTasksIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        return doDrawMeshTasksIndirect(indirectBuffer, offset, drawCount, stride);
    }

    CommandBuffer& CommandBuffer::ClearBuffer(kor::ResourceRef<const Buffer> buffer, const glm::u64 offset, const glm::u64 size)
    {
        if (_failed) return *this;
        if (reject(buffer, "buffer")) return *this;
        return doClearBuffer(buffer, offset, size);
    }

    CommandBuffer& CommandBuffer::ClearColorImage(kor::ResourceRef<const Image> image, const glm::vec4 color)
    {
        if (_failed) return *this;
        if (reject(image, "image")) return *this;
        return doClearColorImage(image, color);
    }

    CommandBuffer& CommandBuffer::FillBuffer(kor::ResourceRef<const Buffer> buffer, void* data, const glm::u64 offset, const glm::u64 size)
    {
        if (_failed) return *this;
        if (reject(buffer, "buffer")) return *this;
        return doFillBuffer(buffer, data, offset, size);
    }

    CommandBuffer& CommandBuffer::CopyBuffer(kor::ResourceRef<const Buffer> srcBuffer, kor::ResourceRef<const Buffer> dstBuffer, const glm::u64 size, const glm::u64 srcOffset, const glm::u64 dstOffset)
    {
        if (_failed) return *this;
        if (reject(srcBuffer, "copy source buffer")) return *this;
        if (reject(dstBuffer, "copy destination buffer")) return *this;
        return doCopyBuffer(srcBuffer, dstBuffer, size, srcOffset, dstOffset);
    }

    CommandBuffer& CommandBuffer::CopyBufferToImage(kor::ResourceRef<const Buffer> buffer, kor::ResourceRef<const Image> image, const kor::Copy copyInfo)
    {
        if (_failed) return *this;
        if (reject(buffer, "copy source buffer")) return *this;
        if (reject(image, "copy destination image")) return *this;
        return doCopyBufferToImage(buffer, image, copyInfo);
    }

    CommandBuffer& CommandBuffer::CopyImageToBuffer(kor::ResourceRef<const Image> image, kor::ResourceRef<const Buffer> buffer, const kor::Copy copyInfo)
    {
        if (_failed) return *this;
        if (reject(image, "copy source image")) return *this;
        if (reject(buffer, "copy destination buffer")) return *this;
        return doCopyImageToBuffer(image, buffer, copyInfo);
    }

    CommandBuffer& CommandBuffer::Blit(kor::ResourceRef<const Image> srcImage, const kor::Blit blitInfo)
    {
        if (_failed) return *this;
        if (reject(srcImage, "blit source image")) return *this;
        return doBlit(srcImage, blitInfo);
    }

    CommandBuffer& CommandBuffer::Blit(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, const kor::Blit blitInfo)
    {
        if (_failed) return *this;
        if (reject(srcImage, "blit source image")) return *this;
        if (reject(dstImage, "blit destination image")) return *this;
        return doBlit(srcImage, dstImage, blitInfo);
    }

    CommandBuffer& CommandBuffer::Resolve(kor::ResourceRef<const Image> srcImage, const kor::Resolve resolveInfo)
    {
        if (_failed) return *this;
        if (reject(srcImage, "resolve source image")) return *this;
        return doResolve(srcImage, resolveInfo);
    }

    CommandBuffer& CommandBuffer::Resolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, const kor::Resolve resolveInfo)
    {
        if (_failed) return *this;
        if (reject(srcImage, "resolve source image")) return *this;
        if (reject(dstImage, "resolve destination image")) return *this;
        return doResolve(srcImage, dstImage, resolveInfo);
    }

}
