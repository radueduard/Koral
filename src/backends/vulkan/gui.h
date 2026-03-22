//
// Created by radue on 3/17/2026.
//

#pragma once

#include <vector>

struct ImGuiContext;
struct ImDrawData;

namespace gfx
{
    class CommandBuffer;
}

namespace gfx::vk
{
    class DescriptorPool;

    class GUI
    {
    public:
        static void Init();
        static void NewFrame();
        static void Render(const gfx::CommandBuffer& commandBuffer, ImDrawData* draw_data);
        static void Shutdown();

    private:
        static gfx::vk::DescriptorPool* _descriptorPool;
    };
}
