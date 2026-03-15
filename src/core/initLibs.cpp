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

#if ( VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1 )
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
#endif

    const vk::detail::DynamicLoader dl;
    VULKAN_HPP_DEFAULT_DISPATCHER.init( dl );

    const PFN_vkGetInstanceProcAddr getInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>( "vkGetInstanceProcAddr" );
    VULKAN_HPP_DEFAULT_DISPATCHER.init( getInstanceProcAddr );
}