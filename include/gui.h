//
// Created by radue on 3/17/2026.
//

#pragma once
#include "commandBuffer.h"
#include "../src/backends/vulkan/commandBuffer.h"

struct ImGui_ImplVulkan_InitInfo;
struct ImDrawData;
struct ImGuiContext;

struct GLFWwindow;

namespace gfx
{
    class Scene;

    class GUI
    {
    public:
        GFX_API static void Init();
        GFX_API static void Render(CommandBuffer& commandBuffer, Scene& scene);
        GFX_API static void Shutdown();

        struct GFX_API Bridge
        {
            bool(*ImplGlfw_InitForOpenGL)(GLFWwindow* window, bool install_callbacks);
            bool(*ImplOpenGL3_Init)(const char* glsl_version);
            void(*ImplOpenGL3_NewFrame)();
            void(*ImplOpenGL3_RenderDrawData)(ImDrawData* draw_data);
            void(*ImplOpenGL3_Shutdown)();
            bool(*ImplGlfw_InitForVulkan)(GLFWwindow* window, bool install_callbacks);

            bool(*ImplVulkan_Init)(ImGui_ImplVulkan_InitInfo* initInfo);
            void(*ImplVulkan_NewFrame)();
            void(*ImplVulkan_RenderDrawData)(ImDrawData* draw_data, VkCommandBuffer commandBuffer, VkPipeline pipeline);
            void(*ImplVulkan_Shutdown)();

            void(*ImplGlfw_NewFrame)();
            void(*ImplGlfw_Shutdown)();
        };
        GFX_API static Bridge bridge;
    };
}
