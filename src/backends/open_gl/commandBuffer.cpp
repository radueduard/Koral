//
// Created by radue on 2/21/2026.
//
module;

#include <glm/gtc/type_ptr.inl>
#include "ogl_err_handling.h"

module gfx;
import :ogl_commandBuffer;
import :ogl_framebuffer;
import :ogl_graphicsPipeline;
import :ogl_image;

import std;
import flags;

namespace gfx::ogl
{
    void CommandBuffer::CheckRecording() const
    {
        if (!_recording)
        {
            throw std::runtime_error("You can't submit commands while not recording!");
        }
    }

    CommandBuffer::CommandBuffer(const Flags<Usage> usage)
        : gfx::CommandBuffer(usage) {}

    gfx::CommandBuffer& CommandBuffer::Begin()
    {
        if (_filled) throw std::runtime_error("CommandBuffer has already been recorded! You must reset it first!");
        _recording = true;

        return *this;
    }

    void CommandBuffer::End()
    {
        if (!_recording) throw std::runtime_error("CommandBuffer is not currently recording!");

        _recording = false;
        _filled = true;
    }

    gfx::CommandBuffer & CommandBuffer::BeginRendering(RenderParameters renderParameters) {
        CheckRecording();
        _commands.emplace_back([this, renderParameters] () {
            if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Another rendering operation is still in progress!");
            gfx::CommandBuffer::BeginRendering(renderParameters);
            const auto framebuffer = _state.boundFramebuffer.value();
            const auto& oglFramebuffer = dynamic_cast<const ogl::Framebuffer&>(*framebuffer);
            framebuffer->Bind();

            // if (renderParameters.colorLoadOperation == LoadOperation::eClear)
                // glClearNamedFramebufferfv(*oglFramebuffer, GL_COLOR, 0, glm::value_ptr(oglFramebuffer.getClearColor(0)));
            // glCheckError();
            // if (renderParameters.depthLoadOperation == LoadOperation::eClear || renderParameters.stencilLoadOperation == LoadOperation::eClear)
                // glClearNamedFramebufferfi(*oglFramebuffer,  GL_DEPTH_STENCIL, 0, oglFramebuffer.getClearDepth(), oglFramebuffer.getClearStencil());
            // glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BeginRendering(gfx::ResourceRef<const gfx::Framebuffer> _framebuffer, RenderParameters renderParameters)
    {
        CheckRecording();
        _commands.emplace_back([_framebuffer, this, renderParameters] ()
        {
            if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Another rendering operation is still in progress!");
            gfx::CommandBuffer::BeginRendering(_framebuffer, renderParameters);
            const auto framebuffer = _state.boundFramebuffer.value();
            const auto& oglFramebuffer = dynamic_cast<const ogl::Framebuffer&>(*framebuffer);
            framebuffer->Bind();

            int i = 0;
            for (const auto& attachment : _state.boundFramebuffer.value()->getColorAttachments())
            {
                if (renderParameters.colorLoadOperation == LoadOperation::eClear) {
                    // glClearNamedFramebufferfv(*oglFramebuffer, GL_COLOR, i, glm::value_ptr(oglFramebuffer.getClearColor(i)));
                    // glCheckError();
                }
                i++;
            }
            if (framebuffer->hasDepthAttachment() && renderParameters.depthLoadOperation == LoadOperation::eClear)
            {
                glClearNamedFramebufferfi(*oglFramebuffer,  GL_DEPTH, 0, oglFramebuffer.getClearDepth(), oglFramebuffer.getClearStencil());
                glCheckError();
            }
                if (framebuffer->hasStencilAttachment() && renderParameters.stencilLoadOperation == LoadOperation::eClear)
                {
                    glClearNamedFramebufferfi(*oglFramebuffer,  GL_STENCIL, 0, oglFramebuffer.getClearDepth(), oglFramebuffer.getClearStencil());
                    glCheckError();
                }
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::EndRendering()
    {
        CheckRecording();
        _commands.emplace_back([this] ()
        {
            gfx::CommandBuffer::EndRendering();
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(gfx::ResourceRef<const gfx::ComputePipeline> pipeline)
    {
        CheckRecording();
        _commands.emplace_back([this, pipeline] ()
        {
            _state.boundComputePipeline = pipeline;
            _state.boundGraphicsPipeline = std::nullopt;
            pipeline->Bind(*this);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(gfx::ResourceRef<const gfx::GraphicsPipeline> pipeline)
    {
        CheckRecording();
        _commands.emplace_back([this, pipeline] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't use a graphics pipeline without a framebuffer!");
            _state.boundGraphicsPipeline = pipeline;
            _state.boundComputePipeline = std::nullopt;
            pipeline->Bind(*this);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindDescriptorSet(glm::u32 index, gfx::ResourceRef<const DescriptorSet> set, bool debug)
    {
        if (debug) set->DebugPrint();

        CheckRecording();
        _commands.emplace_back([set, index, this] ()
        {
            set->bind(*this, index);
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::BindMesh(gfx::ResourceRef<const Mesh> mesh) {
        CheckRecording();
        _commands.emplace_back([mesh, this] () {
            gfx::CommandBuffer::BindMesh(mesh);
            const auto meshVertexBindingDescription = _state.boundGraphicsPipeline.value()->getVertexBindingDescriptions().value();
            const auto meshVertexAttributeDescription = _state.boundGraphicsPipeline.value()->getVertexAttributeDescriptions().value();

            const auto vertexBuffers = mesh->getVertexBuffers();
            for (size_t i = 0; i < vertexBuffers.size(); ++i)
            {
                const auto& vertexBuffer = dynamic_cast<const ogl::Buffer&>(*vertexBuffers[i]);
                glBindBuffer(GL_ARRAY_BUFFER, *vertexBuffer);
                glCheckError();

                const auto& [binding, stride] = meshVertexBindingDescription[i];
                for (const auto& [location, attributeBinding, channelCount, channelType, offset] : meshVertexAttributeDescription)
                {
                    if (binding == attributeBinding)
                    {
                        glEnableVertexAttribArray(location);
                        glCheckError();
                        glVertexAttribPointer(
                            location,
                            static_cast<GLint>(channelCount),
                            toGLChannelType(channelType),
                            GL_FALSE,
                            static_cast<GLsizei>(stride),
                            reinterpret_cast<void*>(static_cast<ptrdiff_t>(offset)));
                        glCheckError();
                    }
                }
            }
            if (const auto indexBuffer = mesh->getIndexBuffer(); indexBuffer.has_value()) {
                const auto& oglIndexBuffer = dynamic_cast<const ogl::Buffer&>(*indexBuffer.value());
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *oglIndexBuffer);
                glCheckError();
            }
        });
        return *this;
    }

    std::optional<GLbitfield> GetBarrierBits(ResourceAccess dst) {
        switch (dst) {
            case ResourceAccess::VertexBuffer:
                return GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;

            case ResourceAccess::IndexBuffer:
                return GL_ELEMENT_ARRAY_BARRIER_BIT;

            case ResourceAccess::IndirectBuffer:
                return GL_COMMAND_BARRIER_BIT;

            case ResourceAccess::ComputeRead:
            case ResourceAccess::VertexShaderRead:
            case ResourceAccess::FragmentShaderRead:
                return GL_SHADER_STORAGE_BARRIER_BIT;

            case ResourceAccess::ComputeReadWrite:
            case ResourceAccess::VertexShaderReadWrite:
            case ResourceAccess::FragmentShaderReadWrite:
                return GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

            case ResourceAccess::TransferSrc:
            case ResourceAccess::TransferDst:
                return GL_TEXTURE_UPDATE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT;

            case ResourceAccess::ColorAttachment:
            case ResourceAccess::DepthAttachment:
            case ResourceAccess::DepthRead:
                return GL_FRAMEBUFFER_BARRIER_BIT;

            default:
                return std::nullopt;
        }
    }

    gfx::CommandBuffer & CommandBuffer::Barrier(std::vector<gfx::BufferBarrier> bufferBarriers,
        std::vector<gfx::ImageBarrier> imageBarriers) {
        CheckRecording();
        _commands.emplace_back([bufferBarriers = std::move(bufferBarriers), imageBarriers = std::move(imageBarriers)] () {
            for (const auto& barrier : bufferBarriers) {
                GetBarrierBits(barrier.getDstAccess()).and_then([&](GLbitfield bits) {
                    glMemoryBarrier(bits);
                    glCheckError();
                    return std::optional(bits);
                });
            }
            for (const auto& barrier : imageBarriers) {
                GetBarrierBits(barrier.getDstAccess()).and_then([&](GLbitfield bits) {
                    glMemoryBarrier(bits);
                    glCheckError();
                    return std::optional(bits);
                });
            }
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ)
    {
        CheckRecording();
        _commands.emplace_back([this, groupCountX, groupCountY, groupCountZ] () {
            if (!_state.boundComputePipeline.has_value())
                throw std::runtime_error("You can't dispatch without a compute pipeline bound!");
            glDispatchCompute(groupCountX, groupCountY, groupCountZ);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DispatchIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) {
        CheckRecording();
        _commands.emplace_back([this, indirectBuffer, offset] () {
            gfx::CommandBuffer::DispatchIndirect(indirectBuffer, offset);
            const auto& oglIndirectBuffer = dynamic_cast<const ogl::Buffer&>(*indirectBuffer);
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, *oglIndirectBuffer);
            glCheckError();
            glDispatchComputeIndirect(offset);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance)
    {
        CheckRecording();
        _commands.emplace_back([this, vertexCount, instanceCount, firstVertex, firstInstance] ()
        {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline.getMode();
            glDrawArraysInstancedBaseInstance(mode, firstVertex, vertexCount, instanceCount, firstInstance);
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) {
        CheckRecording();
        _commands.emplace_back([this, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance] () {
            gfx::CommandBuffer::DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline.getMode();
            const auto mesh = _state.boundMesh.value();
            const auto indexType = mesh->getIndexType().value();
            glDrawElementsInstancedBaseInstance(
                mode,
                mesh->getIndexCount().value(),
                toGLChannelType(indexType),
                nullptr,
                instanceCount,
                firstInstance);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ) {
        CheckRecording();
        _commands.emplace_back([this, taskCountX, taskCountY, taskCountZ] () {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw mesh tasks without a graphics pipeline!");
            glDrawMeshTasksEXT(taskCountX, taskCountY, taskCountZ);
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        CheckRecording();
        _commands.emplace_back([this, indirectBuffer, offset, drawCount, stride] {
            throw std::runtime_error("Indirect drawing is not supported in OpenGL backend yet!");
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndexedIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        CheckRecording();
        _commands.emplace_back([this, indirectBuffer, offset, drawCount, stride] {
            throw std::runtime_error("Indirect drawing is not supported in OpenGL backend yet!");
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawMeshTasksIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        CheckRecording();
        _commands.emplace_back([this, indirectBuffer, offset, drawCount, stride] {
            throw std::runtime_error("Indirect drawing is not supported in OpenGL backend yet!");
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        CheckRecording();
        _commands.emplace_back([this, x, y, width, height] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't set viewport without a framebuffer!");
            glViewport(x, y, width, height);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        CheckRecording();
        _commands.emplace_back([this, x, y, width, height] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't set scissor without a framebuffer!");
            glScissor(x, y, width, height);
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::Blit(ResourceRef<const gfx::Image> srcImage, gfx::Blit blitInfo) {

        if (srcImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Source image must not be multisampled for blit operation!");

        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = glm::uvec3 { Context::GetWindow().getExtent(), 1 };

        CheckRecording();

        _commands.emplace_back([srcImage, blitInfo] ()
        {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            GLuint framebuffer = 0;
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glCheckError();

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glSrcImage, 0);
            glCheckError();

            glBlitNamedFramebuffer(
                framebuffer,
                0,
                blitInfo.srcOffset.x, blitInfo.srcOffset.y, blitInfo.srcOffset.x + blitInfo.srcExtent.x, blitInfo.srcOffset.y + blitInfo.srcExtent.y,
                blitInfo.dstOffset.x, blitInfo.dstOffset.y, blitInfo.dstOffset.x + blitInfo.dstExtent.x, blitInfo.dstOffset.y + blitInfo.dstExtent.y,
                GL_COLOR_BUFFER_BIT,
                blitInfo.filtering == gfx::Filter::eNearest ? GL_NEAREST : GL_LINEAR);
            glCheckError();

            glDeleteFramebuffers(1, &framebuffer);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Blit(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Blit blitInfo)
    {
        if (srcImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Source image must not be multisampled for blit operation!");
        if (dstImage && dstImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Destination image must not be multisampled for blit operation!");

        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = dstImage->getExtent();

        CheckRecording();
        _commands.emplace_back([srcImage, dstImage, blitInfo] {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            const auto& glDstImage = dynamic_cast<const ogl::Image&>(*dstImage);
            GLuint srcFramebuffer = 0;
            glGenFramebuffers(1, &srcFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, srcFramebuffer);
            glCheckError();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glSrcImage, 0);
            glCheckError();

            GLuint dstFramebuffer = 0;
            glGenFramebuffers(1, &dstFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, dstFramebuffer);
            glCheckError();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glDstImage, 0);
            glCheckError();

            glBlitNamedFramebuffer(
                srcFramebuffer,
                dstFramebuffer,
                blitInfo.srcOffset.x, blitInfo.srcOffset.y, blitInfo.srcOffset.x + blitInfo.srcExtent.x, blitInfo.srcOffset.y + blitInfo.srcExtent.y,
                blitInfo.dstOffset.x, blitInfo.dstOffset.y, blitInfo.dstOffset.x + blitInfo.dstExtent.x, blitInfo.dstOffset.y + blitInfo.dstExtent.y,
                GL_COLOR_BUFFER_BIT,
                blitInfo.filtering == gfx::Filter::eNearest ? GL_NEAREST : GL_LINEAR);
            glCheckError();

            glDeleteFramebuffers(1, &srcFramebuffer);
            glDeleteFramebuffers(1, &dstFramebuffer);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::Resolve(ResourceRef<const gfx::Image> srcImage, gfx::Resolve resolveInfo) {
        if (srcImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");

        if (resolveInfo.srcExtent == glm::ivec3(-1))
            resolveInfo.srcExtent = srcImage->getExtent();
        if (resolveInfo.dstExtent == glm::ivec3(-1))
            resolveInfo.dstExtent = glm::uvec3 { Context::GetWindow().getExtent(), 1 };

        CheckRecording();
        _commands.emplace_back([srcImage, resolveInfo] {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            GLuint framebuffer = 0;
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glCheckError();

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glSrcImage, 0);
            glCheckError();

            glBlitNamedFramebuffer(
                framebuffer,
                0,
                resolveInfo.srcOffset.x, resolveInfo.srcOffset.y, resolveInfo.srcOffset.x + resolveInfo.srcExtent.x, resolveInfo.srcOffset.y + resolveInfo.srcExtent.y,
                resolveInfo.dstOffset.x, resolveInfo.dstOffset.y, resolveInfo.dstOffset.x + resolveInfo.dstExtent.x, resolveInfo.dstOffset.y + resolveInfo.dstExtent.y,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            glCheckError();

            glDeleteFramebuffers(1, &framebuffer);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::Resolve(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Resolve resolveInfo) {
        if (srcImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");
        if (dstImage && dstImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Destination image must not be multisampled for resolve operation!");

        if (resolveInfo.srcExtent == glm::ivec3(-1))
            resolveInfo.srcExtent = srcImage->getExtent();
        if (resolveInfo.dstExtent == glm::ivec3(-1))
            resolveInfo.dstExtent = dstImage->getExtent();

        CheckRecording();
        _commands.emplace_back([srcImage, dstImage, resolveInfo] ()
        {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            const auto& glDstImage = dynamic_cast<const ogl::Image&>(*dstImage);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glCheckError();
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glSrcImage, 0);
            glCheckError();

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glCheckError();
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glDstImage, 0);
            glCheckError();

            glBlitFramebuffer(
                resolveInfo.srcOffset.x, resolveInfo.srcOffset.y, resolveInfo.srcOffset.x + resolveInfo.srcExtent.x, resolveInfo.srcOffset.y + resolveInfo.srcExtent.y,
                resolveInfo.dstOffset.x, resolveInfo.dstOffset.y, resolveInfo.dstOffset.x + resolveInfo.dstExtent.x, resolveInfo.dstOffset.y + resolveInfo.dstExtent.y,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::ClearBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) {
        CheckRecording();
        _commands.emplace_back([buffer, offset, size] () {
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*buffer);
            auto actualSize = size;
            if (size == UINT64_MAX) {
                actualSize = oglBuffer.getSize() - offset;
            }
            glNamedBufferSubData(*oglBuffer, offset, actualSize, nullptr);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::FillBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) {
        CheckRecording();
        _commands.emplace_back([buffer, data, offset, size] () {
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*buffer);
            auto actualSize = size;
            if (size == UINT64_MAX) {
                actualSize = oglBuffer.getSize() - offset;
            }
            glNamedBufferSubData(*oglBuffer, offset, actualSize, data);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::CopyBuffer(ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) {
        CheckRecording();
        _commands.emplace_back([srcBuffer, dstBuffer, size, srcOffset, dstOffset] () {
            const auto& oglSrcBuffer = dynamic_cast<const ogl::Buffer&>(*srcBuffer);
            const auto& oglDstBuffer = dynamic_cast<const ogl::Buffer&>(*dstBuffer);
            auto actualSize = size;
            if (size == UINT64_MAX) {
                actualSize = std::min(oglSrcBuffer.getSize() - srcOffset, oglDstBuffer.getSize() - dstOffset);
            }
            glCopyNamedBufferSubData(*oglSrcBuffer, *oglDstBuffer, srcOffset, dstOffset, actualSize);
            glCheckError();
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Run(const std::function<void(gfx::CommandBuffer&)>& command)
    {
        CheckRecording();
        _commands.emplace_back([command, this] ()
        {
            command(*this);
        });
        return *this;
    }

    // TODO!
    gfx::CommandBuffer & CommandBuffer::CopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) {
        CheckRecording();
        _commands.emplace_back([buffer, image, copyInfo] () {
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*buffer);
            const auto& oglImage = dynamic_cast<const ogl::Image&>(*image);

            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, *oglBuffer);
            glCheckError();

            glBindTexture(GL_TEXTURE_2D, *oglImage);
            glCheckError();

            // glTexSubImage2D(
            //     GL_TEXTURE_2D,
            //     copyInfo.imageMipLevel,
            //     copyInfo.imageOffset.x, copyInfo.imageOffset.y,
            //     copyInfo.imageExtent.x, copyInfo.imageExtent.y,
            //     toGLChannelFormat(oglImage.getFormat()),
            //     toGLChannelType(oglImage.getFormat()),
            //     nullptr
            // );
            glCheckError();

            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    // TODO!
    gfx::CommandBuffer & CommandBuffer::CopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) {
        CheckRecording();
        _commands.emplace_back([image, buffer, copyInfo] () {
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*buffer);
            const auto& oglImage = dynamic_cast<const ogl::Image&>(*image);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, *oglBuffer);
            glCheckError();

            glBindTexture(GL_TEXTURE_2D, *oglImage);
            glCheckError();

            // glGetTexImage(
            //     GL_TEXTURE_2D,
            //     copyInfo.imageMipLevel,
            //     toGLChannelFormat(oglImage.getFormat()),
            //     toGLChannelType(oglImage.getFormat()),
            //     nullptr
            // );
            glCheckError();

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    void CommandBuffer::Submit()
    {
        if (!_filled)
            throw std::runtime_error("You can't submit a command buffer that has not been recorded yet!");
        if (_submitted)
            throw std::runtime_error("This command buffer has already been submitted. You should probably reset it!");

        for (const auto& command : _commands)
        {
            command();
        }
        _submitted = true;
    }

    void CommandBuffer::Reset()
    {
        _filled = false;
        _submitted = false;
        _commands.clear();
    }

    const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& CommandBuffer::getRemappingTableForBoundPipeline() const
    {
        if (_state.boundComputePipeline.has_value()) {
            const auto& oglPipeline = dynamic_cast<const gfx::ogl::ComputePipeline&>(*_state.boundComputePipeline.value());
            return oglPipeline.getSetAndBindingToBindingPointMap();
        }
        if (_state.boundGraphicsPipeline.has_value()) {
            const auto& oglPipeline = dynamic_cast<const gfx::ogl::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            return oglPipeline.getSetAndBindingToBindingPointMap();
        }
        throw std::runtime_error("No pipeline is currently bound!");
    }

    gfx::CommandBuffer & CommandBuffer::PushConstants(const void *data, glm::u32 size, glm::u32 offset) {
        std::cerr << "CommandBuffer::PushConstants NOT YET IMPLEMENTED!" << std::endl;
        return *this;
    }
}
