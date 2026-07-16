//
// Created by radue on 2/27/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "runtime.h"
#include <framebuffer.h>
#include <surface.h>

#include <cstdlib>
#include <iostream>
#include <ranges>

#include <window.h>
#include <GLFW/glfw3.h>

#include "../../../include/log.h"
#include "../../core/paths.h"

namespace kor::vk
{
    ::vk::SurfaceKHR CreateSurface(const ::vk::Instance& instance, const kor::Window& window) {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (const auto result = glfwCreateWindowSurface(instance, *window, nullptr, &surface); result != VK_SUCCESS) {
            // A null/garbage surface is unrecoverable — everything downstream (swap
            // chain, present) needs it — so fail fast with a clear message rather
            // than returning an uninitialized handle and crashing later.
            const auto msg = "Failed to create window surface: " + ::vk::to_string(static_cast<::vk::Result>(result));
            kor::log::error("[vulkan] {}", msg);
            throw std::runtime_error(msg);
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
                kor::log::info("[vulkan] {}", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                kor::log::info("[vulkan] {}", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                kor::log::warn("[vulkan] {}", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                kor::log::error("[vulkan] {}", pCallbackData->pMessage);
                // KORAL_BREAK();
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
                break;
        }
        return VK_FALSE;
    }

    namespace {
        // Add a dir/file to one of the loader's *additive* search-path env vars, but
        // only when the user hasn't set it themselves — an explicit override wins.
        void addLoaderSearchPath(const char* var, const char* value) {
            if (std::getenv(var) != nullptr) return;
#ifdef _WIN32
            _putenv_s(var, value);
#else
            setenv(var, value, /*overwrite=*/0);
#endif
        }
    }

    Runtime::Runtime()
    {
        // Point the vcpkg Vulkan-Loader at the bundled validation layers so they resolve
        // with no system Vulkan SDK present. VK_ADD_* are additive, so the user's own
        // layers/drivers still load too.
#ifdef KORAL_VK_LAYER_PATH
        addLoaderSearchPath("VK_ADD_LAYER_PATH", KORAL_VK_LAYER_PATH);
#endif

        // macOS has no native Vulkan driver — MoltenVK supplies one as an ICD, found the same
        // way shaders/assets are (see paths.cpp): beside the library, under the GNU install
        // layout, or — dev builds only — the Homebrew cellar this was configured against.
#if defined(__APPLE__)
        if (const auto icdManifest = kor::detail::dataFile(
                "moltenvk/etc/vulkan/icd.d", "MoltenVK_icd.json", "KORAL_VK_ICD_DIR",
#ifdef KORAL_VK_ICD_DEV_PATH
                KORAL_VK_ICD_DEV_PATH
#else
                ""
#endif
                ); !icdManifest.empty()) {
            addLoaderSearchPath("VK_ADD_DRIVER_FILES", icdManifest.c_str());
        } else {
            kor::log::warn("[vulkan] MoltenVK ICD manifest not found; Vulkan will have no "
                            "driver unless a system Vulkan SDK is installed. Run "
                            "'brew install molten-vk' and reconfigure.");
        }
#endif

        constexpr auto applicationInfo = ::vk::ApplicationInfo()
            .setPApplicationName("GFXFramework Application")
            .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
            .setPEngineName("GFXFramework")
            .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
            .setApiVersion(VK_API_VERSION_1_4);

        // Add GLFW-required surface extensions (VK_KHR_surface + platform-specific)
        // These are REQUIRED — do NOT filter them against available extensions
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
            kor::log::info("[vulkan] {}", glfwExtensions[i]);
        }

        if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
            kor::log::warn("[vulkan] glfwGetRequiredInstanceExtensions returned null — platform surface extension may be missing.");
        } else {
            for (uint32_t i = 0; i < glfwExtensionCount; ++i) {
                // Deduplicate (VK_KHR_surface is already in the hardcoded list)
                const std::string_view ext(glfwExtensions[i]);
                const bool already = std::ranges::any_of(_instanceExtensions,
                    [&](const char* e){ return std::string_view(e) == ext; });
                if (!already) {
                    kor::log::info("[vulkan] Adding GLFW platform extension: {}", glfwExtensions[i]);
                    _instanceExtensions.emplace_back(glfwExtensions[i]);
                }
            }
        }

        // Fallback: if no platform surface extension was added by GLFW (e.g. GLFW built X11-only
        // but running on Wayland, or vice-versa), probe the available extensions and add whatever
        // WSI surface extension is actually present on this system.
        {
            constexpr std::array platformExts = {
                "VK_KHR_wayland_surface",
                "VK_KHR_xcb_surface",
            };
            const bool hasPlatformExt = std::ranges::any_of(_instanceExtensions, [&](const char* e) {
                return std::ranges::any_of(platformExts, [&](const char* p){ return std::string_view(e) == p; });
            });
            if (!hasPlatformExt) {
                kor::log::warn("[vulkan] No platform surface extension from GLFW — probing available WSI extensions.");
                const auto availableExts = ::vk::enumerateInstanceExtensionProperties();
                for (const char* candidate : platformExts) {
                    for (const auto& avail : availableExts) {
                        if (std::string_view(avail.extensionName) == candidate) {
                            kor::log::info("[vulkan] Adding fallback platform extension: {}", candidate);
                            _instanceExtensions.push_back(candidate);
                            break;
                        }
                    }
                }
            }
        }

        // Filter layers to only those available on this system
        {
            const auto availableLayers = ::vk::enumerateInstanceLayerProperties();
            std::vector<const char*> supported;
            for (const char* req : _instanceLayers) {
                bool found = false;
                for (const auto& avail : availableLayers)
                    if (std::string_view(avail.layerName) == req) { found = true; break; }
                if (found) supported.push_back(req);
                else kor::log::warn("[vulkan] Layer '{}' not available, skipping.", req);
            }
            _instanceLayers = supported;
        }

        const bool hasValidation = !_instanceLayers.empty();

        // Filter only our optional extra extensions (NOT the GLFW ones we just added)
        // against what the loader/ICDs actually expose
        bool debugUtilsEnabled = false;
        {
            const auto availableExts = ::vk::enumerateInstanceExtensionProperties();
            // Separate GLFW extensions (already in vector, added above) from our optional ones
            // We only filter the ones that were in the vector BEFORE the GLFW extensions were added
            std::vector<const char*> optionalToCheck = {
                VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
            };
            if (hasValidation) {
                optionalToCheck.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
            for (const char* req : optionalToCheck) {
                bool found = false;
                for (const auto& avail : availableExts)
                    if (std::string_view(avail.extensionName) == req) { found = true; break; }
                if (found) {
                    if (std::string_view(req) == VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                        debugUtilsEnabled = true;
                    _instanceExtensions.push_back(req);
                } else {
                    kor::log::warn("[vulkan] Optional extension '{}' not available, skipping.", req);
                }
            }
        }

        kor::log::info("[vulkan] Final instance extensions ({}):", _instanceExtensions.size());
        for (const auto& ext : _instanceExtensions)
            kor::log::info("[vulkan]   [ext] {}", ext);

        // DebugPrintf GPU-assisted instrumentation is opt-in: it instruments every
        // shader and crashes the validation layer at queue submit on some drivers
        // (seen on NVIDIA 610 / RTX 50-series). Enable only when KORAL_DEBUG_PRINTF is
        // set in the environment (and the shaders actually use debugPrintfEXT).
        const bool enableDebugPrintf = hasValidation && std::getenv("KORAL_DEBUG_PRINTF") != nullptr;

        constexpr std::array<uint32_t, 1> bufferSize = { 1024 * 1024 };
        const auto validationLayerSetting = ::vk::LayerSettingEXT()
            .setPLayerName("VK_LAYER_KHRONOS_validation")
            .setPSettingName("printf_buffer_size")
            .setType(::vk::LayerSettingTypeEXT::eUint32)
            .setValues(bufferSize);

        const auto layerSettingsInfo = ::vk::LayerSettingsCreateInfoEXT()
            .setSettings(validationLayerSetting);

        const ::vk::ValidationFeatureEnableEXT enabledFeatures[] = {
            ::vk::ValidationFeatureEnableEXT::eDebugPrintf
        };

        const auto validationCreateInfo = ::vk::ValidationFeaturesEXT()
            .setPNext(&layerSettingsInfo)
            .setEnabledValidationFeatures(enabledFeatures);

        const auto debugCreateInfo = ::vk::DebugUtilsMessengerCreateInfoEXT()
            .setPNext(enableDebugPrintf ? static_cast<const void*>(&validationCreateInfo) : nullptr)
            .setMessageSeverity(
                ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                | ::vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo
            )
            .setMessageType(
                ::vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                ::vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
            )
            .setPfnUserCallback(reinterpret_cast<::vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback));

        const auto createInfo = ::vk::InstanceCreateInfo()
        #ifdef __APPLE__
            .setFlags(::vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR)
        #endif
            .setPNext(debugUtilsEnabled ? static_cast<const void*>(&debugCreateInfo) : nullptr)
            .setPApplicationInfo(&applicationInfo)
            .setPEnabledExtensionNames(_instanceExtensions)
            .setPEnabledLayerNames(_instanceLayers);

        _instance = ::vk::createInstance(createInfo);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(_instance);

        // Create the debug messenger only when VK_EXT_debug_utils is both enabled and
        // actually resolvable through the active loader. Some loaders (e.g. the WSI-less
        // vcpkg-provided one) advertise and enable the extension yet still return a null
        // vkCreateDebugUtilsMessengerEXT, which would trip vulkan-hpp's debug assertion.
        if (debugUtilsEnabled && VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDebugUtilsMessengerEXT) {
            _debugMessenger = _instance.createDebugUtilsMessengerEXT(debugCreateInfo);
        } else if (hasValidation) {
            kor::log::warn("[vulkan] VK_EXT_debug_utils unavailable — debug messenger disabled.");
        }
    }

    Runtime::~Runtime()
    {
        if (_debugMessenger) {
            _instance.destroyDebugUtilsMessengerEXT(_debugMessenger);
        }
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
