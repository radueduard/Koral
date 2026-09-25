//
// Created by eduard on 11.03.2026.
//

#pragma once
#include <glm/fwd.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "api.h"
#include "commandBuffer.h"
#include "resource.h"
#include "token.h"

struct GLFWwindow;

namespace kor
{
    class Surface;

    /**
     * @brief One frame in flight: the swap-chain image it draws into and the command buffer it records with.
     *
     * The scheduler keeps several, cycling through them so the CPU can record the next frame while
     * the GPU is still working on the previous one.
     */
    class KORAL_API Frame
    {
    public:
        explicit Frame(glm::u32 imageIndex);
        virtual ~Frame() = default;

        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

        /**
         * @brief Which frame in flight this is: the copy of every per-frame resource it uses.
         *
         * Not a swap-chain image. Each window's swap chain hands out its images in its own order, and
         * the image a window shows is the one its swap chain acquired (Image::CopyIndex).
         */
		[[nodiscard]] glm::u32 ImageIndex() const { return _imageIndex; }

        /** @brief The command buffer this frame's work is recorded into. */
		[[nodiscard]] kor::CommandBuffer& Commands() const { return *_commandBuffer; }
    protected:
        glm::u32 _imageIndex;
		std::unique_ptr<kor::CommandBuffer> _commandBuffer;
    };

    /**
     * @brief Owns the swap chain and the frames in flight, and runs one frame from start to present.
     *
     * The run loop calls Draw() once per frame; everything else here is the state a scene may need
     * to read — most often CurrentImageIndex(), which is the index a per-frame resource uses to
     * find the copy the current frame owns.
     *
     * Created by the window as it is built; reach the live one through Context::Scheduler().
     */
    class KORAL_API Scheduler
    {
    public:
        /** @brief How many frames the scheduler keeps in flight. */
        struct Builder {
            /**
             * @brief How many frames may be in flight at once. Two allows double buffering.
             *
             * Exactly what ImageCount() then reports, and what every per-frame resource is sized to.
             * Each window's swap chain has however many images its driver gives it, which is a
             * separate number: those are never indexed by the frame.
             */
            glm::u32 imageCount = 2;

            /** @brief Sets how many frames may be in flight at once. */
            Builder& SetImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            /**
             * @brief Creates the scheduler for the active backend.
             * @return The scheduler; poisoned, with the reason, when the active API has no scheduler.
             */
            [[nodiscard]] Resource<Scheduler> Build() const;
        };

        virtual ~Scheduler() = default;

        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;
        Scheduler(Scheduler&&) = delete;
        Scheduler& operator=(Scheduler&&) = delete;

        /** @brief Creates the swap chain, the frames and their command buffers. Called once by the window. */
    	virtual void Initialize() = 0;

        /** @brief How many frames are in flight: how many copies a per-frame resource has. Fixed for the scheduler's life. */
        [[nodiscard]] glm::u32 ImageCount() const { return _imageCount; }

        /**
         * @brief Which frame is currently being recorded.
         * @return An index below ImageCount(). A per-frame buffer or image uses this to select
         *         the copy that is safe to write this frame.
         */
    	[[nodiscard]] virtual glm::u32 CurrentImageIndex() const { return _currentFrame; }

        /** @brief The frame currently being recorded. */
        [[nodiscard]] const kor::Frame &CurrentFrame() const { return *_frames.at(_currentFrame); }

        /** @brief The frame that will be recorded next, wrapping round at the end. */
        [[nodiscard]] const kor::Frame &NextFrame() const { return *_frames.at((_currentFrame + 1) % _imageCount); }

        /** @brief Moves on to the next frame. Called by Draw(); a scene should not. */
    	void AdvanceFrame() { _currentFrame = (_currentFrame + 1) % _imageCount; }

        /** @brief Every frame in flight, in index order. */
        [[nodiscard]] std::vector<std::reference_wrapper<Frame>> Frames() const
        {
    		std::vector<std::reference_wrapper<Frame>> frames;
			for (const auto& frame : _frames) {
				frames.emplace_back(*frame);
			}
			return frames;
        }

        /**
         * @brief Runs one whole frame: acquire an image, record, submit and present.
         * @param renderFunc Callback that records the frame's work; the run loop passes one that
         *        updates resources and calls Scene::Render.
         */
        virtual void Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) { _started = true; }

        /** @brief Whether the first frame has begun. Before it has, there is no current image to speak of. */
    	[[nodiscard]] bool HasStarted() const { return _started; }

        /**
         * @brief Whether a frame is being recorded right now: inside Draw's render callback.
         *
         * CurrentImageIndex() names the frame being built only then; before the first frame and
         * between frames it names the last one, which may still be running on the GPU.
         */
        [[nodiscard]] bool IsBuildingFrame() const { return _buildingFrame; }

        /** @brief Blocks until the GPU has finished everything submitted so far. Used when tearing down. */
    	virtual void WaitIdle() const = 0;

        /**
         * @brief Destroys a closed window's surface and OS window once no frame in flight uses them.
         *
         * Called by a second window as it is destroyed — possibly in the middle of the frame that is
         * still to present it. Internal.
         */
        virtual void RetireWindow(std::shared_ptr<Surface> surface, GLFWwindow* window);

        // ---- Work from elsewhere --------------------------------------------------------------

        /** @brief Where an executed command buffer runs relative to the frame's own. */
        enum class Placement : std::uint8_t {
            eBeforeFrame, ///< Ahead of the scene's rendering: work whose results the frame uses.
            eAfterFrame,  ///< Behind it: work that uses what the frame produced.
        };

        /**
         * @brief Adds a command buffer recorded elsewhere — another thread, a coroutine — to the next frame.
         * @param commandBuffer Begun and recorded, but **not** ended: the frame ends it, so that its
         *        barriers are worked out in the order it actually runs. Any thread may hand one over.
         * @param placement Before or after the frame's own command buffer. Several with the same
         *        placement run in the order they were handed over.
         * @return The frame's completion token (see FrameCompletion()): the work is done when it is.
         *
         * @code
         * kor::Task<void> Simulate(kor::ResourceRef<const kor::Buffer> particles) {
         *     co_await kor::Context::SwitchToBackgroundThread();
         *     auto cb = kor::CommandBuffer::Create(kor::CommandBuffer::Usage::eCompute);
         *     cb->Begin();
         *     cb->BindComputePipeline(step).BindDescriptorSet(0, set).Dispatch(groups, 1, 1);
         *     co_await kor::Context::Scheduler().Execute(std::move(cb));
         *     // the frame that ran it has finished on the GPU
         * }
         * @endcode
         *
         * The scheduler keeps the command buffer until the GPU is done with it. The resources it
         * uses are the caller's to keep alive until then, as for the frame's own. Under Vulkan it
         * must run on the frame's queue, which a command buffer created with Usage::eGraphics or
         * Usage::eCompute on an ordinary device does.
         */
        Token Execute(std::unique_ptr<CommandBuffer> commandBuffer, Placement placement = Placement::eBeforeFrame);

        /**
         * @brief Holds the next frame back, on the GPU, until @p token has happened.
         *
         * The frame is recorded and submitted as usual, and the GPU starts on it once the token is
         * signalled. It must be signalled, or the frame never finishes.
         *
         * Who signals it decides what the CPU does. A token a Submit() already on its way will
         * signal costs the CPU nothing. One the CPU has yet to signal holds up the frame's
         * *present* until it does — Vulkan does not let a present depend on a signal nobody has
         * submitted — so signal it from another thread or a coroutine, never from code that only
         * runs after this frame.
         */
        void WaitFor(const Token& token);

        /**
         * @brief A token for the frame now being built, signalled once the GPU has finished it.
         *
         * `co_await` it to run code the moment the frame is done — reading back results, say —
         * without stalling the render loop.
         */
        [[nodiscard]] Token FrameCompletion();

        /**
         * @brief Every frame index except @p index.
         * @return The frames a write to @p index still has to be propagated to; see PendingWrite.
         */
    	std::unordered_set<glm::u32> ImageIndicesExcept(const glm::u32 index) const
		{
			std::unordered_set<glm::u32> indices;
			for (glm::u32 i = 0; i < _imageCount; ++i) {
				if (i != index) {
					indices.insert(i);
				}
			}
			return indices;
		}

    protected:
    	virtual void CreateFrames() = 0;
        explicit Scheduler(const Builder& createInfo);
    	bool _started = false;
        /// Set by the backend's Draw from just before the render callback until the frame's work is taken.
        bool _buildingFrame = false;

        /** @brief What a frame picks up from Execute(), WaitFor() and FrameCompletion(). */
        struct Pending {
            std::vector<std::unique_ptr<CommandBuffer>> before;
            std::vector<std::unique_ptr<CommandBuffer>> after;
            std::vector<Token> waits;
            Token completion; ///< Signalled by this frame's submission.
        };

        /**
         * @brief Takes everything queued for the frame being submitted, and moves on to the next.
         *
         * Call once per Draw, after the render callback — which may itself Execute() — and before
         * ending any command buffer.
         */
        Pending TakePending();

        glm::u32 _imageCount;
	    glm::u32 _currentFrame = 0;
    	std::vector<std::unique_ptr<Frame>> _frames;

    private:
        std::mutex _pendingMutex;
        Pending _pending;
        Timeline _frameTimeline;
        std::uint64_t _frameNumber = 1; ///< The frame being built; its completion is _frameTimeline.At() it.
    };
}

