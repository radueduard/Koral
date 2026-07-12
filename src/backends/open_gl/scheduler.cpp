//
// Created by eduard on 11.03.2026.
//
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <framebuffer.h>
#include <surface.h>

#include "scheduler.h"

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <log.h>
#include <window.h>

#include "commandBuffer.h"
#include "gui.h"

namespace kor::ogl
{
    Scheduler::Scheduler(const Builder& createInfo): kor::Scheduler(createInfo) {}
    void Scheduler::Initialize()
    {
        glewInit();

        // Unify only the DEPTH convention with Vulkan. The app builds its projections
        // with GLM_FORCE_DEPTH_ZERO_TO_ONE (clip z in [0,w]), so on stock OpenGL — whose
        // NDC depth is [-1,1] — the depth buffer is compressed into [0.5,1], which breaks
        // the depth buffer, depth testing and the compute frustum/Hi-Z culling. Requesting
        // GL_ZERO_TO_ONE makes GL interpret [0,1] clip depth like Vulkan and fixes all of
        // that. We keep GL_LOWER_LEFT (GL's native Y-up window origin): the app's geometry,
        // texture-sampling and gl_FragCoord conventions are authored for OpenGL's Y, and the
        // Vulkan backend already flips to match (see vk SetViewport/SetFrontFace) — so the Y
        // axis must stay untouched here. Core in GL 4.5 / ARB_clip_control.
        if (GLEW_VERSION_4_5 || GLEW_ARB_clip_control)
            glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

        auto globalVAO = 0u;
        glGenVertexArrays(1, &globalVAO);
        glBindVertexArray(globalVAO);
        createFrames();
    }

    void Scheduler::Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc) const
    {
        kor::Scheduler::Draw(renderFunc);
        const auto& currentFrame = getCurrentFrame();
        auto& commandBuffer = currentFrame.getCommandBuffer();
        commandBuffer.Reset();
        renderFunc(commandBuffer.Begin());
        commandBuffer.End();
        if (const auto submitted = commandBuffer.Submit(); !submitted) {
            kor::log::error("[scheduler] frame submit failed: {}", submitted.error().toString());
        }

        // Debug: dump the final default-framebuffer image to a PPM after N frames.
        // KORAL_SCREENSHOT=<path>[:frame]. Lets us verify rendered output when a live
        // window can't be screen-grabbed (XWayland compositing shows a black root).
        if (const char* env = std::getenv("KORAL_SCREENSHOT")) {
            static int frame = 0;
            std::string spec = env;
            std::string path = spec; int want = 90;
            if (const auto colon = spec.rfind(':'); colon != std::string::npos && colon > 1) {
                path = spec.substr(0, colon); want = std::atoi(spec.c_str() + colon + 1);
            }
            if (frame++ == want) {
                const auto ext = Context::Window().getExtent();
                std::vector<unsigned char> px(static_cast<size_t>(ext.x) * ext.y * 3);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, ext.x, ext.y, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", ext.x, ext.y);
                    for (glm::i32 y = static_cast<glm::i32>(ext.y) - 1; y >= 0; --y) // GL origin bottom-left → flip
                        std::fwrite(px.data() + static_cast<size_t>(y) * ext.x * 3, 1, static_cast<size_t>(ext.x) * 3, f);
                    std::fclose(f);
                    kor::log::info("[screenshot] wrote {} ({}x{})", path, ext.x, ext.y);
                }
            }
        }

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
