//
// Created by radue on 2/27/2026.
//

#pragma once
#include <thread>
#include <glm/fwd.hpp>


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
    class DescriptorPool;
    class Allocator;
    class Runtime;
    class Device;

    class Context
    {
        friend class gfx::io::Window;
        friend class gfx::Engine;
    public:
        static const gfx::vk::Runtime& Runtime();
        static const gfx::vk::Device& Device();
        static const gfx::vk::Allocator& Allocator();
        static const gfx::vk::DescriptorPool& DescriptorPool();

        static glm::u32 ThreadId() { return std::hash<std::thread::id>()(_threadId); }
        static glm::u32 MainThreadId() { return std::hash<std::thread::id>()(_mainThreadId); }

    private:
        static void Init();
        static void Destroy();

        inline static gfx::vk::Runtime* _runtime = nullptr;
        inline static gfx::vk::Device* _device = nullptr;
        inline static gfx::vk::Allocator* _allocator = nullptr;
        inline static gfx::vk::DescriptorPool* _descriptorPool = nullptr;
        inline thread_local static auto _threadId = std::thread::id();
        inline static std::thread::id _mainThreadId = std::this_thread::get_id();
    };
}