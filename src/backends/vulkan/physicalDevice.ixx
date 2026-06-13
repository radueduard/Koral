//
// Created by radue on 2/27/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module gfx:vk_physicalDevice;
import :vk_types;

namespace gfx::vk
{
    export class PhysicalDevice final : public gfx::vk::Wrapper<::vk::PhysicalDevice>
    {
    public:
        explicit PhysicalDevice(::vk::PhysicalDevice physicalDevice);
        ~PhysicalDevice() override = default;
        PhysicalDevice(const PhysicalDevice &) = delete;
        PhysicalDevice &operator=(const PhysicalDevice &) = delete;

        [[nodiscard]] bool isSuitable() const;
        [[nodiscard]] const std::vector<::vk::QueueFamilyProperties>& getQueueFamilyProperties() const { return _queueFamilyProperties; }

    	[[nodiscard]] const ::vk::PhysicalDeviceProperties& getProperties() const { return _properties; }
		[[nodiscard]] const ::vk::PhysicalDeviceFeatures& getFeatures() const { return _features; }
    	[[nodiscard]] const ::vk::PhysicalDeviceMemoryProperties& getMemoryProperties() const { return _memoryProperties; }

    private:
        [[nodiscard]] bool hasRequiredQueueFamilies(const ::vk::QueueFlags&) const;
        [[nodiscard]] bool hasRequiredExtensions(const std::vector<const char*>& requiredExtensions) const;
        [[nodiscard]] bool hasRequiredFeatures(const ::vk::PhysicalDeviceFeatures& requiredFeatures) const;
        // [[nodiscard]] bool isSwapChainSupported() const { return !_formats.empty() && !_presentModes.empty(); }

        ::vk::PhysicalDeviceProperties _properties;
        ::vk::PhysicalDeviceFeatures _features;
        ::vk::PhysicalDeviceMemoryProperties _memoryProperties;
        ::std::vector<::vk::ExtensionProperties> _extensionProperties;

        std::vector<::vk::QueueFamilyProperties> _queueFamilyProperties;
    };
}
