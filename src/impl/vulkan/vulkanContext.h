//
// Created by radue on 2/27/2026.
//

#pragma once
#include <thread>
#include <glm/fwd.hpp>


namespace gfx::io
{
    class Window;
}

namespace gfx::vk
{
    class Allocator;
    class Runtime;
    class Device;

    class Context
    {
        friend class gfx::io::Window;
    public:
        static const gfx::vk::Runtime& Runtime();
        static const gfx::vk::Device& Device();
        static const gfx::vk::Allocator& Allocator();

        static glm::u32 ThreadId() { return std::hash<std::thread::id>()(_threadId); }
        static glm::u32 MainThreadId() { return std::hash<std::thread::id>()(_mainThreadId); }

    private:
        static void Init();
        static void Destroy();

        inline thread_local static gfx::vk::Runtime* _runtime = nullptr;
        inline thread_local static gfx::vk::Device* _device = nullptr;
        inline thread_local static gfx::vk::Allocator* _allocator = nullptr;
        inline thread_local static std::thread::id _threadId = std::thread::id();
        inline static std::thread::id _mainThreadId = std::this_thread::get_id();
    };
}