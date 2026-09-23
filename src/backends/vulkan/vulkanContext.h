//
// Created by radue on 2/27/2026.
//

#pragma once
#include <thread>

#include <api.h>

namespace kor
{
    class Engine;
    class Context;
    class Window;
}

namespace kor::vk
{
    class Surface;
    class Scheduler;
    class DescriptorPool;
    class Allocator;
    class Runtime;
    class Device;
    class TokenReactor;

    class KORAL_API Context
    {
        friend class kor::Window;
        friend class kor::Engine;
        friend class kor::Context;
    public:
        static const kor::vk::Runtime& Runtime();
        static const kor::vk::Device& Device();
        static const kor::vk::Allocator& Allocator();
        static const kor::vk::DescriptorPool& DescriptorPool();
        /** What turns kor::Tokens into timeline semaphores, and wakes their coroutines. */
        static kor::vk::TokenReactor& Tokens();

    private:
        static void Init();
        static void Destroy();
        // Stops the token reactor and hands every timeline back to the CPU. Must run while the
        // executors are still alive, since that hand-back can resume the last parked coroutines.
        static void StopTokens();

        inline static kor::vk::Runtime* _runtime = nullptr;
        inline static kor::vk::Device* _device = nullptr;
        inline static kor::vk::Allocator* _allocator = nullptr;
        inline static kor::vk::DescriptorPool* _descriptorPool = nullptr;
        inline static kor::vk::TokenReactor* _tokenReactor = nullptr;

    };
}