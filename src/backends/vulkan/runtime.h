//
// Created by radue on 2/27/2026.
//

#pragma once

#include <memory>
#include <vulkan/vulkan.hpp>

#include "physicalDevice.h"

namespace kor::vk
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

        // What a physical device must support to be considered at all (see PhysicalDevice::isSuitable).
        [[nodiscard]] const std::vector<const char*>& getRequiredDeviceExtensions() const { return _requiredDeviceExtensions; }

        // Not universally supported — ray tracing being the obvious example: plenty of real
        // hardware (older/integrated GPUs, MoltenVK on macOS) has no acceleration-structure or
        // ray-tracing-pipeline support at all. A device is not rejected for lacking these; they
        // are only requested from vk::DeviceCreateInfo when the selected device actually has them
        // (see Device::Device(), which filters this list through PhysicalDevice::supportsExtension).
        [[nodiscard]] const std::vector<const char*>& getOptionalDeviceExtensions() const { return _optionalDeviceExtensions; }

        [[nodiscard]] ::vk::Instance getInstance() const { return _instance; }
        [[nodiscard]] const PhysicalDevice& getPhysicalDevice() const { return *_physicalDevice; }

    private:
        ::vk::Instance _instance;
        std::vector<const char*> _instanceLayers {
#ifndef NDEBUG
            "VK_LAYER_KHRONOS_validation",
#endif
        };
        std::vector<const char*> _instanceExtensions {
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __APPLE__
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
        };
        std::vector<const char*> _requiredDeviceExtensions {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
#ifdef __APPLE__
            "VK_KHR_portability_subset",
#endif
            VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
            // VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
        };
        std::vector<const char*> _optionalDeviceExtensions {
            // Ray tracing. All three or none: they only mean anything as a group.
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        };

        ::vk::QueueFlags _requiredQueues = ::vk::QueueFlags()
            | ::vk::QueueFlagBits::eGraphics
            | ::vk::QueueFlagBits::eCompute
            | ::vk::QueueFlagBits::eTransfer;

        ::vk::DebugUtilsMessengerEXT _debugMessenger;

        std::unique_ptr<kor::vk::PhysicalDevice> _physicalDevice = nullptr;
    };
}
