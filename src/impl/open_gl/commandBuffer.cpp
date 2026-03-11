//
// Created by radue on 2/21/2026.
//

#include "commandBuffer.h"

#include <ranges>
#include <glm/gtc/type_ptr.inl>

#include "buffer.h"
#include "computePipeline.h"
#include "graphicsPipeline.h"
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
        if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Rendering operation is still in progress!");

        _recording = false;
        _filled = true;
    }

    gfx::CommandBuffer& CommandBuffer::BeginRendering(const gfx::Framebuffer* framebuffer)
    {
        if (framebuffer == nullptr) {
            framebuffer = &Context::DefaultFramebuffer();
        }

        CheckRecording();
        _commands.emplace_back([this, framebuffer] ()
        {
            if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Another rendering operation is still in progress!");
            auto oglFramebuffer = dynamic_cast<const ogl::Framebuffer*>(framebuffer);
            _state.boundFramebuffer = oglFramebuffer;
            _state.boundComputePipeline = std::nullopt;
            _state.boundGraphicsPipeline = std::nullopt;
            framebuffer->Bind();

            if (**oglFramebuffer == 0) {
                glClearNamedFramebufferfv(**oglFramebuffer, GL_COLOR, 0, glm::value_ptr(oglFramebuffer->getClearColor(0)));
            } else {
                for (const auto& [i, attachment] : _state.boundFramebuffer.value()->getColorAttachments() | std::views::enumerate)
                {
                    glClearNamedFramebufferfv(**oglFramebuffer, GL_COLOR, i, glm::value_ptr(oglFramebuffer->getClearColor(i)));
                    glCheckError();
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
            if (!_state.boundFramebuffer.has_value()) throw std::runtime_error("There is no rendering operation in progress!");
            _state.boundFramebuffer.value()->Unbind();
            if (_state.boundGraphicsPipeline.has_value()) {
                _state.boundGraphicsPipeline.value()->Unbind();
                _state.boundComputePipeline = std::nullopt;
            }
            _state.boundFramebuffer = std::nullopt;
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
