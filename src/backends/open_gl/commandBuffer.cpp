//
// Created by radue on 2/21/2026.
//

#include "commandBuffer.h"

#include <descriptorSet.h>
#include <mesh.h>

#include <ranges>
#include <glm/gtc/type_ptr.inl>

#include "buffer.h"
#include "computePipeline.h"
#include "context.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "ogl_err_handling.h"
#include "window.h"
#include "surface.h"

namespace gfx::ogl
{
    void CommandBuffer::CheckRecording() const
    {
        if (!_recording)
        {
            throw std::runtime_error("You can't submit commands while not recording!");
        }
    }

    CommandBuffer::CommandBuffer(const Flags<Usage> usage): gfx::CommandBuffer(usage) {}

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

    gfx::CommandBuffer& CommandBuffer::BeginRendering(const gfx::Framebuffer* _framebuffer)
    {
        CheckRecording();
        _commands.emplace_back([_framebuffer, this] ()
        {
            if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Another rendering operation is still in progress!");
            gfx::CommandBuffer::BeginRendering(_framebuffer);
            const auto framebuffer = _state.boundFramebuffer.value();
            auto oglFramebuffer = dynamic_cast<const ogl::Framebuffer*>(framebuffer);
            _state.boundFramebuffer = oglFramebuffer;
            _state.boundComputePipeline = std::nullopt;
            _state.boundGraphicsPipeline = std::nullopt;
            framebuffer->Bind();

            if (**oglFramebuffer == 0) {
                glClearNamedFramebufferfv(**oglFramebuffer, GL_COLOR, 0, glm::value_ptr(oglFramebuffer->getClearColor(0)));
            } else {
                int i = 0;
                for (const auto& attachment : _state.boundFramebuffer.value()->getColorAttachments())
                {
                    glClearNamedFramebufferfv(**oglFramebuffer, GL_COLOR, i, glm::value_ptr(oglFramebuffer->getClearColor(i)));
                    glCheckError();
                    i++;
                }
            }
            if (framebuffer->hasDepthStencilAttachment())
            {
                glClearNamedFramebufferfi(**oglFramebuffer,  GL_DEPTH_STENCIL, 0, oglFramebuffer->getClearDepth(), oglFramebuffer->getClearStencil());
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

    gfx::CommandBuffer& CommandBuffer::BindPipeline(const gfx::ComputePipeline* pipeline)
    {
        CheckRecording();
        _commands.emplace_back([this, pipeline] ()
        {
            _state.boundComputePipeline = dynamic_cast<const ogl::ComputePipeline*>(pipeline);
            _state.boundGraphicsPipeline = std::nullopt;
            pipeline->Bind(*this);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(const gfx::GraphicsPipeline* pipeline)
    {
        CheckRecording();
        _commands.emplace_back([this, pipeline] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't use a graphics pipeline without a framebuffer!");
            _state.boundGraphicsPipeline = dynamic_cast<const ogl::GraphicsPipeline*>(pipeline);
            _state.boundComputePipeline = std::nullopt;
            pipeline->Bind(*this);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindDescriptorSet(glm::u32 index, const DescriptorSet* set)
    {
        CheckRecording();
        _commands.emplace_back([set, index, this] ()
        {
            set->bind(*this, index);
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::BindMesh(const Mesh *mesh) {
        CheckRecording();
        _commands.emplace_back([mesh, this] () {
            gfx::CommandBuffer::BindMesh(mesh);
            const auto meshVertexBindingDescription = _state.boundGraphicsPipeline.value()->getVertexBindingDescriptions().value();
            const auto meshVertexAttributeDescription = _state.boundGraphicsPipeline.value()->getVertexAttributeDescriptions().value();

            const auto vertexBuffers = mesh->getVertexBuffers();
            for (size_t i = 0; i < vertexBuffers.size(); ++i)
            {
                const auto& vertexBuffer = dynamic_cast<const ogl::Buffer&>(vertexBuffers[i].get());
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
                const auto& oglIndexBuffer = dynamic_cast<const ogl::Buffer&>(indexBuffer.value().get());
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *oglIndexBuffer);
                glCheckError();
            }
        });
        return *this;
    }

    std::optional<GLbitfield> GetBarrierBits(ResourceAccess src, ResourceAccess dst) {
        // Only incoherent writes need a barrier in OpenGL
        bool srcIsIncoherentWrite =
            src == ResourceAccess::ComputeWrite        ||
            src == ResourceAccess::ComputeReadWrite     ||
            src == ResourceAccess::VertexShaderWrite    ||
            src == ResourceAccess::VertexShaderReadWrite||
            src == ResourceAccess::FragmentShaderWrite  ||
            src == ResourceAccess::FragmentShaderReadWrite;

        if (!srcIsIncoherentWrite)
            return std::nullopt;

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
                GetBarrierBits(barrier.getSrcAccess(), barrier.getDstAccess()).and_then([&](GLbitfield bits) {
                    glMemoryBarrier(bits);
                    glCheckError();
                    return std::optional(bits);
                });
            }
            for (const auto& barrier : imageBarriers) {
                GetBarrierBits(barrier.getSrcAccess(), barrier.getDstAccess()).and_then([&](GLbitfield bits) {
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

    gfx::CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance)
    {
        CheckRecording();
        _commands.emplace_back([this, vertexCount, instanceCount, firstVertex, firstInstance] ()
        {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline*>(_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline->getMode();
            glDrawArraysInstancedBaseInstance(mode, firstVertex, vertexCount, instanceCount, firstInstance);
        });
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) {
        CheckRecording();
        _commands.emplace_back([this, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance] () {
            gfx::CommandBuffer::DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline*>(_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline->getMode();
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

    gfx::CommandBuffer& CommandBuffer::Blit(const gfx::Image* srcImage, const gfx::Image* dstImage, gfx::Blit blitInfo)
    {
        if (srcImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Source image must not be multisampled for blit operation!");
        if (dstImage && dstImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Destination image must not be multisampled for blit operation!");

        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = dstImage ? dstImage->getExtent() : glm::uvec3 { Context::Window().getExtent(), 1 };

        CheckRecording();

        if (dstImage == nullptr)
        {
            _commands.emplace_back([srcImage] ()
            {
                const auto* glSrcImage = dynamic_cast<const ogl::Image*>(srcImage);
                GLuint framebuffer = 0;
                glGenFramebuffers(1, &framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                glCheckError();

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, **glSrcImage, 0);
                glCheckError();

                glBlitNamedFramebuffer(
                    framebuffer,
                    0,
                    0, 0, glSrcImage->getExtent().x, glSrcImage->getExtent().y,
                    0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
                glCheckError();

                glDeleteFramebuffers(1, &framebuffer);
                glCheckError();
            });
        } else
        {
            _commands.emplace_back([srcImage, dstImage] ()
            {
                const auto* glSrcImage = dynamic_cast<const ogl::Image*>(srcImage);
                const auto* glDstImage = dynamic_cast<const ogl::Image*>(dstImage);
                GLuint srcFramebuffer = 0;
                glGenFramebuffers(1, &srcFramebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, srcFramebuffer);
                glCheckError();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, **glSrcImage, 0);
                glCheckError();

                GLuint dstFramebuffer = 0;
                glGenFramebuffers(1, &dstFramebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, dstFramebuffer);
                glCheckError();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, **glDstImage, 0);
                glCheckError();

                glBlitNamedFramebuffer(
                    srcFramebuffer,
                    dstFramebuffer,
                    0, 0, glSrcImage->getExtent().x, glSrcImage->getExtent().y,
                    0, 0, glDstImage->getExtent().x, glDstImage->getExtent().y,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
                glCheckError();

                glDeleteFramebuffers(1, &srcFramebuffer);
                glDeleteFramebuffers(1, &dstFramebuffer);
                glCheckError();
            });
        }
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::Resolve(const gfx::Image *srcImage, const gfx::Image *dstImage) {
        if (srcImage->getMSAA() == MSAA::eNone)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");
        if (dstImage && dstImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Destination image must not be multisampled for resolve operation!");

        CheckRecording();
        if (dstImage == nullptr)
        {
            _commands.emplace_back([srcImage] ()
            {
                const auto* glSrcImage = dynamic_cast<const ogl::Image*>(srcImage);
                GLuint framebuffer = 0;
                glGenFramebuffers(1, &framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
                glCheckError();

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, **glSrcImage, 0);
                glCheckError();

                glBlitNamedFramebuffer(
                    framebuffer,
                    0,
                    0, 0, glSrcImage->getExtent().x, glSrcImage->getExtent().y,
                    0, 0, gfx::Context::Window().getExtent().x, gfx::Context::Window().getExtent().y,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
                glCheckError();

                glDeleteFramebuffers(1, &framebuffer);
                glCheckError();
            });
        } else
        {
            _commands.emplace_back([srcImage, dstImage] ()
            {
                const auto* glSrcImage = dynamic_cast<const ogl::Image*>(srcImage);
                const auto* glDstImage = dynamic_cast<const ogl::Image*>(dstImage);

                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glCheckError();
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, **glSrcImage, 0);
                glCheckError();

                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glCheckError();
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, **glDstImage, 0);
                glCheckError();

                glBlitFramebuffer(
                    0, 0, glSrcImage->getExtent().x, glSrcImage->getExtent().y,
                    0, 0, glDstImage->getExtent().x, glDstImage->getExtent().y,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
                glCheckError();
            });
        }
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
            const auto& oglPipeline = dynamic_cast<const gfx::ogl::ComputePipeline*>(_state.boundComputePipeline.value());
            return oglPipeline->getSetAndBindingToBindingPointMap();
        }
        if (_state.boundGraphicsPipeline.has_value()) {
            const auto& oglPipeline = dynamic_cast<const gfx::ogl::GraphicsPipeline*>(_state.boundGraphicsPipeline.value());
            return oglPipeline->getSetAndBindingToBindingPointMap();
        }
        throw std::runtime_error("No pipeline is currently bound!");
    }

    gfx::CommandBuffer & CommandBuffer::PushConstants(const void *data, glm::u32 size, glm::u32 offset) {
        std::cerr << "CommandBuffer::PushConstants NOT YET IMPLEMENTED!" << std::endl;
        return *this;
    }
}
