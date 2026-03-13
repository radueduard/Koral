//
// Created by radue on 2/21/2026.
//

#include "commandBuffer.h"

#include <ranges>
#include <glm/gtc/type_ptr.inl>

#include "buffer.h"
#include "computePipeline.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "core/mesh.h"
#include "utils/ogl_err_handling.h"

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

    gfx::CommandBuffer& CommandBuffer::Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance)
    {
        CheckRecording();
        _commands.emplace_back([this, vertexCount, instanceCount, firstVertex, firstInstance] ()
        {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            const auto& oglPipeline = dynamic_cast<const gfx::ogl::GraphicsPipeline*>(_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline->getMode();
            glDrawArraysInstancedBaseInstance(mode, firstVertex, vertexCount, instanceCount, firstInstance);
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

    gfx::CommandBuffer& CommandBuffer::DrawMesh(const gfx::Mesh* mesh, const glm::u32 instanceCount, const glm::u32 baseInstance)
    {
        CheckRecording();
        _commands.emplace_back([this, mesh, instanceCount, baseInstance] ()
        {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw mesh without a graphics pipeline!");
            const auto& oglPipeline = dynamic_cast<const gfx::ogl::GraphicsPipeline*>(_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline->getMode();

            auto meshVertexBindingDescription = _state.boundGraphicsPipeline.value()->getVertexBindingDescriptions().value();
            auto meshVertexAttributeDescription = _state.boundGraphicsPipeline.value()->getVertexAttributeDescriptions().value();

            const auto vertexBuffers = mesh->getVertexBuffers();
            for (size_t i = 0; i < vertexBuffers.size(); ++i)
            {
                const auto& vertexBuffer = dynamic_cast<const ogl::Buffer&>(vertexBuffers[i].get());
                glBindBuffer(GL_ARRAY_BUFFER, *vertexBuffer);
                glCheckError();

                const auto& [binding, stride] = meshVertexBindingDescription[i];
                for (const auto& attributeDescription : meshVertexAttributeDescription)
                {
                    if (attributeDescription.binding == binding)
                    {
                        glEnableVertexAttribArray(attributeDescription.location);
                        glCheckError();
                        glVertexAttribPointer(
                            attributeDescription.location,
                            attributeDescription.channelCount,
                            toGLChannelType(attributeDescription.channelType),
                            GL_FALSE,
                            stride,
                            nullptr);
                    }
                }
            }
            auto indexBuffer = mesh->getIndexBuffer();
            if (indexBuffer.has_value()) {
                const auto& oglIndexBuffer = dynamic_cast<const ogl::Buffer&>(indexBuffer.value().get());
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *oglIndexBuffer);
                glCheckError();
            }

            if (mesh->hasIndexBuffer()) {
                const auto indexType = mesh->getIndexType().value();
                glDrawElementsInstancedBaseInstance(
                    GL_TRIANGLES,
                    mesh->getIndexCount().value(),
                    toGLChannelType(indexType),
                    nullptr,
                    instanceCount,
                    baseInstance);
            } else {
                glDrawArraysInstancedBaseInstance(
                    mode,
                    0,
                    mesh->getVertexCount(),
                    instanceCount,
                    baseInstance);
            }

            glBindVertexArray(0);
        });
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Blit(const gfx::Image* srcImage, const gfx::Image* dstImage)
    {
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
                    0, 0, glSrcImage->getExtent().x, glSrcImage->getExtent().y,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
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
            });
        }
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
}
