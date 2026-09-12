//
// Created by eduard on 11.03.2026.
//

#pragma once
#include <glm/fwd.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_set>

#include "api.h"
#include "commandBuffer.h"

namespace kor
{
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

        /** @brief Which swap-chain image this frame presents. Also indexes the per-frame copies of resources. */
		[[nodiscard]] glm::u32 imageIndex() const { return _imageIndex; }

        /** @brief The command buffer this frame's work is recorded into. */
		[[nodiscard]] kor::CommandBuffer& commandBuffer() const { return *_commandBuffer; }
    protected:
        glm::u32 _imageIndex;
		std::unique_ptr<kor::CommandBuffer> _commandBuffer;
    };

    /**
     * @brief Owns the swap chain and the frames in flight, and runs one frame from start to present.
     *
     * The run loop calls Draw() once per frame; everything else here is the state a scene may need
     * to read — most often currentImageIndex(), which is the index a per-frame resource uses to
     * find the copy the current frame owns.
     *
     * Created by the window as it is built; reach the live one through Context::Scheduler().
     */
    class KORAL_API Scheduler
    {
    public:
        /** @brief How many swap-chain images the scheduler asks for. */
        struct Builder {
            /**
             * @brief How many frames in flight to ask for. Two allows double buffering.
             *
             * A request, not a guarantee: the surface may require more and the driver may hand out
             * more still. imageCount() reports what was actually allocated, and that is the
             * number everything downstream is sized and indexed by.
             */
            glm::u32 imageCount = 2;        ///< How many to request; the driver may give more, and the actual count is what imageCount() reports.

            /** @brief Sets the minimum number of swap-chain images. */
            /** @brief Sets the number of swap-chain images to request. */
            Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            /** @brief Creates the scheduler for the active backend. Owned by the caller. */
            [[nodiscard]] std::unique_ptr<Scheduler> build() const;
        };

        virtual ~Scheduler() = default;

        /** @brief Creates the swap chain, the frames and their command buffers. Called once by the window. */
    	virtual void Initialize() = 0;

        /** @brief How many frames are in flight — the actual swap-chain image count, which may exceed what was requested. */
        [[nodiscard]] glm::u32 imageCount() const { return _imageCount; }

        /**
         * @brief Which frame is currently being recorded.
         * @return An index below imageCount(). A per-frame buffer or image uses this to select
         *         the copy that is safe to write this frame.
         */
    	[[nodiscard]] virtual glm::u32 currentImageIndex() const { return _currentFrame; }

        /** @brief The frame currently being recorded. */
        [[nodiscard]] const kor::Frame &currentFrame() const { return *_frames.at(_currentFrame); }

        /** @brief The frame that will be recorded next, wrapping round at the end. */
        [[nodiscard]] const kor::Frame &nextFrame() const { return *_frames.at((_currentFrame + 1) % _imageCount); }

        /** @brief Moves on to the next frame. Called by Draw(); a scene should not. */
    	void advanceFrame() { _currentFrame = (_currentFrame + 1) % _imageCount; }

        /** @brief Every frame in flight, in index order. */
        [[nodiscard]] std::vector<std::reference_wrapper<Frame>> frames() const
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
    	[[nodiscard]] bool hasStarted() const { return _started; }

        /** @brief Blocks until the GPU has finished everything submitted so far. Used when tearing down. */
    	virtual void WaitIdle() const = 0;

        /**
         * @brief Every frame index except @p index.
         * @return The frames a write to @p index still has to be propagated to; see PendingWrite.
         */
    	std::unordered_set<glm::u32> imageIndicesExcept(const glm::u32 index) const
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
    	virtual void createFrames() = 0;
        explicit Scheduler(const Builder& createInfo);
    	bool _started = false;

        glm::u32 _imageCount;
	    glm::u32 _currentFrame = 0;
    	std::vector<std::unique_ptr<Frame>> _frames;
    };
}

