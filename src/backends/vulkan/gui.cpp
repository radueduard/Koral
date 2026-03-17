//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include "context.h"
#include "descriptorPool.h"
#include "device.h"
#include "runtime.h"
#include "scheduler.h"
#include "vulkanContext.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"
#include "imageView.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <scheduler.h>

gfx::vk::DescriptorPool* gfx::vk::GUI::_descriptorPool = nullptr;

namespace gfx::vk
{
    void GUI::Init()
    {
        _descriptorPool = gfx::vk::DescriptorPool::Builder()
            .addPoolSize(::vk::DescriptorType::eSampler, 1000)
            .addPoolSize(::vk::DescriptorType::eCombinedImageSampler, 1000)
            .addPoolSize(::vk::DescriptorType::eSampledImage, 1000)
            .addPoolSize(::vk::DescriptorType::eStorageImage, 1000)
            .addPoolSize(::vk::DescriptorType::eUniformTexelBuffer, 1000)
            .addPoolSize(::vk::DescriptorType::eStorageTexelBuffer, 1000)
            .addPoolSize(::vk::DescriptorType::eUniformBuffer, 1000)
            .addPoolSize(::vk::DescriptorType::eStorageBuffer, 1000)
            .setMaxSets(1000 * 8)
            .setPoolFlags(::vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | ::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .build();

        ImGui::CreateContext();
        ImGui_ImplGlfw_InitForVulkan(*gfx::Context::Window(), true);
        ImGui::StyleColorsDark();

        const auto& queue = vk::Context::Device().requestQueue(::vk::QueueFlagBits::eGraphics);

        const auto& vkScheduler = dynamic_cast<const vk::Scheduler&>(gfx::Context::Scheduler());
        const std::array colorAttachmentFormats = {
            static_cast<VkFormat>(getVkFormat(vkScheduler.getSwapChain().getImage().getFormat()))
        };

        const auto pipelineRenderingCreateInfo = VkPipelineRenderingCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size()),
            .pColorAttachmentFormats = colorAttachmentFormats.data(),
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };

        ImGui_ImplVulkan_InitInfo initInfo = {
            .ApiVersion = VK_API_VERSION_1_3,
            .Instance = Context::Runtime().getInstance(),
            .PhysicalDevice = *Context::Runtime().getPhysicalDevice(),
            .Device = *Context::Device(),
            .QueueFamily = queue.getFamily().getIndex(),
            .Queue = *queue,
            .DescriptorPool = **_descriptorPool,
            .RenderPass = VK_NULL_HANDLE,
            .MinImageCount = 2,
            .ImageCount = vkScheduler.getImageCount(),
            .MSAASamples = static_cast<VkSampleCountFlagBits>(vkScheduler.getSwapChain().getMSAA()),
            .UseDynamicRendering = true,
            .PipelineRenderingCreateInfo = pipelineRenderingCreateInfo,
            .CheckVkResultFn = [](const VkResult err)
            {
                if (err != VK_SUCCESS)
                {
                    throw std::runtime_error("Vulkan error: " + std::to_string(err));
                }
            },
        };
        ImGui_ImplVulkan_Init(&initInfo);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui::StyleColorsDark();
    }

    void GUI::NewFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void GUI::Render(gfx::CommandBuffer& commandBuffer)
    {
        const auto& vkCommandBuffer = dynamic_cast<const vk::CommandBuffer&>(commandBuffer);
        const auto& vkFramebuffer = dynamic_cast<const vk::Framebuffer&>(gfx::Context::DefaultFramebuffer());
        const auto& vkColorImageView = dynamic_cast<const vk::ImageView&>(vkFramebuffer.getColorAttachments()[0].get());
        const auto& vkImage = dynamic_cast<const vk::Image&>(vkColorImageView.getImage());
        vkImage.TransitionLayout(vkCommandBuffer, ::vk::ImageLayout::eGeneral);

        auto colorAttachment = ::vk::RenderingAttachmentInfo()
            .setImageView(*vkColorImageView)
            .setImageLayout(::vk::ImageLayout::eGeneral)
            .setLoadOp(::vk::AttachmentLoadOp::eLoad)
            .setStoreOp(::vk::AttachmentStoreOp::eStore);

        const auto renderingInfo = ::vk::RenderingInfo()
            .setRenderArea(::vk::Rect2D()
                .setOffset({ 0, 0 })
                .setExtent({ vkImage.getExtent().x, vkImage.getExtent().y }))
            .setColorAttachments(colorAttachment)
            .setViewMask(0)
            .setLayerCount(1);

        vkCommandBuffer->beginRendering(renderingInfo);

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        if (const bool main_is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f; !main_is_minimized) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, *vkCommandBuffer);
        }

        vkCommandBuffer->endRendering();
    }

    void GUI::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        delete _descriptorPool;
        _descriptorPool = nullptr;
    }
}
