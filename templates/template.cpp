#include "@NAME@.h"
#include <commandBuffer.h>
#include <imgui.h>

void @NAME@::Initialize()
{
    // TODO: setup resources
}

void @NAME@::Update()
{
    // TODO: per-frame logic
}

void @NAME@::Render(gfx::CommandBuffer& commandBuffer)
{
    (void)commandBuffer;
    // TODO: record render commands
}

void @NAME@::RenderUI(ImGuiContext* context) const
{
    ImGui::SetCurrentContext(context);
    // TODO: define ui for the scene
}