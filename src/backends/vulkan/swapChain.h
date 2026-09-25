//
// Created by radue on 2/28/2026.
//

#pragma once
#include <atomic>
#include <memory>
#include <vector>

#include <glm/fwd.hpp>

#include "device.h"
#include "imageView.h"

#include <image.h>

#include "vk_enum_conversions.h"
#include "vk_wrapper.h"

namespace kor::vk
{
    class Surface;
    class Image;
    class Frame;

    /**
     * One window's swap chain. Every window has one; the scheduler acquires an image from each at
     * the start of a frame and presents them all together at the end.
     *
     * How many images it has is the driver's business, and which one it hands out next is too, so
     * nothing else is sized or indexed by them: per-frame resources follow the scheduler's frames in
     * flight, and only the swap chain's own image follows the image it acquired (Image::CopyIndex).
     */
    class SwapChain final : public kor::vk::Wrapper<::vk::SwapchainKHR>
    {
    public:
        struct Builder {
            explicit Builder(const Surface& surface) : surface(surface) {}

            std::reference_wrapper<const Surface> surface;
            glm::u32 imageCount = 2;        ///< The fewest images to ask for.
            glm::u32 framesInFlight = 2;    ///< How many frames may be acquiring at once: one semaphore each.
            glm::uvec2 extent {1, 1};
            bool vsync = true;
            bool transparent = false;
            SampleCount sampleCount = SampleCount::e1;

            Builder& setImageCount(const glm::u32 imageCount) { this->imageCount = imageCount; return *this; }
            Builder& setFramesInFlight(const glm::u32 frames) { this->framesInFlight = frames; return *this; }
            Builder& setExtent(const glm::uvec2 extent) { this->extent = extent; return *this; }
            Builder& setVSync(const bool vsync) { this->vsync = vsync; return *this; }
            Builder& setTransparent(const bool transparent) { this->transparent = transparent; return *this; }
            Builder& setSampleCount(const kor::SampleCount sampleCount) { this->sampleCount = sampleCount; return *this; }
            std::unique_ptr<SwapChain> build() { return std::make_unique<SwapChain>(*this); }
        };

        explicit SwapChain(const Builder& createInfo);
        ~SwapChain() override;

        [[nodiscard]] const glm::uvec2 &extent() const { return _extent; }
        [[nodiscard]] glm::u32 imageCount() const { return _imageCount; }
        [[nodiscard]] ::vk::SampleCountFlagBits getVkSamples() const { return getVkSampleCount(_sampleCount); }

        [[nodiscard]] kor::ResourceRef<const kor::Image> image() const { return _swapChainImages; }
        [[nodiscard]] kor::ResourceRef<const kor::Image> getDepthImage() const { return _depthImages; }

        // Tracked refs, not raw references: these become the default framebuffer's attachments, and
        // a swap chain rebuilt by a resize replaces the views behind them. A ref notices; a
        // reference would be left pointing at the old ones.
        [[nodiscard]] kor::ResourceRef<const kor::ImageView> getSwapChainImageViews() const { return _swapChainImageViews; }
        [[nodiscard]] kor::ResourceRef<const kor::ImageView> getDepthImageViews() const { return _depthImageViews; }

        [[nodiscard]] ::vk::Format getImageFormat() const { return _surfaceFormat.format; }
        [[nodiscard]] glm::u32 currentImageIndex() const { return _imageIndex; }

    	[[nodiscard]] ::vk::Semaphore getCurrentRenderFinishedSemaphore() const { return _renderFinishedSemaphores[_imageIndex]; }
        /// Signalled by the acquire made for frame @p slot; what that frame's submit waits for.
        [[nodiscard]] ::vk::Semaphore getImageAvailableSemaphore(const glm::u32 slot) const { return _imageAvailable[slot]; }
        [[nodiscard]] const kor::vk::Queue& getPresentQueue() const { return _presentQueue; }

        /// Rebuilds it at @p newSize, depth target included. Waits for the device to go idle.
        void Resize(const glm::uvec2& newSize);

        /// Acquires the next image for frame @p slot, signalling that slot's semaphore.
        ::vk::Result Acquire(glm::u32 slot);
        /// Replaces frame @p slot's semaphore, after an acquire that failed may have left it pending.
        void ResetImageAvailable(glm::u32 slot);

        /**
         * Waits until whichever frame last rendered into the just-acquired image has finished, then
         * records @p frameFence as that image's owner. Call it after Acquire and before the submit.
         */
        void ClaimAcquiredImage(const ::vk::Fence& frameFence);

    private:
        glm::uvec2 _extent;
        bool _vsync = true;
        bool _transparent = false;
        SampleCount _sampleCount = SampleCount::e1;
        /// What was asked for, held separately from _imageCount because _imageCount is replaced
        /// by the driver's actual count — and re-requesting that on a Resize would ratchet it up.
        glm::u32 _requestedImageCount = 0;
        glm::u32 _imageCount = 0;   ///< What the driver actually allocated.
        // Written by Acquire on the main thread, read by any thread whose End() picks a per-frame
        // resource's copy (they all go through CurrentImageIndex()). Atomic so that read is not a race.
        std::atomic<glm::u32> _imageIndex = 0;

        std::reference_wrapper<const Surface> _surface;
        ::vk::SurfaceFormatKHR _surfaceFormat = {};
        ::vk::PresentModeKHR _presentMode = {};

        const kor::vk::Queue& _presentQueue;
        kor::Resource<kor::Image> _swapChainImages;
        kor::Resource<kor::Image> _depthImages;
        std::vector<::vk::Semaphore> _renderFinishedSemaphores;
        std::vector<::vk::Semaphore> _imageAvailable;   // one per frame in flight

        // The in-flight fence of the frame that last rendered into each image, or null for an image
        // nothing has touched yet. Not owned — the fences belong to the scheduler's frames.
        std::vector<::vk::Fence> _imagesInFlight;

        kor::Resource<kor::ImageView> _swapChainImageViews;
        kor::Resource<kor::ImageView> _depthImageViews;

        void CreateSwapChain();
        /// The per-frame depth target, sized to the frames in flight — not to the images.
        void CreateDepthResources();

        static ::vk::SurfaceFormatKHR ChooseSurfaceFormat(const std::vector<::vk::SurfaceFormatKHR> &availableFormats);
        static ::vk::PresentModeKHR ChoosePresentMode(const std::vector<::vk::PresentModeKHR> &availablePresentModes, bool vsync);
        static glm::uvec2 ChooseExtent(const ::vk::SurfaceCapabilitiesKHR &capabilities, const glm::uvec2& extent);
    };
}
