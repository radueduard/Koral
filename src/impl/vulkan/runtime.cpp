//
// Created by radue on 2/27/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "runtime.h"

#include <ranges>

#include "vulkanContext.h"
#include "io/window.h"

namespace gfx::vk
{
    std::vector <const char*> GetRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector <const char*> extensions;
        for (uint32_t i = 0; i < glfwExtensionCount; i++) {
            extensions.emplace_back(glfwExtensions[i]);
        }
        return extensions;
    }

    ::vk::SurfaceKHR CreateSurface(const ::vk::Instance& instance, const io::Window& window) {
        VkSurfaceKHR surface;
        if (const auto result = glfwCreateWindowSurface(instance, *window, nullptr, &surface); result != VK_SUCCESS) {
            std::cerr << "Failed to create window surface: " << ::vk::to_string(static_cast<::vk::Result>(result)) << std::endl;
        }
        return { surface };
    }

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    const VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

        // Choose color
        auto color = "\x1b[37m";
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
            color = "\x1b[34m";
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            color = "\x1b[31m";
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            color = "\x1b[33m";
        }

        // Build severity label
        std::string severityLabel;
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            severityLabel = "ERROR";
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            severityLabel = "WARNING";
        } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            severityLabel = "INFO";
        } else {
            severityLabel = "VERBOSE";
        }

        // Build type label(s)
        std::string typeLabel;
        bool first = true;
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
            typeLabel += "GENERAL"; first = false;
        }
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
            if (!first) typeLabel += "|"; typeLabel += "VALIDATION"; first = false;
        }
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
            if (!first) typeLabel += "|"; typeLabel += "PERFORMANCE"; first = false;
        }
        // Print colored message with source location
        std::cout << color << "[" << severityLabel << "][" << typeLabel << "] "
                  << pCallbackData->pMessage << "\x1b[0m" << std::endl;

        return messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? VK_TRUE : VK_FALSE;
    }

    Runtime::Runtime()
    {
        constexpr auto applicationInfo = ::vk::ApplicationInfo()
            .setPApplicationName("GFXFramework Application")
            .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
            .setPEngineName("GFXFramework")
            .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
            .setApiVersion(VK_API_VERSION_1_3);

        const auto windowExtensions = GetRequiredExtensions();
        for (const auto& extension : windowExtensions) {
            _instanceExtensions.emplace_back(extension);
        }

        const auto createInfo = ::vk::InstanceCreateInfo()
        #ifdef __APPLE__
            .setFlags(::vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR)
        #endif
            .setPApplicationInfo(&applicationInfo)
            .setPEnabledExtensionNames(_instanceExtensions)
            .setPEnabledLayerNames(_instanceLayers);

        _instance = ::vk::createInstance(createInfo);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);

        const auto debugCreateInfo = ::vk::DebugUtilsMessengerCreateInfoEXT()
            // .setPNext(&validationCreateInfo)
            .setMessageSeverity(
                ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
                // | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
            )
            .setMessageType(
                ::vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                ::vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation)
            .setPfnUserCallback(reinterpret_cast<::vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback));

        _debugMessenger = _instance.createDebugUtilsMessengerEXT(debugCreateInfo);
    }

    Runtime::~Runtime()
    {
        _instance.destroyDebugUtilsMessengerEXT(_debugMessenger);
        _instance.destroy();
    }

    void Runtime::selectPhysicalDevice()
    {
        auto physicalDevices = _instance.enumeratePhysicalDevices();
        std::ranges::sort(physicalDevices, [] (const ::vk::PhysicalDevice& a, const ::vk::PhysicalDevice& b) {
            const auto deviceTypeScore = [] (const ::vk::PhysicalDeviceType type) {
                switch (type) {
                    case ::vk::PhysicalDeviceType::eDiscreteGpu: return 4;
                    case ::vk::PhysicalDeviceType::eIntegratedGpu: return 3;
                    case ::vk::PhysicalDeviceType::eVirtualGpu: return 2;
                    case ::vk::PhysicalDeviceType::eCpu: return 1;
                    default: return 0;
                }
            };
            const auto propertiesA = a.getProperties();
            const auto propertiesB = b.getProperties();
            if (deviceTypeScore(propertiesA.deviceType) != deviceTypeScore(propertiesB.deviceType)) {
                return deviceTypeScore(propertiesA.deviceType) > deviceTypeScore(propertiesB.deviceType);
            }
            if (propertiesA.limits.maxImageDimension2D != propertiesB.limits.maxImageDimension2D) {
                return propertiesA.limits.maxImageDimension2D > propertiesB.limits.maxImageDimension2D;
            }
            return false;
        });

        for (const auto& physicalDeviceCandidate : physicalDevices) {
            if (auto physicalDevice = std::make_unique<PhysicalDevice>(physicalDeviceCandidate); physicalDevice->isSuitable())
            {
                std::cout << "Selected physical device: " << physicalDevice->getProperties().deviceName << std::endl;
                std::cout << "Device type: " << ::vk::to_string(physicalDevice->getProperties().deviceType) << std::endl;

                _physicalDevice = std::move(physicalDevice);
                return;
            }
        }
        if (!_physicalDevice) {
            throw std::runtime_error("Failed to find a suitable physical device!");
        }
    }
}
