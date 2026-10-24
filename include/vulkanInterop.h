//
// The Vulkan objects under Koral's own, for code that records Vulkan of its own: a renderer a library
// brings, a tool, an experiment. Vulkan only, and only what that code needs to share Koral's device.
//

#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "api.h"
#include "resource.h"

namespace kor
{
    class CommandBuffer;
    class Image;
}

namespace kor::vulkan
{
    /** @brief The instance and device everything Koral makes is made on. */
    struct Handles {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        /** The loader's entry point the instance was made through: resolve functions with it, not another loader's. */
        PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
        /** The version the instance was made for, which caps what the device offers as core. */
        std::uint32_t apiVersion = 0;
        /** Whether VK_EXT_debug_utils is enabled on the instance: whether a messenger of one's own can be made. */
        bool debugUtils = false;
    };

    /** @brief The handles. Throws (std::runtime_error) when there is no Vulkan device yet. */
    KORAL_API Handles Native();

    /**
     * @brief The VkCommandBuffer a command buffer records into.
     *
     * Only inside CommandBuffer::Run, which is when Koral's own commands have been emitted up to the
     * point the callback was written at. Whatever is recorded there is past Koral's barrier analysis:
     * leave each Koral resource touched in the state an explicit Barrier() before the Run declared.
     */
    KORAL_API VkCommandBuffer NativeCommandBuffer(kor::CommandBuffer& commandBuffer);

    /** @brief The VkImage behind an image: this frame's copy, for one made per frame. */
    KORAL_API VkImage NativeImage(const ResourceRef<const Image>& image);

    /** @brief The VkFormat it really is, which for a swap chain's image is not always what PixelFormat() says. */
    KORAL_API VkFormat NativeFormat(const ResourceRef<const Image>& image);
}
