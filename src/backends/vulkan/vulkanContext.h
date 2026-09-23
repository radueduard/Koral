//
// Created by radue on 2/27/2026.
//

#pragma once
#include <functional>
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

        /**
         * Runs `destroy` once the GPU has finished everything submitted so far — the deferred
         * deletion queue. For anything a submitted command buffer may still be using: buffers,
         * images and their views, descriptor sets, pipelines, samplers. Captures handles by value;
         * the object it came from is gone by the time it runs, on whichever thread next collects.
         * Runs at once when nothing has been submitted, or once the device is shutting down.
         */
        static void DestroyWhenUnused(std::function<void()> destroy);

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
        // Set once StopTokens() has drained the device: from then on nothing is in flight and
        // nothing more is submitted, so destruction no longer needs deferring.
        inline static bool _destroyImmediately = false;

    };
}