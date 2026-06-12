//
// Created by radue on 2/27/2026.
//

module;

#include <api.h>

export module vk.context;

namespace gfx
{
    class Engine;
}

namespace gfx::io
{
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

    export class GFX_API Context
    {
        friend class gfx::io::Window;
        friend class gfx::Engine;
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