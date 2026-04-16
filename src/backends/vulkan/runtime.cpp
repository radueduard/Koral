//
// Created by radue on 2/27/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "runtime.h"
#include <framebuffer.h>
#include <surface.h>

#include <iostream>
#include <ranges>

#include <window.h>
#include <GLFW/glfw3.h>

#include "../../log.h"

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

        switch (messageSeverity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
                gfx::log::info("[vulkan] {}", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                gfx::log::info("[vulkan] {}", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                gfx::log::warn("[vulkan] {}", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                gfx::log::error("[vulkan] {}", pCallbackData->pMessage);
                // GFX_BREAK(); // drop into debugger on Vulkan errors too
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
                break;
        }
        return VK_FALSE; // never abort the Vulkan call
    }

    Runtime::Runtime()
    {
        constexpr auto applicationInfo = ::vk::ApplicationInfo()
            .setPApplicationName("GFXFramework Application")
            .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
            .setPEngineName("GFXFramework")
            .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
            .setApiVersion(VK_API_VERSION_1_4);

        const auto windowExtensions = GetRequiredExtensions();
        for (const auto& extension : windowExtensions) {
            _instanceExtensions.emplace_back(extension);
        }

        constexpr std::array<uint32_t, 1> bufferSize = { 1024 * 1024 };
            const auto validationLayerSetting = ::vk::LayerSettingEXT()
                .setPLayerName("VK_LAYER_KHRONOS_validation")
                .setPSettingName("printf_buffer_size")
                .setType(::vk::LayerSettingTypeEXT::eUint32)
                .setValues(bufferSize);

            const auto layerSettingsInfo = ::vk::LayerSettingsCreateInfoEXT()
                .setSettings(validationLayerSetting);
                // pNext = nullptr (tail)

            // 2. Middle — validation features, points to layer settings
            const ::vk::ValidationFeatureEnableEXT enabledFeatures[] = {
                ::vk::ValidationFeatureEnableEXT::eDebugPrintf
            };

            const auto validationCreateInfo = ::vk::ValidationFeaturesEXT()
                .setPNext(&layerSettingsInfo)        // ← points to tail
                .setEnabledValidationFeatures(enabledFeatures);

            // 3. Debug messenger, points to validation features
            const auto debugCreateInfo = ::vk::DebugUtilsMessengerCreateInfoEXT()
                .setPNext(&validationCreateInfo)     // ← points to middle
                .setMessageSeverity(
                    ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                    | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                    // | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
                    | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
                )
                .setMessageType(
                    ::vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                    ::vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
                )
                .setPfnUserCallback(reinterpret_cast<::vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback));

            // 4. Head — instance create info, points to debug messenger
            const auto createInfo = ::vk::InstanceCreateInfo()
            #ifdef __APPLE__
                .setFlags(::vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR)
            #endif
                .setPNext(&debugCreateInfo)
                .setPApplicationInfo(&applicationInfo)
                .setPEnabledExtensionNames(_instanceExtensions)
                .setPEnabledLayerNames(_instanceLayers);

            _instance = ::vk::createInstance(createInfo);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);

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
