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
        //     `Layout(origin_upper_left) in vec4 gl_FragCoord;` to match Vulkan.
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
        CreateFrames();
    }

    void Scheduler::Draw(const std::function<void(kor::CommandBuffer&)>& renderFunc)
    {
        kor::Scheduler::Draw(renderFunc);
        signalFinishedFrames(/*wait=*/false);

        const auto& frame = CurrentFrame();
        auto& commandBuffer = frame.Commands();
        commandBuffer.Reset();
        _buildingFrame = true;
        try {
            renderFunc(commandBuffer.Begin());
        } catch (...) {
            _buildingFrame = false;
            throw;
        }

        // After the render callback, which may Execute() work of its own.
        auto pending = TakePending();
        _buildingFrame = false;

        // The GPU cannot be told to wait, so the CPU does. WaitFor() says as much.
        for (const auto& token : pending.waits) token.Wait();

        // Ended in the order they run, as under Vulkan: each End() resolves its barriers against
        // where the one before it left every resource. Submitted in the same order after.
        for (const auto& external : pending.before) external->End();
        commandBuffer.End();
        for (const auto& external : pending.after) external->End();

        const auto submit = [](kor::CommandBuffer& cb) {
            if (const auto submitted = cb.Submit(); !submitted) {
                kor::log::Error("[scheduler] frame submit failed: {}", submitted.error().ToString());
            }
        };
        for (const auto& external : pending.before) submit(*external);
        submit(commandBuffer);
        for (const auto& external : pending.after) submit(*external);

        std::vector<std::unique_ptr<kor::CommandBuffer>> executed;
        for (auto& external : pending.before) executed.push_back(std::move(external));
        for (auto& external : pending.after) executed.push_back(std::move(external));
        _inFlight.push_back({glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0), pending.completion, std::move(executed)});

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
                const auto ext = Context::Window().Extent();
                std::vector<unsigned char> px(static_cast<size_t>(ext.x) * ext.y * 3);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, ext.x, ext.y, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", ext.x, ext.y);
                    for (glm::i32 y = static_cast<glm::i32>(ext.y) - 1; y >= 0; --y) // GL origin bottom-left → flip
                        std::fwrite(px.data() + static_cast<size_t>(y) * ext.x * 3, 1, static_cast<size_t>(ext.x) * 3, f);
                    std::fclose(f);
                    kor::log::Info("[screenshot] wrote {} ({}x{})", path, ext.x, ext.y);
                }
            }
        }

        glfwSwapBuffers(Context::Window().operator*());
        AdvanceFrame();
    }

    void Scheduler::CreateFrames()
    {
        _frames.clear();
        for (glm::u32 i = 0; i < _imageCount; ++i) {
            _frames.emplace_back(std::make_unique<Frame>(i));
        }
    }

    void Scheduler::WaitIdle() const
    {
        glFinish();
        signalFinishedFrames(/*wait=*/true);
    }

    Scheduler::~Scheduler()
    {
        // Every frame token signalled, so nothing awaiting one is left hanging past shutdown.
        WaitIdle();
    }

    void Scheduler::signalFinishedFrames(const bool wait) const
    {
        // In submission order, and stopping at the first one still running: frame n+1 cannot be
        // done before frame n, and a timeline must be signalled in increasing order anyway.
        std::size_t done = 0;
        for (; done < _inFlight.size(); ++done) {
            const auto fence = static_cast<GLsync>(_inFlight[done].fence);
            // Polling asks with a zero timeout; waiting loops until the fence is through.
            GLenum status;
            do {
                status = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, wait ? 1'000'000'000 : 0);
            } while (wait && status == GL_TIMEOUT_EXPIRED);
            if (status == GL_TIMEOUT_EXPIRED) break;
            glDeleteSync(fence);
            for (const auto& commandBuffer : _inFlight[done].executed) commandBuffer->DeliverTimings();
            _inFlight[done].completion.Signal();
        }
        _inFlight.erase(_inFlight.begin(), _inFlight.begin() + static_cast<std::ptrdiff_t>(done));
    }
}
