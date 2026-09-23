//
// Created by radue on 9/23/2026.
//

#include "timeline.h"

#include <limits>

#include "device.h"
#include "log.h"
#include "vulkanContext.h"
#include "../../core/tokenState.h"

namespace {
    constexpr auto Forever = std::numeric_limits<std::uint64_t>::max();

    ::vk::Semaphore createTimelineSemaphore(const std::uint64_t initialValue) {
        auto typeInfo = ::vk::SemaphoreTypeCreateInfo()
            .setSemaphoreType(::vk::SemaphoreType::eTimeline)
            .setInitialValue(initialValue);
        return kor::vk::Context::Device()->createSemaphore(::vk::SemaphoreCreateInfo().setPNext(&typeInfo));
    }

    // The GPU half of a kor::Timeline: a timeline semaphore, which both the CPU and the GPU can
    // signal and wait on.
    class TimelineSemaphore final : public kor::detail::GpuTimeline {
    public:
        TimelineSemaphore(const std::uint64_t initialValue, kor::vk::TokenReactor& reactor)
            : _semaphore(createTimelineSemaphore(initialValue)), _reactor(reactor) {}

        ~TimelineSemaphore() override {
            kor::vk::Context::Device()->destroySemaphore(_semaphore);
        }

        [[nodiscard]] ::vk::Semaphore handle() const { return _semaphore; }

        [[nodiscard]] std::uint64_t counter() const override {
            try {
                return kor::vk::Context::Device()->getSemaphoreCounterValue(_semaphore);
            } catch (const std::exception& e) {
                // Device lost, in practice. Reporting nothing reached keeps waiters parked rather
                // than resuming them on work that never finished.
                kor::log::error("Reading a token's timeline semaphore failed: {}", e.what());
                return 0;
            }
        }

        void signal(const std::uint64_t value) override {
            kor::vk::Context::Device()->signalSemaphore(::vk::SemaphoreSignalInfo(_semaphore, value));
        }

        void wait(const std::uint64_t value) override {
            const auto result = kor::vk::Context::Device()->waitSemaphores(
                ::vk::SemaphoreWaitInfo().setSemaphores(_semaphore).setValues(value), Forever);
            if (result != ::vk::Result::eSuccess)
                kor::log::error("Waiting for a token failed: {}", ::vk::to_string(result));
        }

        void watch() override { _reactor.poke(); }

    private:
        ::vk::Semaphore _semaphore;
        kor::vk::TokenReactor& _reactor;
    };
}

namespace kor::vk {
    TokenReactor::TokenReactor()
        : _wake(createTimelineSemaphore(0)), _thread([this] { run(); }) {}

    TokenReactor::~TokenReactor() {
        shutdown();
        Context::Device()->destroySemaphore(_wake);
    }

    SemaphoreValue TokenReactor::resolve(const Token& token) {
        const auto& state = detail::TokenAccess::state(token);
        if (!state) return {nullptr, 0};

        const bool created = state->promote([this](const std::uint64_t initialValue) {
            return std::make_unique<TimelineSemaphore>(initialValue, *this);
        });
        if (created) {
            {
                std::lock_guard lock(_registryMutex);
                _timelines.push_back(state);
            }
            // Coroutines may already be parked on it, and until now nothing was watching.
            poke();
        }

        std::lock_guard lock(state->mutex);
        return {static_cast<const TimelineSemaphore&>(*state->gpu).handle(), token.value()};
    }

    void TokenReactor::poke() {
        std::lock_guard lock(_wakeMutex);
        Context::Device()->signalSemaphore(::vk::SemaphoreSignalInfo(_wake, ++_wakeValue));
    }

    void TokenReactor::run() {
        struct Watched {
            std::shared_ptr<detail::TimelineState> state;
            std::uint64_t value;
        };

        std::vector<Watched> watched;
        std::vector<::vk::Semaphore> semaphores;
        std::vector<std::uint64_t> values;

        for (;;) {
            // Read before checking `_stopping`: a poke that lands after this read is what makes the
            // wait below return, however the two interleave with shutdown().
            const std::uint64_t wakeTarget = Context::Device()->getSemaphoreCounterValue(_wake) + 1;
            if (_stopping.load(std::memory_order_acquire)) return;

            watched.clear();
            semaphores.assign({_wake});
            values.assign({wakeTarget});
            {
                std::lock_guard lock(_registryMutex);
                std::erase_if(_timelines, [](const auto& weak) { return weak.expired(); });
                for (const auto& weak : _timelines) {
                    auto state = weak.lock();
                    if (!state) continue;
                    const auto lowest = state->lowestAwaited();
                    if (!lowest) continue;
                    std::lock_guard stateLock(state->mutex);
                    if (!state->gpu) continue;
                    semaphores.push_back(static_cast<const TimelineSemaphore&>(*state->gpu).handle());
                    values.push_back(*lowest);
                    watched.push_back({std::move(state), *lowest});
                }
            }

            const auto result = Context::Device()->waitSemaphores(::vk::SemaphoreWaitInfo()
                .setFlags(::vk::SemaphoreWaitFlagBits::eAny)
                .setSemaphores(semaphores)
                .setValues(values), Forever);
            if (result != ::vk::Result::eSuccess) {
                log::error("The token reactor's wait failed ({}); GPU-signalled tokens will no longer "
                           "resume their coroutines", ::vk::to_string(result));
                return;
            }

            // Any of them may have moved, not just the one that ended the wait.
            for (const auto& w : watched) w.state->observe(w.state->current());
        }
    }

    void TokenReactor::shutdown() {
        if (!_thread.joinable()) return;

        _stopping.store(true, std::memory_order_release);
        poke();
        _thread.join();

        Context::Device()->waitIdle();

        std::vector<std::shared_ptr<detail::TimelineState>> live;
        {
            std::lock_guard lock(_registryMutex);
            for (const auto& weak : _timelines)
                if (auto state = weak.lock()) live.push_back(std::move(state));
            _timelines.clear();
        }
        for (const auto& state : live) state->demote();
    }
}
