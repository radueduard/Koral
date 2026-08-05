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
#include <source_location>
#include "flags.h"
#include "api.h"
#include "error.h"

#include <glm/glm.hpp>

#include "structs.h"

namespace kor
{
    class Buffer;
    class Image;
    class DescriptorSet;
    class GraphicsPipeline;
    class ComputePipeline;
    class RayTracingPipeline;
    class Framebuffer;
    class Mesh;

    /**
     * @brief Records the work a frame submits to the GPU.
     *
     * A scene is handed one in Scene::Render and describes its frame by chaining commands on it —
     * every command returns the command buffer, so a frame reads as one expression:
     *
     * @code
     * commandBuffer
     *     .BeginRendering()
     *     .BindGraphicsPipeline(pipeline)
     *     .BindDescriptorSet(0, frameSet)
     *     .DrawMesh(mesh, 1, 0)
     *     .EndRendering();
     * @endcode
     *
     * Three things about that recording are worth knowing, because they are what this type does
     * for you that the underlying APIs do not:
     *
     * **Commands are recorded, not executed.** Calling a command validates it immediately — a
     * destroyed or unusable resource fails at your line, with your file and line number — and then
     * parks it. Nothing reaches the driver until End(), which walks the whole sequence with the
     * benefit of hindsight.
     *
     * **Barriers are inserted for you.** Each recorded command declares which buffers and images it
     * touches and how. End() works out the transitions that implies and emits them where they are
     * legal, which is frequently not where the command sits: a shadow map sampled inside a render
     * pass has to be transitioned before that pass opens, and no API lets you do it inside one. The
     * exceptions the analysis cannot see are buffers reached through raw device addresses, which
     * appear in no descriptor set — declare those with Barrier().
     *
     * **Errors are sticky, and never thrown.** A failed command records an error and puts the buffer
     * into a failed state where subsequent GPU work is skipped, so a chain never has to be
     * interrupted to be checked. Ask afterwards with ok(), result() or errors(); Submit() returns
     * the same information.
     *
     * A command buffer belongs to the thread that created it and must be recorded from that thread.
     *
     * @see Scene::Render, SingleTimeCommand
     */
    class KORAL_API CommandBuffer
    {
    public:
        virtual ~CommandBuffer() = default;
        CommandBuffer& operator=(const CommandBuffer&) = delete;
        CommandBuffer(const CommandBuffer&) = delete;

        // ---- GPU timers -----------------------------------------------------------------------
        //
        // Scoped device-side timing. A pair of timestamps is written around the commands between
        // BeginTimer and EndTimer, and the difference is what the GPU spent on them.
        //
        // Reading them back is necessarily deferred: the timestamps do not exist until the GPU has
        // executed the commands that write them, which is long after Scene::Render returned. So a
        // scope's result is not available in the frame that recorded it — it is collected at the
        // start of the next recording on the same command buffer, which for the frame's command
        // buffer is once the frame in flight comes round again. getTimings() therefore reports a
        // frame that has definitely completed, a few frames back, and never blocks waiting for one.
        //
        // @note A continuation that runs the instant the GPU signals — rather than at the next
        //       recording — needs the frame's completion exposed as an awaitable token, which is
        //       part of the v2 scheduler work. The recording API here does not change when it lands.

        /**
         * @brief Opens a timed scope. Everything recorded until the matching EndTimer is measured.
         * @param label Name the result is reported under. Need not be unique.
         *
         * Scopes may nest, and the nesting is preserved in the result's TimerResult::depth. A
         * device that cannot timestamp (or a backend without query support) makes this a no-op and
         * reports no timings at all, rather than failing the recording.
         *
         * @warning What is measured is the *emitted* span, not the written one. Barriers are
         *          worked out at End() and inserted where they are legal, which can be inside a
         *          scope that did not write them — so a scope's time legitimately includes waits
         *          the resolver put there on its behalf. That is the cost of the work, but it is
         *          not always the cost of the commands you can see.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& BeginTimer(std::string label, std::source_location where = std::source_location::current());

        /**
         * @brief Closes the innermost scope opened by BeginTimer.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& EndTimer(std::source_location where = std::source_location::current());

        /**
         * @brief Records @p body inside a timed scope, closing it afterwards.
         * @param label Name the result is reported under.
         * @param body Callback that records the commands to measure.
         *
         * Preferred over the Begin/End pair, which can be left unbalanced.
         */
        template<typename Func> requires std::is_invocable_v<Func, CommandBuffer&>
        CommandBuffer& Timer(std::string label, Func&& body) {
            BeginTimer(std::move(label));
            body(*this);
            EndTimer();
            return *this;
        }

        /**
         * @brief Reads back one scope's time, fetching the results if they have arrived.
         * @param label The name given to BeginTimer.
         * @return The GPU time in milliseconds, or an Error explaining why there is none yet.
         *
         * Unlike getTimings(), this goes and looks: call it any time after Submit() and it will
         * collect the timestamps if the GPU has finished with them. That makes it the way to time
         * a command buffer that is submitted once and never re-recorded — a job's compute pass,
         * say — where waiting for the next Begin() would mean waiting forever.
         *
         * @code
         * commandBuffer->Submit();
         * commandBuffer->WaitForFence();
         * if (const auto ms = commandBuffer->CollectTimer("sort"))
         *     kor::log::info("sort took {:.3f} ms", *ms);
         * else
         *     kor::log::warn("{}", ms.error().message);
         * @endcode
         *
         * Never blocks. Called before the GPU has finished — without a WaitForFence, say — it
         * fails with a message saying so rather than stalling, and succeeds on a later call. If
         * more than one scope shares @p label this reports the first one opened; read them all
         * with CollectTimings().
         */
        [[nodiscard]] Result<double> CollectTimer(std::string_view label);

        /**
         * @brief Every scope's result, fetching them if they have arrived.
         * @return One entry per BeginTimer/EndTimer pair, in the order they were opened.
         *
         * The whole-buffer form of CollectTimer, and the same timing rules apply. Empty while
         * nothing has been measured or the GPU has not finished.
         */
        [[nodiscard]] const std::vector<TimerResult>& CollectTimings();

        /**
         * @brief The scopes already collected, without going to look for more.
         * @return One entry per BeginTimer/EndTimer pair, in the order they were opened.
         *
         * What a recurring workload reads: each Begin() collects the previous submission's
         * results, so by the time a frame is recording, the readings from its last trip through
         * the swap chain are already here and this is a plain accessor over them. Reach for
         * CollectTimer or CollectTimings instead when the results are wanted *now*, in the same
         * pass that submitted the work.
         *
         * The results persist until the next completed submission replaces them, so a frame that
         * records no timers leaves the previous frame's readings in place rather than blanking them.
         */
        [[nodiscard]] const std::vector<TimerResult>& getTimings() const { return _timings; }

        /**
         * @brief Whether this command buffer's device and queue can timestamp at all.
         *
         * False on a queue whose family reports no valid timestamp bits — a dedicated transfer
         * queue on some drivers — in which case the timer commands do nothing.
         */
        [[nodiscard]] virtual bool supportsTimers() const { return false; }

        /** @brief The most scopes one recording may open. Beyond this BeginTimer fails the recording. */
        static constexpr glm::u32 MaxTimerScopes = 256;

        /** @brief How many commands the last completed recording emitted. */
        [[nodiscard]] glm::u64 getLastFrameCommandCount() const { return _lastFrameCommandCount; }

        // ---- Error railway --------------------------------------------------------------------

        /**
         * @brief Whether recording is still healthy.
         * @return false once any command has failed. From that point GPU commands are skipped, so
         *         the rest of the chain does nothing rather than compounding the failure.
         */
        [[nodiscard]] bool ok() const { return !_failed; }

        /**
         * @brief The first error recorded, as a result.
         * @return An empty (successful) result if nothing failed, otherwise the first Error —
         *         which carries the failing command's source location and, where one exists, the
         *         cause it inherited from a poisoned resource.
         */
        [[nodiscard]] VoidResult result() const;

        /** @brief Every error recorded, in the order they happened. */
        [[nodiscard]] const std::vector<Error>& errors() const { return _errors; }

        /**
         * @brief Whether anything recorded so far names @p image among what it touches.
         * @param image The image to ask about.
         * @return Whether any command recorded up to now reads it, writes it, renders into it, copies
         *         it, or transitions it.
         *
         * Every command declares what it will touch, which is what the barrier resolver runs on — so
         * this is that declaration read back. It answers "has this frame done anything with the
         * screen yet?", which is how the runtime knows whether to clear the window's framebuffer
         * itself. Asked *during* recording: once End() has run there is nothing left to ask.
         */
        [[nodiscard]] bool hasTouched(const kor::ResourceRef<const Image>& image) const;

        /**
         * @brief What kind of work a command buffer may record.
         *
         * Determines which queue it is created on. Combine them with Flags when one buffer records
         * more than one kind.
         */
        enum class Usage
        {
            eGraphics = 1 << 0, ///< Draws and render passes. Also permits compute and transfers on every device.
            eCompute = 1 << 1,  ///< Compute dispatches, without the graphics pipeline.
            eTransfer = 1 << 2  ///< Copies, blits and clears only. The narrowest, and on discrete GPUs often a dedicated transfer queue.
        };

        // ---- Lifecycle ------------------------------------------------------------------------

        /**
         * @brief Creates a command buffer able to record the given kinds of work.
         * @param usage Which command kinds it must accept.
         * @return An owned command buffer for the active graphics API.
         *
         * A scene rarely calls this: the frame's command buffer is handed to Scene::Render, and
         * one-off work is easier through SingleTimeCommand.
         */
        static std::unique_ptr<CommandBuffer> Create(Flags<Usage> usage);

        /**
         * @brief Records, submits and waits for one throwaway batch of work.
         * @param command Callback that records into a fresh command buffer.
         * @param usage Which command kinds the callback needs.
         *
         * Blocks until the GPU has finished, so on return the results are ready to read. That makes
         * it right for setup and readback and wrong for anything per-frame, which should record
         * into the frame's own command buffer instead. A failure inside is logged, not thrown.
         */
        static void SingleTimeCommand(const std::function<void(kor::CommandBuffer&)>& command, Usage usage = Usage::eGraphics);

        /**
         * @brief Opens recording, discarding anything recorded before.
         *
         * Clears the accumulated errors as well, so a buffer that failed last frame starts clean.
         */
        virtual CommandBuffer& Begin() = 0;

        /**
         * @brief Closes recording and hands the finished sequence to the driver.
         *
         * This is where the recorded commands are resolved — barriers worked out and inserted —
         * and then emitted in order. Nothing has reached the GPU before this, and nothing is
         * executed by it: Submit() does that.
         */
        virtual void End() = 0;

        /**
         * @brief Submits the recorded work to its queue.
         * @return An empty result on success, or the first error recording produced.
         *
         * Does not wait for completion. Use WaitForFence() when the results have to be readable on
         * the CPU.
         */
        virtual VoidResult Submit() = 0;

        /** @brief Returns the buffer to its initial state, dropping everything recorded. */
        virtual void Reset() = 0;

        /**
         * @brief Blocks until the GPU has finished the work submitted from this buffer.
         *
         * The coarsest possible synchronisation — it stalls the calling thread completely. Needed
         * after a readback, unnecessary between frames, which the scheduler already paces.
         */
        virtual void WaitForFence() const = 0;

        // ---- Render passes --------------------------------------------------------------------

        /**
         * @brief Opens a render pass on the window's default framebuffer.
         * @param renderParameters What happens to the attachments on entry and exit.
         *
         * The default framebuffer is the swap-chain image this frame will be presented from, so
         * this is how a scene draws to the screen.
         */
        virtual CommandBuffer& BeginRendering(RenderParameters renderParameters = {});

        /**
         * @brief Opens a render pass on a framebuffer of your own.
         * @param framebuffer The attachments to render into.
         * @param renderParameters What happens to those attachments on entry and exit.
         *
         * Its attachments are declared as used, so the transitions they need are emitted ahead of
         * the pass rather than inside it. Binding a pipeline, a viewport and a scissor does not
         * survive the pass: opening one clears them, since the next pass may have a different
         * format or size.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& BeginRendering(ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters = {},
                                      std::source_location where = std::source_location::current());

        /** @brief Closes the render pass opened by BeginRendering. */
        virtual CommandBuffer& EndRendering();

        /**
         * @brief Sets the region of the attachment the rendering is mapped onto.
         * @param x,y Top-left corner in pixels.
         * @param width,height Size of the region in pixels.
         *
         * Defaults to the whole window if a draw is recorded without one, so a scene drawing to the
         * screen may skip it entirely.
         */
        virtual CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);

        /**
         * @brief Sets the rectangle outside which fragments are discarded.
         * @param x,y Top-left corner in pixels.
         * @param width,height Size of the rectangle in pixels.
         *
         * Like the viewport, defaults to the whole window when a draw is recorded without one.
         * Unlike the viewport it does not rescale anything — it only clips.
         */
        virtual CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);

        // ---- Dynamic state --------------------------------------------------------------------
        //
        // Every setter below overrides a piece of state the bound graphics pipeline already carries
        // a default for. Leave one alone and the pipeline's own value is applied before the draw;
        // call it and the override holds until the next pipeline is bound. All of them need a
        // graphics pipeline bound first, and all of them apply from the next draw onwards, so the
        // same pipeline can be reused across draws that differ only in these.

        /** @brief Sets the width of rasterized lines, in pixels. Applies when the pipeline's polygon mode is PolygonMode::eLine. */
        virtual CommandBuffer& SetLineWidth(float lineWidth);

        /**
         * @brief Sets the depth bias applied to rasterized fragments.
         * @param constantFactor A flat offset added to every fragment's depth.
         * @param clamp The largest bias that may be applied, or 0 for no clamp.
         * @param slopeFactor Scales the fragment's depth slope into the bias, which is what keeps
         *                    steeply angled surfaces from z-fighting when a flat offset cannot.
         *
         * Requires SetDepthBiasEnable(true) or a pipeline that enables it.
         */
        virtual CommandBuffer& SetDepthBias(float constantFactor, float clamp, float slopeFactor);

        /** @brief Sets the constant colour used by blend factors BlendFactor::eConstantColor and eOneMinusConstantColor. */
        virtual CommandBuffer& SetBlendConstants(glm::vec4 constants);

        /** @brief Sets which bits of the stencil value and reference take part in the comparison. */
        virtual CommandBuffer& SetStencilCompareMask(StencilFace face, glm::u32 compareMask);

        /** @brief Sets which bits of the stencil buffer a stencil operation may write. */
        virtual CommandBuffer& SetStencilWriteMask(StencilFace face, glm::u32 writeMask);

        /** @brief Sets the value the stencil test compares against, and the value StencilOp::eReplace writes. */
        virtual CommandBuffer& SetStencilReference(StencilFace face, glm::u32 reference);

        /** @brief Sets which polygon faces are discarded. An empty Flags draws both. */
        virtual CommandBuffer& SetCullMode(Flags<CullMode> cullMode);

        /** @brief Sets which winding order counts as front-facing, and so what SetCullMode culls. */
        virtual CommandBuffer& SetFrontFace(FrontFace frontFace);

        /** @brief Enables or disables the depth test. Disabled, fragments are drawn regardless of what is in front of them. */
        virtual CommandBuffer& SetDepthTestEnable(bool enable);

        /** @brief Enables or disables writing to the depth buffer. Off with the test still on is the usual setup for transparent geometry. */
        virtual CommandBuffer& SetDepthWriteEnable(bool enable);

        /** @brief Sets the comparison a fragment's depth must pass against the depth buffer. */
        virtual CommandBuffer& SetDepthCompareOp(CompareOp compareOp);

        /** @brief Enables or disables the stencil test. */
        virtual CommandBuffer& SetStencilTestEnable(bool enable);

        /**
         * @brief Sets what happens to the stencil buffer on each outcome of the two tests.
         * @param face Which polygon faces this applies to.
         * @param failOp Applied when the stencil test fails.
         * @param passOp Applied when both the stencil and depth tests pass.
         * @param depthFailOp Applied when the stencil test passes but the depth test fails.
         * @param compareOp The comparison the stencil test itself performs.
         */
        virtual CommandBuffer& SetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp);

        /** @brief Enables or disables depth bias. The amount comes from SetDepthBias. */
        virtual CommandBuffer& SetDepthBiasEnable(bool enable);

        /** @brief Discards primitives before rasterization, so vertex work runs and no fragments are produced. */
        virtual CommandBuffer& SetRasterizerDiscardEnable(bool enable);

        /** @brief Enables the index value that restarts a strip or fan mid-buffer (all-ones for the index type). */
        virtual CommandBuffer& SetPrimitiveRestartEnable(bool enable);

        // ---- Bindings -------------------------------------------------------------------------

        /**
         * @brief Binds the compute pipeline that the next Dispatch will run.
         * @param pipeline The pipeline. Recording fails here if it is unusable — a shader that did
         *                 not compile, say — naming the shader that caused it.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& BindComputePipeline(ResourceRef<const ComputePipeline> pipeline, std::source_location where = std::source_location::current());

        /**
         * @brief Binds the graphics pipeline that the next draw will use.
         * @param pipeline The pipeline. Recording fails here if it is unusable, naming the shader responsible.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         *
         * Resets every dynamic state to the pipeline's own defaults, so overrides set for a previous
         * pipeline do not leak into this one.
         */
        CommandBuffer& BindGraphicsPipeline(ResourceRef<const GraphicsPipeline> pipeline, std::source_location where = std::source_location::current());

        /** @brief Binds the ray-tracing pipeline that the next TraceRays will use. Fails on devices without ray-tracing support. */
        CommandBuffer& BindRayTracingPipeline(ResourceRef<const RayTracingPipeline> pipeline, std::source_location where = std::source_location::current());

        /**
         * @brief Binds a descriptor set — the resources a shader reads — to one set index.
         * @param index The set number the shader declares (`set = N` in GLSL, `spaceN` in HLSL/Slang).
         * @param descriptorSet The resources to bind there.
         * @param debug When true, logs the set's contents as it is bound. A debugging aid; leave it false.
         *
         * The set binds to whichever pipeline type is currently bound, and stays bound for later
         * draws or dispatches until another set replaces it at that index. What the bound sets
         * contain is also how the automatic barriers know what a draw is about to touch.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& BindDescriptorSet(glm::u32 index, ResourceRef<const DescriptorSet> descriptorSet, bool debug = false,
                                         std::source_location where = std::source_location::current());

        /**
         * @brief Binds a mesh's vertex and index buffers.
         * @param mesh The mesh whose buffers the following draws read from.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         *
         * Draw and DrawIndexed then default their counts to the whole mesh. DrawMesh does this for
         * you and is usually what you want.
         */
        CommandBuffer& BindMesh(ResourceRef<const Mesh> mesh, std::source_location where = std::source_location::current());

        /**
         * @brief Uploads a small block of data straight into the bound pipeline's push constants.
         * @tparam T Any trivially copyable type; typically the struct the shader declares.
         * @param data The value to upload. Copied immediately, so it need not outlive the call.
         * @param offset Byte offset into the push-constant block.
         *
         * The fastest way to get a handful of bytes to a shader — no buffer, no descriptor, no
         * synchronisation — but the block is small (guaranteed at least 128 bytes) and the data is
         * gone when the pipeline changes. Anything larger belongs in a uniform buffer.
         */
        template<typename T> requires std::is_trivially_copyable_v<T>
        CommandBuffer& PushConstants(const T& data, const glm::u32 offset = 0) {
            return PushConstants(&data, sizeof(T), offset);
        }

        // ---- Barriers -------------------------------------------------------------------------

        /**
         * @brief Declares resource transitions explicitly, instead of letting them be worked out.
         * @param bufferBarriers Buffers to transition, and the access each must support afterwards.
         * @param imageBarriers Images to transition, likewise.
         *
         * Rarely needed: recorded commands declare what they touch, and the transitions are
         * inserted for you. Reach for this when the analysis cannot see the access — a buffer a
         * shader reaches through a raw device address is the usual case, since it appears in no
         * descriptor set.
         *
         * A barrier written here is authoritative for the range it names: the resolver takes the
         * resource to be in that state afterwards and does not emit a second transition, so an
         * explicit barrier replaces the automatic one rather than doubling it.
         *
         * @see BufferBarrier, ImageBarrier
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& Barrier(std::vector<BufferBarrier> bufferBarriers = {}, std::vector<ImageBarrier> imageBarriers = {},
                               std::source_location where = std::source_location::current());

        /** @brief Convenience for Barrier() with a single buffer barrier. */
        CommandBuffer& BufferBarrier(const BufferBarrier& barrier) { return Barrier({ barrier }, {}); }

        /** @brief Convenience for Barrier() with a single image barrier. */
        CommandBuffer& ImageBarrier(const ImageBarrier& barrier) { return Barrier({}, { barrier }); }

        // ---- Debug labels ---------------------------------------------------------------------
        //
        // Named regions and markers that show up in GPU debuggers — RenderDoc, Nsight and the like
        // — turning a flat list of draws into the structure you wrote. Pure debugging aids: on a
        // backend or driver without marker support they are silently no-ops, so they can be left in.

        /**
         * @brief Opens a named region. Everything recorded until EndDebugLabel is nested inside it.
         * @param label Name shown in the debugger.
         * @param color Colour the debugger tints the region with.
         */
        virtual CommandBuffer& BeginDebugLabel(const std::string& label, glm::vec4 color = { 1.f, 1.f, 1.f, 1.f });

        /** @brief Closes the innermost region opened by BeginDebugLabel. */
        virtual CommandBuffer& EndDebugLabel();

        /** @brief Places a single named marker, without opening a region. */
        virtual CommandBuffer& InsertDebugLabel(const std::string& label, glm::vec4 color = { 1.f, 1.f, 1.f, 1.f });

        /**
         * @brief Records @p body inside a named region, closing it afterwards.
         * @param label Name shown in the debugger.
         * @param body Callback that records the region's commands.
         * @param color Colour the debugger tints the region with.
         *
         * Preferred over the Begin/End pair, which can be left unbalanced.
         */
        template<typename Func> requires std::is_invocable_v<Func, CommandBuffer&>
        CommandBuffer& DebugLabel(const std::string& label, Func&& body, glm::vec4 color = { 1.f, 1.f, 1.f, 1.f }) {
            BeginDebugLabel(label, color);
            body(*this);
            EndDebugLabel();
            return *this;
        }

        // ---- Compute and ray tracing ----------------------------------------------------------

        /**
         * @brief Runs the bound compute pipeline over a grid of workgroups.
         * @param groupCountX,groupCountY,groupCountZ Number of workgroups per axis. The threads
         *        each one contains are fixed by the shader's own local size, so the total invocation
         *        count is this multiplied by that.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        virtual CommandBuffer& Dispatch(glm::u32 groupCountX = 1, glm::u32 groupCountY = 1, glm::u32 groupCountZ = 1,
                                        std::source_location where = std::source_location::current()) = 0;

        /**
         * @brief Runs the bound compute pipeline over a grid the GPU itself decided.
         * @param indirectBuffer Buffer holding the three workgroup counts.
         * @param offset Byte offset the counts start at.
         *
         * Lets an earlier dispatch size a later one without a round trip to the CPU.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& DispatchIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0,
                                        std::source_location where = std::source_location::current());

        /**
         * @brief Casts a grid of rays through the bound ray-tracing pipeline.
         * @param width,height,depth Size of the ray grid, usually the target image's dimensions.
         *
         * Fails on devices or backends without ray-tracing support.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        virtual CommandBuffer& TraceRays(glm::u32 width = 1, glm::u32 height = 1, glm::u32 depth = 1,
                                         std::source_location where = std::source_location::current());

        // ---- Draws ----------------------------------------------------------------------------

        /**
         * @brief Draws vertices straight from the bound pipeline's vertex input.
         * @param vertexCount How many vertices to draw. The default takes the bound mesh's vertex count.
         * @param instanceCount How many copies to draw.
         * @param firstVertex Index of the first vertex.
         * @param firstInstance Index of the first instance, as the shader sees it.
         *
         * Needs a graphics pipeline bound. If no viewport or scissor was set, both default to the
         * whole window first.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        virtual CommandBuffer& Draw(glm::u64 vertexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstVertex = 0, glm::u32 firstInstance = 0,
                                    std::source_location where = std::source_location::current());

        /**
         * @brief Draws through the bound mesh's index buffer.
         * @param indexCount How many indices to draw. The default takes the bound mesh's index count.
         * @param instanceCount How many copies to draw.
         * @param firstIndex Index of the first index to read.
         * @param vertexOffset Added to every index before the vertex is fetched, which is how one
         *        vertex buffer can hold several meshes.
         * @param firstInstance Index of the first instance, as the shader sees it.
         *
         * Needs a graphics pipeline and a mesh with an index buffer bound.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        virtual CommandBuffer& DrawIndexed(glm::u64 indexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstIndex = 0, glm::i32 vertexOffset = 0, glm::u32 firstInstance = 0,
                                           std::source_location where = std::source_location::current());

        /**
         * @brief Binds a mesh and draws all of it.
         * @param mesh The mesh to draw.
         * @param instanceCount How many copies to draw.
         * @param baseInstance Index of the first instance, as the shader sees it.
         *
         * The shorthand for the common case: BindMesh followed by DrawIndexed over the whole thing.
         */
        CommandBuffer& DrawMesh(ResourceRef<const Mesh> mesh, glm::u32 instanceCount , glm::u32 baseInstance);

        /**
         * @brief Binds a mesh and draws one contiguous run of its indices.
         * @param mesh The mesh to draw from.
         * @param baseIndex First index of the run.
         * @param indexCount How many indices it covers.
         *
         * How a model split into per-material sections is drawn a section at a time.
         */
        CommandBuffer& DrawSubMesh(ResourceRef<const Mesh> mesh, glm::u32 baseIndex, glm::u32 indexCount);

        /**
         * @brief Launches the bound pipeline's mesh-shader workgroups.
         * @param taskCountX,taskCountY,taskCountZ Number of workgroups per axis.
         *
         * Requires mesh-shader support; unavailable on the OpenGL backend.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        virtual CommandBuffer& DrawMeshTasks(glm::u32 taskCountX = 1, glm::u32 taskCountY = 1, glm::u32 taskCountZ = 1,
                                             std::source_location where = std::source_location::current());

        /**
         * @brief Draws with the parameters read from a buffer instead of given here.
         * @param indirectBuffer Buffer holding one or more IndirectDrawCommand structures.
         * @param offset Byte offset the first one starts at.
         * @param drawCount How many of them to execute.
         * @param stride Bytes between consecutive commands; 0 means they are tightly packed.
         *
         * The point of every indirect draw: a compute shader can cull and build the draw list on
         * the GPU, and the CPU never learns how much survived.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& DrawIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0,
                                    std::source_location where = std::source_location::current());

        /**
         * @brief Indexed draw with the parameters read from a buffer.
         * @param indirectBuffer Buffer holding one or more IndirectDrawIndexedCommand structures.
         * @param offset Byte offset the first one starts at.
         * @param drawCount How many of them to execute.
         * @param stride Bytes between consecutive commands; 0 means they are tightly packed.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& DrawIndexedIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0,
                                           std::source_location where = std::source_location::current());

        /**
         * @brief Mesh-task draw with the parameters read from a buffer.
         * @param indirectBuffer Buffer holding one or more IndirectDrawMeshTasksCommand structures.
         * @param offset Byte offset the first one starts at.
         * @param drawCount How many of them to execute.
         * @param stride Bytes between consecutive commands; 0 means they are tightly packed.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& DrawMeshTasksIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0,
                                             std::source_location where = std::source_location::current());

        // ---- Transfers ------------------------------------------------------------------------

        /**
         * @brief Fills a range of a buffer with zeroes.
         * @param buffer The buffer to clear.
         * @param offset Byte offset to start at.
         * @param size How many bytes to clear; the default runs to the end of the buffer.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& ClearBuffer(ResourceRef<const Buffer> buffer, glm::u64 offset = 0, glm::u64 size = UINT64_MAX,
                                   std::source_location where = std::source_location::current());

        /**
         * @brief Fills every mip level and array layer of a colour image with one colour.
         * @param image The image to clear.
         * @param color The colour, in the image format's own range — for a UNORM format, 0..1.
         *
         * Clearing an attachment at the start of a pass is cheaper; prefer LoadOperation::eClear
         * when that is what you are doing.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& ClearColorImage(ResourceRef<const Image> image, glm::vec4 color = { 0.f, 0.f, 0.f, 1.f },
                                       std::source_location where = std::source_location::current());

        /**
         * @brief Writes a small block of CPU data straight into a buffer, without a staging buffer.
         * @param buffer Destination.
         * @param data Source bytes. Read during recording, so they need not outlive the call.
         * @param offset Byte offset into the buffer.
         * @param size How many bytes to write; the default runs to the end of the buffer.
         *
         * Meant for small updates (a few tens of kilobytes at most) — the data travels inside the
         * command buffer itself. Larger uploads belong in Buffer::Write.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& FillBuffer(ResourceRef<const Buffer> buffer, void* data, glm::u64 offset = 0, glm::u64 size = UINT64_MAX,
                                  std::source_location where = std::source_location::current());

        /**
         * @brief Copies bytes between two buffers on the GPU.
         * @param srcBuffer Source.
         * @param dstBuffer Destination.
         * @param size How many bytes; the default is as much as both sides can hold from their offsets.
         * @param srcOffset Byte offset into the source.
         * @param dstOffset Byte offset into the destination.
         *
         * Fails the recording if the range would run past either buffer.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& CopyBuffer(ResourceRef<const Buffer> srcBuffer, ResourceRef<const Buffer> dstBuffer, glm::u64 size = UINT64_MAX, glm::u64 srcOffset = 0, glm::u64 dstOffset = 0,
                                  std::source_location where = std::source_location::current());

        /**
         * @brief Copies an image onto the swap-chain image this frame presents, rescaling it.
         * @param srcImage The image to present.
         * @param blitInfo Which region to take and where it lands; see kor::Blit.
         *
         * How a scene that renders offscreen — at a different resolution, or through a post
         * pipeline — gets its result onto the screen.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& Blit(ResourceRef<const Image> srcImage, kor::Blit blitInfo = {}, std::source_location where = std::source_location::current());

        /**
         * @brief Copies a region between two images, rescaling and reformatting it as needed.
         * @param srcImage Source.
         * @param dstImage Destination.
         * @param blitInfo Which region to take, where it lands, and how it is filtered; see kor::Blit.
         *
         * Unlike a copy, the two regions may differ in size — that is what kor::Blit::filtering is for.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& Blit(ResourceRef<const Image> srcImage, ResourceRef<const Image> dstImage, kor::Blit blitInfo = {},
                            std::source_location where = std::source_location::current());

        /**
         * @brief Resolves a multisampled image onto the swap-chain image this frame presents.
         * @param srcImage The multisampled image.
         * @param resolveInfo Which region to take and where it lands; see kor::Resolve.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& Resolve(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo = {}, std::source_location where = std::source_location::current());

        /**
         * @brief Collapses a multisampled image into a single-sampled one.
         * @param srcImage The multisampled source.
         * @param dstImage The single-sampled destination.
         * @param resolveInfo Which region to take and where it lands; see kor::Resolve.
         *
         * The step that makes an MSAA render target usable as an ordinary texture. The samples are
         * combined by the framebuffer's resolve mode, not filtered, so the regions should match in size.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& Resolve(ResourceRef<const Image> srcImage, ResourceRef<const Image> dstImage, kor::Resolve resolveInfo = {},
                               std::source_location where = std::source_location::current());

        /**
         * @brief Fills an image's mip chain from its top level.
         * @param image An image with more than one mip level, usable as both transfer source and destination.
         *
         * Each level is produced from the one above it by a linear-filtered downscale, with the
         * transitions between levels handled for you. Call it after uploading level 0.
         */
        CommandBuffer& GenerateMipmaps(ResourceRef<const Image> image);

        /**
         * @brief Copies buffer memory into an image.
         * @param buffer Source bytes, laid out as kor::Copy describes.
         * @param image Destination image.
         * @param copyInfo Which region, which mip level and layers, and how the buffer is packed.
         *
         * The upload path for texture data that is already on the GPU. Fails the recording if the
         * region would read past the buffer or name a level the image does not have.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& CopyBufferToImage(ResourceRef<const Buffer> buffer, ResourceRef<const Image> image, kor::Copy copyInfo = {},
                                         std::source_location where = std::source_location::current());

        /**
         * @brief Copies an image into buffer memory.
         * @param image Source image.
         * @param buffer Destination bytes, laid out as kor::Copy describes.
         * @param copyInfo Which region, which mip level and layers, and how the buffer is packed.
         *
         * The readback path: point it at a host-visible buffer and the pixels are readable once the
         * work completes. The transition to a transfer source is inserted for you.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& CopyImageToBuffer(ResourceRef<const Image> image, ResourceRef<const Buffer> buffer, kor::Copy copyInfo = {},
                                         std::source_location where = std::source_location::current());

        // ---- Recording helpers ----------------------------------------------------------------
        //
        // Ordinary C++ control flow works perfectly well around these calls; the helpers exist so a
        // frame written as a single chained expression does not have to be broken up to branch or
        // loop. All of them run their callbacks *while recording*, not on the GPU.

        /**
         * @brief Records commands through the backend's own command buffer.
         * @param command Callback handed this command buffer.
         *
         * The escape hatch for work the API does not express — it is how the ImGui backend records
         * its draws. Like every other command it is deferred, so the callback runs at End(), in the
         * position it was written, rather than where it was called.
         */
        virtual CommandBuffer& Run(const std::function<void(CommandBuffer&)>& command) = 0;

        /**
         * @brief Records one branch or the other.
         * @param condition Evaluated immediately, during recording.
         * @param trueCommands Recorded when it holds.
         * @param falseCommands Recorded when it does not; may be omitted.
         */
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

        /**
         * @brief Records one of several alternatives, chosen by index.
         * @param condition Evaluated immediately; its result indexes @p commands.
         * @param commands The alternatives, in order.
         *
         * @warning The index is not bounds-checked. Returning one past the last alternative is
         *          undefined behaviour.
         */
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

        /**
         * @brief Records the same commands once per element of a range.
         * @param range Anything iterable — a container of draw items, for instance.
         * @param func Called as func(commandBuffer, element) for each one.
         */
        template<std::ranges::range R, typename Func> requires std::is_invocable_v<Func, CommandBuffer&, std::ranges::range_value_t<R>>
        CommandBuffer& ForEach(R&& range, Func&& func) {
            for (auto&& item : range) {
                func(*this, item);
            }
            return *this;
        }

    protected:
        /// Pipeline, framebuffer and descriptor bindings as of the current point in the recording.
        /// The backends advance this from inside their do* implementations, so that during the
        /// emit walk it reflects the point being emitted rather than the end of recording.
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

            /// Which dynamic states currently hold a value on the GPU. Cleared by
            /// BindGraphicsPipeline, set by the matching Set* call.
            Flags<DynamicState> dynamicStateSet;
        } _state;

        explicit CommandBuffer(const Flags<Usage> usage) : _usage(usage) {}
        Flags<Usage> _usage;

        /** @brief Applies the bound pipeline's default for every dynamic state not overridden since it was bound. */
        void applyDynamicDefaults();

        /** @brief Records an error and enters the failed state. Returns *this, so it can be returned from a command. */
        CommandBuffer& record(ErrorCode code, std::string message);

        /** @brief Records an already-built error, preserving its cause chain. */
        CommandBuffer& record(Error error);

        /** @brief Clears the accumulated errors and leaves the failed state. Called by Begin(). */
        void resetErrors() { _errors.clear(); _failed = false; }

        /**
         * @brief Refuses a resource that cannot be used, failing the recording rather than the process.
         * @param input The resource the command was handed.
         * @param what What it is, for the message ("framebuffer", "descriptor set", ...).
         * @return true if the command must not proceed.
         *
         * A poisoned resource fails the command buffer with its *own* error linked as the cause, so
         * the report names the shader that failed to compile rather than an opaque "invalid
         * pipeline". Nothing throws, and the backend is never reached.
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

        /// Where a command sits relative to a render pass, so the resolver knows which barriers
        /// have to be hoisted ahead of the record that opens one.
        enum class PassEdge : glm::u8 {
            eNone,      ///< Ordinary command, inside or outside a pass.
            eOpens,     ///< Opens a render pass.
            eCloses     ///< Closes one.
        };

        /// One resource a command touches, and how. Exactly one of buffer/image is set; the other
        /// is left default-constructed, for which alive() is false.
        struct ResourceUse {
            kor::ResourceRef<const Buffer> buffer;
            kor::ResourceRef<const Image>  image;
            ResourceAccess access = ResourceAccess::AllShaderRead;

            /// Image subresource range; nullopt means the whole image, matching ImageBarrier.
            std::optional<glm::u32> baseMipLevel;
            std::optional<glm::u32> levelCount;
            std::optional<glm::u32> baseArrayLayer;
            std::optional<glm::u32> layerCount;

            /// Buffer range.
            glm::u64 offset = 0;
            glm::u64 size = UINT64_MAX;
        };

        /// One parked command: what it will do, and what it will touch when it does.
        struct Record {
            std::function<void()> emit;
            std::vector<ResourceUse> uses;
            PassEdge pass = PassEdge::eNone;
            const char* command = "";
            std::source_location where;
            /// This record *is* a barrier: its uses describe the state it establishes rather than
            /// state it requires, so the resolver advances past them and emits nothing.
            bool transitions = false;
            /// This record runs shaders that dereference raw device addresses, so its `uses` are
            /// known to be incomplete: any buffer reached through a pointer is missing from them.
            bool dereferencesDeviceAddresses = false;
        };

        /**
         * @brief Parks a command until End(), or runs it in place if End() is already emitting.
         * @param command Name of the command, for diagnostics and statistics.
         * @param where Source location of the caller.
         * @param uses The resources it will touch, for the barrier resolver.
         * @param pass Whether it opens or closes a render pass.
         * @param emit Callback that performs the command against the backend.
         * @param transitions Whether the record establishes state rather than requiring it.
         * @param dereferencesDeviceAddresses Whether its shaders reach buffers by raw address.
         * @param where Source location the command was recorded at, used to point error messages back at your code. Leave it defaulted.
         */
        CommandBuffer& enqueue(const char* command, std::source_location where,
                               std::vector<ResourceUse> uses, PassEdge pass,
                               std::function<void()> emit, bool transitions = false,
                               bool dereferencesDeviceAddresses = false);

        /**
         * @brief The extent a draw falls back to when no viewport or scissor was set.
         *
         * The framebuffer the current pass renders into, which is the window's only when the pass
         * targets the screen. @see Draw
         */
        [[nodiscard]] glm::uvec2 defaultViewportExtent() const;

        /** @brief Whether the currently bound pipeline reaches buffers through device addresses. */
        [[nodiscard]] bool boundPipelineUsesDeviceAddresses() const;

        /**
         * @brief Everything the currently bound descriptor sets and mesh will be touched for.
         * @param includeMesh Whether to include the bound mesh's vertex and index buffers.
         *
         * Built at record time, when the bindings are known, and attached to the draw or dispatch
         * record so the resolver can synchronise them. Bindings the entry point never reaches are
         * skipped, as are samplers and acceleration structures, which name nothing an image or
         * buffer barrier applies to.
         */
        [[nodiscard]] std::vector<ResourceUse> usesForBoundResources(bool includeMesh) const;

        /** @brief Inserts the barriers the recorded commands imply. Called by End(). */
        void resolveBarriers();

        /** @brief Runs every record's emit callback in order. Called by End() after resolving. */
        void emitRecords();

        /** @brief Drops the recorded commands and the tracked state. Called by Begin() and Reset(). */
        void clearRecords() { _records.clear(); _emitting = false; resetTrackedState(); }

        /** @brief Returns the mirrored pipeline and binding state to where recording began, so the emit walk replays from the same start. */
        void resetTrackedState();

        std::vector<Record> _records;
        bool _emitting = false;  ///< True while End() is walking _records.

        // ---- Tracked-state updates ------------------------------------------------------------
        //
        // The state mutations the commands above perform, split out from the commands themselves so
        // the OpenGL backend can re-apply them at replay time as well as record time. Callers must
        // already have validated the resource: these only touch _state.

        /** @brief Records that a render pass opened on this framebuffer, clearing the pipeline and viewport bindings. */
        void stateBeginRendering(const ResourceRef<const Framebuffer>& framebuffer);
        /** @brief Records the bound compute pipeline. */
        void stateBindComputePipeline(const ResourceRef<const ComputePipeline>& pipeline);
        /** @brief Records the bound graphics pipeline, and clears the dynamic-state mask. */
        void stateBindGraphicsPipeline(const ResourceRef<const GraphicsPipeline>& pipeline);
        /** @brief Records the bound ray-tracing pipeline. */
        void stateBindRayTracingPipeline(const ResourceRef<const RayTracingPipeline>& pipeline);
        /** @brief Records the bound mesh. */
        void stateBindMesh(const ResourceRef<const Mesh>& mesh);
        /** @brief Records which descriptor set is bound at which index, for whichever pipeline type is bound. */
        void stateBindDescriptorSet(glm::u32 index, const ResourceRef<const DescriptorSet>& descriptorSet);

        // ---- Backend commands -----------------------------------------------------------------
        //
        // The backend half of every command above that takes a resource. The public wrapper
        // validates, rejects unusable resources and updates the tracked state; only then is the
        // matching do* called, so a backend is never handed a poisoned or destroyed resource.

        virtual CommandBuffer& doBeginRendering(ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters) = 0;
        virtual CommandBuffer& doBindComputePipeline(ResourceRef<const ComputePipeline> pipeline) = 0;
        virtual CommandBuffer& doBindGraphicsPipeline(ResourceRef<const GraphicsPipeline> pipeline) = 0;
        /// Defaults to reporting ray tracing as unsupported, like TraceRays; OpenGL leaves it alone.
        virtual CommandBuffer& doBindRayTracingPipeline(ResourceRef<const RayTracingPipeline> pipeline);
        virtual CommandBuffer& doBindDescriptorSet(glm::u32 index, ResourceRef<const DescriptorSet> descriptorSet, bool debug) = 0;
        virtual CommandBuffer& doBindMesh(ResourceRef<const Mesh> mesh) = 0;
        virtual CommandBuffer& doBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) = 0;
        virtual CommandBuffer& doDispatchIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset) = 0;
        virtual CommandBuffer& doDrawIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) = 0;
        virtual CommandBuffer& doDrawIndexedIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) = 0;
        /// Unimplemented on OpenGL, where it is a no-op.
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

        // ---- GPU timers, backend half -----------------------------------------------------------
        //
        // The bookkeeping is here because it is identical everywhere: two query slots per scope,
        // indices allocated at record time so the emit walk only has to write the timestamp it was
        // given. A backend supplies the two device-specific halves below.

        /** One scope recorded by BeginTimer, awaiting the timestamps that will resolve it. */
        struct TimerScope {
            std::string label;
            glm::u32 depth = 0;
            /// Where BeginTimer was called, so that a scope left open is blamed on the line that
            /// opened it rather than on End(), which is only where it was noticed.
            std::source_location where;
        };

        /** @brief Backend hook: write the timestamp for query slot @p queryIndex at this point in the stream. */
        virtual void writeTimerTimestamp(glm::u32 queryIndex) {}

        /**
         * @brief Backend hook: read back the previous submission's timestamps, without blocking.
         * @param scopeCount How many scopes that submission recorded; slots 2i and 2i+1 bound scope i.
         * @param millisecondsOut Filled with one duration per scope, in scope order.
         * @return false if the GPU has not finished with them, in which case nothing is reported
         *         and the previous results stand.
         */
        virtual bool readTimerTimestamps(glm::u32 scopeCount, std::vector<double>& millisecondsOut) { return false; }

        /**
         * @brief Fetches the last submission's timestamps if the GPU has finished with them.
         * @return Whether results are now published in _timings — false while they are still in
         *         flight, or when there was nothing submitted to collect.
         *
         * Idempotent and non-blocking, which is what lets both the recurring path (Begin()) and
         * the one-shot path (CollectTimer) share it without either having to know about the other.
         */
        bool collectTimers();

        /**
         * @brief Collects the previous submission's timings and starts a fresh set. Called by Begin().
         *
         * A command buffer is only re-recorded once the GPU is done with it, so this is the natural
         * collection point for anything recurring — and the only one that also has to clear the
         * recording state for the pass about to begin.
         */
        void retireTimers();

        /** @brief Closes out the recorded scopes and hands them to the next retireTimers(). Called by End(). */
        void submitTimers();

        /** @brief How many scopes the recording being built has opened. Valid until submitTimers(). */
        [[nodiscard]] glm::u32 timerScopeCount() const { return static_cast<glm::u32>(_pendingTimers.size()); }

        std::vector<TimerScope> _pendingTimers;    ///< Scopes in the recording being built.
        std::vector<TimerScope> _submittedTimers;  ///< Scopes of the submission whose results are still on the GPU.
        std::vector<glm::u32> _timerStack;         ///< Indices into _pendingTimers for the scopes currently open.
        std::vector<TimerResult> _timings;         ///< Last results that arrived; see getTimings().

        glm::u64 _lastFrameCommandCount = 0;

        virtual CommandBuffer& PushConstants(const void* data, glm::u32 size, glm::u32 offset) = 0;
    };
}
