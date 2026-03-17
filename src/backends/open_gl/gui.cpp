//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include <commandBuffer.h>

#include "context.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "commandBuffer.h"

namespace gfx
{
    void ogl::GUI::Init()
    {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui_ImplGlfw_InitForOpenGL(*Context::Window(), true);
        ImGui_ImplOpenGL3_Init("#version 450");

        ImGui::StyleColorsDark();
    }

    void ogl::GUI::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void ogl::GUI::Render(gfx::CommandBuffer& commandBuffer)
    {
        commandBuffer.Run([](gfx::CommandBuffer&)
        {
            ImGui::Render();

            ImDrawData* draw_data = ImGui::GetDrawData();
            if (const bool main_is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f; !main_is_minimized) {
                ImGui_ImplOpenGL3_RenderDrawData(draw_data);
            }
            if (const ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                GLFWwindow* backup_current_context = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(backup_current_context);
            }
        });
    }

    void ogl::GUI::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}
