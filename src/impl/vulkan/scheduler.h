//
// Created by radue on 2/28/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "descriptorPool.h"
#include "device.h"
#include "swapChain.h"

namespace gfx::vk
{
	class Frame {
	public:
		explicit Frame(glm::u32 imageIndex, const gfx::vk::Queue& queue);
		~Frame();

		Frame(const Frame&) = delete;
		Frame& operator=(const Frame&) = delete;

		[[nodiscard]] glm::u32 getImageIndex() const { return _imageIndex; }
		[[nodiscard]] ::vk::Semaphore getImageAvailableSemaphore() const { return _imageAvailable; }
		[[nodiscard]] ::vk::Semaphore getReadyToPresentSemaphore() const { return _readyToPresent; }
		[[nodiscard]] ::vk::Fence getInFlightFence() const { return _inFlightFence; }

		[[nodiscard]] const gfx::vk::Queue& getQueue() const { return _queue; }


	private:
		glm::u32 _imageIndex;
		::vk::Semaphore _imageAvailable;
		::vk::Semaphore _readyToPresent;
    	::vk::Fence _inFlightFence;
		const gfx::vk::Queue& _queue;
	};

    struct SubmitInfo {
        const CommandBuffer& commandBuffer;
        std::vector<::vk::Semaphore> waitSemaphores;
        std::vector<::vk::PipelineStageFlags> waitStages;
        std::vector<::vk::Semaphore> signalSemaphores;
        ::vk::Fence fence = nullptr;
    };

    class Scheduler {
    public:
    	struct Builder {
    		glm::u32 minImageCount;
    		glm::u32 imageCount;
    		gfx::MSAA msaa;

    		Builder& setMinImageCount(const glm::u32 minImageCount) { this->minImageCount = minImageCount; return *this; }
    		Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
    		Builder& setMSAA(const gfx::MSAA msaa) { this->msaa = msaa; return *this; }
    		std::unique_ptr<Scheduler> build() { return std::make_unique<Scheduler>(*this); }

    	};

    	explicit Scheduler(const Builder &createInfo);
    	~Scheduler();
    	Scheduler(const Scheduler &) = delete;
    	Scheduler &operator=(const Scheduler &) = delete;

    	void Draw();

    	[[nodiscard]] const gfx::vk::SwapChain &getSwapChain() const { return *_swapChain; }
    	[[nodiscard]] const Frame &getCurrentFrame() const { return *_frames.at(_currentFrame); }
    	[[nodiscard]] const Frame &getNextFrame() const { return *_frames.at((_currentFrame + 1) % _imageCount); }
    	void advanceFrame() { _currentFrame = (_currentFrame + 1) % _imageCount; }
    	[[nodiscard]] const ::vk::Extent2D &getExtent() const { return _extent; }
    	[[nodiscard]] bool isResized() const { return _resized; }
    	[[nodiscard]] uint32_t getImageCount() const { return _imageCount; }

    	[[nodiscard]] std::vector<Frame*> getFrames() const;

    private:
    	std::unique_ptr<gfx::vk::Surface> _surface;

	    glm::u32 _imageCount;
    	MSAA _msaa = MSAA::e2x;
    	std::unique_ptr<gfx::vk::SwapChain> _swapChain;

    	void createFrames(const Queue& queue);
	    glm::u32 _currentFrame = 0;
    	std::vector<std::unique_ptr<Frame>> _frames;

    	bool _resized = false;
    	::vk::Extent2D _extent;
    };
}
