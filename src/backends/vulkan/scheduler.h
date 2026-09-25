//
// Created by radue on 2/28/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "device.h"
#include "swapChain.h"
#include "../../../include/scheduler.h"

namespace kor::vk
{
	class Frame final : public kor::Frame
	{
	public:
		explicit Frame(glm::u32 imageIndex, const Queue& queue);
		~Frame() override;

		Frame(const Frame&) = delete;
		Frame& operator=(const Frame&) = delete;

		[[nodiscard]] ::vk::Semaphore getImageAvailableSemaphore() const { return _imageAvailable; }
		[[nodiscard]] ::vk::Fence getInFlightFence() const { return _inFlightFence; }

		[[nodiscard]] const Queue& getQueue() const { return _queue; }

		void ResetSemaphore() const;

		// What this frame's last submission needs kept alive until its fence says the GPU is done:
		// the command buffers handed to Scheduler::Execute(), and the tokens it waited on or
		// signalled (their timeline semaphores). Released once the fence has been waited on.
		void hold(std::vector<std::unique_ptr<kor::CommandBuffer>> commandBuffers, std::vector<Token> tokens) const;
		void release() const;

	private:
		mutable std::vector<std::unique_ptr<kor::CommandBuffer>> _executed;
		mutable std::vector<Token> _tokens;
		const Queue& _queue;
		mutable ::vk::Semaphore _imageAvailable;
    	::vk::Fence _inFlightFence;
	};

    // One batch: its command buffers run in the order given. Values sit alongside semaphores and
    // only matter for timeline ones; a binary semaphore's is ignored (pass 0).
    struct SubmitInfo {
        std::vector<::vk::CommandBuffer> commandBuffers;
        std::vector<::vk::Semaphore> waitSemaphores;
        std::vector<std::uint64_t> waitValues;
        std::vector<::vk::PipelineStageFlags> waitStages;
        std::vector<::vk::Semaphore> signalSemaphores;
        std::vector<std::uint64_t> signalValues;
        ::vk::Fence fence = nullptr;
    };

    class Scheduler final : public kor::Scheduler {
    public:
    	explicit Scheduler(const Builder &createInfo);
    	void Initialize() override;

    	~Scheduler() override;
    	Scheduler(const Scheduler &) = delete;
    	Scheduler &operator=(const Scheduler &) = delete;

    	void Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) override;

    	[[nodiscard]] const kor::vk::SwapChain &getSwapChain() const { return *_swapChain; }
    	[[nodiscard]] bool isResized() const { return _resized; }
		glm::u32 CurrentImageIndex() const override { return _swapChain->currentImageIndex(); }


    private:
    	std::unique_ptr<kor::vk::SwapChain> _swapChain;

    	/// Takes the swap chain's actual image count as this scheduler's, then (re)builds everything
    	/// sized to it. Runs at Initialize and again on every resize.
    	void adoptSwapChainSizing();

    	/// Resize the swap chain, re-adopt its sizing, and re-point the default framebuffer — in that
    	/// order, because each step feeds the next.
    	void recreateSwapChain(const glm::uvec2& extent);

    	void CreateFrames() override;

    public:
	    void WaitIdle() const override;

    private:
	    bool _resized = false;
    };
}
