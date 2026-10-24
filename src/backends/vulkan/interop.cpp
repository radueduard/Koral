//
// What vulkanInterop.h hands out: the handles under Koral's own objects.
//

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkanInterop.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>

#include <context.h>

#include "commandBuffer.h"
#include "device.h"
#include "image.h"
#include "physicalDevice.h"
#include "runtime.h"
#include "vulkanContext.h"

namespace kor::vulkan
{
    Handles Native() {
        if (!kor::Context::HasDevice() || kor::Context::ActiveAPI() != API::eVulkan) {
            throw std::runtime_error("kor::vulkan::Native : there is no Vulkan device yet");
        }
        const auto& runtime = kor::vk::Context::Runtime();
        return Handles {
            .instance = runtime.getInstance(),
            .physicalDevice = *runtime.getPhysicalDevice(),
            .device = *kor::vk::Context::Device(),
            .getInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
            .apiVersion = VK_API_VERSION_1_3,
            .debugUtils = std::ranges::any_of(runtime.getInstanceExtensions(), [](const char* name) {
                return std::string_view(name) == VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            }),
        };
    }

    VkCommandBuffer NativeCommandBuffer(kor::CommandBuffer& commandBuffer) {
        return *dynamic_cast<kor::vk::CommandBuffer&>(commandBuffer);
    }

    VkImage NativeImage(const ResourceRef<const Image>& image) {
        return *dynamic_cast<const kor::vk::Image&>(*image);
    }

    VkFormat NativeFormat(const ResourceRef<const Image>& image) {
        return static_cast<VkFormat>(dynamic_cast<const kor::vk::Image&>(*image).getFormat());
    }
}
