//
// Created by radue on 2/27/2026.
//

#include "physicalDevice.h"

#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <glm/fwd.hpp>

#include "runtime.h"
#include "vulkanContext.h"

namespace kor::vk
{
    PhysicalDevice::PhysicalDevice(::vk::PhysicalDevice physicalDevice)
    {
        _handle = physicalDevice;
        _properties = _handle.getProperties();
        _features = _handle.getFeatures();
        _memoryProperties = _handle.getMemoryProperties();
        _extensionProperties = _handle.enumerateDeviceExtensionProperties();
        _queueFamilyProperties = _handle.getQueueFamilyProperties();
    }

    bool PhysicalDevice::isSuitable() const {
        return hasRequiredQueueFamilies(kor::vk::Context::Runtime().getRequiredQueueFamilies())
            && hasRequiredExtensions(kor::vk::Context::Runtime().getRequiredDeviceExtensions())
            && hasRequiredFeatures(_features);
    }

    bool PhysicalDevice::supportsExtension(const std::string_view name) const {
        return std::ranges::any_of(_extensionProperties, [name](const ::vk::ExtensionProperties& extension) {
            return name == std::string_view(extension.extensionName.data());
        });
    }

    bool PhysicalDevice::hasRequiredQueueFamilies(const ::vk::QueueFlags& requiredQueueFamilies) const {
        ::vk::QueueFlags supportedQueueFamilies {};

        for (const auto& queueFamily : _queueFamilyProperties) {
            const auto supportedByThisFamily = queueFamily.queueFlags & requiredQueueFamilies;
            supportedQueueFamilies |= supportedByThisFamily;
        }

        return requiredQueueFamilies == supportedQueueFamilies;
    }

    bool PhysicalDevice::hasRequiredExtensions(const std::vector<const char*>& requiredExtensions) const {
        auto remainingExtensions = std::unordered_set<std::string>{ requiredExtensions.begin(), requiredExtensions.end() };
        for (const auto& availableExtension : _extensionProperties) {
            std::string extensionName = availableExtension.extensionName;
            remainingExtensions.erase(extensionName);
        }
        for (const auto& extension : remainingExtensions) {
            std::cerr << "PhysicalDevice : Missing required extension " << extension << std::endl;
        }

        return remainingExtensions.empty();
    }

    bool PhysicalDevice::hasRequiredFeatures(const ::vk::PhysicalDeviceFeatures& requiredFeatures) const {
        const auto requiredFeaturesPtr = reinterpret_cast<const ::vk::Bool32*>(&requiredFeatures);
        const auto deviceFeaturesPtr = reinterpret_cast<const ::vk::Bool32*>(&_features);

        for (size_t i = 0; i < sizeof(::vk::PhysicalDeviceFeatures) / sizeof(::vk::Bool32); ++i) {
            if (requiredFeaturesPtr[i] && !deviceFeaturesPtr[i]) {
                return false;
            }
        }

        return true;
    }
}
