//
// Created by radue on 2/17/2026.
//

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

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
}