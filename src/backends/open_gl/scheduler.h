//
// Created by eduard on 11.03.2026.
//

#pragma once
#include "../../../include/scheduler.h"

#include <vector>

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
        void Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) override;

    protected:
        void createFrames() override;

    public:
        void WaitIdle() const override;
        ~Scheduler() override;

    private:
        // OpenGL cannot signal a token from the GPU, so each frame leaves a fence behind and the
        // next Draw() signals the frame's completion once the fence has passed — polled, never
        // waited on, so the render loop keeps its pipelining.
        void signalFinishedFrames(bool wait) const;
        struct InFlight {
            void* fence; // GLsync, kept opaque so this header need not pull in GL
            Token completion;
        };
        mutable std::vector<InFlight> _inFlight;
    };
}
