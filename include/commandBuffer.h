//
// Created by radue on 2/21/2026.
//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <array>
#include <ranges>
#include <string>
#include <map>
#include <unordered_map>
#include "flags.h"
#include "api.h"
#include "error.h"

#include <glm/glm.hpp>

#include "sampler.h"
#include "structs.h"

namespace kor
{
    enum class Filter;
    class Buffer;
    class Image;
    class DescriptorSet;
    class GraphicsPipeline;
    class ComputePipeline;
    class RayTracingPipeline;
    class Framebuffer;
    class Mesh;

    enum class PipelineStage {
        // Compute
        Compute = 1 << 0,

        // Vertex pipeline
        VertexInput = 1 << 1,
        VertexShader = 1 << 2,
        FragmentShader = 1 << 3,
        EarlyFragmentTests = 1 << 4,
        LateFragmentTests = 1 << 5,
        ColorAttachmentOutput = 1 << 6,

        // Transfer
        Transfer = 1 << 7,

        // Bottom and top of pipe
        TopOfPipe = 1 << 8,
        BottomOfPipe = 1 << 9
    };

    enum class ResourceAccess {
        // Compute
        ComputeRead,         // COMPUTE_SHADER + SHADER_READ
        ComputeWrite,        // COMPUTE_SHADER + SHADER_WRITE
        ComputeReadWrite,    // COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Vertex pipeline
        VertexBuffer,        // VERTEX_INPUT + VERTEX_ATTRIBUTE_READ
        IndexBuffer,         // VERTEX_INPUT + INDEX_READ
        IndirectBuffer,      // DRAW_INDIRECT + INDIRECT_COMMAND_READ

        // Vertex shader
        VertexShaderRead,    // VERTEX_SHADER + SHADER_READ
        VertexShaderWrite,   // VERTEX_SHADER + SHADER_WRITE
        VertexShaderReadWrite, // VERTEX_SHADER + SHADER_READ | SHADER_WRITE

        // Fragment shader
        FragmentShaderRead,  // FRAGMENT_SHADER + SHADER_READ
        FragmentShaderWrite, // FRAGMENT_SHADER + SHADER_WRITE
        FragmentShaderReadWrite, // FRAGMENT_SHADER + SHADER_READ | SHADER_WRITE

        // Attachments
        ColorAttachment,         // COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE
        DepthStencilAttachment,  // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        DepthStencilRead,        // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ
        DepthAttachment,         // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        DepthRead,               // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ
        StencilAttachment,       // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        StencilRead,             // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ

        // Transfer
        TransferSrc,         // TRANSFER + TRANSFER_READ
        TransferDst,         // TRANSFER + TRANSFER_WRITE

        // General
        AllShaderRead,          // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ
        AllShaderWrite,         // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_WRITE
        AllShaderReadWrite,     // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Present
        Present,             // COLOR_ATTACHMENT_OUTPUT + 0 (no access mask needed)
    };

    class KORAL_API BufferBarrier {
    public:
        BufferBarrier(
            const kor::ResourceRef<const kor::Buffer> &buffer,
            ResourceAccess dstAccess,
            glm::u64 offset = 0,
            glm::u64 size = UINT64_MAX);

        [[nodiscard]] kor::ResourceRef<const kor::Buffer> getBuffer() const { return _buffer; }
        [[nodiscard]] ResourceAccess getDstAccess() const { return _dstAccess; }
        [[nodiscard]] glm::u64 getOffset() const { return _offset; }
        [[nodiscard]] glm::u64 getSize() const { return _size; }

    private:
        kor::ResourceRef<const kor::Buffer> _buffer;
        ResourceAccess _dstAccess;
        glm::u64 _offset;
        glm::u64 _size;
    };

    class KORAL_API ImageBarrier {
    public:
        ImageBarrier(
            const kor::ResourceRef<const kor::Image> &image,
            ResourceAccess dstAccess,
            std::optional<glm::u32> baseMipLevel = std::nullopt,
            std::optional<glm::u32> levelCount = std::nullopt,
            std::optional<glm::u32> baseArrayLayer = std::nullopt,
            std::optional<glm::u32> layerCount = std::nullopt);

        [[nodiscard]] kor::ResourceRef<const kor::Image> getImage() const { return _image; }
        [[nodiscard]] ResourceAccess getDstAccess() const { return _dstAccess; }
        [[nodiscard]] std::optional<glm::u32> getBaseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] std::optional<glm::u32> getLevelCount() const { return _levelCount; }
        [[nodiscard]] std::optional<glm::u32> getBaseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] std::optional<glm::u32> getLayerCount() const { return _layerCount; }

    private:
        kor::ResourceRef<const kor::Image> _image;
        ResourceAccess _dstAccess;
        std::optional<glm::u32> _baseMipLevel;
        std::optional<glm::u32> _levelCount;
        std::optional<glm::u32> _baseArrayLayer;
        std::optional<glm::u32> _layerCount;
    };

    class KORAL_API Blit {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };
        glm::ivec3 srcExtent = { -1, -1, -1 };
        glm::ivec3 dstOffset = { 0, 0, 0 };
        glm::ivec3 dstExtent = { -1, -1, -1 };
        glm::u32 srcBaseArrayLayer = 0;
        glm::u32 dstBaseArrayLayer = 0;
        glm::u32 layerCount = 1;
        glm::u32 srcMipLevel = 0;
        glm::u32 dstMipLevel = 0;
        kor::Filter filtering = kor::Filter::eNearest;
    };

    class KORAL_API Resolve {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };
        glm::ivec3 srcExtent = { -1, -1, -1 };
        glm::ivec3 dstOffset = { 0, 0, 0 };
        glm::ivec3 dstExtent = { -1, -1, -1 };
        glm::u32 srcBaseArrayLayer = 0;
        glm::u32 dstBaseArrayLayer = 0;
        glm::u32 layerCount = 1;
        glm::u32 srcMipLevel = 0;
        glm::u32 dstMipLevel = 0;
    };

    class KORAL_API Copy {
    public:
        glm::u64 bufferOffset = 0;
        glm::u64 bufferRowLength = 0;
        glm::u64 bufferImageHeight = 0;
        glm::ivec3 imageOffset = { 0, 0, 0 };
        glm::ivec3 imageExtent = { -1, -1, -1 };
        glm::u32 imageBaseArrayLayer = 0;
        glm::u32 imageLayerCount = 1;
        glm::u32 imageMipLevel = 0;
    };

    enum class LoadOperation {
        eLoad,
        eClear,
        eDontCare
    };

    enum class StoreOperation {
        eStore,
        eDontCare
    };

    class KORAL_API RenderParameters {
    public:
        kor::LoadOperation colorLoadOperation = kor::LoadOperation::eClear;
        kor::LoadOperation depthLoadOperation = kor::LoadOperation::eClear;
        kor::LoadOperation stencilLoadOperation = kor::LoadOperation::eClear;

        kor::StoreOperation colorStoreOperation = kor::StoreOperation::eStore;
        kor::StoreOperation depthStoreOperation = kor::StoreOperation::eStore;
        kor::StoreOperation stencilStoreOperation = kor::StoreOperation::eStore;
    };

    // Per-command GPU timing accumulated by a statistics-collecting command buffer.
    // Times are in milliseconds.
    struct CommandStatistics {
        glm::u64 count = 0;     ///< Number of times this command has been recorded/timed.
        double   avgMs  = 0.0;  ///< Running average GPU time.
        double   minMs  = 0.0;  ///< Minimum observed GPU time.
        double   maxMs  = 0.0;  ///< Maximum observed GPU time.
        double   totalMs = 0.0; ///< Accumulated GPU time.
    };

    // Selects which polygon face(s) a stencil setter affects. Mirrors Vulkan's
    // per-face stencil model so front and back can be configured independently.
    enum class StencilFace : glm::u8 {
        eFront = 1,
        eBack = 2,
        eFrontAndBack = 3,
    };

    // One flag per pipeline-derived dynamic state the command buffer tracks. A bit
    // is set once the state has a current value on the GPU (either applied from the
    // bound pipeline's default or overridden by an explicit Set* call) and cleared
    // when a new graphics pipeline is bound. See CommandBuffer::applyDynamicDefaults.
    enum class DynamicState : glm::u32 {
        eLineWidth              = 1 << 0,
        eDepthBias              = 1 << 1,
        eBlendConstants         = 1 << 2,
        eStencilCompareMask     = 1 << 3,
        eStencilWriteMask       = 1 << 4,
        eStencilReference       = 1 << 5,
        eCullMode               = 1 << 6,
        eFrontFace              = 1 << 7,
        eDepthTestEnable        = 1 << 8,
        eDepthWriteEnable       = 1 << 9,
        eDepthCompareOp         = 1 << 10,
        eStencilTestEnable      = 1 << 11,
        eStencilOp              = 1 << 12,
        eDepthBiasEnable        = 1 << 13,
        eRasterizerDiscardEnable = 1 << 14,
        ePrimitiveRestartEnable = 1 << 15,
    };

    class KORAL_API CommandBuffer
    {
    public:
        virtual ~CommandBuffer() = default;
        CommandBuffer& operator=(const CommandBuffer&) = delete;
        CommandBuffer(const CommandBuffer&) = delete;

        // ---- Statistics ---------------------------------------------------
        // Per-command GPU timing facility. Backends may populate _statistics /
        // _lastFrameCommandCount while recording; the base provides storage and
        // accessors so consumers (e.g. benchmarking/GUI) can read them uniformly.
        [[nodiscard]] bool isCollectingStatistics() const { return _collectingStatistics; }
        void setCollectingStatistics(const bool enabled) { _collectingStatistics = enabled; }
        [[nodiscard]] const std::unordered_map<std::string, CommandStatistics>& getStatistics() const { return _statistics; }
        [[nodiscard]] glm::u64 getLastFrameCommandCount() const { return _lastFrameCommandCount; }
        void resetStatistics() { _statistics.clear(); _lastFrameCommandCount = 0; }

        // ---- Error railway ------------------------------------------------
        // Recording uses a sticky-error model: a validation failure is recorded and
        // the command buffer enters a failed state in which later GPU ops become
        // no-ops. Chaining still returns CommandBuffer&; inspect the outcome with
        // ok()/result()/errors() and Submit()'s VoidResult after recording.
        [[nodiscard]] bool ok() const { return !_failed; }
        [[nodiscard]] VoidResult result() const;
        [[nodiscard]] const std::vector<Error>& errors() const { return _errors; }

        enum class Usage
        {
            eGraphics = 1 << 0,
            eCompute = 1 << 1,
            eTransfer = 1 << 2
        };

        virtual CommandBuffer& Begin() = 0;
        virtual void End() = 0;

        virtual CommandBuffer& BeginRendering(RenderParameters renderParameters = {});
        CommandBuffer& BeginRendering(ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters = {});
        virtual CommandBuffer& EndRendering();
        virtual CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);
        virtual CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);

        // ---- Dynamic state ------------------------------------------------
        // Per-draw overrides for the pipeline state that every GPU since 2015 can
        // change dynamically (Vulkan 1.0 core + extended-dynamic-state 1/2, all of
        // which are core in the 1.3 floor this engine targets). Each state has a
        // default baked into the bound pipeline; if you never call the setter, that
        // default is applied automatically before the draw. Calling a setter after
        // BindGraphicsPipeline overrides the default until the next pipeline bind.
        // All require a graphics pipeline to be bound.
        virtual CommandBuffer& SetLineWidth(float lineWidth);
        virtual CommandBuffer& SetDepthBias(float constantFactor, float clamp, float slopeFactor);
        virtual CommandBuffer& SetBlendConstants(glm::vec4 constants);
        virtual CommandBuffer& SetStencilCompareMask(StencilFace face, glm::u32 compareMask);
        virtual CommandBuffer& SetStencilWriteMask(StencilFace face, glm::u32 writeMask);
        virtual CommandBuffer& SetStencilReference(StencilFace face, glm::u32 reference);
        virtual CommandBuffer& SetCullMode(Flags<CullMode> cullMode);
        virtual CommandBuffer& SetFrontFace(FrontFace frontFace);
        virtual CommandBuffer& SetDepthTestEnable(bool enable);
        virtual CommandBuffer& SetDepthWriteEnable(bool enable);
        virtual CommandBuffer& SetDepthCompareOp(CompareOp compareOp);
        virtual CommandBuffer& SetStencilTestEnable(bool enable);
        virtual CommandBuffer& SetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp);
        virtual CommandBuffer& SetDepthBiasEnable(bool enable);
        virtual CommandBuffer& SetRasterizerDiscardEnable(bool enable);
        virtual CommandBuffer& SetPrimitiveRestartEnable(bool enable);
        CommandBuffer& BindComputePipeline(ResourceRef<const ComputePipeline> pipeline);
        CommandBuffer& BindGraphicsPipeline(ResourceRef<const GraphicsPipeline> pipeline);
        CommandBuffer& BindRayTracingPipeline(ResourceRef<const RayTracingPipeline> pipeline);
        CommandBuffer& BindDescriptorSet(glm::u32 index, ResourceRef<const DescriptorSet> descriptorSet, bool debug = false);
        CommandBuffer& BindMesh(ResourceRef<const Mesh> mesh);

        template<typename T> requires std::is_trivially_copyable_v<T>
        CommandBuffer& PushConstants(const T& data, const glm::u32 offset = 0) {
            return PushConstants(&data, sizeof(T), offset);
        }

        CommandBuffer& Barrier(std::vector<BufferBarrier> bufferBarriers = {}, std::vector<ImageBarrier> imageBarriers = {});

        // ---- Debug labels -------------------------------------------------
        // Named regions and single markers surfaced in GPU debuggers (RenderDoc,
        // Nsight, etc.). These are pure debugging aids: on backends or drivers
        // without marker support they are silently no-ops.
        virtual CommandBuffer& BeginDebugLabel(const std::string& label, glm::vec4 color = { 1.f, 1.f, 1.f, 1.f });
        virtual CommandBuffer& EndDebugLabel();
        virtual CommandBuffer& InsertDebugLabel(const std::string& label, glm::vec4 color = { 1.f, 1.f, 1.f, 1.f });

        virtual CommandBuffer& Dispatch(glm::u32 groupCountX = 1, glm::u32 groupCountY = 1, glm::u32 groupCountZ = 1) = 0;
        CommandBuffer& DispatchIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0);

        virtual CommandBuffer& TraceRays(glm::u32 width = 1, glm::u32 height = 1, glm::u32 depth = 1);

        virtual CommandBuffer& Draw(glm::u64 vertexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstVertex = 0, glm::u32 firstInstance = 0);
        virtual CommandBuffer& DrawIndexed(glm::u64 indexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstIndex = 0, glm::i32 vertexOffset = 0, glm::u32 firstInstance = 0);
        virtual CommandBuffer& DrawMeshTasks(glm::u32 taskCountX = 1, glm::u32 taskCountY = 1, glm::u32 taskCountZ = 1);

        CommandBuffer& DrawIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0);
        CommandBuffer& DrawIndexedIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0);
        CommandBuffer& DrawMeshTasksIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0);

        CommandBuffer& ClearBuffer(ResourceRef<const Buffer> buffer, glm::u64 offset = 0, glm::u64 size = UINT64_MAX);
        // Clears every mip/array level of a color image to a constant value. The image is
        // transitioned to a transfer-destination state automatically; a follow-up barrier
        // is still needed before sampling/rendering with it again.
        CommandBuffer& ClearColorImage(ResourceRef<const Image> image, glm::vec4 color = { 0.f, 0.f, 0.f, 1.f });
        CommandBuffer& FillBuffer(ResourceRef<const Buffer> buffer, void* data, glm::u64 offset = 0, glm::u64 size = UINT64_MAX);
        CommandBuffer& CopyBuffer(ResourceRef<const Buffer> srcBuffer, ResourceRef<const Buffer> dstBuffer, glm::u64 size = UINT64_MAX, glm::u64 srcOffset = 0, glm::u64 dstOffset = 0);

        CommandBuffer& Blit(ResourceRef<const Image> srcImage, kor::Blit blitInfo = {});
        CommandBuffer& Blit(ResourceRef<const Image> srcImage, ResourceRef<const Image> dstImage, kor::Blit blitInfo = {});
        CommandBuffer& Resolve(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo = {});
        CommandBuffer& Resolve(ResourceRef<const Image> srcImage, ResourceRef<const Image> dstImage, kor::Resolve resolveInfo = {});

        CommandBuffer& GenerateMipmaps(ResourceRef<const Image> image);

        CommandBuffer& CopyBufferToImage(ResourceRef<const Buffer> buffer, ResourceRef<const Image> image, kor::Copy copyInfo = {});
        CommandBuffer& CopyImageToBuffer(ResourceRef<const Image> image, ResourceRef<const Buffer> buffer, kor::Copy copyInfo = {});

        virtual CommandBuffer& Run(const std::function<void(CommandBuffer&)>& command) = 0;

        template<typename Func> requires std::is_invocable_v<Func, CommandBuffer&>
        CommandBuffer& If(
            const std::function<bool()>& condition,
            Func&& trueCommands,
            std::optional<Func> falseCommands = std::nullopt
        ) {
            if (condition()) {
                trueCommands(*this);
            } else if (falseCommands) {
                falseCommands->operator()(*this);
            }
            return *this;
        }

        template<typename... Args> requires (std::is_convertible_v<std::invoke_result_t<Args, CommandBuffer&>, void> && ...)
        CommandBuffer& Condition(
            const std::function<glm::u8()>& condition,
            Args... commands
        ) {
            const glm::u8 cond = condition();
            const std::array<std::function<void(CommandBuffer&)>, sizeof...(Args)> commandArray = { commands... };
            commandArray[cond](*this);
            return *this;
        }

        template<std::ranges::range R, typename Func> requires std::is_invocable_v<Func, CommandBuffer&, std::ranges::range_value_t<R>>
        CommandBuffer& ForEach(R&& range, Func&& func) {
            for (auto&& item : range) {
                func(*this, item);
            }
            return *this;
        }

        // Scoped debug region: opens a label, records everything in `body` inside it,
        // then closes it. Pairs BeginDebugLabel/EndDebugLabel so they can't be unbalanced.
        template<typename Func> requires std::is_invocable_v<Func, CommandBuffer&>
        CommandBuffer& DebugLabel(const std::string& label, Func&& body, glm::vec4 color = { 1.f, 1.f, 1.f, 1.f }) {
            BeginDebugLabel(label, color);
            body(*this);
            EndDebugLabel();
            return *this;
        }

        static void SingleTimeCommand(const std::function<void(kor::CommandBuffer&)>& command, Usage usage = Usage::eGraphics);

        virtual VoidResult Submit() = 0;
        virtual void Reset() = 0;

        CommandBuffer& BufferBarrier(const BufferBarrier& barrier) { return Barrier({ barrier }, {}); }
        CommandBuffer& ImageBarrier(const ImageBarrier& barrier) { return Barrier({}, { barrier }); }

        CommandBuffer& DrawMesh(ResourceRef<const Mesh> mesh, glm::u32 instanceCount , glm::u32 baseInstance);
        CommandBuffer& DrawSubMesh(ResourceRef<const Mesh> mesh, glm::u32 baseIndex, glm::u32 indexCount);

        static std::unique_ptr<CommandBuffer> Create(Flags<Usage> usage);

        virtual void WaitForFence() const = 0;

    protected:
        struct {
            std::optional<kor::ResourceRef<const Framebuffer>> boundFramebuffer = std::nullopt;
            std::optional<kor::ResourceRef<const ComputePipeline>> boundComputePipeline = std::nullopt;
            std::optional<kor::ResourceRef<const GraphicsPipeline>> boundGraphicsPipeline = std::nullopt;
            std::optional<kor::ResourceRef<const RayTracingPipeline>> boundRayTracingPipeline = std::nullopt;
            std::optional<kor::ResourceRef<const Mesh>> boundMesh = std::nullopt;

            std::map<glm::u32, kor::ResourceRef<const DescriptorSet>> boundGraphicsDescriptorSets;
            std::map<glm::u32, kor::ResourceRef<const DescriptorSet>> boundComputeDescriptorSets;
            std::map<glm::u32, kor::ResourceRef<const DescriptorSet>> boundRayTracingDescriptorSets;

            bool viewportSet = false;
            bool scissorSet = false;

            // Which pipeline-derived dynamic states currently have a value on the GPU.
            // Reset on BindGraphicsPipeline; a bit is set by the matching Set* call.
            Flags<DynamicState> dynamicStateSet;
        } _state;

        explicit CommandBuffer(const Flags<Usage> usage) : _usage(usage) {}
        Flags<Usage> _usage;

        // Before a draw, apply the bound pipeline's default for every dynamic state
        // the caller has not explicitly overridden since the pipeline was bound.
        // Backends invoke this from their draw commands; it dispatches through the
        // virtual Set* overrides so the backend records the actual GPU commands.
        void applyDynamicDefaults();

        // Append an error and enter the failed state; returns *this for chaining.
        CommandBuffer& record(ErrorCode code, std::string message);
        // Append an already-built error (so a cause chain survives); returns *this for chaining.
        CommandBuffer& record(Error error);
        // Clear accumulated errors (called by Begin()).
        void resetErrors() { _errors.clear(); _failed = false; }

        /**
         * @brief Refuse a resource that cannot be used, failing the recording rather than the process.
         * @return true if the command must not proceed.
         *
         * A poisoned resource fails the command buffer with its *own* error linked as the cause, so
         * Submit() reports the shader that failed to compile rather than an opaque "invalid pipeline".
         * Nothing throws, and the backend is never reached.
         */
        template<typename T>
        bool reject(const ResourceRef<T>& input, const std::string_view what) {
            if (!input.alive()) {
                record(ErrorCode::eInvalidArgument, std::format("The {} has been destroyed.", what));
                return true;
            }
            if (input.poisoned()) {
                const auto name = input.name();
                record(Error{
                    .code = input.error()->code,
                    .message = std::format("Cannot record with the {} '{}': it is unusable.",
                                           what, name.empty() ? "<unnamed>" : name),
                    .cause = input.errorPtr(),
                });
                return true;
            }
            return false;
        }

        // ---- Tracked-state updates ------------------------------------------
        // The state mutations the gated commands perform, split out from the gate itself so the
        // OpenGL backend can re-apply them at *replay* time as well as record time (it defers its
        // commands, and its state mirror has to advance in both passes). Callers must already have
        // validated the resource: these only touch _state.
        void stateBeginRendering(const ResourceRef<const Framebuffer>& framebuffer);
        void stateBindComputePipeline(const ResourceRef<const ComputePipeline>& pipeline);
        void stateBindGraphicsPipeline(const ResourceRef<const GraphicsPipeline>& pipeline);
        void stateBindRayTracingPipeline(const ResourceRef<const RayTracingPipeline>& pipeline);
        void stateBindMesh(const ResourceRef<const Mesh>& mesh);

        // ---- Backend commands (non-virtual interface) -----------------------
        // Every command above that takes a resource is a non-virtual wrapper in the core: it
        // validates, rejects unusable resources, updates the tracked state, and only then calls
        // the matching do* below. A backend therefore *cannot* be handed a poisoned or destroyed
        // resource — which is the precondition that all ~150 `dynamic_cast<const vk::X&>(*ref)`
        // sites in the backends have always silently assumed. That used to be a convention each
        // override had to remember, and several did not; now it is structural.
        virtual CommandBuffer& doBeginRendering(ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters) = 0;
        virtual CommandBuffer& doBindComputePipeline(ResourceRef<const ComputePipeline> pipeline) = 0;
        virtual CommandBuffer& doBindGraphicsPipeline(ResourceRef<const GraphicsPipeline> pipeline) = 0;
        // Defaults to reporting ray tracing as unsupported, like TraceRays; OpenGL leaves it alone.
        virtual CommandBuffer& doBindRayTracingPipeline(ResourceRef<const RayTracingPipeline> pipeline);
        virtual CommandBuffer& doBindDescriptorSet(glm::u32 index, ResourceRef<const DescriptorSet> descriptorSet, bool debug) = 0;
        virtual CommandBuffer& doBindMesh(ResourceRef<const Mesh> mesh) = 0;
        virtual CommandBuffer& doBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) = 0;
        virtual CommandBuffer& doDispatchIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset) = 0;
        virtual CommandBuffer& doDrawIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) = 0;
        virtual CommandBuffer& doDrawIndexedIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) = 0;
        // OpenGL never implemented this; the base is a no-op there, as it was before.
        virtual CommandBuffer& doDrawMeshTasksIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) { return *this; }
        virtual CommandBuffer& doClearBuffer(ResourceRef<const Buffer> buffer, glm::u64 offset, glm::u64 size) = 0;
        virtual CommandBuffer& doClearColorImage(ResourceRef<const Image> image, glm::vec4 color) = 0;
        virtual CommandBuffer& doFillBuffer(ResourceRef<const Buffer> buffer, void* data, glm::u64 offset, glm::u64 size) = 0;
        virtual CommandBuffer& doCopyBuffer(ResourceRef<const Buffer> srcBuffer, ResourceRef<const Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) = 0;
        virtual CommandBuffer& doGenerateMipmaps(ResourceRef<const Image> image);
        virtual CommandBuffer& doCopyBufferToImage(ResourceRef<const Buffer> buffer, ResourceRef<const Image> image, kor::Copy copyInfo) = 0;
        virtual CommandBuffer& doCopyImageToBuffer(ResourceRef<const Image> image, ResourceRef<const Buffer> buffer, kor::Copy copyInfo) = 0;
        virtual CommandBuffer& doBlit(ResourceRef<const Image> srcImage, kor::Blit blitInfo) = 0;
        virtual CommandBuffer& doBlit(ResourceRef<const Image> srcImage, ResourceRef<const Image> dstImage, kor::Blit blitInfo) = 0;
        virtual CommandBuffer& doResolve(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo) = 0;
        virtual CommandBuffer& doResolve(ResourceRef<const Image> srcImage, ResourceRef<const Image> dstImage, kor::Resolve resolveInfo) = 0;

        std::vector<Error> _errors;
        bool _failed = false;

        bool _collectingStatistics = false;
        std::unordered_map<std::string, CommandStatistics> _statistics;
        glm::u64 _lastFrameCommandCount = 0;

        virtual CommandBuffer& PushConstants(const void* data, glm::u32 size, glm::u32 offset) = 0;
    };
}
