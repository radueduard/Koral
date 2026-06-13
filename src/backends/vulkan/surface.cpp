//
// Created by radue on 3/7/2026.
//

module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_to_string.hpp>

module gfx;
import :vk_surface;

import :surface;
import :window;
import :vk_context;
import :vk_runtime;

namespace gfx::vk
{
    Surface::Surface(const gfx::Window& window) : gfx::Surface(window)
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

    const std::vector<::vk::SurfaceFormatKHR>& Surface::getFormats() const
    {
        _formats = vk::Context::Runtime().getPhysicalDevice()->getSurfaceFormatsKHR(**this);
        return _formats;
    }

    const std::vector<::vk::PresentModeKHR>& Surface::getPresentModes() const
    {
        _presentModes = vk::Context::Runtime().getPhysicalDevice()->getSurfacePresentModesKHR(**this);
        return _presentModes;
    }

    const ::vk::SurfaceCapabilitiesKHR& Surface::getCapabilities() const
    {
        _capabilities = vk::Context::Runtime().getPhysicalDevice()->getSurfaceCapabilitiesKHR(**this);
        return _capabilities;
    }
}
