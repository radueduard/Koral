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

        // Koral's canonical clip space is Vulkan's: Y points down in NDC (-1 is the top of
        // the image) and depth is [0,1]. GL is configured once, here, to rasterize that same
        // space; nothing downstream compensates per-draw. This is the whole reason the
        // backends no longer carry viewport/front-face fixups.
        //
        //   GL_ZERO_TO_ONE  — stock GL maps NDC depth [-1,1], which squashes a [0,1] clip
        //                     depth into [0.5,1] and wrecks depth testing and the compute
        //                     frustum/Hi-Z culling.
        //   GL_UPPER_LEFT   — negates NDC Y in the viewport transform. That lands Y-down clip
        //                     content right side up on screen AND flips window-space triangle
        //                     winding to agree with Vulkan, so front faces need no inversion.
        //
        // Two things ARB_clip_control does NOT cover, handled elsewhere:
        //   * Viewport/scissor rects stay in GL's bottom-left window space, so the top-left
        //     rects the API takes are converted in CommandBuffer::SetViewport/SetScissor.
        //   * gl_FragCoord.y still counts from the bottom; a shader that reads it needs
        //     `layout(origin_upper_left) in vec4 gl_FragCoord;` to match Vulkan.
        //
        // Consequence worth knowing: an offscreen target's rows land in memory bottom-up
        // relative to Vulkan's. That is invisible to a render→sample→present chain (every
        // stage is mirrored alike) but it is visible to host readback and to shaders that
        // mix a rendered target with a disk-loaded texture at the same UV.
        //
        // Core in GL 4.5 / ARB_clip_control. There is no fallback path: without it GL cannot
        // rasterize the canonical space at all, so fail loudly rather than render garbage.
        if (!GLEW_VERSION_4_5 && !GLEW_ARB_clip_control)
            throw std::runtime_error(
                "OpenGL 4.5 or ARB_clip_control is required: Koral's clip space (Y-down, depth [0,1]) "
                "cannot be expressed without glClipControl.");
        glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);

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
