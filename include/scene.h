//
// Created by radue on 2/21/2026.
//

#pragma once

#include <commandBuffer.h>
#include <glm/glm.hpp>

#include "api.h"
// Not for anything declared below: RenderUI() makes every scene an ImGui client, and including this
// is what registers the scene library's own copy of ImGui with the engine.
#include "gui.h"

namespace kor
{
    class CommandBuffer;

    /**
     * @brief What a windowed project implements: one frame's worth of behaviour.
     *
     * A project is a shared library that derives from this and exports a factory the runtime looks
     * for:
     *
     * @code
     * class MyScene final : public kor::Scene {
     *     void Initialize() override;
     *     void Update() override;
     *     void Render(kor::CommandBuffer& commandBuffer) override;
     * };
     *
     * extern "C" KORAL_EXPORT kor::Scene* CreateScene() { return new MyScene(); }
     * @endcode
     *
     * The runtime creates the scene, brings the window and device up, calls Initialize() once, and
     * then drives Update() → Render() → RenderUI() every frame until the window closes. It owns the
     * scene and destroys it before the device goes away, so resources held as members are released
     * at the right time without any teardown code.
     *
     * Everything happens on one thread — the same one throughout — so a scene needs no locking.
     * Work that would stall the frame belongs on a background executor; see kor::Task.
     *
     * @see Job for the headless counterpart, which runs once and exits.
     */
    class KORAL_API Scene {
    public:
        virtual ~Scene() = default;

        /**
         * @brief Called once, after the window and graphics device exist and before the first frame.
         *
         * Where resources are created: buffers, images, shaders, pipelines, descriptor sets.
         * Creating them earlier — in the constructor — is too early, since there is no device yet.
         */
        virtual void Initialize() = 0;

        /**
         * @brief Called once per frame, before Render.
         *
         * Where per-frame logic goes: input, animation, camera. Scale anything rate-dependent by
         * Time::FrameTime(). Optional; a scene that only draws need not override it.
         */
        virtual void Update() {}

        /**
         * @brief Called once per frame to record the frame's GPU work.
         * @param commandBuffer The frame's command buffer, already open for recording.
         *
         * Record draws and dispatches here; do not begin, end or submit the command buffer, which
         * the runtime does around this call.
         */
        virtual void Render(kor::CommandBuffer& commandBuffer) = 0;

        /**
         * @brief Called once per frame to define the scene's Dear ImGui interface.
         *
         * Call ImGui functions directly — the frame is already begun and is rendered after this
         * returns. Optional; a scene with no interface need not override it.
         */
        virtual void RenderUI() {}

        /**
         * @brief Called when the window's drawable area has changed size.
         * @param extent The new size in pixels.
         *
         * Dispatched once per frame, after a change, before Update. Rebuild anything sized to the
         * window here — offscreen render targets, projection matrices. The default framebuffer and
         * swap chain are resized for you.
         *
         * Other input (keys, mouse, scroll) is available through kor::Input.
         */
        virtual void OnResize(glm::uvec2 extent) {}
    };
}

