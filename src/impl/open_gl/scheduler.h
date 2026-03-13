//
// Created by eduard on 11.03.2026.
//

#pragma once
#include "core/scheduler.h"

namespace gfx::ogl
{
    class Frame final : public gfx::Frame {
    public:
        explicit Frame(const glm::u32 imageIndex)
            : gfx::Frame(imageIndex) {};
    };

    class Scheduler final : public gfx::Scheduler
    {
    public:
        explicit Scheduler(const Builder& createInfo);
        void Initialize() override;
        void Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const override;

    protected:
        void createFrames() override;
    };
}
