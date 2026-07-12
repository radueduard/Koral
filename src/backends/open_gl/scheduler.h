//
// Created by eduard on 11.03.2026.
//

#pragma once
#include "../../../include/scheduler.h"

namespace kor::ogl
{
    class Frame final : public kor::Frame {
    public:
        explicit Frame(const glm::u32 imageIndex)
            : kor::Frame(imageIndex) {};
    };

    class Scheduler final : public kor::Scheduler
    {
    public:
        explicit Scheduler(const Builder& createInfo);
        void Initialize() override;
        void Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) const override;

    protected:
        void createFrames() override;

    public:
        void WaitIdle() const override;
    };
}
