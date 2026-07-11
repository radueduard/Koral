//
// Created by radue on 2/27/2026.
//

#pragma once
#include <thread>

#include <api.h>

namespace gfx
{
    class Engine;
    class Context;
    class Window;
}

namespace gfx::vk
{
    class Surface;
    class Scheduler;
    class DescriptorPool;
    class Allocator;
    class Runtime;
    class Device;

    class GFX_API Context
    {
        friend class gfx::Window;
        friend class gfx::Engine;
        friend class gfx::Context;
    public:
        static const gfx::vk::Runtime& Runtime();
        static const gfx::vk::Device& Device();
        static const gfx::vk::Allocator& Allocator();
        static const gfx::vk::DescriptorPool& DescriptorPool();

    private:
        static void Init();
        static void Destroy();

        inline static gfx::vk::Runtime* _runtime = nullptr;
        inline static gfx::vk::Device* _device = nullptr;
        inline static gfx::vk::Allocator* _allocator = nullptr;
        inline static gfx::vk::DescriptorPool* _descriptorPool = nullptr;

    };
}