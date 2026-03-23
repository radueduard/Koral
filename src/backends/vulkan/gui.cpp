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
    GUI_Image::GUI_Image(const gfx::Image& image, const glm::u32 layer, const glm::u32 level) : _image(image)
    {
        _helperSampler = Sampler::Builder().build();
        setImage(image);
        setLayerAndLevel(layer, level);
    }

    GUI_Image::~GUI_Image()
    {
        ImGui::SetCurrentContext(gfx::Context::GetCurrentImGuiContext());
        if (_id != 0)
        {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(_id));
            _id = 0;
        }
    }

    void GUI_Image::setLayerAndLevel(const glm::u32 layer, const glm::u32 level)
    {
        Context::Device().runSingleTimeCommand([&](const CommandBuffer& commandBuffer)
            {
                const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(_image.get());
                const auto& vkHelperImage = *dynamic_cast<const gfx::vk::Image*>(_helperImage.get());

                vkImage.TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferSrcOptimal);
                vkHelperImage.TransitionLayout(commandBuffer, ::vk::ImageLayout::eTransferDstOptimal);
                commandBuffer->blitImage(
                    *vkImage, ::vk::ImageLayout::eTransferSrcOptimal,
                    *vkHelperImage, ::vk::ImageLayout::eTransferDstOptimal,
                    ::vk::ImageBlit()
                        .setSrcSubresource(::vk::ImageSubresourceLayers()
                            .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                            .setMipLevel(level)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1))
                        .setSrcOffsets({
                            ::vk::Offset3D{ 0, 0, static_cast<int32_t>(layer) },
                            ::vk::Offset3D{ static_cast<glm::i32>(vkImage.getExtent().x), static_cast<glm::i32>(vkImage.getExtent().y), static_cast<int32_t>(layer + 1) }
                        })
                        .setDstSubresource(::vk::ImageSubresourceLayers()
                            .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                            .setMipLevel(0)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1))
                        .setDstOffsets({
                            ::vk::Offset3D{ 0, 0, 0 },
                            ::vk::Offset3D{ static_cast<glm::i32>(vkHelperImage.getExtent().x), static_cast<glm::i32>(vkHelperImage.getExtent().y), 1 }
                        }),
                    ::vk::Filter::eNearest);
                vkImage.TransitionLayout(commandBuffer, ::vk::ImageLayout::eGeneral);
                vkHelperImage.TransitionLayout(commandBuffer, ::vk::ImageLayout::eGeneral);
            }, ::vk::QueueFlagBits::eGraphics);
    }

    void GUI_Image::setImage(const gfx::Image& image)
    {
        Context::Device()->waitIdle();
        ImGui::SetCurrentContext(gfx::Context::GetCurrentImGuiContext());
        if (_id != 0)
        {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(_id));
            _id = 0;
        }

        _image = image;
        _helperImage = gfx::Image::Builder()
            .setType(Image::Type::e2D)
            .setFormat(image.getFormat())
            .setExtent({ image.getExtent().x, image.getExtent().y })
            .setMSAA(image.getMSAA())
            .setUsage(Image::Usage::eTransferDst)
            .addUsage(Image::Usage::eSampled)
            .build();

        _helperImageView = gfx::ImageView::Builder(*_helperImage)
            .setViewType(ImageView::Type::e2D)
            .build();

        _descriptorSet = ImGui_ImplVulkan_AddTexture(
            **dynamic_cast<const gfx::vk::Sampler*>(_helperSampler.get()),
            **dynamic_cast<const gfx::vk::ImageView*>(_helperImageView.get()),
            VK_IMAGE_LAYOUT_GENERAL);

        _id = reinterpret_cast<ImTextureID>(_descriptorSet);
        setLayerAndLevel(0, 0);
    }

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

        if (!ImGui_ImplGlfw_InitForVulkan(*gfx::Context::Window(), true)) {
            throw std::runtime_error("Failed to initialize ImGui for GLFW");
        }

        const auto& queue = vk::Context::Device().requestQueue(::vk::QueueFlagBits::eGraphics);

        const auto& vkScheduler = dynamic_cast<const vk::Scheduler&>(gfx::Context::Scheduler());
        static std::vector colorAttachmentFormats = {
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
                    std::cerr << "Vulkan error: " << ::vk::to_string(static_cast<::vk::Result>(err)) << std::endl;
                }
            },
        };

        if (const bool success = ImGui_ImplVulkan_Init(&initInfo); !success) {
            throw std::runtime_error("Failed to initialize ImGui for Vulkan");
        }
    }

    void GUI::NewFrame()
    {
        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplVulkan_NewFrame();
    }

    void GUI::Render(const gfx::CommandBuffer& commandBuffer, ImDrawData* draw_data)
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

        if (const bool main_is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f; !main_is_minimized) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, *vkCommandBuffer);
        }

        vkCommandBuffer->endRendering();
    }

    void GUI::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();

        delete _descriptorPool;
        _descriptorPool = nullptr;
    }
}
