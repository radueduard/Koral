
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#if VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

#include <iostream>
#include "io/manager.h"


using gfx::io::Window;

namespace gfx
{
    class Engine
    {
        friend int ::main();
        static void Run();
    };
}

#ifdef _WIN32
#include <windows.h>
void enableANSI() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD dwMode = 0;

    if (hOut != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hOut, &dwMode);
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    if (hErr != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hErr, &dwMode);
        SetConsoleMode(hErr, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}
#endif

int main() {
#ifdef _WIN32
    enableANSI();
#endif

#if ( VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1 )
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
#endif

    const vk::detail::DynamicLoader dl;
    VULKAN_HPP_DEFAULT_DISPATCHER.init( dl );

    const PFN_vkGetInstanceProcAddr getInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>( "vkGetInstanceProcAddr" );
    VULKAN_HPP_DEFAULT_DISPATCHER.init( getInstanceProcAddr );

    try {
        gfx::io::Manager manager;
        gfx::Engine::Run();
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
