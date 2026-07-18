//
// Created by radue on 2/21/2026.
//

#include "commandBuffer.h"

#include <descriptorSet.h>
#include <mesh.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <string_view>
#include <variant>
#include <glm/gtc/type_ptr.inl>
#include <magic_enum/magic_enum.hpp>

#include "imageView.h"

#include "buffer.h"
#include "computePipeline.h"
#include "context.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "image.h"
#include "ogl_err_handling.h"
#include "window.h"
#include "surface.h"

namespace kor::ogl
{
    // Defined in graphicsPipeline.cpp (same namespace); reused here for dynamic depth state.
    GLenum toGLOperator(CompareOp op);
    GLenum toGLStencilOp(StencilOp op);

    // A load-op clear covers the whole attachment, but GL's glClear* honour the scissor
    // test and the color/depth/stencil write masks. Reset them so a prior draw's state
    // can't restrict the clear; the next pipeline bind / draw re-establishes real state.
    static void resetStateForClear() {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFFu);
    }

    // Clear one color attachment with the function matching its data type. GL rejects
    // glClearNamedFramebufferfv on an integer attachment (leaving e.g. an r32ui
    // visibility buffer uncleared, so its sentinel is never written); integer
    // attachments need the ui/i variants. Vulkan derives this from the format
    // automatically. The Koral clear value carries the right variant type, but the
    // authoritative signal is the attachment format.
    static void clearColorBuffer(GLuint fbo, GLint drawBuffer, kor::Image::Format format,
                                 const kor::ClearColor& clearColor) {
        const std::string_view name = magic_enum::enum_name(format);
        if (name.find("_UINT") != std::string_view::npos) {
            const glm::uvec4 v = std::visit([](auto&& c) -> glm::uvec4 {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, glm::u32>) return glm::uvec4(c, 0u, 0u, 0u);
                else if constexpr (std::is_same_v<T, glm::uvec2>) return glm::uvec4(c, 0u, 0u);
                else if constexpr (std::is_same_v<T, glm::uvec3>) return glm::uvec4(c, 0u);
                else if constexpr (std::is_same_v<T, glm::uvec4>) return c;
                else return glm::uvec4(0u);
            }, clearColor);
            glClearNamedFramebufferuiv(fbo, GL_COLOR, drawBuffer, &v[0]);
        } else if (name.find("_SINT") != std::string_view::npos) {
            const glm::ivec4 v = std::visit([](auto&& c) -> glm::ivec4 {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, glm::i32>) return glm::ivec4(c, 0, 0, 0);
                else if constexpr (std::is_same_v<T, glm::ivec2>) return glm::ivec4(c, 0, 0);
                else if constexpr (std::is_same_v<T, glm::ivec3>) return glm::ivec4(c, 0);
                else if constexpr (std::is_same_v<T, glm::ivec4>) return c;
                else return glm::ivec4(0);
            }, clearColor);
            glClearNamedFramebufferiv(fbo, GL_COLOR, drawBuffer, &v[0]);
        } else {
            const glm::vec4 v = std::visit([](auto&& c) -> glm::vec4 {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, glm::vec4>) return c;
                else if constexpr (std::is_same_v<T, glm::vec3>) return glm::vec4(c, 0.f);
                else if constexpr (std::is_same_v<T, glm::vec2>) return glm::vec4(c, 0.f, 0.f);
                else if constexpr (std::is_same_v<T, float>) return glm::vec4(c, 0.f, 0.f, 0.f);
                else return glm::vec4(0.f, 0.f, 0.f, 1.f);
            }, clearColor);
            glClearNamedFramebufferfv(fbo, GL_COLOR, drawBuffer, glm::value_ptr(v));
        }
        glCheckError();
    }

    void CommandBuffer::CheckRecording() const
    {
        // _executing: a command is being recorded from inside another command's replay
        // (e.g. a Run lambda calling Dispatch); that is legal — enqueue() runs it in
        // place rather than appending. Only reject recording on a genuinely idle buffer.
        if (!_recording && !_executing)
        {
            throw std::runtime_error("You can't submit commands while not recording!");
        }
    }

    CommandBuffer::CommandBuffer(const Flags<Usage> usage): kor::CommandBuffer(usage) {}

    kor::CommandBuffer& CommandBuffer::Begin()
    {
        if (_filled) throw std::runtime_error("CommandBuffer has already been recorded! You must reset it first!");
        resetErrors();
        _recording = true;

        return *this;
    }

    void CommandBuffer::End()
    {
        if (!_recording) throw std::runtime_error("CommandBuffer is not currently recording!");

        _recording = false;
        _filled = true;
    }

    kor::CommandBuffer& CommandBuffer::BeginDebugLabel(const std::string& label, glm::vec4)
    {
        CheckRecording();
        enqueue([label] () {
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(label.size()), label.c_str());
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::EndDebugLabel()
    {
        CheckRecording();
        enqueue([] () {
            glPopDebugGroup();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::InsertDebugLabel(const std::string& label, glm::vec4)
    {
        CheckRecording();
        enqueue([label] () {
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                                 GL_DEBUG_SEVERITY_NOTIFICATION, static_cast<GLsizei>(label.size()), label.c_str());
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::BeginRendering(RenderParameters renderParameters) {
        CheckRecording();
        enqueue([this, renderParameters] () {
            if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Another rendering operation is still in progress!");
            kor::CommandBuffer::BeginRendering(renderParameters);
            const auto framebuffer = _state.boundFramebuffer.value();
            const auto& oglFramebuffer = dynamic_cast<const ogl::Framebuffer&>(*framebuffer);
            framebuffer->Bind();
            resetStateForClear();

            if (renderParameters.colorLoadOperation == LoadOperation::eClear) {
                const auto& clearColor = oglFramebuffer.getClearColor(0);
                const glm::vec4 clearVec4 = std::visit([](auto&& v) -> glm::vec4 {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, glm::vec4>) return v;
                    else if constexpr (std::is_same_v<T, glm::vec3>) return glm::vec4(v, 0.f);
                    else if constexpr (std::is_same_v<T, glm::vec2>) return glm::vec4(v, 0.f, 0.f);
                    else if constexpr (std::is_same_v<T, float>) return glm::vec4(v, 0.f, 0.f, 0.f);
                    else return glm::vec4(0.f, 0.f, 0.f, 1.f);
                }, clearColor);
                glClearNamedFramebufferfv(*oglFramebuffer, GL_COLOR, 0, glm::value_ptr(clearVec4));
            }
            glCheckError();
            if (renderParameters.depthLoadOperation == LoadOperation::eClear || renderParameters.stencilLoadOperation == LoadOperation::eClear)
                glClearNamedFramebufferfi(*oglFramebuffer,  GL_DEPTH_STENCIL, 0, oglFramebuffer.getClearDepth(), oglFramebuffer.getClearStencil());
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBeginRendering(kor::ResourceRef<const kor::Framebuffer> _framebuffer, RenderParameters renderParameters)
    {
        CheckRecording();
        enqueue([_framebuffer, this, renderParameters] ()
        {
            if (_state.boundFramebuffer.has_value())  throw std::runtime_error("Another rendering operation is still in progress!");
            stateBeginRendering(_framebuffer);
            const auto framebuffer = _state.boundFramebuffer.value();
            const auto& oglFramebuffer = dynamic_cast<const ogl::Framebuffer&>(*framebuffer);
            framebuffer->Bind();
            resetStateForClear();

            int i = 0;
            for (const auto& attachment : _state.boundFramebuffer.value()->getColorAttachments())
            {
                if (renderParameters.colorLoadOperation == LoadOperation::eClear) {
                    // Clear with the function matching the attachment's data type — an
                    // integer attachment (e.g. the r32ui visibility buffer) must not be
                    // cleared as float or its sentinel never gets written.
                    const auto format = attachment.get().getImage()->getFormat();
                    clearColorBuffer(*oglFramebuffer, i, format, oglFramebuffer.getClearColor(i));
                }
                i++;
            }
            if (framebuffer->hasDepthStencilAttachment() && (renderParameters.depthLoadOperation == LoadOperation::eClear || renderParameters.stencilLoadOperation == LoadOperation::eClear))
            {
                glClearNamedFramebufferfi(*oglFramebuffer,  GL_DEPTH_STENCIL, 0, oglFramebuffer.getClearDepth(), oglFramebuffer.getClearStencil());
                glCheckError();
            }
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::EndRendering()
    {
        CheckRecording();
        enqueue([this] ()
        {
            kor::CommandBuffer::EndRendering();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline)
    {
        CheckRecording();
        // Mirror the bind into record-time state so the base convenience methods
        // (DrawMesh/DrawSubMesh, which validate as they are recorded) see it; the
        // replay lambda sets the same values again when it actually binds on the GPU.
        _state.boundComputePipeline = pipeline;
        _state.boundGraphicsPipeline = std::nullopt;
        enqueue([this, pipeline] ()
        {
            _state.boundComputePipeline = pipeline;
            _state.boundGraphicsPipeline = std::nullopt;
            pipeline->Bind(*this);
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline)
    {
        CheckRecording();
        _state.boundGraphicsPipeline = pipeline;
        _state.boundComputePipeline = std::nullopt;
        enqueue([this, pipeline] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't use a graphics pipeline without a framebuffer!");
            _state.boundGraphicsPipeline = pipeline;
            _state.boundComputePipeline = std::nullopt;
            pipeline->Bind(*this);
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBindDescriptorSet(glm::u32 index, kor::ResourceRef<const DescriptorSet> set, bool debug)
    {
        if (debug) set->DebugPrint();

        if (_state.boundComputePipeline.has_value())
            _state.boundComputeDescriptorSets.insert_or_assign(index, set);
        else if (_state.boundGraphicsPipeline.has_value())
            _state.boundGraphicsDescriptorSets.insert_or_assign(index, set);

        CheckRecording();
        enqueue([set, index, this] ()
        {
            set->bind(*this, index);
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doBindMesh(kor::ResourceRef<const Mesh> mesh) {
        CheckRecording();
        // Record-time mirror (see BindGraphicsPipeline).
        stateBindMesh(mesh);
        enqueue([mesh, this] () {
            stateBindMesh(mesh);
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

            // Reads may come through SSBOs, UBOs or sampled textures; cover all three.
            case ResourceAccess::ComputeRead:
            case ResourceAccess::VertexShaderRead:
            case ResourceAccess::FragmentShaderRead:
            case ResourceAccess::AllShaderRead:
                return GL_SHADER_STORAGE_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT;

            case ResourceAccess::ComputeWrite:
            case ResourceAccess::VertexShaderWrite:
            case ResourceAccess::FragmentShaderWrite:
            case ResourceAccess::AllShaderWrite:
            case ResourceAccess::ComputeReadWrite:
            case ResourceAccess::VertexShaderReadWrite:
            case ResourceAccess::FragmentShaderReadWrite:
            case ResourceAccess::AllShaderReadWrite:
                return GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

            case ResourceAccess::TransferSrc:
            case ResourceAccess::TransferDst:
                return GL_TEXTURE_UPDATE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT;

            case ResourceAccess::ColorAttachment:
            case ResourceAccess::DepthStencilAttachment:
            case ResourceAccess::DepthStencilRead:
            case ResourceAccess::DepthAttachment:
            case ResourceAccess::DepthRead:
            case ResourceAccess::StencilAttachment:
            case ResourceAccess::StencilRead:
                return GL_FRAMEBUFFER_BARRIER_BIT;

            // Presentation has no GL analogue (the swap is externally synchronized);
            // no memory barrier is required.
            case ResourceAccess::Present:
                return std::nullopt;

            default:
                return std::nullopt;
        }
    }

    kor::CommandBuffer & CommandBuffer::doBarrier(std::vector<kor::BufferBarrier> bufferBarriers,
        std::vector<kor::ImageBarrier> imageBarriers) {
        CheckRecording();
        enqueue([bufferBarriers = std::move(bufferBarriers), imageBarriers = std::move(imageBarriers)] () {
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

    kor::CommandBuffer& CommandBuffer::Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ)
    {
        CheckRecording();
        enqueue([this, groupCountX, groupCountY, groupCountZ] () {
            if (!_state.boundComputePipeline.has_value())
                throw std::runtime_error("You can't dispatch without a compute pipeline bound!");
            glDispatchCompute(groupCountX, groupCountY, groupCountZ);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, const glm::u64 offset)
    {
        CheckRecording();
        enqueue([this, indirectBuffer, offset] () {
            if (!_state.boundComputePipeline.has_value())
                throw std::runtime_error("You can't dispatch without a compute pipeline bound!");
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*indirectBuffer);
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, *oglBuffer);
            glDispatchComputeIndirect(static_cast<GLintptr>(offset));
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride)
    {
        CheckRecording();
        enqueue([this, indirectBuffer, offset, drawCount, stride] () {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            applyDefaultViewportScissor();
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*indirectBuffer);
            // kor::IndirectDrawCommand matches GL's DrawArraysIndirectCommand layout.
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, *oglBuffer);
            glMultiDrawArraysIndirect(
                oglPipeline.getMode(),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset)),
                static_cast<GLsizei>(drawCount),
                static_cast<GLsizei>(stride));
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride)
    {
        CheckRecording();
        enqueue([this, indirectBuffer, offset, drawCount, stride] () {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            if (!_state.boundMesh.has_value())
                throw std::runtime_error("Cannot draw indexed indirect without a mesh bound!");
            applyDefaultViewportScissor();
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*indirectBuffer);
            const auto indexType = _state.boundMesh.value()->getIndexType().value();
            // kor::IndirectDrawIndexedCommand matches GL's DrawElementsIndirectCommand layout.
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, *oglBuffer);
            if (std::getenv("KORAL_DUMP_INDIRECT")) {
                struct Cmd { GLuint count, instanceCount, firstIndex, baseVertex, baseInstance; };
                std::vector<Cmd> cmds(drawCount);
                glGetNamedBufferSubData(*oglBuffer, static_cast<GLintptr>(offset),
                                        static_cast<GLsizeiptr>(drawCount * sizeof(Cmd)), cmds.data());
                glm::u32 visible = 0, totalIdx = 0;
                for (const auto& c : cmds) { if (c.instanceCount) visible++; totalIdx += c.count; }
                static int frame = 0;
                if (frame++ % 60 == 0)
                    std::cerr << "[indirect] off=" << offset << " drawCount=" << drawCount
                              << " visible=" << visible << " sampleCount0=" << cmds[0].count
                              << " sampleInst0=" << cmds[0].instanceCount << std::endl;
            }
            glMultiDrawElementsIndirect(
                oglPipeline.getMode(),
                toGLChannelType(indexType),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset)),
                static_cast<GLsizei>(drawCount),
                static_cast<GLsizei>(stride));
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    // Extent of the currently bound framebuffer, falling back to the window for the
    // default framebuffer (whose GL object carries no explicit extent). Used to convert
    // the API's top-left viewport/scissor origin to GL's bottom-left.
    glm::uvec2 CommandBuffer::currentFramebufferExtent() const
    {
        glm::uvec2 extent{ 0, 0 };
        if (_state.boundFramebuffer.has_value())
            extent = _state.boundFramebuffer.value()->getExtent();
        if (extent.x == 0 || extent.y == 0)
            extent = Context::Window().getExtent();
        return extent;
    }

    // Apply the default full-framebuffer viewport/scissor that a draw expects when the
    // caller never set them explicitly. Runs at replay time, issuing GL directly (the
    // recording Set* overrides can't be called mid-replay). A full-framebuffer rect is
    // origin-agnostic, so no top-left/bottom-left conversion is needed here.
    void CommandBuffer::applyDefaultViewportScissor()
    {
        const glm::uvec2 extent = currentFramebufferExtent();
        if (!_state.viewportSet)
            glViewport(0, 0, static_cast<GLsizei>(extent.x), static_cast<GLsizei>(extent.y));
        if (!_state.scissorSet) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, static_cast<GLsizei>(extent.x), static_cast<GLsizei>(extent.y));
        }
        glCheckError();
    }

    kor::CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance)
    {
        CheckRecording();
        enqueue([this, vertexCount, instanceCount, firstVertex, firstInstance] () mutable
        {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            if (vertexCount == UINT64_MAX) {
                if (!_state.boundMesh.has_value())
                    throw std::runtime_error("Draw called with default vertex count but no mesh is bound!");
                vertexCount = _state.boundMesh.value()->getVertexCount();
            }
            applyDefaultViewportScissor();
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline.getMode();
            glDrawArraysInstancedBaseInstance(mode, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount), instanceCount, firstInstance);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) {
        CheckRecording();
        enqueue([this, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance] () mutable {
            if (!_state.boundGraphicsPipeline.has_value())
                throw std::runtime_error("You can't draw without a graphics pipeline!");
            if (!_state.boundMesh.has_value())
                throw std::runtime_error("Cannot draw indexed without a mesh bound!");
            applyDefaultViewportScissor();
            const auto& oglPipeline = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            const auto mode = oglPipeline.getMode();
            const auto mesh = _state.boundMesh.value();
            const auto indexType = mesh->getIndexType().value();
            if (indexCount == UINT64_MAX)
                indexCount = mesh->getIndexCount().value();
            // firstIndex is a texel offset into the index buffer; convert to a byte offset.
            const auto indexSize = indexType == ChannelType::eUShort ? 2 : 4;
            const auto byteOffset = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(firstIndex) * indexSize);
            glDrawElementsInstancedBaseVertexBaseInstance(
                mode,
                static_cast<GLsizei>(indexCount),
                toGLChannelType(indexType),
                byteOffset,
                instanceCount,
                vertexOffset,
                firstInstance);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        CheckRecording();
        enqueue([this, x, y, width, height] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't set viewport without a framebuffer!");
            _state.viewportSet = true;
            // Viewport rects are given in the canonical (Vulkan) top-left origin. glClipControl
            // does not move GL's window-space origin — it only negates NDC Y — so the rect
            // itself still has to be converted to GL's bottom-left. See Scheduler::Initialize.
            const glm::u32 glY = currentFramebufferExtent().y - y - height;
            glViewport(x, glY, width, height);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        CheckRecording();
        enqueue([this, x, y, width, height] ()
        {
            if (!_state.boundFramebuffer.has_value())
                throw std::runtime_error("You can't set scissor without a framebuffer!");
            _state.scissorSet = true;
            // Vulkan's scissor always clips; GL needs the scissor test explicitly on.
            // Top-left origin converted to GL's bottom-left, as for the viewport above.
            const glm::u32 glY = currentFramebufferExtent().y - y - height;
            glEnable(GL_SCISSOR_TEST);
            glScissor(x, glY, width, height);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetLineWidth(const float lineWidth)
    {
        CheckRecording();
        enqueue([lineWidth] { glLineWidth(lineWidth); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetDepthBias(const float constantFactor, float, const float slopeFactor)
    {
        // GL has no depth-bias clamp without the polygon-offset-clamp extension, so the
        // clamp argument is dropped here to match GraphicsPipeline::Bind.
        CheckRecording();
        enqueue([constantFactor, slopeFactor] { glPolygonOffset(slopeFactor, constantFactor); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetBlendConstants(const glm::vec4 constants)
    {
        CheckRecording();
        enqueue([constants] { glBlendColor(constants.r, constants.g, constants.b, constants.a); });
        return *this;
    }

    namespace {
        GLenum toGLStencilFace(const StencilFace face) {
            switch (face) {
            case StencilFace::eFront: return GL_FRONT;
            case StencilFace::eBack: return GL_BACK;
            case StencilFace::eFrontAndBack: return GL_FRONT_AND_BACK;
            default: throw std::runtime_error("Unknown stencil face!");
            }
        }
    }

    // GL fuses func/ref/mask into glStencilFuncSeparate, whereas the API (like Vulkan)
    // exposes them as three independent dynamic states. The command buffer therefore
    // keeps a per-face shadow copy and re-issues the fused call whenever any of the
    // three changes.
    void CommandBuffer::applyStencilFunc(const StencilFace face)
    {
        if (face == StencilFace::eFront || face == StencilFace::eFrontAndBack)
            glStencilFuncSeparate(GL_FRONT, _stencilShadow[0].compareOp, static_cast<GLint>(_stencilShadow[0].reference), _stencilShadow[0].compareMask);
        if (face == StencilFace::eBack || face == StencilFace::eFrontAndBack)
            glStencilFuncSeparate(GL_BACK, _stencilShadow[1].compareOp, static_cast<GLint>(_stencilShadow[1].reference), _stencilShadow[1].compareMask);
        glCheckError();
    }

    kor::CommandBuffer& CommandBuffer::SetStencilCompareMask(const StencilFace face, const glm::u32 compareMask)
    {
        CheckRecording();
        enqueue([this, face, compareMask] {
            if (face != StencilFace::eBack) _stencilShadow[0].compareMask = compareMask;
            if (face != StencilFace::eFront) _stencilShadow[1].compareMask = compareMask;
            applyStencilFunc(face);
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetStencilWriteMask(const StencilFace face, const glm::u32 writeMask)
    {
        CheckRecording();
        enqueue([face, writeMask] {
            glStencilMaskSeparate(toGLStencilFace(face), writeMask);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetStencilReference(const StencilFace face, const glm::u32 reference)
    {
        CheckRecording();
        enqueue([this, face, reference] {
            if (face != StencilFace::eBack) _stencilShadow[0].reference = reference;
            if (face != StencilFace::eFront) _stencilShadow[1].reference = reference;
            applyStencilFunc(face);
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetStencilTestEnable(const bool enable)
    {
        CheckRecording();
        enqueue([enable] {
            if (enable) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetStencilOp(const StencilFace face, const StencilOp failOp, const StencilOp passOp, const StencilOp depthFailOp, const CompareOp compareOp)
    {
        CheckRecording();
        enqueue([this, face, failOp, passOp, depthFailOp, compareOp] {
            glStencilOpSeparate(toGLStencilFace(face), toGLStencilOp(failOp), toGLStencilOp(depthFailOp), toGLStencilOp(passOp));
            glCheckError();
            if (face != StencilFace::eBack) _stencilShadow[0].compareOp = toGLOperator(compareOp);
            if (face != StencilFace::eFront) _stencilShadow[1].compareOp = toGLOperator(compareOp);
            applyStencilFunc(face);
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetCullMode(const Flags<CullMode> cullMode)
    {
        CheckRecording();
        enqueue([cullMode]
        {
            if (cullMode == Flags<CullMode>()) {
                glDisable(GL_CULL_FACE);
                return;
            }
            glEnable(GL_CULL_FACE);
            const bool front = cullMode & CullMode::eFront;
            const bool back  = cullMode & CullMode::eBack;
            glCullFace(front && back ? GL_FRONT_AND_BACK : front ? GL_FRONT : GL_BACK);
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetFrontFace(const FrontFace frontFace)
    {
        CheckRecording();
        enqueue([frontFace] { glFrontFace(frontFace == FrontFace::eCounterClockwise ? GL_CCW : GL_CW); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetDepthTestEnable(const bool enable)
    {
        CheckRecording();
        enqueue([enable] { if (enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetDepthWriteEnable(const bool enable)
    {
        CheckRecording();
        enqueue([enable] { glDepthMask(enable ? GL_TRUE : GL_FALSE); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetDepthCompareOp(const CompareOp compareOp)
    {
        CheckRecording();
        enqueue([compareOp] { glDepthFunc(toGLOperator(compareOp)); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetDepthBiasEnable(const bool enable)
    {
        CheckRecording();
        enqueue([enable] { if (enable) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetRasterizerDiscardEnable(const bool enable)
    {
        CheckRecording();
        enqueue([enable] { if (enable) glEnable(GL_RASTERIZER_DISCARD); else glDisable(GL_RASTERIZER_DISCARD); });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::SetPrimitiveRestartEnable(const bool enable)
    {
        CheckRecording();
        enqueue([enable] { if (enable) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART); });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doBlit(ResourceRef<const kor::Image> srcImage, kor::Blit blitInfo) {

        if (srcImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Source image must not be multisampled for blit operation!");

        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = glm::uvec3 { Context::Window().getExtent(), 1 };

        CheckRecording();

        enqueue([srcImage, blitInfo] ()
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
                blitInfo.filtering == kor::Filter::eNearest ? GL_NEAREST : GL_LINEAR);
            glCheckError();

            glDeleteFramebuffers(1, &framebuffer);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBlit(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Blit blitInfo)
    {
        if (srcImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Source image must not be multisampled for blit operation!");
        if (dstImage && dstImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Destination image must not be multisampled for blit operation!");

        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = dstImage->getExtent();

        CheckRecording();
        enqueue([srcImage, dstImage, blitInfo] {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            const auto& glDstImage = dynamic_cast<const ogl::Image&>(*dstImage);
            GLuint srcFramebuffer = 0;
            glGenFramebuffers(1, &srcFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, srcFramebuffer);
            glCheckError();
            // Attach the requested mip (and layer for arrays) so GenerateMipmaps'
            // level-to-level blits read/write the right storage.
            if (srcImage->getArrayLayers() > 1) {
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *glSrcImage,
                                          static_cast<GLint>(blitInfo.srcMipLevel), static_cast<GLint>(blitInfo.srcBaseArrayLayer));
            } else {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glSrcImage, static_cast<GLint>(blitInfo.srcMipLevel));
            }
            glCheckError();

            GLuint dstFramebuffer = 0;
            glGenFramebuffers(1, &dstFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, dstFramebuffer);
            glCheckError();
            if (dstImage->getArrayLayers() > 1) {
                glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *glDstImage,
                                          static_cast<GLint>(blitInfo.dstMipLevel), static_cast<GLint>(blitInfo.dstBaseArrayLayer));
            } else {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *glDstImage, static_cast<GLint>(blitInfo.dstMipLevel));
            }
            glCheckError();

            glBlitNamedFramebuffer(
                srcFramebuffer,
                dstFramebuffer,
                blitInfo.srcOffset.x, blitInfo.srcOffset.y, blitInfo.srcOffset.x + blitInfo.srcExtent.x, blitInfo.srcOffset.y + blitInfo.srcExtent.y,
                blitInfo.dstOffset.x, blitInfo.dstOffset.y, blitInfo.dstOffset.x + blitInfo.dstExtent.x, blitInfo.dstOffset.y + blitInfo.dstExtent.y,
                GL_COLOR_BUFFER_BIT,
                blitInfo.filtering == kor::Filter::eNearest ? GL_NEAREST : GL_LINEAR);
            glCheckError();

            glDeleteFramebuffers(1, &srcFramebuffer);
            glDeleteFramebuffers(1, &dstFramebuffer);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doResolve(ResourceRef<const kor::Image> srcImage, kor::Resolve resolveInfo) {
        if (srcImage->getMSAA() == MSAA::eNone)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");

        if (resolveInfo.srcExtent == glm::ivec3(-1))
            resolveInfo.srcExtent = srcImage->getExtent();
        if (resolveInfo.dstExtent == glm::ivec3(-1))
            resolveInfo.dstExtent = glm::uvec3 { Context::Window().getExtent(), 1 };

        CheckRecording();
        enqueue([srcImage, resolveInfo] {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            GLuint framebuffer = 0;
            glGenFramebuffers(1, &framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glCheckError();

            // glFramebufferTexture works for both single-sample and MSAA textures.
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *glSrcImage, 0);
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

    kor::CommandBuffer & CommandBuffer::doResolve(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Resolve resolveInfo) {
        if (srcImage->getMSAA() == MSAA::eNone)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");
        if (dstImage && dstImage->getMSAA() != MSAA::eNone)
            throw std::runtime_error("Destination image must not be multisampled for resolve operation!");

        if (resolveInfo.srcExtent == glm::ivec3(-1))
            resolveInfo.srcExtent = srcImage->getExtent();
        if (resolveInfo.dstExtent == glm::ivec3(-1))
            resolveInfo.dstExtent = dstImage->getExtent();

        CheckRecording();
        enqueue([srcImage, dstImage, resolveInfo] ()
        {
            const auto& glSrcImage = dynamic_cast<const ogl::Image&>(*srcImage);
            const auto& glDstImage = dynamic_cast<const ogl::Image&>(*dstImage);

            // Multisample resolve = blit from an MSAA FBO to a single-sample FBO.
            // Textures cannot be attached to the default framebuffer, so both ends
            // need a temporary FBO; glFramebufferTexture handles the MSAA target.
            GLuint srcFramebuffer = 0, dstFramebuffer = 0;
            glGenFramebuffers(1, &srcFramebuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFramebuffer);
            glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *glSrcImage, static_cast<GLint>(resolveInfo.srcMipLevel));
            glCheckError();

            glGenFramebuffers(1, &dstFramebuffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFramebuffer);
            glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *glDstImage, static_cast<GLint>(resolveInfo.dstMipLevel));
            glCheckError();

            glBlitFramebuffer(
                resolveInfo.srcOffset.x, resolveInfo.srcOffset.y, resolveInfo.srcOffset.x + resolveInfo.srcExtent.x, resolveInfo.srcOffset.y + resolveInfo.srcExtent.y,
                resolveInfo.dstOffset.x, resolveInfo.dstOffset.y, resolveInfo.dstOffset.x + resolveInfo.dstExtent.x, resolveInfo.dstOffset.y + resolveInfo.dstExtent.y,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST);
            glCheckError();

            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &srcFramebuffer);
            glDeleteFramebuffers(1, &dstFramebuffer);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doGenerateMipmaps(kor::ResourceRef<const kor::Image> image) {
        // The base implementation generates mips with a chain of framebuffer blits,
        // which requires a color-renderable, filterable format and fails on the sRGB /
        // compressed material textures this is typically used for. GL has a dedicated
        // entry point that works for any mipmappable texture, so use it directly.
        CheckRecording();
        enqueue([image] () {
            // Tolerate a non-image handle: the base blit-based implementation silently
            // no-ops when getMipLevels() reads as 0 (as it does for a mis-typed handle),
            // so match that with a pointer-form cast rather than throwing.
            const auto* oglImage = dynamic_cast<const ogl::Image*>(image.operator->());
            if (oglImage == nullptr || image->getMipLevels() <= 1) return;
            glGenerateTextureMipmap(**oglImage);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) {
        CheckRecording();
        enqueue([buffer, offset, size] () {
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

    kor::CommandBuffer & CommandBuffer::doClearColorImage(kor::ResourceRef<const kor::Image> image, glm::vec4 color) {
        CheckRecording();
        enqueue([image, color] () {
            const auto& oglImage = dynamic_cast<const ogl::Image&>(*image);
            const float rgba[4] = { color.r, color.g, color.b, color.a };
            for (glm::u32 level = 0; level < image->getMipLevels(); ++level) {
                glClearTexImage(*oglImage, static_cast<GLint>(level), GL_RGBA, GL_FLOAT, rgba);
            }
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) {
        CheckRecording();
        enqueue([buffer, data, offset, size] () {
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

    kor::CommandBuffer & CommandBuffer::doCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) {
        CheckRecording();
        enqueue([srcBuffer, dstBuffer, size, srcOffset, dstOffset] () {
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

    kor::CommandBuffer& CommandBuffer::Run(const std::function<void(kor::CommandBuffer&)>& command)
    {
        CheckRecording();
        enqueue([command, this] ()
        {
            command(*this);
        });
        return *this;
    }

    namespace {
        // Resolve kor::Copy's sentinel (-1) extent to the image's mip-level extent, and
        // clamp each axis to at least 1 texel. Mirrors the Vulkan backend's defaulting.
        glm::ivec3 resolveCopyExtent(const kor::Image& image, const kor::Copy& copyInfo) {
            const glm::uvec3 base = image.getExtent();
            const glm::u32 mip = copyInfo.imageMipLevel;
            glm::ivec3 ext = copyInfo.imageExtent;
            if (ext.x < 0) ext.x = static_cast<glm::i32>(std::max(base.x >> mip, 1u));
            if (ext.y < 0) ext.y = static_cast<glm::i32>(std::max(base.y >> mip, 1u));
            if (ext.z < 0) ext.z = static_cast<glm::i32>(std::max(base.z >> mip, 1u));
            return ext;
        }
    }

    kor::CommandBuffer & CommandBuffer::doCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) {
        CheckRecording();
        enqueue([buffer, image, copyInfo] () {
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*buffer);
            const auto& oglImage = dynamic_cast<const ogl::Image&>(*image);

            const GLenum baseFormat = ogl::Image::BaseFormatFromImageFormat(image->getFormat());
            const GLenum dataType = ogl::Image::DataTypeFromImageFormat(image->getFormat());
            const glm::ivec3 ext = resolveCopyExtent(*image, copyInfo);
            const GLint mip = static_cast<GLint>(copyInfo.imageMipLevel);

            // Source pixels come from the bound unpack buffer; the pointer argument
            // becomes a byte offset into it.
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, *oglBuffer);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(copyInfo.bufferRowLength));
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, static_cast<GLint>(copyInfo.bufferImageHeight));
            glCheckError();

            const auto ptr = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(copyInfo.bufferOffset));

            switch (image->getType()) {
            case kor::Image::Type::e1D:
                if (image->getArrayLayers() == 1) {
                    glTextureSubImage1D(*oglImage, mip, copyInfo.imageOffset.x, ext.x, baseFormat, dataType, ptr);
                } else {
                    glTextureSubImage2D(*oglImage, mip, copyInfo.imageOffset.x, static_cast<GLint>(copyInfo.imageBaseArrayLayer),
                                        ext.x, static_cast<GLint>(copyInfo.imageLayerCount), baseFormat, dataType, ptr);
                }
                break;
            case kor::Image::Type::e2D:
                if (image->getArrayLayers() == 1) {
                    glTextureSubImage2D(*oglImage, mip, copyInfo.imageOffset.x, copyInfo.imageOffset.y,
                                        ext.x, ext.y, baseFormat, dataType, ptr);
                } else {
                    glTextureSubImage3D(*oglImage, mip, copyInfo.imageOffset.x, copyInfo.imageOffset.y, static_cast<GLint>(copyInfo.imageBaseArrayLayer),
                                        ext.x, ext.y, static_cast<GLint>(copyInfo.imageLayerCount), baseFormat, dataType, ptr);
                }
                break;
            case kor::Image::Type::e3D:
                glTextureSubImage3D(*oglImage, mip, copyInfo.imageOffset.x, copyInfo.imageOffset.y, copyInfo.imageOffset.z,
                                    ext.x, ext.y, ext.z, baseFormat, dataType, ptr);
                break;
            default:
                throw std::runtime_error("Unsupported image type for buffer-to-image copy!");
            }
            glCheckError();

            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) {
        CheckRecording();
        enqueue([image, buffer, copyInfo] () {
            const auto& oglBuffer = dynamic_cast<const ogl::Buffer&>(*buffer);
            const auto& oglImage = dynamic_cast<const ogl::Image&>(*image);

            const GLenum baseFormat = ogl::Image::BaseFormatFromImageFormat(image->getFormat());
            const GLenum dataType = ogl::Image::DataTypeFromImageFormat(image->getFormat());
            const glm::ivec3 ext = resolveCopyExtent(*image, copyInfo);
            const GLint mip = static_cast<GLint>(copyInfo.imageMipLevel);

            // glGetTextureSubImage interprets array layers as the z axis (2D arrays) or
            // y axis (1D arrays); map our offsets/extents accordingly.
            GLint xo = copyInfo.imageOffset.x, yo = copyInfo.imageOffset.y, zo = copyInfo.imageOffset.z;
            GLsizei w = ext.x, h = ext.y, d = ext.z;
            switch (image->getType()) {
            case kor::Image::Type::e1D:
                h = 1; d = 1;
                if (image->getArrayLayers() > 1) {
                    yo = static_cast<GLint>(copyInfo.imageBaseArrayLayer);
                    h = static_cast<GLsizei>(copyInfo.imageLayerCount);
                }
                break;
            case kor::Image::Type::e2D:
                d = 1;
                if (image->getArrayLayers() > 1) {
                    zo = static_cast<GLint>(copyInfo.imageBaseArrayLayer);
                    d = static_cast<GLsizei>(copyInfo.imageLayerCount);
                }
                break;
            case kor::Image::Type::e3D:
                break;
            default:
                throw std::runtime_error("Unsupported image type for image-to-buffer copy!");
            }

            // Destination is the bound pack buffer; the pointer becomes a byte offset.
            glBindBuffer(GL_PIXEL_PACK_BUFFER, *oglBuffer);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(copyInfo.bufferRowLength));
            glPixelStorei(GL_PACK_IMAGE_HEIGHT, static_cast<GLint>(copyInfo.bufferImageHeight));
            glCheckError();

            const auto bufSize = static_cast<GLsizei>(oglBuffer.getSize() - copyInfo.bufferOffset);
            const auto ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(copyInfo.bufferOffset));
            glGetTextureSubImage(*oglImage, mip, xo, yo, zo, w, h, d, baseFormat, dataType, bufSize, ptr);
            glCheckError();

            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glCheckError();
        });
        return *this;
    }

    kor::VoidResult CommandBuffer::Submit()
    {
        if (!_filled)
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = "Cannot submit a command buffer that has not been recorded yet." });
        if (_submitted)
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = "This command buffer has already been submitted; reset it before re-submitting." });

        // Replay. _executing lets commands recorded from within a Run lambda execute
        // in place (see enqueue / CheckRecording) instead of appending to _commands
        // while we iterate it.
        _executing = true;
        for (size_t i = 0; i < _commands.size(); ++i)
            _commands[i]();
        _executing = false;
        _submitted = true;
        return result();
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
            const auto& oglPipeline = dynamic_cast<const kor::ogl::ComputePipeline&>(*_state.boundComputePipeline.value());
            return oglPipeline.getSetAndBindingToBindingPointMap();
        }
        if (_state.boundGraphicsPipeline.has_value()) {
            const auto& oglPipeline = dynamic_cast<const kor::ogl::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            return oglPipeline.getSetAndBindingToBindingPointMap();
        }
        throw std::runtime_error("No pipeline is currently bound!");
    }

    GLuint CommandBuffer::getBoundPipelineProgram() const
    {
        if (_state.boundGraphicsPipeline.has_value())
            return dynamic_cast<const kor::ogl::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value()).getProgram();
        if (_state.boundComputePipeline.has_value())
            return dynamic_cast<const kor::ogl::ComputePipeline&>(*_state.boundComputePipeline.value()).getProgram();
        return 0;
    }

    const std::vector<BindlessSamplerArray>& CommandBuffer::getBoundPipelineBindlessArrays() const
    {
        static const std::vector<BindlessSamplerArray> empty;
        if (_state.boundGraphicsPipeline.has_value())
            return dynamic_cast<const kor::ogl::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value()).getBindlessArrays();
        return empty;
    }

    kor::CommandBuffer & CommandBuffer::PushConstants(const void *data, glm::u32 size, glm::u32 offset) {
        CheckRecording();
        // The caller's `data` is transient, so snapshot it now and upload at replay
        // time. Push constants are emulated as a std140 UBO the pipeline owns (see
        // GraphicsPipeline::Setup / Shader::compile).
        std::vector<std::uint8_t> bytes(size);
        std::memcpy(bytes.data(), data, size);
        enqueue([this, bytes = std::move(bytes), size, offset] () {
            GLuint ubo = 0;
            glm::u32 binding = 0;
            glm::u32 total = 0;
            bool has = false;
            if (_state.boundComputePipeline.has_value()) {
                const auto& p = dynamic_cast<const ComputePipeline&>(*_state.boundComputePipeline.value());
                has = p.hasPushConstants(); ubo = p.getPushConstantUBO();
                binding = p.getPushConstantBindingPoint(); total = p.getPushConstantSize();
            } else if (_state.boundGraphicsPipeline.has_value()) {
                const auto& p = dynamic_cast<const GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
                has = p.hasPushConstants(); ubo = p.getPushConstantUBO();
                binding = p.getPushConstantBindingPoint(); total = p.getPushConstantSize();
            } else {
                std::cerr << "No pipeline bound when trying to push constants" << std::endl;
                return;
            }
            if (!has) return;
            glNamedBufferSubData(ubo, offset, size, bytes.data());
            glCheckError();
            glBindBufferRange(GL_UNIFORM_BUFFER, binding, ubo, 0, total);
            glCheckError();
        });
        return *this;
    }
}
