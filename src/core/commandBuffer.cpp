//
// Created by radue on 2/21/2026.
//

#include <commandBuffer.h>
#include <cstring>
#include <algorithm>
#include <format>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/commandBuffer.h"
#include "../backends/vulkan/commandBuffer.h"

#include "buffer.h"
#include "image.h"
#include "imageView.h"
#include "mesh.h"
#include "descriptorSet.h"
#include "descriptorSetLayout.h"
#include "graphicsPipeline.h"
#include "computePipeline.h"
#include "rayTracingPipeline.h"
#include "../../include/log.h"
#include "../backends/vulkan/device.h"
#include "../backends/vulkan/vulkanContext.h"
#include "../../include/window.h"

namespace kor
{
    namespace
    {
        // Fuse "what the shader does with it" and "which stages do it" into the single
        // ResourceAccess the barrier API speaks. Naming the exact stage is only an
        // optimisation — AllShader* is always correct, merely wider than it needs to be — so
        // anything that is not cleanly one stage falls back to it.
        ResourceAccess shaderAccess(const Shader::AccessKind kind, const Flags<Shader::Stage> stages)
        {
            const auto only = [&](const Shader::Stage stage) {
                return stages.value() == Flags<Shader::Stage>(stage).value();
            };

            switch (kind) {
            case Shader::AccessKind::eWrite:
                if (only(Shader::Stage::eCompute))  return ResourceAccess::ComputeWrite;
                if (only(Shader::Stage::eVertex))   return ResourceAccess::VertexShaderWrite;
                if (only(Shader::Stage::eFragment)) return ResourceAccess::FragmentShaderWrite;
                return ResourceAccess::AllShaderWrite;
            case Shader::AccessKind::eReadWrite:
                if (only(Shader::Stage::eCompute))  return ResourceAccess::ComputeReadWrite;
                if (only(Shader::Stage::eVertex))   return ResourceAccess::VertexShaderReadWrite;
                if (only(Shader::Stage::eFragment)) return ResourceAccess::FragmentShaderReadWrite;
                return ResourceAccess::AllShaderReadWrite;
            case Shader::AccessKind::eRead:
            default:
                if (only(Shader::Stage::eCompute))  return ResourceAccess::ComputeRead;
                if (only(Shader::Stage::eVertex))   return ResourceAccess::VertexShaderRead;
                if (only(Shader::Stage::eFragment)) return ResourceAccess::FragmentShaderRead;
                return ResourceAccess::AllShaderRead;
            }
        }

        // Whether an access can modify the resource. Two writes to the same resource hazard
        // even when the access is identical on both sides, which is why the resolver cannot
        // just compare states for inequality.
        bool writes(const ResourceAccess access)
        {
            switch (access) {
            case ResourceAccess::ComputeWrite:
            case ResourceAccess::ComputeReadWrite:
            case ResourceAccess::VertexShaderWrite:
            case ResourceAccess::VertexShaderReadWrite:
            case ResourceAccess::FragmentShaderWrite:
            case ResourceAccess::FragmentShaderReadWrite:
            case ResourceAccess::AllShaderWrite:
            case ResourceAccess::AllShaderReadWrite:
            case ResourceAccess::ColorAttachment:
            case ResourceAccess::DepthStencilAttachment:
            case ResourceAccess::DepthAttachment:
            case ResourceAccess::StencilAttachment:
            case ResourceAccess::TransferDst:
                return true;
            default:
                return false;
            }
        }

        // Descriptor types the resolver does not synchronise.
        //
        // Samplers are pure state — there is nothing to order against.
        //
        // Acceleration structures are safe for a different reason, and it is worth writing down
        // because it is an assumption rather than a property: a build is submitted through
        // Device::runSingleTimeCommand, which defaults to wait = true and blocks on
        // queue->waitIdle() before returning (see AccelerationStructure::Build). The build has
        // therefore fully completed on the GPU before any command buffer that traces against it
        // is even recorded, so no barrier can be missing. Move AS builds onto a user-recorded
        // command buffer — a per-frame TLAS rebuild for dynamic geometry would do it — and that
        // stops being true, at which point they need real tracking here.
        bool synchronisable(const DescriptorType type)
        {
            switch (type) {
            case DescriptorType::eSampler:
            case DescriptorType::eAccelerationStructure:
                return false;
            default:
                return true;
            }
        }
    }

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


    CommandBuffer& CommandBuffer::enqueue(const char* command, const std::source_location where,
                                          std::vector<ResourceUse> uses, const PassEdge pass,
                                          std::function<void()> emit, const bool transitions,
                                          const bool dereferencesDeviceAddresses)
    {
        // Recorded from inside another command's emit: there is no recording left to join, and
        // appending here would invalidate emitRecords()' walk. Run it where it stands, which
        // also keeps it in the right order relative to the command that triggered it.
        if (_emitting) {
            emit();
            return *this;
        }
        _records.push_back(Record{
            .emit = std::move(emit),
            .uses = std::move(uses),
            .pass = pass,
            .command = command,
            .where = where,
            .transitions = transitions,
            .dereferencesDeviceAddresses = dereferencesDeviceAddresses,
        });
        return *this;
    }

    bool CommandBuffer::boundPipelineUsesDeviceAddresses() const
    {
        if (_state.boundComputePipeline.has_value() && _state.boundComputePipeline->alive())
            return _state.boundComputePipeline.value()->usesDeviceAddresses();
        if (_state.boundGraphicsPipeline.has_value() && _state.boundGraphicsPipeline->alive())
            return _state.boundGraphicsPipeline.value()->usesDeviceAddresses();
        if (_state.boundRayTracingPipeline.has_value() && _state.boundRayTracingPipeline->alive())
            return _state.boundRayTracingPipeline.value()->usesDeviceAddresses();
        return false;
    }

    std::vector<CommandBuffer::ResourceUse> CommandBuffer::usesForBoundResources(const bool includeMesh) const
    {
        std::vector<ResourceUse> uses;

        // Only one of the three can be bound at a time; the state helpers clear the others.
        const auto& sets = _state.boundComputePipeline.has_value()    ? _state.boundComputeDescriptorSets
                         : _state.boundGraphicsPipeline.has_value()   ? _state.boundGraphicsDescriptorSets
                         : _state.boundRayTracingDescriptorSets;

        for (const auto& set : sets | std::views::values) {
            if (!set.alive() || set.poisoned()) continue;
            const auto layout = set->getLayout();
            if (!layout.alive() || layout.poisoned()) continue;

            const auto& descriptions = layout->getBindingDescriptions();
            for (const auto& [binding, written] : set->getWrites()) {
                const auto description = descriptions.find(binding);
                if (description == descriptions.end()) continue;
                if (!description->second.active) continue;
                if (!synchronisable(description->second.type)) continue;

                const auto access = shaderAccess(description->second.access, description->second.stages);

                // Every element, which is what makes a bindless array work: the index a draw
                // picks is unknowable, but requiring the same access on all of them is correct
                // and settles into a no-op once they share that state.
                for (const auto& descriptor : written) {
                    if (!descriptor.isValid()) continue;  // sparse bindless slot

                    if (const auto buffer = descriptor.getBufferRef(); buffer.alive() && !buffer.poisoned()) {
                        uses.push_back(ResourceUse{ .buffer = buffer, .access = access });
                        continue;
                    }
                    if (const auto view = descriptor.getImageViewRef(); view.alive() && !view.poisoned()) {
                        const auto image = view->getImage();
                        if (!image.alive() || image.poisoned()) continue;
                        // The view's own slice, not the whole image: a shadow atlas layer or a
                        // single mip can legitimately be in a different state from its siblings.
                        uses.push_back(ResourceUse{
                            .image = image,
                            .access = access,
                            .baseMipLevel = view->getBaseMipLevel(),
                            .levelCount = view->getMipLevelCount(),
                            .baseArrayLayer = view->getBaseArrayLayer(),
                            .layerCount = view->getArrayLayerCount(),
                        });
                    }
                }
            }
        }

        if (includeMesh && _state.boundMesh.has_value()) {
            const auto& mesh = _state.boundMesh.value();
            if (mesh.alive() && !mesh.poisoned()) {
                for (const auto& buffer : mesh->getVertexBuffers()) {
                    if (buffer.alive() && !buffer.poisoned())
                        uses.push_back(ResourceUse{ .buffer = buffer, .access = ResourceAccess::VertexBuffer });
                }
                if (mesh->hasIndexBuffer()) {
                    if (const auto index = mesh->getIndexBuffer().value(); index.alive() && !index.poisoned())
                        uses.push_back(ResourceUse{ .buffer = index, .access = ResourceAccess::IndexBuffer });
                }
            }
        }

        return uses;
    }

    void CommandBuffer::resolveBarriers()
    {
        // Current access per tracked subresource, seeded lazily from the state the resource
        // carries between frames. Keyed by (resource, mip, layer) for images and by resource
        // alone for buffers, which have no subresources.
        struct Key {
            const void* resource;
            glm::u32 mip;
            glm::u32 layer;
            bool operator==(const Key&) const = default;
        };
        struct KeyHash {
            std::size_t operator()(const Key& key) const noexcept {
                return std::hash<const void*>{}(key.resource)
                     ^ (std::hash<glm::u32>{}(key.mip) << 1)
                     ^ (std::hash<glm::u32>{}(key.layer) << 2);
            }
        };
        std::unordered_map<Key, ResourceAccess, KeyHash> state;

        // Keep a way back from the raw pointer the key holds to the resource itself, so the
        // final state can be written back. Refs, not pointers: a resource destroyed during
        // recording must not be touched at the end of it.
        std::unordered_map<const void*, kor::ResourceRef<const Image>> trackedImages;
        std::unordered_map<const void*, kor::ResourceRef<const Buffer>> trackedBuffers;

        // Barriers waiting to be emitted, and where they must land. Inside a render pass that
        // is not the current position: Vulkan forbids a layout transition between
        // BeginRendering and EndRendering, so anything a draw inside the pass needs goes in
        // front of the record that opened it.
        struct Pending {
            std::size_t at;                          // index to insert before
            std::vector<kor::BufferBarrier> buffers;
            std::vector<kor::ImageBarrier> images;
        };
        std::vector<Pending> pending;
        std::optional<std::size_t> openPassAt;

        // Writes to buffers the engine cannot follow. A buffer built with eShaderDeviceAddress
        // may be reached by a shader through a raw pointer, which reflection can detect but
        // never attribute, so a write to one is only *provably* safe once a barrier covers it.
        // Remember the last unguarded write to each so the report can name it.
        struct UnguardedWrite {
            kor::ResourceRef<const Buffer> buffer;
            const char* command;
            std::source_location where;
        };
        std::unordered_map<const void*, UnguardedWrite> unguardedWrites;

        // Which record last established each subresource's state. A transition can only be
        // hoisted in front of a render pass if the state it is transitioning *from* was
        // established before that pass opened. If the previous state came from a command
        // inside the same pass, no legal place to put the barrier exists — hoisting it would
        // put it before the writer it is supposed to wait on, which synchronises nothing.
        struct Established { std::size_t at; const char* command; std::source_location where; };
        std::unordered_map<Key, Established, KeyHash> establishedAt;

        // Report a hazard that cannot be resolved by moving a barrier, rather than emitting one
        // in a place where it does nothing.
        const auto reportIntraPass = [&](const std::string& resource, const Established& writer, const Record& reader) {
            const std::string name = resource.empty() ? "<unnamed>" : resource;
            const std::string where = std::format(
                "  Written by:  {} at {}:{}\n"
                "  Read by:     {} at {}:{}\n",
                writer.command, writer.where.file_name(), writer.where.line(),
                reader.command, reader.where.file_name(), reader.where.line());

            // Two different mistakes wearing the same shape. If the state came from the record
            // that opened the pass, the resource is an attachment of that very pass and this is
            // a feedback loop — there is no ordering to fix, the read is simply not allowed.
            // Otherwise it is a real producer/consumer pair that just needs the pass split.
            const bool feedbackLoop = openPassAt && writer.at == *openPassAt;
            CommandBuffer::record(Error{
                .code = ErrorCode::eMissingBarrier,
                .message = feedbackLoop
                    ? std::format(
                        "'{}' is being sampled by a draw in the render pass that is rendering "
                        "into it.\n{}"
                        "A pass cannot both write an attachment and read it: the read would need "
                        "a layout the attachment cannot be in while it is still being rendered "
                        "to. Render into a separate image and sample that, or finish this pass "
                        "(EndRendering) before the draw that samples the result.",
                        name, where)
                    : std::format(
                        "Unsynchronised access to '{}' inside a single render pass.\n{}"
                        "Vulkan forbids a barrier between these two — a layout transition cannot "
                        "be recorded inside a render pass, and moving it in front of the pass "
                        "would put it before the write it has to wait on. Split the pass: "
                        "EndRendering after the {}, then BeginRendering again before the {}.",
                        name, where, writer.command, reader.command),
            });
        };

        const auto batchFor = [&](const std::size_t index) -> Pending& {
            // Hoist out of an open render pass, otherwise sit immediately before the command.
            const std::size_t at = openPassAt.value_or(index);
            if (!pending.empty() && pending.back().at == at) return pending.back();
            pending.push_back(Pending{ .at = at });
            return pending.back();
        };

        for (std::size_t i = 0; i < _records.size(); ++i) {
            const auto& record = _records[i];

            if (record.pass == PassEdge::eOpens) openPassAt = i;

            for (const auto& use : record.uses) {
                if (use.buffer.alive()) {
                    const Key key{ use.buffer.get(), 0, 0 };
                    trackedBuffers.emplace(use.buffer.get(), use.buffer);
                    const auto current = state.find(key);
                    const auto previous = current == state.end()
                        ? use.buffer->getTrackedAccess()
                        : std::optional(current->second);

                    // Never synchronised, a genuine transition, or a second write that has to
                    // wait on the first — two writes hazard even at the same access.
                    if (!record.transitions && (!previous || *previous != use.access || writes(use.access))) {
                        const auto established = establishedAt.find(key);
                        if (openPassAt && established != establishedAt.end() && established->second.at >= *openPassAt) {
                            reportIntraPass(use.buffer.name(), established->second, record);
                        } else {
                            batchFor(i).buffers.emplace_back(use.buffer, use.access, use.offset, use.size);
                        }
                    }
                    state[key] = use.access;
                    establishedAt.insert_or_assign(key, Established{ i, record.command, record.where });

                    if (use.buffer->getUsage() & Buffer::Usage::eShaderDeviceAddress) {
                        if (record.transitions) {
                            // A barrier naming it: from here on it is guarded.
                            unguardedWrites.erase(use.buffer.get());
                        } else if (writes(use.access)) {
                            unguardedWrites.insert_or_assign(use.buffer.get(),
                                UnguardedWrite{ use.buffer, record.command, record.where });
                        }
                    }
                    continue;
                }

                if (!use.image.alive()) continue;
                trackedImages.emplace(use.image.get(), use.image);

                const auto baseMip = use.baseMipLevel.value_or(0);
                const auto mipCount = use.levelCount.value_or(use.image->getMipLevels());
                const auto baseLayer = use.baseArrayLayer.value_or(0);
                const auto layerCount = use.layerCount.value_or(use.image->getArrayLayers());

                // Per subresource: a range can straddle subresources sitting in different
                // states — right after GenerateMipmaps the last mip is still TransferDst while
                // the rest are TransferSrc — and one of them needing a transition does not mean
                // all of them do.
                bool needed = false;
                const Established* blocker = nullptr;
                for (auto mip = baseMip; mip < baseMip + mipCount; ++mip) {
                    for (auto layer = baseLayer; layer < baseLayer + layerCount; ++layer) {
                        const Key key{ use.image.get(), mip, layer };
                        const auto current = state.find(key);
                        const auto previous = current == state.end()
                            ? use.image->getTrackedAccess(mip, layer)
                            : std::optional(current->second);
                        if (!previous || *previous != use.access || writes(use.access)) {
                            needed = true;
                            // Whether *this* subresource's state came from inside the open pass,
                            // which is what makes the transition impossible to place.
                            if (openPassAt && !blocker) {
                                if (const auto established = establishedAt.find(key);
                                    established != establishedAt.end() && established->second.at >= *openPassAt) {
                                    blocker = &established->second;
                                }
                            }
                        }
                    }
                }

                if (needed && !record.transitions) {
                    if (blocker) {
                        reportIntraPass(use.image.name(), *blocker, record);
                    } else {
                        batchFor(i).images.emplace_back(use.image, use.access, baseMip, mipCount, baseLayer, layerCount);
                    }
                }
                for (auto mip = baseMip; mip < baseMip + mipCount; ++mip) {
                    for (auto layer = baseLayer; layer < baseLayer + layerCount; ++layer) {
                        const Key key{ use.image.get(), mip, layer };
                        state[key] = use.access;
                        establishedAt.insert_or_assign(key, Established{ i, record.command, record.where });
                    }
                }
            }

            // A shader that chases raw pointers may read any device-address buffer, and the
            // engine has no way to know which. If one was written without a barrier since, say
            // so precisely rather than emit a barrier that might be for the wrong buffer.
            if (record.dereferencesDeviceAddresses && !unguardedWrites.empty()) {
                for (const auto& write : unguardedWrites | std::views::values) {
                    const auto name = write.buffer.name();
                    CommandBuffer::record(Error{
                        .code = ErrorCode::eMissingBarrier,
                        .message = std::format(
                            "Missing barrier for buffer '{}' (device-address; the engine cannot see "
                            "which buffers a shader reaches through a pointer).\n"
                            "  Written by:  {} at {}:{}\n"
                            "  Read by:     {} at {}:{}\n"
                            "  Add before the {}:\n"
                            "      commandBuffer.BufferBarrier({{ buffer, kor::ResourceAccess::{} }});",
                            name.empty() ? "<unnamed>" : name,
                            write.command, write.where.file_name(), write.where.line(),
                            record.command, record.where.file_name(), record.where.line(),
                            record.command,
                            _state.boundComputePipeline.has_value() ? "ComputeRead" : "AllShaderRead"),
                    });
                }
                unguardedWrites.clear();  // reported once; do not repeat for every later draw
            }

            if (record.pass == PassEdge::eCloses) openPassAt.reset();
        }

        // Hand the simulated end state back to the resources, so the next command buffer
        // resolves against where this one actually left them rather than starting over.
        for (const auto& [key, access] : state) {
            if (const auto image = trackedImages.find(key.resource); image != trackedImages.end()) {
                image->second->setTrackedAccess(access, key.mip, key.layer);
            } else if (const auto buffer = trackedBuffers.find(key.resource); buffer != trackedBuffers.end()) {
                buffer->second->setTrackedAccess(access);
            }
        }

        if (pending.empty()) return;

        // Splice back to front so the earlier indices stay valid.
        for (auto batch = pending.rbegin(); batch != pending.rend(); ++batch) {
            auto buffers = std::move(batch->buffers);
            auto images = std::move(batch->images);
            if (buffers.empty() && images.empty()) continue;

            Record barrier{
                .emit = [this, buffers = std::move(buffers), images = std::move(images)]() mutable {
                    doBarrier(std::move(buffers), std::move(images));
                },
                .pass = PassEdge::eNone,
                .command = "Barrier",
            };
            _records.insert(_records.begin() + static_cast<std::ptrdiff_t>(batch->at), std::move(barrier));
        }
    }

    void CommandBuffer::resetTrackedState()
    {
        _state.boundFramebuffer = std::nullopt;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
        _state.boundMesh = std::nullopt;
        _state.boundGraphicsDescriptorSets.clear();
        _state.boundComputeDescriptorSets.clear();
        _state.boundRayTracingDescriptorSets.clear();
        _state.viewportSet = false;
        _state.scissorSet = false;
        _state.dynamicStateSet = Flags<DynamicState>{};
    }

    void CommandBuffer::emitRecords()
    {
        // Rewind the mirrored state so the backends' own replay of it starts where recording
        // did; without this the first emitted command still sees the end-of-recording values.
        resetTrackedState();
        _emitting = true;
        // Index rather than iterate: an emit closure may enqueue (which runs in place and does
        // not append while _emitting), but Run()'s lambda can reach code paths that append
        // before the flag is observed. Indexing survives a reallocation either way.
        for (std::size_t i = 0; i < _records.size(); ++i) {
            if (_records[i].emit) _records[i].emit();
        }
        _emitting = false;
        _records.clear();
    }

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

    CommandBuffer& CommandBuffer::BeginRendering(kor::ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters,
                                                 const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(framebuffer, "framebuffer")) return *this;
        stateBeginRendering(framebuffer);

        // The attachments, as uses rather than as the hand-rolled barrier the backend used to
        // emit. Declaring them means they batch with whatever else the pass needs, and all of
        // it lands in front of the pass instead of illegally inside it.
        std::vector<ResourceUse> uses;
        for (const auto& attachment : framebuffer->getColorAttachments()) {
            uses.push_back(ResourceUse{ .image = attachment.get().getImage(), .access = ResourceAccess::ColorAttachment });
        }
        if (framebuffer->hasDepthAttachment())
            uses.push_back(ResourceUse{ .image = framebuffer->getDepthAttachment().getImage(), .access = ResourceAccess::DepthAttachment });
        if (framebuffer->hasStencilAttachment())
            uses.push_back(ResourceUse{ .image = framebuffer->getStencilAttachment().getImage(), .access = ResourceAccess::StencilAttachment });

        return enqueue("BeginRendering", where, std::move(uses), PassEdge::eOpens,
            [this, framebuffer, renderParameters] { doBeginRendering(framebuffer, renderParameters); });
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

    CommandBuffer& CommandBuffer::BindComputePipeline(kor::ResourceRef<const ComputePipeline> pipeline, const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(pipeline, "compute pipeline")) return *this;

        stateBindComputePipeline(pipeline);
        return enqueue("BindComputePipeline", where, {}, PassEdge::eNone,
            [this, pipeline] { doBindComputePipeline(pipeline); });
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

    CommandBuffer& CommandBuffer::BindGraphicsPipeline(kor::ResourceRef<const GraphicsPipeline> pipeline, const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(pipeline, "graphics pipeline")) return *this;

        stateBindGraphicsPipeline(pipeline);
        return enqueue("BindGraphicsPipeline", where, {}, PassEdge::eNone,
            [this, pipeline] { doBindGraphicsPipeline(pipeline); });
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

    CommandBuffer& CommandBuffer::BindRayTracingPipeline(kor::ResourceRef<const RayTracingPipeline> pipeline, const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(pipeline, "ray tracing pipeline")) return *this;

        stateBindRayTracingPipeline(pipeline);
        return enqueue("BindRayTracingPipeline", where, {}, PassEdge::eNone,
            [this, pipeline] { doBindRayTracingPipeline(pipeline); });
    }

    CommandBuffer& CommandBuffer::TraceRays(glm::u32 width, glm::u32 height, glm::u32 depth, const std::source_location where)
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

    CommandBuffer& CommandBuffer::BindMesh(kor::ResourceRef<const Mesh> mesh, const std::source_location where) {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot bind a mesh without a graphics pipeline bound.");
        if (reject(mesh, "mesh")) return *this;

        stateBindMesh(mesh);
        return enqueue("BindMesh", where, {}, PassEdge::eNone,
            [this, mesh] { doBindMesh(mesh); });
    }

    CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance, const std::source_location where)
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

    CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance, const std::source_location where) {
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

    CommandBuffer & CommandBuffer::DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ, const std::source_location where) {
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
        return doGenerateMipmaps(image);  // expands into Blit records, each declaring its own uses
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

    void CommandBuffer::stateBindDescriptorSet(const glm::u32 index, const kor::ResourceRef<const DescriptorSet>& descriptorSet)
    {
        // Mirrors the dispatch the backends do: the set belongs to whichever pipeline type is
        // currently bound. With none bound the backends log and drop it, so do nothing here too.
        if (_state.boundComputePipeline.has_value())
            _state.boundComputeDescriptorSets.insert_or_assign(index, descriptorSet);
        else if (_state.boundGraphicsPipeline.has_value())
            _state.boundGraphicsDescriptorSets.insert_or_assign(index, descriptorSet);
        else if (_state.boundRayTracingPipeline.has_value())
            _state.boundRayTracingDescriptorSets.insert_or_assign(index, descriptorSet);
    }

    CommandBuffer& CommandBuffer::Barrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers,
                                          const std::source_location where)
    {
        if (_failed) return *this;
        for (const auto& barrier : bufferBarriers)
            if (reject(barrier.getBuffer(), "barrier's buffer")) return *this;
        for (const auto& barrier : imageBarriers)
            if (reject(barrier.getImage(), "barrier's image")) return *this;

        // Describe what this barrier *establishes*, so the resolver advances its tracking past
        // it and emits nothing of its own. A hand-written barrier therefore suppresses the
        // automatic one rather than being doubled by it — which is what keeps the escape hatch
        // for device-address buffers free of side effects.
        std::vector<ResourceUse> uses;
        uses.reserve(bufferBarriers.size() + imageBarriers.size());
        for (const auto& barrier : bufferBarriers) {
            uses.push_back(ResourceUse{
                .buffer = barrier.getBuffer(), .access = barrier.getDstAccess(),
                .offset = barrier.getOffset(), .size = barrier.getSize(),
            });
        }
        for (const auto& barrier : imageBarriers) {
            uses.push_back(ResourceUse{
                .image = barrier.getImage(), .access = barrier.getDstAccess(),
                .baseMipLevel = barrier.getBaseMipLevel(), .levelCount = barrier.getLevelCount(),
                .baseArrayLayer = barrier.getBaseArrayLayer(), .layerCount = barrier.getLayerCount(),
            });
        }

        return enqueue("Barrier", where, std::move(uses), PassEdge::eNone,
            [this, buffers = std::move(bufferBarriers), images = std::move(imageBarriers)]() mutable {
                doBarrier(std::move(buffers), std::move(images));
            }, /*transitions=*/true);
    }

    namespace
    {
        // Clamp a declared subresource range / buffer range to what the resource actually has.
        //
        // A command whose arguments are out of bounds is rejected by the backend, but that
        // happens at *emit* time, after the resolver has already turned this command's uses
        // into barriers. An unclamped range would therefore produce a barrier naming a mip or
        // layer that does not exist — invalid in its own right, and reported as such — for a
        // command that then never runs. Clamping keeps every emitted barrier well-formed and
        // leaves the rejection itself to the backend, which owns the error codes for it.
        struct ClampedSubresource { glm::u32 baseMip, mipCount, baseLayer, layerCount; };

        ClampedSubresource clampToImage(const Image& image, const glm::u32 baseMip, const glm::u32 mipCount,
                                        const glm::u32 baseLayer, const glm::u32 layerCount)
        {
            const glm::u32 mips = std::max(image.getMipLevels(), 1u);
            const glm::u32 layers = std::max(image.getArrayLayers(), 1u);
            const glm::u32 firstMip = std::min(baseMip, mips - 1);
            const glm::u32 firstLayer = std::min(baseLayer, layers - 1);
            return {
                firstMip,   std::min(mipCount,   mips - firstMip),
                firstLayer, std::min(layerCount, layers - firstLayer),
            };
        }

        // Same idea for buffers: an offset past the end would name a range outside the
        // allocation. UINT64_MAX keeps its "to the end" meaning.
        std::pair<glm::u64, glm::u64> clampToBuffer(const Buffer& buffer, const glm::u64 offset, const glm::u64 size)
        {
            const glm::u64 total = buffer.getSize();
            // Strictly inside the allocation: Vulkan requires offset < size, not <=.
            const glm::u64 start = total == 0 ? 0 : std::min(offset, total - 1);
            if (size == UINT64_MAX) return { start, UINT64_MAX };
            return { start, std::min(size, total - start) };
        }
    }

    // ---- Non-virtual interface: the gate ---------------------------------
    // Each of these validates, refuses unusable resources, updates the tracked state, works out
    // which resources the command touches, and then *records* it. Nothing reaches the backend
    // until End() resolves barriers and emits. The do* implementations may still assume every
    // resource they receive is alive and usable — that is what the validation here guarantees —
    // and additionally that whatever barriers they need have already been emitted ahead of them.

    CommandBuffer& CommandBuffer::BindDescriptorSet(const glm::u32 index, kor::ResourceRef<const DescriptorSet> descriptorSet, const bool debug,
                                                    const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(descriptorSet, "descriptor set")) return *this;
        stateBindDescriptorSet(index, descriptorSet);
        return enqueue("BindDescriptorSet", where, {}, PassEdge::eNone,
            [this, index, descriptorSet, debug] { doBindDescriptorSet(index, descriptorSet, debug); });
    }

    CommandBuffer& CommandBuffer::DispatchIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset,
                                                   const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundComputePipeline.has_value())
            return record(ErrorCode::eNoComputePipelineBound, "Cannot dispatch without a compute pipeline bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;

        auto uses = usesForBoundResources(false);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::IndirectBuffer, .offset = offset });
        return enqueue("DispatchIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset] { doDispatchIndirect(indirectBuffer, offset); },
            /*transitions=*/false, boundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::DrawIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride,
                                               const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);

        auto uses = usesForBoundResources(true);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::IndirectBuffer, .offset = offset });
        return enqueue("DrawIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset, drawCount, stride] { doDrawIndirect(indirectBuffer, offset, drawCount, stride); },
            /*transitions=*/false, boundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::DrawIndexedIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride,
                                                      const std::source_location where)
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

        auto uses = usesForBoundResources(true);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::IndirectBuffer, .offset = offset });
        return enqueue("DrawIndexedIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset, drawCount, stride] { doDrawIndexedIndirect(indirectBuffer, offset, drawCount, stride); },
            /*transitions=*/false, boundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::DrawMeshTasksIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride,
                                                        const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return record(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);

        auto uses = usesForBoundResources(true);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::IndirectBuffer, .offset = offset });
        return enqueue("DrawMeshTasksIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset, drawCount, stride] { doDrawMeshTasksIndirect(indirectBuffer, offset, drawCount, stride); },
            /*transitions=*/false, boundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::ClearBuffer(kor::ResourceRef<const Buffer> buffer, const glm::u64 offset, const glm::u64 size,
                                              const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(buffer, "buffer")) return *this;
        const auto [start, span] = clampToBuffer(*buffer, offset, size);
        return enqueue("ClearBuffer", where,
            { ResourceUse{ .buffer = buffer, .access = ResourceAccess::TransferDst, .offset = start, .size = span } },
            PassEdge::eNone, [this, buffer, offset, size] { doClearBuffer(buffer, offset, size); });
    }

    CommandBuffer& CommandBuffer::ClearColorImage(kor::ResourceRef<const Image> image, const glm::vec4 color,
                                                  const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(image, "image")) return *this;
        return enqueue("ClearColorImage", where,
            { ResourceUse{ .image = image, .access = ResourceAccess::TransferDst } },
            PassEdge::eNone, [this, image, color] { doClearColorImage(image, color); });
    }

    CommandBuffer& CommandBuffer::FillBuffer(kor::ResourceRef<const Buffer> buffer, void* data, const glm::u64 offset, const glm::u64 size,
                                             const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(buffer, "buffer")) return *this;

        // Copy the payload rather than the pointer: the caller's storage is very often a
        // temporary, and nothing reaches the GPU until End().
        const glm::u64 byteCount = size == UINT64_MAX ? buffer->getSize() - offset : size;
        std::vector<std::byte> bytes(byteCount);
        if (data && byteCount) std::memcpy(bytes.data(), data, byteCount);

        const auto [start, span] = clampToBuffer(*buffer, offset, size);
        return enqueue("FillBuffer", where,
            { ResourceUse{ .buffer = buffer, .access = ResourceAccess::TransferDst, .offset = start, .size = span } },
            PassEdge::eNone,
            [this, buffer, bytes = std::move(bytes), offset, size] () mutable {
                doFillBuffer(buffer, bytes.data(), offset, size);
            });
    }

    CommandBuffer& CommandBuffer::CopyBuffer(kor::ResourceRef<const Buffer> srcBuffer, kor::ResourceRef<const Buffer> dstBuffer, const glm::u64 size, const glm::u64 srcOffset, const glm::u64 dstOffset,
                                             const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(srcBuffer, "copy source buffer")) return *this;
        if (reject(dstBuffer, "copy destination buffer")) return *this;
        const auto [srcStart, srcSpan] = clampToBuffer(*srcBuffer, srcOffset, size);
        const auto [dstStart, dstSpan] = clampToBuffer(*dstBuffer, dstOffset, size);
        return enqueue("CopyBuffer", where, {
                ResourceUse{ .buffer = srcBuffer, .access = ResourceAccess::TransferSrc, .offset = srcStart, .size = srcSpan },
                ResourceUse{ .buffer = dstBuffer, .access = ResourceAccess::TransferDst, .offset = dstStart, .size = dstSpan },
            }, PassEdge::eNone,
            [this, srcBuffer, dstBuffer, size, srcOffset, dstOffset] { doCopyBuffer(srcBuffer, dstBuffer, size, srcOffset, dstOffset); });
    }

    CommandBuffer& CommandBuffer::CopyBufferToImage(kor::ResourceRef<const Buffer> buffer, kor::ResourceRef<const Image> image, const kor::Copy copyInfo,
                                                    const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(buffer, "copy source buffer")) return *this;
        if (reject(image, "copy destination image")) return *this;
        const auto range = clampToImage(*image, copyInfo.imageMipLevel, 1u,
                                        copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount);
        const auto [srcOffset, srcSize] = clampToBuffer(*buffer, copyInfo.bufferOffset, UINT64_MAX);
        return enqueue("CopyBufferToImage", where, {
                ResourceUse{ .buffer = buffer, .access = ResourceAccess::TransferSrc, .offset = srcOffset, .size = srcSize },
                ResourceUse{ .image = image, .access = ResourceAccess::TransferDst,
                             .baseMipLevel = range.baseMip, .levelCount = range.mipCount,
                             .baseArrayLayer = range.baseLayer, .layerCount = range.layerCount },
            }, PassEdge::eNone,
            [this, buffer, image, copyInfo] { doCopyBufferToImage(buffer, image, copyInfo); });
    }

    CommandBuffer& CommandBuffer::CopyImageToBuffer(kor::ResourceRef<const Image> image, kor::ResourceRef<const Buffer> buffer, const kor::Copy copyInfo,
                                                    const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(image, "copy source image")) return *this;
        if (reject(buffer, "copy destination buffer")) return *this;
        const auto range = clampToImage(*image, copyInfo.imageMipLevel, 1u,
                                        copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount);
        const auto [dstOffset, dstSize] = clampToBuffer(*buffer, copyInfo.bufferOffset, UINT64_MAX);
        return enqueue("CopyImageToBuffer", where, {
                ResourceUse{ .image = image, .access = ResourceAccess::TransferSrc,
                             .baseMipLevel = range.baseMip, .levelCount = range.mipCount,
                             .baseArrayLayer = range.baseLayer, .layerCount = range.layerCount },
                ResourceUse{ .buffer = buffer, .access = ResourceAccess::TransferDst, .offset = dstOffset, .size = dstSize },
            }, PassEdge::eNone,
            [this, image, buffer, copyInfo] { doCopyImageToBuffer(image, buffer, copyInfo); });
    }

    CommandBuffer& CommandBuffer::Blit(kor::ResourceRef<const Image> srcImage, const kor::Blit blitInfo,
                                       const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(srcImage, "blit source image")) return *this;
        // The destination is the swapchain image, which the backend resolves for itself; only
        // the source can be described here, so the backend still transitions the destination.
        return enqueue("Blit", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::TransferSrc,
                             .baseMipLevel = blitInfo.srcMipLevel, .levelCount = 1u,
                             .baseArrayLayer = blitInfo.srcBaseArrayLayer, .layerCount = blitInfo.layerCount },
            }, PassEdge::eNone, [this, srcImage, blitInfo] { doBlit(srcImage, blitInfo); });
    }

    CommandBuffer& CommandBuffer::Blit(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, const kor::Blit blitInfo,
                                       const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(srcImage, "blit source image")) return *this;
        if (reject(dstImage, "blit destination image")) return *this;
        return enqueue("Blit", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::TransferSrc,
                             .baseMipLevel = blitInfo.srcMipLevel, .levelCount = 1u,
                             .baseArrayLayer = blitInfo.srcBaseArrayLayer, .layerCount = blitInfo.layerCount },
                ResourceUse{ .image = dstImage, .access = ResourceAccess::TransferDst,
                             .baseMipLevel = blitInfo.dstMipLevel, .levelCount = 1u,
                             .baseArrayLayer = blitInfo.dstBaseArrayLayer, .layerCount = blitInfo.layerCount },
            }, PassEdge::eNone, [this, srcImage, dstImage, blitInfo] { doBlit(srcImage, dstImage, blitInfo); });
    }

    CommandBuffer& CommandBuffer::Resolve(kor::ResourceRef<const Image> srcImage, const kor::Resolve resolveInfo,
                                          const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(srcImage, "resolve source image")) return *this;
        return enqueue("Resolve", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::TransferSrc,
                             .baseMipLevel = resolveInfo.srcMipLevel, .levelCount = 1u,
                             .baseArrayLayer = resolveInfo.srcBaseArrayLayer, .layerCount = resolveInfo.layerCount },
            }, PassEdge::eNone, [this, srcImage, resolveInfo] { doResolve(srcImage, resolveInfo); });
    }

    CommandBuffer& CommandBuffer::Resolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, const kor::Resolve resolveInfo,
                                          const std::source_location where)
    {
        if (_failed) return *this;
        if (reject(srcImage, "resolve source image")) return *this;
        if (reject(dstImage, "resolve destination image")) return *this;
        return enqueue("Resolve", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::TransferSrc,
                             .baseMipLevel = resolveInfo.srcMipLevel, .levelCount = 1u,
                             .baseArrayLayer = resolveInfo.srcBaseArrayLayer, .layerCount = resolveInfo.layerCount },
                ResourceUse{ .image = dstImage, .access = ResourceAccess::TransferDst,
                             .baseMipLevel = resolveInfo.dstMipLevel, .levelCount = 1u,
                             .baseArrayLayer = resolveInfo.dstBaseArrayLayer, .layerCount = resolveInfo.layerCount },
            }, PassEdge::eNone, [this, srcImage, dstImage, resolveInfo] { doResolve(srcImage, dstImage, resolveInfo); });
    }
}
