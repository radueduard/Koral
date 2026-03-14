//
// Created by radue on 3/7/2026.
//

#include "surface.h"

#include <iostream>

#include "runtime.h"
#include "vulkanContext.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "io/window.h"

namespace gfx::vk
{
    Surface::Surface(const gfx::io::Window& window) : gfx::Surface(window)
    {
        const auto instance = Context::Runtime().getInstance();
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();
        VkSurfaceKHR surface;
        if (const auto result = glfwCreateWindowSurface(instance, *window, nullptr, &surface); result != VK_SUCCESS) {
            std::cerr << "Failed to create window surface: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
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
