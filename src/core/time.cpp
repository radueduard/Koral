//
// Created by radue on 2/24/2026.
//

#include <chrono>
#include "gtime.h"

namespace gfx
{
    namespace {
        // Process-wide time state: there is only ever one window, so this need not
        // be per-window. Encapsulated in this TU; reached through Time's methods.
        struct TimeState
        {
            float timeSinceStart = 0;

            std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameStart;
            float frameDeltaTime;

            std::chrono::time_point<std::chrono::high_resolution_clock> lastFixedPoint;
            float fixedDeltaTime;
            bool shouldRunFixedUpdate = false;

            void setup()
            {
                const auto now = std::chrono::high_resolution_clock::now();
                lastFrameStart = now;
                lastFixedPoint = now;
                shouldRunFixedUpdate = false;
                fixedDeltaTime = std::chrono::milliseconds(1000 / 60).count();
            }

            void update()
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
        };

        TimeState g_time;
    }

    /**
     *
     * @return The time in seconds it took to render the last frame. This is updated at the end of each frame,
     * so it represents the time taken for the previous frame, not the current one.
     */
    float Time::FrameTime()
    {
        return g_time.frameDeltaTime;
    }

    float Time::FixedDeltaTime()
    {
        return g_time.fixedDeltaTime;
    }

    float Time::WindowTime()
    {
        return g_time.timeSinceStart;
    }

    void Time::setup() { g_time.setup(); }
    void Time::update() { g_time.update(); }
}
