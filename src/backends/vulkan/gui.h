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
        explicit GUI_Image(const gfx::Image& image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;
        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(const gfx::Image& image) override;

    private:
        std::reference_wrapper<const gfx::Image> _image;
        std::unique_ptr<gfx::Image> _helperImage;
        std::unique_ptr<gfx::ImageView> _helperImageView;
        std::unique_ptr<gfx::Sampler> _helperSampler;

        VkDescriptorSet _descriptorSet;
    };

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
