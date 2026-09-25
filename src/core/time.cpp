//
// Created by radue on 2/24/2026.
//

#include <algorithm>
#include <cmath>

#include "gtime.h"

namespace kor
{
    void Time::SetTimeScale(const float scale)
    {
        _timeScale = std::max(scale, 0.f);
    }

    void Time::SetFixedDeltaTime(const float seconds)
    {
        _fixedDeltaTime = std::max(seconds, 1e-4f);
        _fixedAccumulator = std::min(_fixedAccumulator, _fixedDeltaTime);
    }

    void Time::Advance(const float frameTime)
    {
        _frameTime = frameTime;
        _elapsed += frameTime * _timeScale;
        ++_frames;
    }

    std::uint32_t Time::TakeFixedSteps()
    {
        _fixedAccumulator += _frameTime * _timeScale;
        auto steps = static_cast<std::uint32_t>(std::floor(_fixedAccumulator / _fixedDeltaTime));
        _fixedAccumulator -= static_cast<float>(steps) * _fixedDeltaTime;
        if (steps > MaxFixedSteps) {
            // Behind by more than a frame can catch up on: run what it can and let the rest go,
            // or every slow frame makes the next one slower still.
            steps = MaxFixedSteps;
        }
        _fixedAccumulator = std::clamp(_fixedAccumulator, 0.f, _fixedDeltaTime);
        return steps;
    }
}
