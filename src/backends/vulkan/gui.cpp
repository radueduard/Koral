//
// Created by radue on 3/17/2026.
//

module;

#include <iostream>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "vk_enum_conversions.h"

module vk.gui;
import vk.descriptorPool;
import gfx.gui;
import gfx.resource;
import gfx.image;
import gfx.sampler;
import gfx.structs;
import gfx.context;
import vk.image;
import vk.imageView;
import vk.sampler;
import vk.context;
import vk.scheduler;
import vk.commandBuffer;
import vk.framebuffer;

gfx::vk::DescriptorPool* gfx::vk::GUI::_descriptorPool = nullptr;

namespace gfx::vk
{
    GUI_Image::GUI_Image(ResourceRef<const gfx::Image> image, const glm::u32 layer, const glm::u32 level) : _image(image)
    {
        _helperSampler = Sampler::Builder()
            .setMagFilter(Filter::eNearest)
            .setMinFilter(Filter::eNearest)
            .build();
        setImage(image);
        setLayerAndLevel(layer, level);
    }

    GUI_Image::~GUI_Image()
    {
        ImGui::SetCurrentContext(gfx::Context::GetCurrentImGuiContext());
        for (const auto& descriptorSet : _descriptorSets) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
    }

    void GUI_Image::setLayerAndLevel(const glm::u32 layer, const glm::u32 level)
    {
        Context::Device().runSingleTimeCommand([&](CommandBuffer& commandBuffer)
            {
                const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*_image);
                const auto& vkHelperImage = dynamic_cast<const gfx::vk::Image&>(*_helperImage);

                const auto imageType = vkImage.getType();

                commandBuffer.Blit(
                    _image, _helperImage,
                    gfx::Blit {
                        .srcOffset = { 0, 0, imageType == Image::Type::e3D ? static_cast<int32_t>(layer) : 0 },
                        .srcExtent = {
                            static_cast<glm::i32>(vkImage.getExtent().x),
                            static_cast<glm::i32>(vkImage.getExtent().y),
                            1
                        },
                        .dstOffset = { 0, 0, 0 },
                        .dstExtent = { (vkHelperImage.getExtent().x), (vkHelperImage.getExtent().y), 1 },
                        .srcBaseArrayLayer = imageType == Image::Type::e3D ? 0 : layer,
                        .dstBaseArrayLayer = 0,
                        .layerCount = 1,
                        .srcMipLevel = level,
                        .dstMipLevel = 0,
                        .filtering = gfx::Filter::eNearest
                    });

                commandBuffer.ImageBarrier({ _helperImage, ResourceAccess::FragmentShaderRead });
            }, ::vk::QueueFlagBits::eGraphics);
    }

    void GUI_Image::setImage(gfx::ResourceRef<const gfx::Image> image)
    {
        Context::Device()->waitIdle();
        ImGui::SetCurrentContext(gfx::Context::GetCurrentImGuiContext());
        for (const auto& descriptorSet : _descriptorSets) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
        _descriptorSets.clear();

        _image = image;
        _helperImage = gfx::Image::Builder()
            .setIsPerFrame(_image->isPerFrame())
            .setType(Image::Type::e2D)
            .setFormat(image->getFormat())
            .setExtent({ image->getExtent().x, image->getExtent().y })
            .setSampleCount(image->getSampleCount())
            .setUsage(Image::Usage::eTransferDst)
            .addUsage(Image::Usage::eSampled)
            .build();

        auto components = gfx::ImageView::ComponentMapping();
        if (image->getFormat() == Image::Format::eR8_UNORM) {
            components.r = gfx::ImageView::Swizzle::eR;
            components.g = gfx::ImageView::Swizzle::eR;
            components.b = gfx::ImageView::Swizzle::eR;
            components.a = gfx::ImageView::Swizzle::eR;
        }

        _helperImageView = gfx::ImageView::Builder(_helperImage)
            .setViewType(ImageView::Type::e2D)
            .setComponentMapping(components)
            .build();

        for (int frame = 0; frame < gfx::Context::Scheduler().getImageCount(); ++frame) {
            auto imageView = dynamic_cast<const gfx::vk::ImageView&>(*_helperImageView)[_helperImageView->isPerFrame() ? frame : 0];
            _descriptorSets.emplace_back(ImGui_ImplVulkan_AddTexture(
                *dynamic_cast<const gfx::vk::Sampler&>(*_helperSampler),
                imageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }

        setLayerAndLevel(0, 0);
    }

    ImTextureID GUI_Image::operator*() const {
        const auto frameIndex = gfx::Context::Scheduler().getCurrentImageIndex();
        return reinterpret_cast<ImTextureID>(_descriptorSets[frameIndex]);
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
            static_cast<VkFormat>(getVkFormat(vkScheduler.getSwapChain().getImage()->getFormat()))
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

    void GUI::Render(gfx::CommandBuffer& commandBuffer, ImDrawData* draw_data)
    {
        const auto& vkCommandBuffer = dynamic_cast<const vk::CommandBuffer&>(commandBuffer);
        const auto& vkFramebuffer = dynamic_cast<const vk::Framebuffer&>(*gfx::Context::DefaultFramebuffer());
        const auto& vkColorImageView = dynamic_cast<const vk::ImageView&>(*vkFramebuffer.getColorAttachments()[0]);
        const auto& vkImage = dynamic_cast<const vk::Image&>(*vkColorImageView.getImage());

        auto colorAttachment = ::vk::RenderingAttachmentInfo()
            .setImageView(*vkColorImageView)
            .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(::vk::AttachmentLoadOp::eLoad)
            .setStoreOp(::vk::AttachmentStoreOp::eStore);

        const auto renderingInfo = ::vk::RenderingInfo()
            .setRenderArea(::vk::Rect2D()
                .setOffset({ 0, 0 })
                .setExtent({ vkImage.getExtent().x, vkImage.getExtent().y }))
            .setColorAttachments(colorAttachment)
            .setViewMask(0)
            .setLayerCount(1);

        commandBuffer.ImageBarrier({
            vkColorImageView.getImage(),
            ResourceAccess::ColorAttachment
        });

        vkCommandBuffer->beginRendering(renderingInfo);

        if (const bool main_is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f; !main_is_minimized) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, *vkCommandBuffer);
        }

        vkCommandBuffer->endRendering();

        commandBuffer.ImageBarrier({
            vkColorImageView.getImage(),
            ResourceAccess::Present
        });
    }

    void GUI::Shutdown()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();

        delete _descriptorPool;
        _descriptorPool = nullptr;
    }
}
