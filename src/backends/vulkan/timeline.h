//
// Created by radue on 9/23/2026.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <vulkan/vulkan.hpp>

#include "token.h"

namespace kor::detail { struct TimelineState; }

namespace kor::vk {
    /** A token as a submission sees it: which semaphore, and the value to wait for or signal. */
    struct SemaphoreValue {
        ::vk::Semaphore semaphore;
        std::uint64_t value;
    };

    /**
     * Wakes coroutines parked on tokens the GPU signals.
     *
     * Vulkan never calls back when a semaphore reaches a value, so one thread sits in
     * vkWaitSemaphores (wait-any) over every timeline that has a coroutine parked on it, and hands
     * the ones that are due to their executors when it wakes. A private "wake" timeline sits in the
     * same wait, so a coroutine parking on a new value — or shutdown — can interrupt it.
     */
    class TokenReactor {
    public:
        TokenReactor();
        ~TokenReactor();

        TokenReactor(const TokenReactor&) = delete;
        TokenReactor& operator=(const TokenReactor&) = delete;

        /**
         * The semaphore and value behind `token`, giving its timeline a semaphore first if it has
         * none. A default-constructed token has no semaphore: the returned one is null.
         */
        SemaphoreValue resolve(const Token& token);

        /** Interrupts the wait so the next round sees newly parked coroutines. */
        void poke();

        /**
         * Stops the thread and hands every timeline back to the CPU, with its last GPU value, so a
         * token that outlives the device neither touches it nor loses what it reached. Waits for
         * the device to go idle first: a semaphore cannot be destroyed while a submission uses it.
         */
        void shutdown();

    private:
        void run();

        ::vk::Semaphore _wake;
        std::mutex _wakeMutex;          // a timeline must be signalled with increasing values
        std::uint64_t _wakeValue = 0;

        std::mutex _registryMutex;
        std::vector<std::weak_ptr<detail::TimelineState>> _timelines;

        std::atomic<bool> _stopping{false};
        std::thread _thread;
    };
}
