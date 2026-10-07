//
// Created by radue on 3/7/2026.
//

#pragma once
#include <surface.h>

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

#include "runtime.h"
#include "swapChain.h"
#include "vulkanContext.h"

namespace kor
{
    class Window;
}

namespace kor::vk
{
    class Surface final : public kor::Surface, public Wrapper<::vk::SurfaceKHR>
    {
    public:
        explicit Surface(const kor::Window& window);
        ~Surface() override;

        /** @brief Creates the swap chain presenting to this surface, sized to @p window. */
        void CreateSwapChain(const kor::Window& window, kor::u32 framesInFlight);
        [[nodiscard]] bool HasSwapChain() const { return _swapChain != nullptr; }
        [[nodiscard]] SwapChain& swapChain() const { return *_swapChain; }

        [[nodiscard]] const std::vector<::vk::SurfaceFormatKHR>& getFormats() const
        {
            _formats = vk::Context::Runtime().getPhysicalDevice()->getSurfaceFormatsKHR(**this);
            return _formats;
        }
        [[nodiscard]] const std::vector<::vk::PresentModeKHR>& getPresentModes() const
        {
            _presentModes = vk::Context::Runtime().getPhysicalDevice()->getSurfacePresentModesKHR(**this);
            return _presentModes;
        }
        [[nodiscard]] const ::vk::SurfaceCapabilitiesKHR& getCapabilities() const
        {
            _capabilities = vk::Context::Runtime().getPhysicalDevice()->getSurfaceCapabilitiesKHR(**this);
            return _capabilities;
        }
    private:
        std::unique_ptr<SwapChain> _swapChain;
        mutable ::vk::SurfaceCapabilitiesKHR _capabilities;
        mutable ::std::vector<::vk::SurfaceFormatKHR> _formats;
        mutable ::std::vector<::vk::PresentModeKHR> _presentModes;
    };
}
