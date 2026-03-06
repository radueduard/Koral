//
// Created by radue on 2/28/2026.
//

#include "scheduler.h"

#include "swapChain.h"
#include "vulkanContext.h"

#include "io/window.h"


namespace gfx::vk
{
    Frame::Frame(const glm::u32 imageIndex, const gfx::vk::Queue& queue)
        : _imageIndex(imageIndex), _queue(queue)
    {
        _imageAvailable = vk::Context::Device()->createSemaphore({});
        _readyToPresent = vk::Context::Device()->createSemaphore({});
        _inFlightFence = Context::Device()->createFence(::vk::FenceCreateInfo().setFlags(::vk::FenceCreateFlagBits::eSignaled));
    }

	Frame::~Frame() {
        Context::Device()->destroySemaphore(_imageAvailable);
        Context::Device()->destroySemaphore(_readyToPresent);
        Context::Device()->destroyFence(_inFlightFence);
    }

    Scheduler::Scheduler(const Builder& createInfo)
        : _imageCount(createInfo.imageCount), _msaa(createInfo.msaa)
    {

        const auto& queue = Context::Device().requestPresentQueue();
        createFrames(queue);

        _swapChain = gfx::vk::SwapChain::Builder()
            .setMinImageCount(createInfo.minImageCount)
            .setImageCount(_imageCount)
            .setMSAA(_msaa)
            .build();
        createDescriptorPool();
    }

    Scheduler::~Scheduler() {
        Context::Device()->waitIdle();
    }

    void Scheduler::Draw() {
        const auto& frame = getCurrentFrame();

        const auto& fence = frame.getInFlightFence();
        if (const auto result = Context::Device()->waitForFences(1, &fence, ::vk::True, UINT64_MAX); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to wait fence: " + ::vk::to_string(result));
        }

        if (const auto result = Context::Device()->resetFences(1, &fence); result != ::vk::Result::eSuccess) {
            throw std::runtime_error("Failed to reset fence: " + ::vk::to_string(result));
        }

        if (const auto result = _swapChain->Acquire(frame);
            result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {
            _swapChain->Resize(gfx::Context::Window().getExtent());
        }

        if (const auto result = _swapChain->Present(frame);
            result == ::vk::Result::eErrorOutOfDateKHR || result == ::vk::Result::eSuboptimalKHR) {

            _swapChain->Resize(gfx::Context::Window().getExtent());
            return;
        }

        advanceFrame();
    }

    std::vector<Frame*> Scheduler::getFrames() const
    {
        std::vector<Frame*> frames;
        for (const auto& frame : _frames) {
            frames.emplace_back(frame.get());
        }
        return frames;
    }

    void Scheduler::createFrames(const Queue& queue) {
        _frames.reserve(_imageCount);
        for (uint32_t i = 0; i < _imageCount; i++) {
            _frames.emplace_back(std::make_unique<Frame>(i, queue));
        }
    }


    void Scheduler::createDescriptorPool() {
        _descriptorPool = gfx::vk::DescriptorPool::Builder()
            .addPoolSize(::vk::DescriptorType::eUniformBuffer, 1000)
            .addPoolSize(::vk::DescriptorType::eStorageBuffer, 1000)
            .addPoolSize(::vk::DescriptorType::eCombinedImageSampler, 1000)
            .addPoolSize(::vk::DescriptorType::eStorageImage, 1000)
            .addPoolSize(::vk::DescriptorType::eSampler, 1000)
            .setPoolFlags(::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(1000)
            .build();
    }
}
