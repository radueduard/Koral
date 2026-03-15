//
// Created by eduard on 11.03.2026.
//
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <framebuffer.h>
#include <surface.h>

#include "scheduler.h"
#include <window.h>

#include "comandBuffer.h"

namespace gfx::ogl
{
    Scheduler::Scheduler(const Builder& createInfo): gfx::Scheduler(createInfo) {}
    void Scheduler::Initialize()
    {
        glewInit();
        auto globalVAO = 0u;
        glGenVertexArrays(1, &globalVAO);
        glBindVertexArray(globalVAO);
        createFrames();
    }

    void Scheduler::Draw(const std::function<void(gfx::CommandBuffer&)>& renderFunc) const
    {
        gfx::Scheduler::Draw(renderFunc);
        const auto& currentFrame = getCurrentFrame();
        auto& commandBuffer = currentFrame.getCommandBuffer();
        commandBuffer.Reset();
        renderFunc(commandBuffer.Begin());
        commandBuffer.End();
        commandBuffer.Submit();

        glfwSwapBuffers(Context::Window().operator*());
        advanceFrame();
    }

    void Scheduler::createFrames()
    {
        _frames.clear();
        for (glm::u32 i = 0; i < _imageCount; ++i) {
            _frames.emplace_back(std::make_unique<Frame>(i));
        }
    }

    void Scheduler::WaitIdle() const
    {
        glFinish();
    }
}
