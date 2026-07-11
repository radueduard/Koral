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


namespace gfx::vk
{
    Surface::Surface(const gfx::Window& window) : gfx::Surface(window)
    {
        const auto instance = Context::Runtime().getInstance();
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (const auto result = glfwCreateWindowSurface(instance, *window, nullptr, &surface); result != VK_SUCCESS) {
            // Unrecoverable: the swap chain and all presentation depend on a valid
            // surface. Fail fast with a clear message instead of continuing with a
            // null handle (which later trips validation and crashes).
            const auto msg = "Failed to create window surface: " + ::vk::to_string(static_cast<::vk::Result>(result));
            gfx::log::error("[vulkan] {}", msg);
            throw std::runtime_error(msg);
        }
        _handle = surface;

        _capabilities = physicalDevice->getSurfaceCapabilitiesKHR(_handle);
        _formats = physicalDevice->getSurfaceFormatsKHR(_handle);
        _presentModes = physicalDevice->getSurfacePresentModesKHR(_handle);
    }

    Surface::~Surface()
    {
        if (_handle) {
            Context::Runtime().getInstance().destroySurfaceKHR(_handle);
        }
    }
}
