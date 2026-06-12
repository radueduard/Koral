//
// Created by radue on 2/24/2026.
//

module;

#include <chrono>

module gfx.time;
import gfx.context;

namespace gfx::io
{
    /**
     *
     * @return The time in seconds it took to render the last frame. This is updated at the end of each frame,
     * so it represents the time taken for the previous frame, not the current one.
     */
    float Time::FrameTime()
    {
        return Context::Window()._timeState.frameDeltaTime;
    }

    float Time::FixedDeltaTime()
    {
        return Context::Window()._timeState.fixedDeltaTime;
    }

    float Time::WindowTime()
    {
        return Context::Window()._timeState.timeSinceStart;
    }

    void Time::State::setup()
    {
        const auto now = std::chrono::high_resolution_clock::now();
        lastFrameStart = now;
        lastFixedPoint = now;
        shouldRunFixedUpdate = false;
        fixedDeltaTime = std::chrono::milliseconds(1000 / 60).count();
    }

    void Time::State::update()
    {
        const auto now = std::chrono::high_resolution_clock::now();
        frameDeltaTime = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastFrameStart).count();
        lastFrameStart = now;
        timeSinceStart += frameDeltaTime;

        if (now - lastFixedPoint >= std::chrono::duration<double>(1.0 / 60.0)) {
            shouldRunFixedUpdate = true;
            fixedDeltaTime = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastFixedPoint).count();
            lastFixedPoint = now;
        } else {
            shouldRunFixedUpdate = false;
        }
    }
}
