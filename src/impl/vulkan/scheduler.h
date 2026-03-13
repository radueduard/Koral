//
// Created by radue on 2/28/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "device.h"
#include "swapChain.h"
#include "core/scheduler.h"

namespace gfx::vk
{
	class Frame final : public gfx::Frame
	{
	public:
		explicit Frame(glm::u32 imageIndex, const Queue& queue);
		~Frame() override;

		Frame(const Frame&) = delete;
		Frame& operator=(const Frame&) = delete;

		[[nodiscard]] ::vk::Semaphore getImageAvailableSemaphore() const { return _imageAvailable; }
		[[nodiscard]] ::vk::Fence getInFlightFence() const { return _inFlightFence; }

		[[nodiscard]] const Queue& getQueue() const { return _queue; }

	private:
		const Queue& _queue;
		::vk::Semaphore _imageAvailable;
    	::vk::Fence _inFlightFence;
	};

    struct SubmitInfo {
        const CommandBuffer& commandBuffer;
        std::vector<::vk::Semaphore> waitSemaphores;
        std::vector<::vk::PipelineStageFlags> waitStages;
        std::vector<::vk::Semaphore> signalSemaphores;
        ::vk::Fence fence = nullptr;
    };

    class Scheduler final : public gfx::Scheduler {
    public:
    	explicit Scheduler(const Builder &createInfo);
    	void Initialize() override;

    	~Scheduler() override;
    	Scheduler(const Scheduler &) = delete;
    	Scheduler &operator=(const Scheduler &) = delete;

    	void Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const override;

    	[[nodiscard]] const gfx::vk::SwapChain &getSwapChain() const { return *_swapChain; }
    	[[nodiscard]] bool isResized() const { return _resized; }


    private:
    	std::unique_ptr<gfx::vk::Surface> _surface;
    	std::unique_ptr<gfx::vk::SwapChain> _swapChain;

    	void createFrames() override;
    	bool _resized = false;
    };
}
