//
// Created by radue on 2/17/2026.
//

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

// After vulkan.hpp so VK_VERSION_1_0 is defined, which is what gates the
// declaration of glfwInitVulkanLoader in glfw3.h.
#include <GLFW/glfw3.h>

#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

void initDispatcher()
{
    // Koral links the Vulkan loader directly (Vulkan::Vulkan), so seed the dispatcher from the
    // linked vkGetInstanceProcAddr instead of dlopen()ing the loader again through
    // vk::detail::DynamicLoader. The dlopen path probes unversioned leaf names
    // ("libvulkan.dylib", "libvulkan.1.dylib") that only exist as symlinks in the build
    // machine's vcpkg tree — the installed package ships the loader under its fully-versioned
    // name only (resolved via the @rpath load command), so on any other machine the dlopen
    // finds nothing and aborts with "Failed to load vulkan library!".
    VULKAN_HPP_DEFAULT_DISPATCHER.init( vkGetInstanceProcAddr );

    // GLFW's Vulkan support has the same problem: it lazily dlopen()s the loader by
    // unversioned name on its first Vulkan call, and when that fails it reports Vulkan as
    // unsupported — glfwGetRequiredInstanceExtensions returns null, no platform surface
    // extension gets enabled, and window surface creation fails with
    // ErrorInitializationFailed. Hand GLFW the linked loader instead. This must happen
    // before GLFW's first Vulkan call (the instance-extension query in the Vulkan runtime),
    // which is the case here: initDispatcher runs at the top of vk::Context::Init.
    glfwInitVulkanLoader( vkGetInstanceProcAddr );
}