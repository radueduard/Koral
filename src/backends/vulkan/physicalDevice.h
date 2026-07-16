//
// Created by radue on 2/27/2026.
//

#pragma once

#include "vk_wrapper.h"
#include <string_view>
#include <vulkan/vulkan.hpp>

namespace kor::vk
{
    class PhysicalDevice final : public kor::vk::Wrapper<::vk::PhysicalDevice>
    {
    public:
        explicit PhysicalDevice(::vk::PhysicalDevice physicalDevice);
        ~PhysicalDevice() override = default;
        PhysicalDevice(const PhysicalDevice &) = delete;
        PhysicalDevice &operator=(const PhysicalDevice &) = delete;

        [[nodiscard]] bool isSuitable() const;
        [[nodiscard]] const std::vector<::vk::QueueFamilyProperties>& getQueueFamilyProperties() const { return _queueFamilyProperties; }

        // Whether this physical device advertises a given extension. Used to enable optional
        // extensions (ray tracing, ...) only where they actually exist, instead of demanding them
        // up front and rejecting every device that lacks them.
        [[nodiscard]] bool supportsExtension(std::string_view name) const;

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
