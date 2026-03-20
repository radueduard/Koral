//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include "context.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"

#include "../backends/open_gl/gui.h"
#include "../backends/vulkan/gui.h"

#include <imgui.h>
#include <iostream>

#include "commandBuffer.h"
#include "../backends/open_gl/ogl_err_handling.h"

void gfx::GUI::Init()
{
    Context::_imguiContext = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    switch (Context::Window().getAPI())
    {
    case API::eOpenGL:
         ogl::GUI::Init();
        break;
    case API::eVulkan:
        vk::GUI::Init();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }
}

void gfx::GUI::Render(gfx::CommandBuffer& commandBuffer, Scene& scene)
{
    auto* context = Context::GetCurrentImGuiContext();

    switch (Context::Window().getAPI())
    {
    case API::eOpenGL:
        ogl::GUI::NewFrame();
        glCheckError();
        break;
    case API::eVulkan:
        vk::GUI::NewFrame();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    ImGui::SetCurrentContext(context);
    ImGui::NewFrame();
    constexpr ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::Begin("DockSpace", nullptr, window_flags);

    const ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

    scene.RenderUI(context);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();
    switch (Context::Window().getAPI())
    {
    case API::eOpenGL:
        ogl::GUI::Render(commandBuffer, draw_data);
        break;
    case API::eVulkan:
        vk::GUI::Render(commandBuffer, draw_data);
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }
}

void gfx::GUI::Shutdown()
{
    switch (Context::Window().getAPI())
    {
    case API::eOpenGL:
        ogl::GUI::Shutdown();
        break;
    case API::eVulkan:
        vk::GUI::Shutdown();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }
    ImGui::DestroyContext();
}
