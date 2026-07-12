//
// Created by radue on 7/5/2026.
//

#pragma once

#include "api.h"
#include "task.h"

namespace kor
{
    /**
     * @brief A windowless app: initialize, run to completion, terminate.
     *
     * A Job is the headless counterpart to @ref Scene. Where a Scene owns a window
     * and a per-frame Update/Render loop, a Job has a single @ref Run that runs
     * once on a device-only context (no window, surface, swap chain or GUI — see
     * @ref Context::InitHeadless) and then the program exits. Use it for offscreen
     * rendering, compute or asset-processing tools.
     *
     * @ref Run returns a @ref Task, so it may `co_await` background work (asset
     * import, @ref Context::SwitchToBackgroundThread, ...). The engine drives the
     * returned task to completion — pumping the async executors — before it tears
     * the device down, so any coroutines it spawns get to finish.
     *
     * A project's shared library exports exactly one entry point: `CreateScene` for
     * the windowed path or `CreateJob` for this one. The engine picks the path by
     * which symbol it finds, so a Job never opens a window.
     */
    class KORAL_API Job {
    public:
        virtual ~Job() = default;

        virtual kor::Task<void> Run() = 0;
    };
}
