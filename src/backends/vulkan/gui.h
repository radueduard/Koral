//
// Created by radue on 3/17/2026.
//

#pragma once
#include <commandBuffer.h>

#include <imgui.h>

#include "image.h"
#include <gui.h>

namespace gfx::vk
{
    class DescriptorPool;

    class GFX_API GUI_Image final : public gfx::GUI_Image
    {
    public:
        explicit GUI_Image(gfx::ResourceRef<const gfx::Image> image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;
        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(gfx::ResourceRef<const gfx::Image> image) override;

        ImTextureID operator*() const override;

    private:
        gfx::ResourceRef<const gfx::Image> _image;
        gfx::Resource<gfx::Image> _helperImage;
        gfx::Resource<gfx::ImageView> _helperImageView;
        gfx::Resource<gfx::Sampler> _helperSampler;

        std::vector<VkDescriptorSet> _descriptorSets;
    };

    class GUI
    {
    public:
        static void Init();
        static void NewFrame();
        static void Render(gfx::CommandBuffer& commandBuffer, ImDrawData* draw_data);
        static void Shutdown();

    private:
        static gfx::vk::DescriptorPool* _descriptorPool;
    };
}
