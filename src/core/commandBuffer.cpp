//
// Created by radue on 2/21/2026.
//

#include <commandBuffer.h>
#include "current.h"
#include <window.h>
#include "tokenState.h"
#include <cstring>
#include <mutex>
#include <algorithm>
#include <format>
#include <framebuffer.h>
#include <surface.h>

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

// Every command here that takes a resource is a non-virtual wrapper: it validates, rejects
// unusable resources, updates the tracked state, and only then calls the matching do* the backend
// implements. A backend therefore *cannot* be handed a poisoned or destroyed resource — which is
// the precondition that all ~150 `dynamic_cast<const vk::X&>(*ref)` sites in the backends have
// always silently assumed. That used to be a convention each override had to remember, and several
// did not; the split makes it structural.

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
                return stages.Value() == Flags<Shader::Stage>(stage).Value();
            };

            switch (kind) {
            case Shader::AccessKind::eWrite:
                if (only(Shader::Stage::eCompute))  return ResourceAccess::eComputeWrite;
                if (only(Shader::Stage::eVertex))   return ResourceAccess::eVertexShaderWrite;
                if (only(Shader::Stage::eFragment)) return ResourceAccess::eFragmentShaderWrite;
                return ResourceAccess::eAllShaderWrite;
            case Shader::AccessKind::eReadWrite:
                if (only(Shader::Stage::eCompute))  return ResourceAccess::eComputeReadWrite;
                if (only(Shader::Stage::eVertex))   return ResourceAccess::eVertexShaderReadWrite;
                if (only(Shader::Stage::eFragment)) return ResourceAccess::eFragmentShaderReadWrite;
                return ResourceAccess::eAllShaderReadWrite;
            case Shader::AccessKind::eRead:
            default:
                if (only(Shader::Stage::eCompute))  return ResourceAccess::eComputeRead;
                if (only(Shader::Stage::eVertex))   return ResourceAccess::eVertexShaderRead;
                if (only(Shader::Stage::eFragment)) return ResourceAccess::eFragmentShaderRead;
                return ResourceAccess::eAllShaderRead;
            }
        }

        // Whether an access can modify the resource. Two writes to the same resource hazard
        // even when the access is identical on both sides, which is why the resolver cannot
        // just compare states for inequality.
        bool writes(const ResourceAccess access)
        {
            switch (access) {
            case ResourceAccess::eComputeWrite:
            case ResourceAccess::eComputeReadWrite:
            case ResourceAccess::eVertexShaderWrite:
            case ResourceAccess::eVertexShaderReadWrite:
            case ResourceAccess::eFragmentShaderWrite:
            case ResourceAccess::eFragmentShaderReadWrite:
            case ResourceAccess::eAllShaderWrite:
            case ResourceAccess::eAllShaderReadWrite:
            case ResourceAccess::eColorAttachment:
            case ResourceAccess::eDepthStencilAttachment:
            case ResourceAccess::eDepthAttachment:
            case ResourceAccess::eStencilAttachment:
            case ResourceAccess::eTransferDst:
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
        // Device::runSingleTimeCommand, and AccelerationStructure::Build waits on the token it
        // returns before returning itself. The build has
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

    RenderInfo::RenderInfo() : RenderInfo(detail::CurrentWindow("A RenderInfo naming no framebuffer").DefaultFramebuffer()) {}

    RenderInfo::RenderInfo(const kor::ResourceRef<const kor::Framebuffer>& framebuffer) : _framebuffer(framebuffer)
    {
        // Sized here so the common case — override one attachment, leave the rest — needs no
        // resizing later. A framebuffer that failed to build has nothing to ask; BeginRendering
        // rejects it by name, and this must not throw before it gets the chance.
        if (framebuffer.Valid())
            _clearColors.resize(framebuffer->ColorAttachmentCount(), std::nullopt);
    }

    RenderInfo::RenderInfo(const kor::ResourceRef<kor::Framebuffer>& framebuffer)
        : RenderInfo(ResourceRef<const Framebuffer>(framebuffer)) {}

    RenderInfo::RenderInfo(const kor::Resource<kor::Framebuffer>& framebuffer)
        : RenderInfo(ResourceRef<const Framebuffer>(framebuffer)) {}

    const ClearColor& RenderInfo::ClearColorAt(const glm::u32 index) const
    {
        // Opaque black, for an attachment that neither the pass nor the framebuffer describes.
        // Unreachable through BeginRendering, which resolves against the framebuffer first.
        static const ClearColor black = glm::vec4(0.f, 0.f, 0.f, 1.f);
        if (index >= _clearColors.size() || !_clearColors[index].has_value()) return black;
        return *_clearColors[index];
    }

    void RenderInfo::ResolveClearValues(const kor::Framebuffer& framebuffer)
    {
        const auto declared = framebuffer.Clears().clearColor.size();
        if (_clearColors.size() < declared) _clearColors.resize(declared, std::nullopt);

        for (std::size_t i = 0; i < _clearColors.size(); ++i) {
            if (_clearColors[i].has_value()) continue;
            if (i < declared) _clearColors[i] = framebuffer.ClearColorAt(static_cast<glm::u32>(i));
        }

        if (!_clearDepth.has_value()) _clearDepth = framebuffer.ClearDepth();
        if (!_clearStencil.has_value()) _clearStencil = framebuffer.ClearStencil();
    }


    // ---- Deferred recording ---------------------------------------------------------------
    //
    // A command does not reach the backend when it is called. It validates immediately — so a
    // destroyed or poisoned resource still fails at the caller's line — and then parks an emit
    // closure together with the set of resources it touches. End() walks that list twice: once to
    // work out where barriers belong, once to emit everything in order.
    //
    // The lookahead is the whole point. A transition a draw needs often has to be emitted *before*
    // the render pass containing that draw was opened (sample a shadow map that was rendered
    // earlier in the frame), and Vulkan forbids a layout transition inside a render pass. Holding
    // the commands lets the resolver insert the barrier at a legal point instead of breaking the
    // pass apart.
    CommandBuffer& CommandBuffer::Enqueue(const char* command, const std::source_location where,
                                          std::vector<ResourceUse> uses, const PassEdge pass,
                                          std::function<void()> emit, const bool transitions,
                                          const bool dereferencesDeviceAddresses)
    {
        // Recorded from inside another command's emit — Run()'s lambda calls straight back into the
        // API, and ApplyDynamicDefaults() reaches back through the virtual Set* overrides. There is
        // no recording left to join, and appending here would invalidate EmitRecords()' walk. Run it
        // where it stands, which also keeps it in the right order relative to the command that
        // triggered it.
        // Every transfer command already says which resource it reads from and which it writes to,
        // because the barrier resolver needs exactly that — so the usage those roles require can be
        // checked here, once, instead of in each of the dozen commands that perform one. A command
        // added later is covered without knowing about this.
        if (auto missing = MissingTransferUsage(uses, command, where)) {
            return RecordError(std::move(*missing));
        }

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

    bool CommandBuffer::BoundPipelineUsesDeviceAddresses() const
    {
        if (_state.boundComputePipeline.has_value() && _state.boundComputePipeline->Alive())
            return _state.boundComputePipeline.value()->UsesDeviceAddresses();
        if (_state.boundGraphicsPipeline.has_value() && _state.boundGraphicsPipeline->Alive())
            return _state.boundGraphicsPipeline.value()->UsesDeviceAddresses();
        if (_state.boundRayTracingPipeline.has_value() && _state.boundRayTracingPipeline->Alive())
            return _state.boundRayTracingPipeline.value()->UsesDeviceAddresses();
        return false;
    }

    const Pipeline* CommandBuffer::BoundPipeline() const
    {
        // Only one of the three can be bound at a time; the state helpers clear the others.
        if (_state.boundComputePipeline.has_value() && _state.boundComputePipeline->Valid())
            return _state.boundComputePipeline.value().Get();
        if (_state.boundGraphicsPipeline.has_value() && _state.boundGraphicsPipeline->Valid())
            return _state.boundGraphicsPipeline.value().Get();
        if (_state.boundRayTracingPipeline.has_value() && _state.boundRayTracingPipeline->Valid())
            return _state.boundRayTracingPipeline.value().Get();
        return nullptr;
    }

    namespace {
        // "float3", "float4x4", "float3[4]" — how a shape reads in a mismatch report.
        std::string describeShape(const ValueScalar scalar, const glm::u8 rows, const glm::u8 columns,
                                  const glm::u32 count)
        {
            static constexpr std::string_view names[] { "float", "int", "uint", "bool", "double", "struct" };
            const auto index = static_cast<std::size_t>(scalar);
            std::string text(index < std::size(names) ? names[index] : "unknown");

            if (columns > 1)   text += std::format("{}x{}", columns, rows);
            else if (rows > 1) text += std::to_string(rows);
            if (count > 1)     text += std::format("[{}]", count);
            return text;
        }
    }

    CommandBuffer& CommandBuffer::PushConstant(const std::string_view name, const void* data, const glm::u32 size,
                                               const ValueShape shape, const std::source_location where)
    {
        if (_failed) return *this;

        const auto* pipeline = BoundPipeline();
        if (pipeline == nullptr)
            return RecordError(ErrorCode::eNoPipelineBound,
                std::format("Cannot push the constant '{}': no pipeline is bound to look it up on.", name));

        const auto* member = pipeline->FindPushConstant(name);
        if (member == nullptr) {
            // Naming what there is turns "no such constant" into a fix: the usual cause is a
            // rename on one side of the pair, or a field addressed as a whole when the shader
            // nests it (or the other way round).
            std::string available;
            for (const auto& declared : pipeline->PushConstants() | std::views::keys) {
                if (!available.empty()) available += ", ";
                available += declared;
            }
            if (available.empty()) available = "none at all";

            return RecordError(ErrorCode::ePushConstantMismatch,
                std::format("The bound pipeline declares no push constant called '{}'. It declares: {}.",
                            name, available));
        }

        // A shape the engine cannot see inside — one of the caller's own structs, or the shader's
        // own aggregate. Nothing can be laid out for it, so it goes in as it stands and the size
        // has to match exactly: a longer write would run into whatever the shader put next.
        if (!shape.known || member->aggregate) {
            if (member->size != size)
                return RecordError(ErrorCode::ePushConstantMismatch,
                    std::format("Push constant '{}' is {} bytes in the shader, but {} were given. "
                                "A struct is copied as it stands, so the two layouts have to agree — "
                                "or write its fields one at a time, as '{}.field'.",
                                name, member->size, size, name));
            return PushConstantBlock(data, size, member->offset);
        }

        const auto declared = ValueShape{ static_cast<ValueScalar>(member->scalar), member->rows,
                                          member->columns, member->count, true };
        if (!declared.SameAs(shape))
            return RecordError(ErrorCode::ePushConstantMismatch,
                std::format("Push constant '{}' is declared as {} but a {} was given.",
                            name, describeShape(declared.scalar, declared.rows, declared.columns, declared.count),
                            describeShape(shape.scalar, shape.rows, shape.columns, shape.count)));

        // Same shape, possibly different padding: the shader spaces array elements and matrix
        // columns however its own rules say, and C++ packs them tight. Copying the value straight
        // over is what puts two thirds of a mat3 in the right place and the rest anywhere; so it is
        // reassembled here, one column at a time, into the strides reflection reported.
        const glm::u32 scalarSize = shape.ScalarSize();
        const glm::u32 tightColumn = scalarSize * shape.rows;
        const glm::u32 tightElement = tightColumn * shape.columns;
        const glm::u32 columnStride = member->matrixStride > 0 ? member->matrixStride : tightColumn;
        const glm::u32 elementStride = member->arrayStride > 0 ? member->arrayStride : tightElement;

        if (elementStride == tightElement && columnStride == tightColumn)
            return PushConstantBlock(data, size, member->offset);   // laid out alike; nothing to do

        std::vector<std::byte> laidOut(member->size, std::byte{});
        const auto* source = static_cast<const std::byte*>(data);
        for (glm::u32 element = 0; element < shape.count; ++element) {
            for (glm::u32 column = 0; column < shape.columns; ++column) {
                const glm::u32 to = element * elementStride + column * columnStride;
                const glm::u32 from = element * tightElement + column * tightColumn;
                if (to + tightColumn > laidOut.size() || from + tightColumn > size) break;
                std::memcpy(laidOut.data() + to, source + from, tightColumn);
            }
        }
        return PushConstantBlock(laidOut.data(), static_cast<glm::u32>(laidOut.size()), member->offset);
    }

    namespace {
        ResourceAccess withWrite(const ResourceAccess access) {
            switch (access) {
            case ResourceAccess::eComputeRead:        return ResourceAccess::eComputeReadWrite;
            case ResourceAccess::eVertexShaderRead:   return ResourceAccess::eVertexShaderReadWrite;
            case ResourceAccess::eFragmentShaderRead: return ResourceAccess::eFragmentShaderReadWrite;
            case ResourceAccess::eAllShaderRead:      return ResourceAccess::eAllShaderReadWrite;
            default:                                  return access;
            }
        }
    }

    std::vector<CommandBuffer::ResourceUse> CommandBuffer::UsesForBoundResources(const bool includeMesh) const
    {
        std::vector<ResourceUse> uses;

        // Only one of the three can be bound at a time; the state helpers clear the others.
        const auto& sets = _state.boundComputePipeline.has_value()    ? _state.boundComputeDescriptorSets
                         : _state.boundGraphicsPipeline.has_value()   ? _state.boundGraphicsDescriptorSets
                         : _state.boundRayTracingDescriptorSets;

        const Pipeline* pipeline = BoundPipeline();
        for (const auto& [index, set] : sets) {
            if (!set.Alive() || set.Poisoned()) continue;
            // How a binding is accessed is up to the shader that consumes it, so it is read from the
            // bound pipeline's layout, not from the one the set was allocated with: a set built for a
            // compute pass that writes it may be bound, unchanged, to a draw that only reads it.
            auto layout = pipeline ? pipeline->SetLayoutRef(index) : kor::ResourceRef<const DescriptorSetLayout>{};
            if (!layout.Alive() || layout.Poisoned()) layout = set->Layout();
            if (!layout.Alive() || layout.Poisoned()) continue;

            const auto& descriptions = layout->Bindings();
            for (const auto& [binding, written] : set->Writes()) {
                const auto description = descriptions.find(binding);
                if (description == descriptions.end()) continue;
                if (!description->second.active) continue;
                if (!synchronisable(description->second.type)) continue;

                auto access = shaderAccess(description->second.access, description->second.stages);
                // A storage image is only ever accessed in the general layout, even read-only; the
                // plain read accesses map to the sampled layout instead. So a read of one is recorded
                // as its read-write form — the layout is what matters, and the only cost is a barrier
                // between two back-to-back read-only uses that did not strictly need one.
                if (description->second.type == DescriptorType::eStorageImage) access = withWrite(access);

                // Every element, which is what makes a bindless array work: the index a draw
                // picks is unknowable, but requiring the same access on all of them is correct
                // and settles into a no-op once they share that state.
                for (const auto& descriptor : written) {
                    if (!descriptor.IsValid()) continue;  // sparse bindless slot

                    if (const auto buffer = descriptor.BufferRef(); buffer.Alive() && !buffer.Poisoned()) {
                        uses.push_back(ResourceUse{ .buffer = buffer, .access = access });
                        continue;
                    }
                    if (const auto view = descriptor.ImageViewRef(); view.Alive() && !view.Poisoned()) {
                        const auto image = view->SourceImage();
                        if (!image.Alive() || image.Poisoned()) continue;
                        // The view's own slice, not the whole image: a shadow atlas layer or a
                        // single mip can legitimately be in a different state from its siblings.
                        uses.push_back(ResourceUse{
                            .image = image,
                            .access = access,
                            .baseMipLevel = view->BaseMipLevel(),
                            .levelCount = view->MipLevelCount(),
                            .baseArrayLayer = view->BaseArrayLayer(),
                            .layerCount = view->ArrayLayerCount(),
                        });
                    }
                }
            }
        }

        if (includeMesh && _state.boundMesh.has_value()) {
            const auto& mesh = _state.boundMesh.value();
            if (mesh.Alive() && !mesh.Poisoned()) {
                for (const auto& buffer : mesh->VertexBuffers()) {
                    if (buffer.Alive() && !buffer.Poisoned())
                        uses.push_back(ResourceUse{ .buffer = buffer, .access = ResourceAccess::eVertexBuffer });
                }
                if (mesh->HasIndexBuffer()) {
                    if (const auto index = mesh->IndexBuffer().value(); index.Alive() && !index.Poisoned())
                        uses.push_back(ResourceUse{ .buffer = index, .access = ResourceAccess::eIndexBuffer });
                }
            }
        }

        return uses;
    }

    void CommandBuffer::ResolveBarriers()
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
            CommandBuffer::RecordError(Error{
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
                if (use.buffer.Alive()) {
                    const Key key{ use.buffer.Get(), 0, 0 };
                    trackedBuffers.emplace(use.buffer.Get(), use.buffer);
                    const auto current = state.find(key);
                    const auto previous = current == state.end()
                        ? use.buffer->TrackedAccess()
                        : std::optional(current->second);

                    // Never synchronised, a genuine transition, or a second write that has to
                    // wait on the first — two writes hazard even at the same access.
                    if (!record.transitions && (!previous || *previous != use.access || writes(use.access))) {
                        const auto established = establishedAt.find(key);
                        if (openPassAt && established != establishedAt.end() && established->second.at >= *openPassAt) {
                            reportIntraPass(use.buffer.Name(), established->second, record);
                        } else {
                            batchFor(i).buffers.emplace_back(use.buffer, use.access, use.offset, use.size);
                        }
                    }
                    state[key] = use.access;
                    establishedAt.insert_or_assign(key, Established{ i, record.command, record.where });

                    if (use.buffer->UsageFlags() & Buffer::Usage::eShaderDeviceAddress) {
                        if (record.transitions) {
                            // A barrier naming it: from here on it is guarded.
                            unguardedWrites.erase(use.buffer.Get());
                        } else if (writes(use.access)) {
                            unguardedWrites.insert_or_assign(use.buffer.Get(),
                                UnguardedWrite{ use.buffer, record.command, record.where });
                        }
                    }
                    continue;
                }

                if (!use.image.Alive()) continue;
                trackedImages.emplace(use.image.Get(), use.image);

                // An absent count means "the rest of the image", so it is measured from the base
                // rather than from zero. Resolving it to the image's *total* count instead walked
                // past the last level whenever a base was given without one.
                const auto baseMip = use.baseMipLevel.value_or(0);
                const auto mipCount = use.levelCount.value_or(use.image->MipLevels() - baseMip);
                const auto baseLayer = use.baseArrayLayer.value_or(0);
                const auto layerCount = use.layerCount.value_or(use.image->ArrayLayers() - baseLayer);

                // Per subresource: a range can straddle subresources sitting in different
                // states — right after GenerateMipmaps the last mip is still TransferDst while
                // the rest are TransferSrc — and one of them needing a transition does not mean
                // all of them do.
                bool needed = false;
                const Established* blocker = nullptr;
                for (auto mip = baseMip; mip < baseMip + mipCount; ++mip) {
                    for (auto layer = baseLayer; layer < baseLayer + layerCount; ++layer) {
                        const Key key{ use.image.Get(), mip, layer };
                        const auto current = state.find(key);
                        const auto previous = current == state.end()
                            ? use.image->TrackedAccess(mip, layer)
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
                        reportIntraPass(use.image.Name(), *blocker, record);
                    } else {
                        batchFor(i).images.emplace_back(use.image, use.access, baseMip, mipCount, baseLayer, layerCount);
                    }
                }
                for (auto mip = baseMip; mip < baseMip + mipCount; ++mip) {
                    for (auto layer = baseLayer; layer < baseLayer + layerCount; ++layer) {
                        const Key key{ use.image.Get(), mip, layer };
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
                    const auto name = write.buffer.Name();
                    CommandBuffer::RecordError(Error{
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
                image->second->SetTrackedAccess(access, key.mip, key.layer);
            } else if (const auto buffer = trackedBuffers.find(key.resource); buffer != trackedBuffers.end()) {
                buffer->second->SetTrackedAccess(access);
            }
        }

        if (pending.empty()) return;

        // Merged in one pass into a fresh vector rather than spliced in place.
        //
        // Splicing read better — insert each barrier at its index, back to front so the earlier
        // indices stay valid — but every insert shifts the tail of the vector, and a Record is not
        // cheap to move: a std::function, a vector of ResourceRefs, a source_location. A recording
        // where most commands need a barrier therefore cost O(n²) moves, which is not a corner
        // case: back-to-back dispatches over one storage buffer are the normal shape of an
        // iterative GPU algorithm, and each one write-after-writes the last. An odd-even
        // transposition sort at a few thousand passes spent most of a second here.
        //
        // batchFor() only ever appends with a non-decreasing `at` — inside a pass every barrier
        // hoists to the index that opened it, and outside one `at` is the current index — so the
        // batches are already in order and a single merge walk is enough.
        std::vector<Record> merged;
        merged.reserve(_records.size() + pending.size());

        std::size_t next = 0;
        for (auto& batch : pending) {
            while (next < batch.at) merged.push_back(std::move(_records[next++]));

            if (batch.buffers.empty() && batch.images.empty()) continue;
            merged.push_back(Record{
                .emit = [this, buffers = std::move(batch.buffers), images = std::move(batch.images)]() mutable {
                    DoBarrier(std::move(buffers), std::move(images));
                },
                .pass = PassEdge::eNone,
                .command = "Barrier",
            });
        }
        while (next < _records.size()) merged.push_back(std::move(_records[next++]));

        _records = std::move(merged);
    }

    // The backends advance _state from inside their do* implementations so their own emit-time
    // decisions (which bind point a descriptor set belongs to, which dynamic states a draw still
    // needs defaults for) see the values in force at *that* point in the sequence rather than at the
    // end of recording. That replay only lands correctly if it starts from the same blank slate
    // recording did, which is what this restores.
    void CommandBuffer::ResetTrackedState()
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

    void CommandBuffer::EmitRecords()
    {
        // Rewind the mirrored state so the backends' own replay of it starts where recording
        // did; without this the first emitted command still sees the end-of-recording values.
        ResetTrackedState();
        _emitting = true;
        // Index rather than iterate: an emit closure may enqueue (which runs in place and does
        // not append while _emitting), but Run()'s lambda can reach code paths that append
        // before the flag is observed. Indexing survives a reallocation either way.
        for (std::size_t i = 0; i < _records.size(); ++i) {
            if (_records[i].emit) _records[i].emit();
        }
        _emitting = false;
        // Counted after the walk so that anything appended during it is included, which is what
        // the barriers the resolver inserted and Run()'s lambda can both amount to.
        _lastFrameCommandCount = _records.size();
        _records.clear();
    }

    std::optional<Error> CommandBuffer::MissingTransferUsage(const std::vector<ResourceUse>& uses,
                                                             const char* command,
                                                             const std::source_location where)
    {
        for (const auto& use : uses) {
            const bool asSource = use.access == ResourceAccess::eTransferSrc;
            const bool asDestination = use.access == ResourceAccess::eTransferDst;
            if (!asSource && !asDestination) continue;

            const char* flag = asSource ? "eTransferSrc" : "eTransferDst";
            const char* role = asSource ? "read from" : "written to";

            // Both kinds carry the same two flags under the same names, so one message serves both
            // and simply says which resource it is talking about.
            const auto complain = [&](const char* kind) {
                return Error{
                    .code = ErrorCode::eInvalidArgument,
                    .message = std::format(
                        "{} would have this {} {} as a transfer, but it was not created with "
                        "Usage::{}. Include it in the set setUsage() names where the {} is built — "
                        "setUsage replaces the default roles rather than adding to them, so the "
                        "transfer flags have to be named alongside the others: "
                        ".setUsage(... | kor::{}::Usage::{}).",
                        command, kind, role, flag, kind, kind, flag),
                    .where = where,
                };
            };

            if (use.buffer.Alive() && use.buffer.Valid()) {
                const auto usage = use.buffer->UsageFlags();
                if (!(usage & (asSource ? Buffer::Usage::eTransferSrc : Buffer::Usage::eTransferDst)))
                    return complain("Buffer");
            }
            if (use.image.Alive() && use.image.Valid()) {
                const auto usage = use.image->UsageFlags();
                if (!(usage & (asSource ? Image::Usage::eTransferSrc : Image::Usage::eTransferDst)))
                    return complain("Image");
            }
        }
        return std::nullopt;
    }

    CommandBuffer& CommandBuffer::RecordError(const ErrorCode code, std::string message)
    {
        return RecordError(Error{ .code = code, .message = std::move(message) });
    }

    CommandBuffer& CommandBuffer::RecordError(Error error)
    {
        // History() rather than ToString(): when a command fails because a resource is unusable,
        // the line the user needs is the root cause (the shader that would not compile), not the
        // symptom (the pipeline that could not be bound).
        kor::log::Error("[command] {}", error.History());
        _errors.push_back(std::move(error));
        _failed = true;
        return *this;
    }

    VoidResult CommandBuffer::Outcome() const
    {
        if (_errors.empty()) return {};
        return std::unexpected(_errors.front());
    }

    void CommandBuffer::StateBeginRendering(const kor::ResourceRef<const Framebuffer>& framebuffer)
    {
        _state.boundFramebuffer = framebuffer;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
        _state.viewportSet = false;
        _state.scissorSet = false;
    }

    CommandBuffer& CommandBuffer::BeginRendering(const RenderInfo& renderInfo, const std::source_location where)
    {
        if (_failed) return *this;

        const auto framebuffer = renderInfo.Target();
        if (Reject(framebuffer, "framebuffer")) return *this;
        StateBeginRendering(framebuffer);

        // The attachments, as uses rather than as the hand-rolled barrier the backend used to
        // emit. Declaring them means they batch with whatever else the pass needs, and all of
        // it lands in front of the pass instead of illegally inside it.
        std::vector<ResourceUse> uses;
        for (const auto& attachment : framebuffer->ColorAttachments()) {
            uses.push_back(ResourceUse{ .image = attachment.view->SourceImage(), .access = ResourceAccess::eColorAttachment });
        }
        // Depth and stencil are declared as one use per *image*, at the combined
        // depth/stencil layout, rather than one per attachment slot.
        //
        // A combined format (D32_S8, D24_S8) is one image serving both slots, which is exactly
        // what the default framebuffer is. Declaring it twice was wrong three times over: the
        // resolver saw two writes to one subresource in one record and reported the pass as
        // sampling its own attachment; the barrier it emitted carried the whole format's aspect
        // mask (depth|stencil) with a depth-only layout, which Vulkan forbids outright; and the
        // layout it left the image in was not the one BeginRendering then declares.
        //
        // One layout per image is also the only thing the tracker can represent — its key is
        // image + level + layer, with no aspect — and the combined layout is legal for a
        // depth-only or stencil-only image too, so nothing is given up by using it everywhere.
        const auto declareDepthStencil = [&](const ResourceRef<const ImageView>& attachment) {
            if (!attachment.Valid()) return;
            auto image = attachment->SourceImage();
            for (const auto& use : uses) {
                if (use.image.Get() == image.Get()) return;  // the other slot, same image
            }
            uses.push_back(ResourceUse{ .image = std::move(image),
                                        .access = ResourceAccess::eDepthStencilAttachment });
        };
        if (framebuffer->HasDepthAttachment())   declareDepthStencil(framebuffer->DepthAttachment());
        if (framebuffer->HasStencilAttachment()) declareDepthStencil(framebuffer->StencilAttachment());

        // Whatever this pass did not say is taken from the framebuffer *now*, while it is in hand,
        // and travels with the record. @see RenderInfo::ResolveClearValues
        RenderInfo resolved = renderInfo;
        resolved.ResolveClearValues(*framebuffer);

        return Enqueue("BeginRendering", where, std::move(uses), PassEdge::eOpens,
            [this, resolved = std::move(resolved)] { DoBeginRendering(resolved); });
    }

    void CommandBuffer::StateEndRendering()
    {
        _state.boundFramebuffer = std::nullopt;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
    }

    CommandBuffer& CommandBuffer::EndRendering()
    {
        StateEndRendering();
        return DoEndRendering();
    }

    CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot set the viewport without a graphics pipeline bound.");
        _state.viewportSet = true;
        return DoSetViewport(x, y, width, height);
    }

    CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot set the scissor without a graphics pipeline bound.");
        _state.scissorSet = true;
        return DoSetScissor(x, y, width, height);
    }

    // ---- Dynamic state --------------------------------------------------
    // One shape for all of them: refuse the call without a graphics pipeline bound, mark the
    // tracking bit that stops applyDynamicDefaults stamping the pipeline's own value over this
    // one, then hand the emit to the backend. Backends implement only the do* half, so neither
    // the guard nor the bit can be forgotten by one of them.
#define KORAL_DYNAMIC_STATE_SETTER_GUARD(bit, name)                                            \
        if (!_state.boundGraphicsPipeline.has_value())                                       \
            return RecordError(ErrorCode::eNoGraphicsPipelineBound,                               \
                "Cannot set " name " without a graphics pipeline bound.");                   \
        _state.dynamicStateSet |= DynamicState::bit;

    CommandBuffer& CommandBuffer::SetLineWidth(const float lineWidth)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eLineWidth, "line width") return DoSetLineWidth(lineWidth); }

    CommandBuffer& CommandBuffer::SetDepthBias(const float constantFactor, const float clamp, const float slopeFactor)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthBias, "depth bias") return DoSetDepthBias(constantFactor, clamp, slopeFactor); }

    CommandBuffer& CommandBuffer::SetBlendConstants(const glm::vec4 constants)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eBlendConstants, "blend constants") return DoSetBlendConstants(constants); }

    CommandBuffer& CommandBuffer::SetStencilCompareMask(const StencilFace face, const glm::u32 compareMask)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilCompareMask, "stencil compare mask") return DoSetStencilCompareMask(face, compareMask); }

    CommandBuffer& CommandBuffer::SetStencilWriteMask(const StencilFace face, const glm::u32 writeMask)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilWriteMask, "stencil write mask") return DoSetStencilWriteMask(face, writeMask); }

    CommandBuffer& CommandBuffer::SetStencilReference(const StencilFace face, const glm::u32 reference)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilReference, "stencil reference") return DoSetStencilReference(face, reference); }

    CommandBuffer& CommandBuffer::SetCullMode(const Flags<CullMode> cullMode)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eCullMode, "cull mode") return DoSetCullMode(cullMode); }

    CommandBuffer& CommandBuffer::SetFrontFace(const FrontFace frontFace)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eFrontFace, "front face") return DoSetFrontFace(frontFace); }

    CommandBuffer& CommandBuffer::SetDepthTestEnable(const bool enable)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthTestEnable, "depth test enable") return DoSetDepthTestEnable(enable); }

    CommandBuffer& CommandBuffer::SetDepthWriteEnable(const bool enable)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthWriteEnable, "depth write enable") return DoSetDepthWriteEnable(enable); }

    CommandBuffer& CommandBuffer::SetDepthCompareOp(const CompareOp compareOp)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthCompareOp, "depth compare op") return DoSetDepthCompareOp(compareOp); }

    CommandBuffer& CommandBuffer::SetStencilTestEnable(const bool enable)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilTestEnable, "stencil test enable") return DoSetStencilTestEnable(enable); }

    CommandBuffer& CommandBuffer::SetStencilOp(const StencilFace face, const StencilOp failOp, const StencilOp passOp, const StencilOp depthFailOp, const CompareOp compareOp)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eStencilOp, "stencil op") return DoSetStencilOp(face, failOp, passOp, depthFailOp, compareOp); }

    CommandBuffer& CommandBuffer::SetDepthBiasEnable(const bool enable)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eDepthBiasEnable, "depth bias enable") return DoSetDepthBiasEnable(enable); }

    CommandBuffer& CommandBuffer::SetRasterizerDiscardEnable(const bool enable)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(eRasterizerDiscardEnable, "rasterizer discard enable") return DoSetRasterizerDiscardEnable(enable); }

    CommandBuffer& CommandBuffer::SetPrimitiveRestartEnable(const bool enable)
    { KORAL_DYNAMIC_STATE_SETTER_GUARD(ePrimitiveRestartEnable, "primitive restart enable") return DoSetPrimitiveRestartEnable(enable); }

#undef KORAL_DYNAMIC_STATE_SETTER_GUARD

    void CommandBuffer::ApplyDynamicDefaults()
    {
        if (!_state.boundGraphicsPipeline.has_value()) return;
        const auto& pipeline = *_state.boundGraphicsPipeline.value();
        const RasterizationState& rs = pipeline.Rasterization();
        const DepthStencilState&  ds = pipeline.DepthStencil();
        const ColorBlendState&    cb = pipeline.ColorBlend();
        const InputAssemblyState& ia = pipeline.InputAssembly();

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

    namespace {
        // Whether a set bound under one layout still stands under another: the same bindings, each
        // of the same type and count (Binding's own interface comparison). Stages are left out on
        // purpose: the backend's layouts are visible to every stage, which is what lets a set built
        // for a compute pipeline be bound to a graphics one — and people do exactly that.
        bool compatibleLayouts(const DescriptorSetLayout& a, const DescriptorSetLayout& b) {
            if (&a == &b) return true;
            return std::ranges::equal(a.Bindings(), b.Bindings(), [](const auto& l, const auto& r) {
                return l.first == r.first && l.second == r.second;
            });
        }

        // Binding a different pipeline does not unbind descriptor sets. Vulkan keeps every set whose
        // layout the new pipeline shares, up to the first one it does not, and the GPU goes on
        // reading them — so the sets tracked here must follow the same rule. Forgetting them all
        // (as this once did) left the draws after a pipeline switch with no recorded uses, and the
        // barriers their images needed were never emitted.
        void keepCompatibleSets(std::map<glm::u32, kor::ResourceRef<const DescriptorSet>>& sets, const Pipeline& pipeline) {
            for (auto it = sets.begin(); it != sets.end(); ++it) {
                const auto wanted = pipeline.SetLayoutRef(it->first);
                const auto& set = it->second;
                const bool stands = wanted.Alive() && set.Alive() && !set.Poisoned() && set->Layout().Alive()
                                 && compatibleLayouts(*set->Layout(), *wanted);
                if (!stands) {
                    sets.erase(it, sets.end());  // an incompatible set disturbs every set after it
                    return;
                }
            }
        }
    }

    void CommandBuffer::StateBindComputePipeline(const kor::ResourceRef<const ComputePipeline>& pipeline)
    {
        // Get(), not &*: comparing identity must not dereference.
        // Each bind point keeps its own sets, whatever the others do in between; what a new
        // pipeline disturbs is decided by layout, as Vulkan decides it.
        if (!_state.boundComputePipeline.has_value() || _state.boundComputePipeline->Get() != pipeline.Get()) {
            if (pipeline.Alive() && !pipeline.Poisoned()) keepCompatibleSets(_state.boundComputeDescriptorSets, *pipeline);
            else _state.boundComputeDescriptorSets.clear();
        }
        _state.boundComputePipeline = pipeline;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.boundRayTracingPipeline = std::nullopt;
    }

    CommandBuffer& CommandBuffer::BindComputePipeline(kor::ResourceRef<const ComputePipeline> pipeline, const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(pipeline, "compute pipeline")) return *this;

        StateBindComputePipeline(pipeline);
        return Enqueue("BindComputePipeline", where, {}, PassEdge::eNone,
            [this, pipeline] { DoBindComputePipeline(pipeline); });
    }

    void CommandBuffer::StateBindGraphicsPipeline(const kor::ResourceRef<const GraphicsPipeline>& pipeline)
    {
        // Get(), not &*: comparing identity must not dereference.
        // Each bind point keeps its own sets, whatever the others do in between; what a new
        // pipeline disturbs is decided by layout, as Vulkan decides it.
        if (!_state.boundGraphicsPipeline.has_value() || _state.boundGraphicsPipeline->Get() != pipeline.Get()) {
            if (pipeline.Alive() && !pipeline.Poisoned()) keepCompatibleSets(_state.boundGraphicsDescriptorSets, *pipeline);
            else _state.boundGraphicsDescriptorSets.clear();
        }
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
        if (Reject(pipeline, "graphics pipeline")) return *this;

        StateBindGraphicsPipeline(pipeline);
        return Enqueue("BindGraphicsPipeline", where, {}, PassEdge::eNone,
            [this, pipeline] { DoBindGraphicsPipeline(pipeline); });
    }

    void CommandBuffer::StateBindRayTracingPipeline(const kor::ResourceRef<const RayTracingPipeline>& pipeline)
    {
        // Get(), not &*: comparing identity must not dereference.
        // Each bind point keeps its own sets, whatever the others do in between; what a new
        // pipeline disturbs is decided by layout, as Vulkan decides it.
        if (!_state.boundRayTracingPipeline.has_value() || _state.boundRayTracingPipeline->Get() != pipeline.Get()) {
            if (pipeline.Alive() && !pipeline.Poisoned()) keepCompatibleSets(_state.boundRayTracingDescriptorSets, *pipeline);
            else _state.boundRayTracingDescriptorSets.clear();
        }
        _state.boundRayTracingPipeline = pipeline;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
    }

    CommandBuffer& CommandBuffer::BindRayTracingPipeline(kor::ResourceRef<const RayTracingPipeline> pipeline, const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(pipeline, "ray tracing pipeline")) return *this;

        StateBindRayTracingPipeline(pipeline);
        return Enqueue("BindRayTracingPipeline", where, {}, PassEdge::eNone,
            [this, pipeline] { DoBindRayTracingPipeline(pipeline); });
    }

    CommandBuffer& CommandBuffer::TraceRays(const glm::u32 width, const glm::u32 height, const glm::u32 depth, const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundRayTracingPipeline.has_value())
            return RecordError(ErrorCode::eNoRayTracingPipelineBound, "Cannot trace rays without a ray-tracing pipeline bound.");
        return DoTraceRays(width, height, depth, where);
    }

    CommandBuffer& CommandBuffer::DoTraceRays(glm::u32, glm::u32, glm::u32, std::source_location)
    {
        return RecordError(ErrorCode::eRayTracingUnsupported, "Ray tracing is not supported on this backend.");
    }

    CommandBuffer& CommandBuffer::DoBindRayTracingPipeline(kor::ResourceRef<const RayTracingPipeline>)
    {
        return RecordError(ErrorCode::eRayTracingUnsupported, "Ray tracing is not supported on this backend.");
    }

    // A backend without debug-marker support ignores these; the do* defaults are the no-ops.
    CommandBuffer& CommandBuffer::BeginDebugLabel(const std::string& label, const glm::vec4 color) { return DoBeginDebugLabel(label, color); }
    CommandBuffer& CommandBuffer::EndDebugLabel() { return DoEndDebugLabel(); }
    CommandBuffer& CommandBuffer::InsertDebugLabel(const std::string& label, const glm::vec4 color) { return DoInsertDebugLabel(label, color); }

    // ---- GPU timers ---------------------------------------------------------------------------
    //
    // A scope owns two query slots, 2i and 2i+1 for the i-th scope opened. Allocating them at
    // record time is what keeps the emit closures trivial — each one writes a slot it was handed —
    // and it is safe because the emit walk visits the records in the order they were made, so slot
    // order and timestamp order agree even after the resolver has interleaved barriers between them.

    CommandBuffer& CommandBuffer::BeginTimer(std::string label, const std::source_location where)
    {
        if (_failed) return *this;
        // Nothing to measure with. Silently inert rather than an error: a scene that times itself
        // should still run on a queue that cannot timestamp.
        if (!SupportsTimers()) return *this;
        // Recorded from inside another command's emit callback — a Run() lambda reaching back into
        // the API. Too late for a scope: the query slots this would need were counted and reset
        // before the walk began, so its timestamps would be written into queries nothing prepared.
        if (_emitting) return *this;

        if (_pendingTimers.size() >= MaxTimerScopes) {
            return RecordError(Error{
                .code = ErrorCode::eInvalidArgument,
                .message = std::format("Cannot open the timer '{}': a recording may open at most {} timer scopes.",
                                       label, MaxTimerScopes),
                .where = where,
            });
        }

        const auto scope = static_cast<glm::u32>(_pendingTimers.size());
        _pendingTimers.push_back(TimerScope{
            .label = std::move(label),
            .depth = static_cast<glm::u32>(_timerStack.size()),
            .where = where,
        });
        _timerStack.push_back(scope);

        return Enqueue("BeginTimer", where, {}, PassEdge::eNone,
                       [this, scope] { DoWriteTimerTimestamp(scope * 2); });
    }

    CommandBuffer& CommandBuffer::EndTimer(const std::source_location where)
    {
        if (_failed) return *this;
        if (!SupportsTimers()) return *this;
        if (_emitting) return *this;   // paired with the same guard in BeginTimer

        if (_timerStack.empty()) {
            return RecordError(Error{
                .code = ErrorCode::eInvalidArgument,
                .message = "EndTimer without a matching BeginTimer.",
                .where = where,
            });
        }

        const auto scope = _timerStack.back();
        _timerStack.pop_back();

        return Enqueue("EndTimer", where, {}, PassEdge::eNone,
                       [this, scope] { DoWriteTimerTimestamp(scope * 2 + 1); });
    }

    bool CommandBuffer::CollectTimers()
    {
        // Already collected: the results are sitting in _timings and _submittedTimers was emptied
        // when they landed. Says yes so a repeated collectTimer keeps working.
        if (_submittedTimers.empty()) return !_timings.empty();

        std::vector<double> milliseconds;
        // Not ready is not an error — the results simply stay as they were, which keeps a
        // profiler's readings steady instead of flickering to nothing.
        if (!DoReadTimerTimestamps(static_cast<glm::u32>(_submittedTimers.size()), milliseconds)
            || milliseconds.size() != _submittedTimers.size())
            return false;

        _timings.clear();
        _timings.reserve(_submittedTimers.size());
        for (std::size_t i = 0; i < _submittedTimers.size(); ++i) {
            _timings.push_back(TimerResult{
                .label = _submittedTimers[i].label,
                .milliseconds = milliseconds[i],
                .depth = _submittedTimers[i].depth,
            });
        }
        _submittedTimers.clear();
        return true;
    }

    void CommandBuffer::RetireTimers()
    {
        // The scopes recorded last time round, now that the GPU has had a whole cycle of frames in
        // flight to finish them.
        CollectTimers();

        _pendingTimers.clear();
        _timerStack.clear();
    }

    const std::vector<TimerResult>& CommandBuffer::CollectTimings()
    {
        CollectTimers();
        return _timings;
    }

    Result<double> CommandBuffer::CollectTimer(const std::string_view label)
    {
        if (!SupportsTimers())
            return Fail(ErrorCode::eInvalidArgument,
                        "Cannot read the timer '{}': this command buffer's queue cannot timestamp.", label);

        // Distinguishes the two ways there can be no answer, because the caller's fix differs: work
        // still in flight needs a WaitForFence or another try, a name that was never recorded needs
        // the code changed.
        const bool ready = CollectTimers();

        for (const auto& timing : _timings) {
            if (timing.label == label) return timing.milliseconds;
        }

        if (!ready)
            return Fail(ErrorCode::eInvalidArgument,
                        "The timer '{}' has no result yet: the GPU has not finished the work it "
                        "measures. Wait for the submission to complete (WaitForFence) or ask again later.", label);

        return Fail(ErrorCode::eInvalidArgument,
                    "No timer named '{}' was recorded in the last submission.", label);
    }

    void CommandBuffer::SubmitTimers()
    {
        // An unclosed scope has a begin timestamp and no end, so it can never resolve. Say so at
        // End(), where the whole recording is visible, rather than letting it silently vanish —
        // unless the recording already failed, which is why the scope was left open and has been
        // reported once already.
        if (!_failed) {
            for (const auto scope : _timerStack) {
                RecordError(Error{
                    .code = ErrorCode::eInvalidArgument,
                    .message = std::format("The timer '{}' was never closed with EndTimer.", _pendingTimers[scope].label),
                    .where = _pendingTimers[scope].where,
                });
            }
        }
        _timerStack.clear();

        // Replaced outright, never merged. This recording reuses the same query slots from zero, so
        // whatever the last one left in them is gone whether or not it was ever collected — and
        // keeping its labels around would pin the new timestamps to the old scopes.
        //
        // Note what this does *not* touch: the results already published in _timings. A recording
        // that opens no scope leaves nothing to collect, and the last real reading stands.
        _submittedTimers.clear();
        // Only a complete set can be read back, so a recording that failed publishes nothing: its
        // scopes may have a begin timestamp and no end.
        if (_errors.empty())
            _submittedTimers = std::move(_pendingTimers);
        _pendingTimers.clear();
    }

    void CommandBuffer::StateBindMesh(const kor::ResourceRef<const Mesh>& mesh)
    {
        _state.boundMesh = mesh;
    }

    CommandBuffer& CommandBuffer::BindMesh(kor::ResourceRef<const Mesh> mesh, const std::source_location where) {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot bind a mesh without a graphics pipeline bound.");
        if (Reject(mesh, "mesh")) return *this;

        StateBindMesh(mesh);
        return Enqueue("BindMesh", where, {}, PassEdge::eNone,
            [this, mesh] { DoBindMesh(mesh); });
    }

    glm::uvec2 CommandBuffer::DefaultViewportExtent() const
    {
        // The framebuffer being rendered into, not the window.
        //
        // These are the same thing only when the pass targets the screen. A pass that renders into a
        // target of its own — a viewport's image, a shadow map, a reflection — would otherwise be
        // rasterised at the window's size and show a crop of a picture drawn for a surface it is not:
        // the symptom is a scene that looks "zoomed in" inside a small target and ignores its size.
        if (_state.boundFramebuffer.has_value() && _state.boundFramebuffer->Valid()) {
            if (const auto extent = (*_state.boundFramebuffer)->Extent(); extent.x > 0 && extent.y > 0)
                return extent;
        }
        // No pass, or one whose framebuffer says nothing: the window is the only size left to assume,
        // and it is the right one for the default framebuffer.
        if (const auto* window = detail::CurrentWindowOrNull()) return window->Extent();
        return { 1, 1 };
    }

    CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, const glm::u32 instanceCount, const glm::u32 firstVertex, const glm::u32 firstInstance, const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");

        // The defaulted vertex count means "as many as the bound mesh holds", resolved here so
        // both backends are handed a number rather than each working the sentinel out again.
        if (vertexCount == WholeSize) {
            if (!_state.boundMesh.has_value())
                return RecordError(ErrorCode::eNoMeshBound, "Cannot draw with the default vertex count: no mesh is bound to take it from.");
            vertexCount = _state.boundMesh.value()->VertexCount();
        }

        EnsureViewportAndScissor();
        return DoDraw(vertexCount, instanceCount, firstVertex, firstInstance, where);
    }

    CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, const glm::u32 instanceCount, const glm::u32 firstIndex, const glm::i32 vertexOffset, const glm::u32 firstInstance, const std::source_location where) {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (!_state.boundMesh.has_value())
            return RecordError(ErrorCode::eNoMeshBound, "Cannot draw indexed without a mesh bound.");
        if (!_state.boundMesh.value()->HasIndexBuffer())
            return RecordError(ErrorCode::eMeshHasNoIndexBuffer, "Cannot draw indexed: the bound mesh has no index buffer.");

        if (indexCount == WholeSize)
            indexCount = _state.boundMesh.value()->IndexCount().value();

        EnsureViewportAndScissor();
        return DoDrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, where);
    }

    CommandBuffer & CommandBuffer::DrawMeshTasks(const glm::u32 taskCountX, const glm::u32 taskCountY, const glm::u32 taskCountZ, const std::source_location where) {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw mesh tasks without a graphics pipeline bound.");

        EnsureViewportAndScissor();
        return DoDrawMeshTasks(taskCountX, taskCountY, taskCountZ, where);
    }

    // A draw with no viewport or scissor of its own gets the whole target, which is what a
    // full-screen pass means and what every backend needed anyway. Recorded as ordinary Set calls
    // so the tracking bits and the emitted commands stay in step.
    void CommandBuffer::EnsureViewportAndScissor()
    {
        if (!_state.viewportSet) {
            const auto extent = DefaultViewportExtent();
            SetViewport(0, 0, extent.x, extent.y);
        }
        if (!_state.scissorSet) {
            const auto extent = DefaultViewportExtent();
            SetScissor(0, 0, extent.x, extent.y);
        }
    }

    CommandBuffer& CommandBuffer::Dispatch(const glm::u32 groupCountX, const glm::u32 groupCountY, const glm::u32 groupCountZ, const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundComputePipeline.has_value())
            return RecordError(ErrorCode::eNoComputePipelineBound, "Cannot dispatch without a compute pipeline bound.");
        return DoDispatch(groupCountX, groupCountY, groupCountZ, where);
    }

    // ---- Recording lifecycle --------------------------------------------------------------
    //
    // Everything both backends did identically lives here; each supplies only the API call that
    // finishes the job. Begin clears what the previous recording left behind, End resolves the
    // barriers now that the whole sequence is visible, and Reset drops both.

    CommandBuffer& CommandBuffer::Begin()
    {
        ResetErrors();
        ClearRecords();
        // Before the backend resets anything the results live in: re-recording is proof the GPU is
        // done with the last submission, so this is the earliest the timestamps can be read.
        RetireTimers();
        _recording = true;
        return DoBegin();
    }

    namespace {
        // Every resource carries the state the last End() left it in, which the next End() resolves
        // against — and Vulkan images carry their layouts, which emitting moves along. Two threads
        // ending command buffers at once would race on both. Recursive because emitting can run
        // user code (Run's lambda) that ends a command buffer of its own.
        std::recursive_mutex& resolveMutex() {
            static std::recursive_mutex mutex;
            return mutex;
        }
    }

    void CommandBuffer::End()
    {
        std::lock_guard lock(resolveMutex());
        // Nothing recorded so far has reached the GPU. Work out where the barriers belong now that
        // the whole sequence is visible; the backend then emits, or defers emitting to Submit.
        ResolveBarriers();
        DoEnd();
        _recording = false;
    }

    VoidResult CommandBuffer::Submit(const SubmitInfo& info)
    {
        return DoSubmit(info);
    }

    void CommandBuffer::Reset()
    {
        _state = {};
        ClearRecords();
        _recording = false;
        DoReset();
    }

    void CommandBuffer::WaitForFence() const
    {
        DoWaitForFence();
    }

    CommandBuffer& CommandBuffer::Run(const std::function<void(CommandBuffer&)>& command)
    {
        if (_failed) return *this;
        return DoRun(command);
    }

    CommandBuffer& CommandBuffer::PushConstantBlock(const void* data, const glm::u32 size, const glm::u32 offset)
    {
        if (_failed) return *this;
        return DoPushConstantBlock(data, size, offset);
    }


    kor::ResourceRef<const Image> CommandBuffer::ScreenImage()
    {
        const auto* window = detail::CurrentWindowOrNull();
        if (!window) return {};
        const auto framebuffer = window->DefaultFramebuffer();
        if (!framebuffer.Valid() || framebuffer->ColorAttachments().empty()) return {};
        return framebuffer->ColorImage(0);
    }

    bool CommandBuffer::HasTouched(const kor::ResourceRef<const Image>& image) const
    {
        if (!image.Alive()) return false;

        // Compared by what they point at: a use holds its own ref to the same image.
        const auto* target = image.Get();
        for (const auto& record : _records) {
            for (const auto& use : record.uses) {
                if (use.image.Alive() && use.image.Get() == target) return true;
            }
        }
        return false;
    }

    CommandBuffer& CommandBuffer::GenerateMipmaps(kor::ResourceRef<const Image> image)
    {
        if (_failed) return *this;
        if (Reject(image, "image")) return *this;

        // Mips are generated by blitting each level from the one above it, and a block-compressed
        // format cannot be blitted into — the hardware would have to decompress, filter and
        // re-encode. Said here rather than left to the backend, because the fix is upstream: a
        // compressed texture carries the mip chain it was encoded with. @see the image modules
        if (Image::IsBlockCompressed(image->PixelFormat()))
            return RecordError(ErrorCode::eInvalidArgument,
                "Mipmaps cannot be generated for a block-compressed image; encode the mip chain "
                "into the file instead.");

        return DoGenerateMipmaps(image);  // expands into Blit records, each declaring its own uses
    }

    // Default: blit each mip from the one above it. Vulkan uses this; GL overrides it with
    // glGenerateMipmap. Reached only through the wrapper above, so `image` is always usable.
    CommandBuffer& CommandBuffer::DoGenerateMipmaps(ResourceRef<const Image> image) {
        const auto& extent = image->Extent();
        const auto mipLevels = image->MipLevels();
        const auto arrayLayers = image->ArrayLayers();

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

    Token CommandBuffer::SingleTimeCommand(const std::function<void(kor::CommandBuffer &)> &command, const Usage usage) {
        // Earlier one-offs the GPU has since finished.
        detail::collectRetired();

        std::unique_ptr<CommandBuffer> commandBuffer = Create(usage);
        commandBuffer->Begin();
        command(*commandBuffer);
        commandBuffer->End();

        const Token done = Token::Create();
        if (auto submitted = commandBuffer->Submit({.signal = {done}}); !submitted) {
            kor::log::Error("[command] single-time command failed: {}", submitted.error().ToString());
        }
        detail::retireAfter(done, std::shared_ptr<CommandBuffer>(std::move(commandBuffer)));
        return done;
    }

    CommandBuffer& CommandBuffer::DrawMesh(kor::ResourceRef<const Mesh> mesh, const glm::u32 instanceCount, const glm::u32 baseInstance)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw a mesh without a graphics pipeline bound.");
        if (!_state.viewportSet) {
            const auto extent = DefaultViewportExtent();
            this->SetViewport(0, 0, extent.x, extent.y);
        }
        if (!_state.scissorSet) {
            const auto extent = DefaultViewportExtent();
            this->SetScissor(0, 0, extent.x, extent.y);
        }
        BindMesh(mesh);
        DrawIndexed(WholeSize, instanceCount, 0, 0, baseInstance);
        return *this;
    }

    CommandBuffer & CommandBuffer::DrawSubMesh(kor::ResourceRef<const Mesh> mesh, glm::u32 baseIndex, glm::u32 indexCount) {
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw a mesh without a graphics pipeline bound.");
        if (!_state.viewportSet) {
            const auto extent = DefaultViewportExtent();
            this->SetViewport(0, 0, extent.x, extent.y);
        }
        if (!_state.scissorSet) {
            const auto extent = DefaultViewportExtent();
            this->SetScissor(0, 0, extent.x, extent.y);
        }
        BindMesh(mesh);
        DrawIndexed(indexCount, 1, baseIndex, 0, 0);
        return *this;
    }

    std::unique_ptr<CommandBuffer> CommandBuffer::Create(const Flags<Usage> usage)
    {
        switch (Context::ActiveAPI()) {
        case API::eVulkan:
            {
                const auto& queue = vk::Context::Device().requestQueue(getQueueFlagsFromUsage(usage));
                return vk::Context::Device().requestCommandBuffer(queue);
            }
        default:
            throw std::runtime_error("Unknown API");
        }
    }

    void CommandBuffer::StateBindDescriptorSet(const glm::u32 index, const kor::ResourceRef<const DescriptorSet>& descriptorSet)
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
            if (Reject(barrier.TargetBuffer(), "barrier's buffer")) return *this;
        for (const auto& barrier : imageBarriers)
            if (Reject(barrier.TargetImage(), "barrier's image")) return *this;

        // Describe what this barrier *establishes*, so the resolver advances its tracking past
        // it and emits nothing of its own. A hand-written barrier therefore suppresses the
        // automatic one rather than being doubled by it — which is what keeps the escape hatch
        // for device-address buffers free of side effects.
        std::vector<ResourceUse> uses;
        uses.reserve(bufferBarriers.size() + imageBarriers.size());
        for (const auto& barrier : bufferBarriers) {
            uses.push_back(ResourceUse{
                .buffer = barrier.TargetBuffer(), .access = barrier.DstAccess(),
                .offset = barrier.Offset(), .size = barrier.size(),
            });
        }
        for (const auto& barrier : imageBarriers) {
            uses.push_back(ResourceUse{
                .image = barrier.TargetImage(), .access = barrier.DstAccess(),
                .baseMipLevel = barrier.BaseMipLevel(), .levelCount = barrier.LevelCount(),
                .baseArrayLayer = barrier.BaseArrayLayer(), .layerCount = barrier.LayerCount(),
            });
        }

        return Enqueue("Barrier", where, std::move(uses), PassEdge::eNone,
            [this, buffers = std::move(bufferBarriers), images = std::move(imageBarriers)]() mutable {
                DoBarrier(std::move(buffers), std::move(images));
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
            const glm::u32 mips = std::max(image.MipLevels(), 1u);
            const glm::u32 layers = std::max(image.ArrayLayers(), 1u);
            const glm::u32 firstMip = std::min(baseMip, mips - 1);
            const glm::u32 firstLayer = std::min(baseLayer, layers - 1);
            return {
                firstMip,   std::min(mipCount,   mips - firstMip),
                firstLayer, std::min(layerCount, layers - firstLayer),
            };
        }

        // Same idea for buffers: an offset past the end would name a range outside the
        // allocation. WholeSize keeps its "to the end" meaning.
        std::pair<glm::u64, glm::u64> clampToBuffer(const Buffer& buffer, const glm::u64 offset, const glm::u64 size)
        {
            const glm::u64 total = buffer.size();
            // Strictly inside the allocation: Vulkan requires offset < size, not <=.
            const glm::u64 start = total == 0 ? 0 : std::min(offset, total - 1);
            if (size == WholeSize) return { start, WholeSize };
            return { start, std::min(size, total - start) };
        }
    }

    // ---- Non-virtual interface: the gate ---------------------------------
    // Each of these validates, refuses unusable resources, updates the tracked state, works out
    // which resources the command touches, and then *records* it. Nothing reaches the backend
    // until End() resolves barriers and emits. The do* implementations may still assume every
    // resource they receive is alive and usable — that is what the validation here guarantees —
    // and additionally that whatever barriers they need have already been emitted ahead of them.

    CommandBuffer& CommandBuffer::BindDescriptorSet(const glm::u32 index, kor::ResourceRef<const DescriptorSet> descriptorSet,
                                                    const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(descriptorSet, "descriptor set")) return *this;
        StateBindDescriptorSet(index, descriptorSet);
        return Enqueue("BindDescriptorSet", where, {}, PassEdge::eNone,
            [this, index, descriptorSet] { DoBindDescriptorSet(index, descriptorSet); });
    }

    CommandBuffer& CommandBuffer::DispatchIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset,
                                                   const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundComputePipeline.has_value())
            return RecordError(ErrorCode::eNoComputePipelineBound, "Cannot dispatch without a compute pipeline bound.");
        if (Reject(indirectBuffer, "indirect buffer")) return *this;

        auto uses = UsesForBoundResources(false);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::eIndirectBuffer, .offset = offset });
        return Enqueue("DispatchIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset] { DoDispatchIndirect(indirectBuffer, offset); },
            /*transitions=*/false, BoundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::DrawIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride,
                                               const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (Reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, DefaultViewportExtent().x, DefaultViewportExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, DefaultViewportExtent().x, DefaultViewportExtent().y);

        auto uses = UsesForBoundResources(true);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::eIndirectBuffer, .offset = offset });
        return Enqueue("DrawIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset, drawCount, stride] { DoDrawIndirect(indirectBuffer, offset, drawCount, stride); },
            /*transitions=*/false, BoundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::DrawIndexedIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride,
                                                      const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (!_state.boundMesh.has_value())
            return RecordError(ErrorCode::eNoMeshBound, "Cannot draw indexed without a mesh bound.");
        if (Reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, DefaultViewportExtent().x, DefaultViewportExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, DefaultViewportExtent().x, DefaultViewportExtent().y);

        auto uses = UsesForBoundResources(true);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::eIndirectBuffer, .offset = offset });
        return Enqueue("DrawIndexedIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset, drawCount, stride] { DoDrawIndexedIndirect(indirectBuffer, offset, drawCount, stride); },
            /*transitions=*/false, BoundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::DrawMeshTasksIndirect(kor::ResourceRef<const Buffer> indirectBuffer, const glm::u64 offset, const glm::u32 drawCount, const glm::u32 stride,
                                                        const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundGraphicsPipeline.has_value())
            return RecordError(ErrorCode::eNoGraphicsPipelineBound, "Cannot draw without a graphics pipeline bound.");
        if (Reject(indirectBuffer, "indirect buffer")) return *this;

        if (!_state.viewportSet)
            SetViewport(0, 0, DefaultViewportExtent().x, DefaultViewportExtent().y);
        if (!_state.scissorSet)
            SetScissor(0, 0, DefaultViewportExtent().x, DefaultViewportExtent().y);

        auto uses = UsesForBoundResources(true);
        uses.push_back(ResourceUse{ .buffer = indirectBuffer, .access = ResourceAccess::eIndirectBuffer, .offset = offset });
        return Enqueue("DrawMeshTasksIndirect", where, std::move(uses), PassEdge::eNone,
            [this, indirectBuffer, offset, drawCount, stride] { DoDrawMeshTasksIndirect(indirectBuffer, offset, drawCount, stride); },
            /*transitions=*/false, BoundPipelineUsesDeviceAddresses());
    }

    CommandBuffer& CommandBuffer::ClearBuffer(kor::ResourceRef<const Buffer> buffer, const glm::u64 offset, const glm::u64 size,
                                              const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(buffer, "buffer")) return *this;
        const auto [start, span] = clampToBuffer(*buffer, offset, size);
        return Enqueue("ClearBuffer", where,
            { ResourceUse{ .buffer = buffer, .access = ResourceAccess::eTransferDst, .offset = start, .size = span } },
            PassEdge::eNone, [this, buffer, offset, size] { DoClearBuffer(buffer, offset, size); });
    }

    CommandBuffer& CommandBuffer::ClearColorImage(kor::ResourceRef<const Image> image, const glm::vec4 color,
                                                  const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(image, "image")) return *this;
        return Enqueue("ClearColorImage", where,
            { ResourceUse{ .image = image, .access = ResourceAccess::eTransferDst } },
            PassEdge::eNone, [this, image, color] { DoClearColorImage(image, color); });
    }

    CommandBuffer& CommandBuffer::FillBuffer(kor::ResourceRef<const Buffer> buffer, const void* data, const glm::u64 offset, const glm::u64 size,
                                             const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(buffer, "buffer")) return *this;

        // Copy the payload rather than the pointer: the caller's storage is very often a
        // temporary, and nothing reaches the GPU until End().
        const glm::u64 byteCount = size == WholeSize ? buffer->size() - offset : size;
        std::vector<std::byte> bytes(byteCount);
        if (data && byteCount) std::memcpy(bytes.data(), data, byteCount);

        const auto [start, span] = clampToBuffer(*buffer, offset, size);
        return Enqueue("FillBuffer", where,
            { ResourceUse{ .buffer = buffer, .access = ResourceAccess::eTransferDst, .offset = start, .size = span } },
            PassEdge::eNone,
            [this, buffer, bytes = std::move(bytes), offset, size] () mutable {
                DoFillBuffer(buffer, bytes.data(), offset, size);
            });
    }

    CommandBuffer& CommandBuffer::CopyBuffer(kor::ResourceRef<const Buffer> srcBuffer, kor::ResourceRef<const Buffer> dstBuffer, const glm::u64 size, const glm::u64 srcOffset, const glm::u64 dstOffset,
                                             const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(srcBuffer, "copy source buffer")) return *this;
        if (Reject(dstBuffer, "copy destination buffer")) return *this;
        const auto [srcStart, srcSpan] = clampToBuffer(*srcBuffer, srcOffset, size);
        const auto [dstStart, dstSpan] = clampToBuffer(*dstBuffer, dstOffset, size);
        return Enqueue("CopyBuffer", where, {
                ResourceUse{ .buffer = srcBuffer, .access = ResourceAccess::eTransferSrc, .offset = srcStart, .size = srcSpan },
                ResourceUse{ .buffer = dstBuffer, .access = ResourceAccess::eTransferDst, .offset = dstStart, .size = dstSpan },
            }, PassEdge::eNone,
            [this, srcBuffer, dstBuffer, size, srcOffset, dstOffset] { DoCopyBuffer(srcBuffer, dstBuffer, size, srcOffset, dstOffset); });
    }

    CommandBuffer& CommandBuffer::CopyBufferToImage(kor::ResourceRef<const Buffer> buffer, kor::ResourceRef<const Image> image, const kor::Copy copyInfo,
                                                    const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(buffer, "copy source buffer")) return *this;
        if (Reject(image, "copy destination image")) return *this;
        const auto range = clampToImage(*image, copyInfo.imageMipLevel, 1u,
                                        copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount);
        const auto [srcOffset, srcSize] = clampToBuffer(*buffer, copyInfo.bufferOffset, WholeSize);
        return Enqueue("CopyBufferToImage", where, {
                ResourceUse{ .buffer = buffer, .access = ResourceAccess::eTransferSrc, .offset = srcOffset, .size = srcSize },
                ResourceUse{ .image = image, .access = ResourceAccess::eTransferDst,
                             .baseMipLevel = range.baseMip, .levelCount = range.mipCount,
                             .baseArrayLayer = range.baseLayer, .layerCount = range.layerCount },
            }, PassEdge::eNone,
            [this, buffer, image, copyInfo] { DoCopyBufferToImage(buffer, image, copyInfo); });
    }

    CommandBuffer& CommandBuffer::CopyImageToBuffer(kor::ResourceRef<const Image> image, kor::ResourceRef<const Buffer> buffer, const kor::Copy copyInfo,
                                                    const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(image, "copy source image")) return *this;
        if (Reject(buffer, "copy destination buffer")) return *this;
        const auto range = clampToImage(*image, copyInfo.imageMipLevel, 1u,
                                        copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount);
        const auto [dstOffset, dstSize] = clampToBuffer(*buffer, copyInfo.bufferOffset, WholeSize);
        return Enqueue("CopyImageToBuffer", where, {
                ResourceUse{ .image = image, .access = ResourceAccess::eTransferSrc,
                             .baseMipLevel = range.baseMip, .levelCount = range.mipCount,
                             .baseArrayLayer = range.baseLayer, .layerCount = range.layerCount },
                ResourceUse{ .buffer = buffer, .access = ResourceAccess::eTransferDst, .offset = dstOffset, .size = dstSize },
            }, PassEdge::eNone,
            [this, image, buffer, copyInfo] { DoCopyImageToBuffer(image, buffer, copyInfo); });
    }

    CommandBuffer& CommandBuffer::BlitToScreen(kor::ResourceRef<const Image> srcImage, const kor::Blit blitInfo,
                                       const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(srcImage, "blit source image")) return *this;

        // The destination is the window's framebuffer image, which the backend resolves for itself
        // — but it is *declared* here all the same. Leaving it out made this the one way of drawing
        // to the screen that HasTouched() could not see, so the runtime decided the frame had not
        // touched the framebuffer and cleared it (@see the clear in engine.cpp) on top of the blit
        // that had just happened: a scene whose whole output is Blit(image) presented a blank
        // window. Declaring it also puts the destination in front of the barrier resolver rather
        // than relying solely on the backend's own transition, which is what the two-image
        // overload below has always done.
        std::vector<ResourceUse> uses {
            ResourceUse{ .image = srcImage, .access = ResourceAccess::eTransferSrc,
                         .baseMipLevel = blitInfo.srcMipLevel, .levelCount = 1u,
                         .baseArrayLayer = blitInfo.srcBaseArrayLayer, .layerCount = blitInfo.layerCount },
        };
        if (const auto screen = ScreenImage(); screen.Alive()) {
            uses.push_back(ResourceUse{ .image = screen, .access = ResourceAccess::eTransferDst,
                                        .baseMipLevel = blitInfo.dstMipLevel, .levelCount = 1u,
                                        .baseArrayLayer = blitInfo.dstBaseArrayLayer, .layerCount = blitInfo.layerCount });
        }

        return Enqueue("Blit", where, std::move(uses),
            PassEdge::eNone, [this, srcImage, blitInfo] { DoBlitToScreen(srcImage, blitInfo); });
    }

    CommandBuffer& CommandBuffer::Blit(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, const kor::Blit blitInfo,
                                       const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(srcImage, "blit source image")) return *this;
        if (Reject(dstImage, "blit destination image")) return *this;
        return Enqueue("Blit", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::eTransferSrc,
                             .baseMipLevel = blitInfo.srcMipLevel, .levelCount = 1u,
                             .baseArrayLayer = blitInfo.srcBaseArrayLayer, .layerCount = blitInfo.layerCount },
                ResourceUse{ .image = dstImage, .access = ResourceAccess::eTransferDst,
                             .baseMipLevel = blitInfo.dstMipLevel, .levelCount = 1u,
                             .baseArrayLayer = blitInfo.dstBaseArrayLayer, .layerCount = blitInfo.layerCount },
            }, PassEdge::eNone, [this, srcImage, dstImage, blitInfo] { DoBlit(srcImage, dstImage, blitInfo); });
    }

    CommandBuffer& CommandBuffer::CopyImage(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage,
                                            const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(srcImage, "copy source image")) return *this;
        if (Reject(dstImage, "copy destination image")) return *this;
        const auto& src = *srcImage;
        const auto& dst = *dstImage;
        if (src.PixelFormat() != dst.PixelFormat() || src.Extent() != dst.Extent() || src.MipLevels() != dst.MipLevels()
            || src.ArrayLayers() != dst.ArrayLayers() || src.Samples() != dst.Samples() || src.ImageType() != dst.ImageType()) {
            return RecordError(ErrorCode::eInvalidArgument, std::format(
                "CopyImage needs two images of the same shape; '{}' and '{}' differ in format, extent, "
                "mip levels, layers, samples or type. Blit copies between different ones.",
                srcImage.Name().empty() ? "<source>" : srcImage.Name(),
                dstImage.Name().empty() ? "<destination>" : dstImage.Name()));
        }
        return Enqueue("CopyImage", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::eTransferSrc },
                ResourceUse{ .image = dstImage, .access = ResourceAccess::eTransferDst },
            }, PassEdge::eNone, [this, srcImage, dstImage] { DoCopyImage(srcImage, dstImage); });
    }

    CommandBuffer& CommandBuffer::ResolveToScreen(kor::ResourceRef<const Image> srcImage, const kor::Resolve resolveInfo,
                                          const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(srcImage, "resolve source image")) return *this;

        // The window's framebuffer image, declared for the same reason as in Blit above.
        std::vector<ResourceUse> uses {
            ResourceUse{ .image = srcImage, .access = ResourceAccess::eTransferSrc,
                         .baseMipLevel = resolveInfo.srcMipLevel, .levelCount = 1u,
                         .baseArrayLayer = resolveInfo.srcBaseArrayLayer, .layerCount = resolveInfo.layerCount },
        };
        if (const auto screen = ScreenImage(); screen.Alive()) {
            uses.push_back(ResourceUse{ .image = screen, .access = ResourceAccess::eTransferDst,
                                        .baseMipLevel = resolveInfo.dstMipLevel, .levelCount = 1u,
                                        .baseArrayLayer = resolveInfo.dstBaseArrayLayer, .layerCount = resolveInfo.layerCount });
        }

        return Enqueue("Resolve", where, std::move(uses),
            PassEdge::eNone, [this, srcImage, resolveInfo] { DoResolveToScreen(srcImage, resolveInfo); });
    }

    CommandBuffer& CommandBuffer::Resolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, const kor::Resolve resolveInfo,
                                          const std::source_location where)
    {
        if (_failed) return *this;
        if (Reject(srcImage, "resolve source image")) return *this;
        if (Reject(dstImage, "resolve destination image")) return *this;
        return Enqueue("Resolve", where, {
                ResourceUse{ .image = srcImage, .access = ResourceAccess::eTransferSrc,
                             .baseMipLevel = resolveInfo.srcMipLevel, .levelCount = 1u,
                             .baseArrayLayer = resolveInfo.srcBaseArrayLayer, .layerCount = resolveInfo.layerCount },
                ResourceUse{ .image = dstImage, .access = ResourceAccess::eTransferDst,
                             .baseMipLevel = resolveInfo.dstMipLevel, .levelCount = 1u,
                             .baseArrayLayer = resolveInfo.dstBaseArrayLayer, .layerCount = resolveInfo.layerCount },
            }, PassEdge::eNone, [this, srcImage, dstImage, resolveInfo] { DoResolve(srcImage, dstImage, resolveInfo); });
    }
}
