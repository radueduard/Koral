//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include <iostream>

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

kor::vk::DescriptorPool* kor::vk::GUI::_descriptorPool = nullptr;

namespace kor::vk
{
    namespace
    {
        /**
         * @brief Whether ImGui can sample this image as it is, with no copy in between.
         *
         * The copy exists to give ImGui something it can always take: one 2D image, four channels in
         * the order it expects. Most of that an *image view* can do on its own — one mip of one array
         * layer of a 2D image is a view, not a copy — so what is left needing the helper is only what
         * a view cannot express:
         *
         *  - a **3D** image, whose "layer" is a z slice and has to be blitted out;
         *  - a **multisampled** image, which cannot be sampled at all;
         *  - **eR8_UNORM**, which is shown as grey through a channel swizzle the helper applies;
         *  - an image that is not **sampleable**, which is the one case there is nothing to be done
         *    about but copy.
         *
         * Everything else — a viewport's colour target above all — is bound directly, which saves a
         * full-image blit every frame and asks nothing of the image but eSampled.
         */
        bool canSampleDirectly(const kor::Image& image, const glm::u32 layer, const glm::u32 level)
        {
            return image.type() == kor::Image::Type::e2D
                && image.sampleCount() == kor::SampleCount::e1
                && image.format() != kor::Image::Format::eR8_UNORM
                && (image.usage() & kor::Image::Usage::eSampled)
                && layer < image.arrayLayers()
                && level < image.mipLevels();
        }
    }

    GuiImage::GuiImage(kor::ResourceRef<const kor::Image> image, const glm::u32 layer, const glm::u32 level) : _image(image)
    {
        _helperSampler = Sampler::Builder()
            .setMagFilter(Filter::eNearest)
            .setMinFilter(Filter::eNearest)
            .build();

        // Before setImage, not after: which layer and level is shown decides how the image is bound —
        // a view on the direct path — so binding first would build the wrong one and then rebuild it.
        _layer = layer;
        _level = level;
        setImage(image);
    }

    GuiImage::~GuiImage()
    {
        for (const auto& descriptorSet : _descriptorSets) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
    }

    void GuiImage::setLayerAndLevel(const glm::u32 layer, const glm::u32 level)
    {
        if (_layer == layer && _level == level && !_descriptorSets.empty()) return;

        _layer = layer;
        _level = level;

        // Which layer and level is shown decides the binding itself, not just what is copied: on the
        // direct path it *is* the view. So both paths rebind, which setImage does for either.
        if (_image.valid()) setImage(_image);
    }

    void GuiImage::refresh(kor::CommandBuffer& commandBuffer)
    {
        // A resized image is a *different* image: its views are rebuilt lazily, but the descriptor
        // ImGui samples through was written with the old view and has to be written again. Caught here
        // rather than left to whoever owns the handle — a viewport following a window being dragged
        // resizes every frame, and the frame it forgets is a frame ImGui samples a freed view.
        if (_image.valid() && _boundGeneration != _image->generation()) {
            setImage(_image);
        }

        // Two things at once, and both matter:
        //
        //  - the helper is brought up to date, so a viewport shows what was rendered *this* frame
        //    rather than what the image held when the handle was made;
        //  - this frame's copy of the helper is written at all. A per-frame image has one copy per
        //    swap-chain image, and the blit in setLayerAndLevel only ever touched the copy that was
        //    current then. The others stayed in VK_IMAGE_LAYOUT_UNDEFINED, and ImGui sampling one of
        //    those is a validation error the moment the swap chain comes round to it.
        recordBlit(commandBuffer);
    }

    void GuiImage::recordBlit(kor::CommandBuffer& commandBuffer) const
    {
        if (!_image.valid()) return;

        // Nothing to copy: ImGui reads the image itself, and all it needs is to find it in the layout
        // the descriptor was written with.
        if (_direct) {
            commandBuffer.ImageBarrier({ _image, ResourceAccess::eFragmentShaderRead });
            return;
        }

        if (!_helperImage) return;

        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*_image);
        const auto& vkHelperImage = dynamic_cast<const kor::vk::Image&>(*_helperImage);

        const auto imageType = vkImage.type();

        commandBuffer.Blit(
            _image, _helperImage,
            kor::Blit {
                .srcOffset = { 0, 0, imageType == Image::Type::e3D ? static_cast<int32_t>(_layer) : 0 },
                .srcExtent = {
                    static_cast<glm::i32>(vkImage.extent().x),
                    static_cast<glm::i32>(vkImage.extent().y),
                    1
                },
                .dstOffset = { 0, 0, 0 },
                .dstExtent = { (vkHelperImage.extent().x), (vkHelperImage.extent().y), 1 },
                .srcBaseArrayLayer = imageType == Image::Type::e3D ? 0 : _layer,
                .dstBaseArrayLayer = 0,
                .layerCount = 1,
                .srcMipLevel = _level,
                .dstMipLevel = 0,
                .filtering = kor::Filter::eNearest
            });

        // The layout the descriptor was written with. @see ImGui_ImplVulkan_AddTexture above.
        commandBuffer.ImageBarrier({ _helperImage, ResourceAccess::eFragmentShaderRead });
    }

    void GuiImage::setImage(kor::ResourceRef<const kor::Image> image)
    {
        Context::Device()->waitIdle();
        for (const auto& descriptorSet : _descriptorSets) {
            ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        }
        _descriptorSets.clear();

        _image = image;
        _boundGeneration = image.valid() ? image->generation() : 0;

        // The direct path: ImGui samples the image itself. No helper, no blit, and — the part a caller
        // notices — no eTransferSrc usage required of an image that only ever wanted to be looked at.
        _direct = canSampleDirectly(*image, _layer, _level);
        if (_direct) {
            _helperImage = {};
            // One layer, one level, seen as a plain 2D image: this is the whole of what the helper was
            // copying for, expressed as a view instead.
            _helperImageView = kor::ImageView::Builder(image)
                .setViewType(ImageView::Type::e2D)
                .setBaseArrayLayer(_layer)
                .setArrayLayerCount(1)
                .setBaseMipLevel(_level)
                .setMipLevelCount(1)
                .build();

            for (int frame = 0; frame < kor::Context::Scheduler().imageCount(); ++frame) {
                _descriptorSets.emplace_back(ImGui_ImplVulkan_AddTexture(
                    *dynamic_cast<const kor::vk::Sampler&>(*_helperSampler),
                    dynamic_cast<const kor::vk::ImageView&>(*_helperImageView)[frame],
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
            }
            return;
        }

        // The copying path needs to *read* the image, which an image created without eTransferSrc
        // cannot do. Said once, here, rather than left to the validation layer: the failure is a
        // missing usage flag at creation, and that is not something the messages point at.
        if (!(image->usage() & kor::Image::Usage::eTransferSrc)) {
            throw BackendException(Error{ .code = ErrorCode::eInvalidArgument, .message =
                "This image cannot be shown in the GUI. Showing a 3D image, a multisampled one, a "
                "single-channel one, or one that is not sampleable copies from the image, so it has to "
                "be created with Image::Usage::eTransferSrc. An ordinary 2D sampled image — including "
                "one mip or one array layer of it — needs no copy and no such flag." });
        }

        _helperImage = kor::Image::Builder()
            .setIsPerFrame(_image->isPerFrame())
            .setType(Image::Type::e2D)
            .setFormat(image->format())
            .setExtent({ image->extent().x, image->extent().y })
            .setSampleCount(image->sampleCount())
            // The helper is copied into and then sampled by ImGui, so it needs both.
            .setUsage(Image::Usage::eTransferDst | Image::Usage::eSampled)
            .build();

        auto components = kor::ImageView::ComponentMapping();
        if (image->format() == Image::Format::eR8_UNORM) {
            components.r = kor::ImageView::Swizzle::eR;
            components.g = kor::ImageView::Swizzle::eR;
            components.b = kor::ImageView::Swizzle::eR;
            components.a = kor::ImageView::Swizzle::eR;
        }

        _helperImageView = kor::ImageView::Builder(_helperImage)
            .setViewType(ImageView::Type::e2D)
            .setComponentMapping(components)
            .build();

        for (int frame = 0; frame < kor::Context::Scheduler().imageCount(); ++frame) {
            _descriptorSets.emplace_back(ImGui_ImplVulkan_AddTexture(
                *dynamic_cast<const kor::vk::Sampler&>(*_helperSampler),
                dynamic_cast<const kor::vk::ImageView&>(*_helperImageView)[frame],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }

        // Filled once, now, on its own submit: there may be no frame in progress — a scene creating a
        // handle in Initialize is the ordinary case — and a handle should show something immediately
        // rather than a frame later. Every frame after this, refresh() records the same blit.
        Context::Device().runSingleTimeCommand([this](CommandBuffer& commandBuffer) {
            recordBlit(commandBuffer);
        }, ::vk::QueueFlagBits::eGraphics);
    }

    ImTextureID GuiImage::operator*() const {
        const auto frameIndex = kor::Context::Scheduler().currentImageIndex();
        return reinterpret_cast<ImTextureID>(_descriptorSets[frameIndex]);
    }

    void GUI::Init()
    {
        _descriptorPool = kor::vk::DescriptorPool::Builder()
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

        if (!ImGui_ImplGlfw_InitForVulkan(*kor::Context::Window(), false)) {
            throw std::runtime_error("Failed to initialize ImGui for GLFW");
        }

        const auto& queue = vk::Context::Device().requestQueue(::vk::QueueFlagBits::eGraphics);

        const auto& vkScheduler = dynamic_cast<const vk::Scheduler&>(kor::Context::Scheduler());
        static std::vector colorAttachmentFormats = {
            static_cast<VkFormat>(getVkFormat(vkScheduler.getSwapChain().image()->format()))
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
            .ImageCount = vkScheduler.imageCount(),
            .MSAASamples = static_cast<VkSampleCountFlagBits>(vkScheduler.getSwapChain().getVkSamples()),
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

    void GUI::Render(kor::CommandBuffer& commandBuffer, ImDrawData* draw_data)
    {
        const auto& vkFramebuffer = dynamic_cast<const vk::Framebuffer&>(*kor::Context::defaultFramebuffer());
        const auto& vkColorImageView = dynamic_cast<const vk::ImageView&>(*vkFramebuffer.colorAttachment(0));
        const auto& vkImage = dynamic_cast<const vk::Image&>(*vkColorImageView.image());

        commandBuffer.ImageBarrier({
            vkColorImageView.image(),
            ResourceAccess::eColorAttachment
        });

        // Through Run, not straight at the handle. Commands are recorded and emitted at End(),
        // so a raw beginRendering/RenderDrawData here would execute while the frame was still
        // being recorded — ahead of every scene command — and the scene would paint over the
        // GUI. Run parks it in the same stream, between the two barriers either side.
        //
        // The attachment info is built *inside* the closure on purpose: RenderingInfo holds a
        // pointer to the RenderingAttachmentInfo, so building it out here and capturing it would
        // leave that pointer dangling by the time this runs. Only handles and plain values are
        // captured, all of which outlive the frame.
        const ::vk::ImageView colorView = *vkColorImageView;
        const auto extent = vkImage.extent();
        commandBuffer.Run([colorView, extent, draw_data](kor::CommandBuffer& cb) {
            const auto& raw = dynamic_cast<const vk::CommandBuffer&>(cb);

            auto colorAttachment = ::vk::RenderingAttachmentInfo()
                .setImageView(colorView)
                .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                .setLoadOp(::vk::AttachmentLoadOp::eLoad)
                .setStoreOp(::vk::AttachmentStoreOp::eStore);

            const auto renderingInfo = ::vk::RenderingInfo()
                .setRenderArea(::vk::Rect2D()
                    .setOffset({ 0, 0 })
                    .setExtent({ extent.x, extent.y }))
                .setColorAttachments(colorAttachment)
                .setViewMask(0)
                .setLayerCount(1);

            raw->beginRendering(renderingInfo);

            if (const bool main_is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f; !main_is_minimized) {
                ImGui_ImplVulkan_RenderDrawData(draw_data, *raw);
            }

            raw->endRendering();
        });

        commandBuffer.ImageBarrier({
            vkColorImageView.image(),
            ResourceAccess::ePresent
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
