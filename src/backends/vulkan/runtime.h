//
// Created by radue on 2/27/2026.
//

#pragma once

#include <memory>
#include <vulkan/vulkan.hpp>

#include "physicalDevice.h"

namespace gfx::vk
{
    struct DeviceFeatures
    {
        ::vk::PhysicalDeviceFeatures deviceFeatures;
    };

    class Runtime
    {
    public:
        Runtime();
        ~Runtime();

        void selectPhysicalDevice();

        [[nodiscard]] const ::vk::QueueFlags& getRequiredQueueFamilies() const { return _requiredQueues; }
        [[nodiscard]] std::vector<const char*> getDeviceExtensions() const
        {
            std::vector<const char*> extensions {};
            for (const auto& extension : _deviceExtensions) {
                extensions.push_back(extension);
            }
            return extensions;
        }

        [[nodiscard]] ::vk::Instance getInstance() const { return _instance; }
        [[nodiscard]] const PhysicalDevice& getPhysicalDevice() const { return *_physicalDevice; }

    private:
        ::vk::Instance _instance;
        std::vector<const char*> _instanceLayers {
            "VK_LAYER_KHRONOS_validation",
        };
        std::vector<const char*> _instanceExtensions {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __APPLE__
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
            VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
        };
        std::vector<const char*> _deviceExtensions {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
#ifdef __APPLE__
            "VK_KHR_portability_subset",
#endif
            VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
            // VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
        };

        ::vk::QueueFlags _requiredQueues = ::vk::QueueFlags()
            | ::vk::QueueFlagBits::eGraphics
            | ::vk::QueueFlagBits::eCompute
            | ::vk::QueueFlagBits::eTransfer;

        ::vk::DebugUtilsMessengerEXT _debugMessenger;

        std::unique_ptr<gfx::vk::PhysicalDevice> _physicalDevice = nullptr;
    };
}
