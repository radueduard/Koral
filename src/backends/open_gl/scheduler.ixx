//
// Created by eduard on 11.03.2026.
//

module;

#include <glm/glm.hpp>

export module ogl.scheduler;

import gfx.scheduler;
import gfx.commandBuffer;

namespace gfx::ogl
{
    export class Frame final : public gfx::Frame {
    public:
        explicit Frame(const glm::u32 imageIndex)
            : gfx::Frame(imageIndex) {};
    };

    export class Scheduler final : public gfx::Scheduler
    {
    public:
        explicit Scheduler(const Builder& createInfo);
        void Initialize() override;
        void Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const override;

    protected:
        void createFrames() override;

    public:
        void WaitIdle() const override;
    };
}
