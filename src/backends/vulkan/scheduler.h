//
// Created by radue on 2/28/2026.
//

#pragma once
#include <glm/fwd.hpp>
#include <map>

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

		[[nodiscard]] ::vk::Fence getInFlightFence() const { return _inFlightFence; }

		[[nodiscard]] const Queue& getQueue() const { return _queue; }

		// What this frame's last submission needs kept alive until its fence says the GPU is done:
		// the command buffers handed to Scheduler::Execute(), and the tokens it waited on or
		// signalled (their timeline semaphores). Released once the fence has been waited on.
		void hold(std::vector<std::unique_ptr<kor::CommandBuffer>> commandBuffers, std::vector<Token> tokens) const;
		void release() const;

	private:
		mutable std::vector<std::unique_ptr<kor::CommandBuffer>> _executed;
		mutable std::vector<Token> _tokens;
		const Queue& _queue;
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

    	void Draw(std::span<kor::Window* const> windows, const std::function<void(kor::CommandBuffer&)>& renderFunc) override;

    	void RetireWindow(std::shared_ptr<kor::Surface> surface, GLFWwindow* window) override;

    protected:
    	[[nodiscard]] std::uint32_t QueueOf(const kor::CommandBuffer& commandBuffer) const override;

    private:
    	/// Rebuild @p window's swap chain at its current size and re-point its default framebuffer.
    	static void recreateSwapChain(kor::Window& window);

    	void CreateFrames() override;

    	/// Destroys the retired windows whose last frame has finished; all of them with @p all.
    	void destroyRetiredWindows(bool all);

    	struct Retired {
    		std::shared_ptr<kor::Surface> surface;
    		GLFWwindow* window = nullptr;
    		Token lastUse;   // the frame being built when it closed
    	};
    	std::vector<Retired> _retired;
    	bool _drawnOnce = false;
    	// One per queue: every submission of a frame signals its queue's next value, which is what
    	// work on another queue waits for.
    	std::map<glm::u32, kor::Timeline> _queueTimelines;

    public:
	    void WaitIdle() const override;
    };
}
