//
// Created by radue on 3/7/2026.
//

#include "surface.h"
#include <framebuffer.h>
#include <surface.h>
#include <window.h>

#include <iostream>

#include "runtime.h"
#include "vulkanContext.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


namespace kor::vk
{
    Surface::Surface(const kor::Window& window) : kor::Surface(window)
    {
        const auto instance = Context::Runtime().getInstance();
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (const auto result = glfwCreateWindowSurface(instance, *window, nullptr, &surface); result != VK_SUCCESS) {
            // Unrecoverable: the swap chain and all presentation depend on a valid
            // surface. Fail fast with a clear message instead of continuing with a
            // null handle (which later trips validation and crashes).
            const auto msg = "Failed to create window surface: " + ::vk::to_string(static_cast<::vk::Result>(result));
            kor::log::Error("[vulkan] {}", msg);
            throw std::runtime_error(msg);
        }
        _handle = surface;

        _capabilities = physicalDevice->getSurfaceCapabilitiesKHR(_handle);
        _formats = physicalDevice->getSurfaceFormatsKHR(_handle);
        _presentModes = physicalDevice->getSurfacePresentModesKHR(_handle);
    }

    void Surface::CreateSwapChain(const kor::Window& window, const glm::u32 framesInFlight)
    {
        _swapChain = SwapChain::Builder(*this)
            .setImageCount(2)
            .setFramesInFlight(framesInFlight)
            .setExtent(window.Extent())
            .setVSync(window.IsVSync())
            .setTransparent(window.IsFramebufferTransparent())
            .setAlphaVisual(glfwGetPlatform() == GLFW_PLATFORM_X11
                            && glfwGetWindowAttrib(*window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE)
            .setFormats(window.RequestedFormats())
            .setSampleCount(SampleCount::e1)
            .build();
    }

    Surface::~Surface()
    {
        // The swap chain first: it is made from the surface, and has to go before it does.
        _swapChain.reset();
        if (_handle) {
            Context::Runtime().getInstance().destroySurfaceKHR(_handle);
        }
    }
}
