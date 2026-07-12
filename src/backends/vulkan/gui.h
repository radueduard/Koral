//
// Created by radue on 3/17/2026.
//

#pragma once
#include <commandBuffer.h>

#include <imgui.h>

#include "image.h"
#include <gui.h>

namespace kor::vk
{
    class DescriptorPool;

    class KORAL_API GUI_Image final : public kor::GUI_Image
    {
    public:
        explicit GUI_Image(kor::ResourceRef<const kor::Image> image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;
        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(kor::ResourceRef<const kor::Image> image) override;

        ImTextureID operator*() const override;

    private:
        kor::ResourceRef<const kor::Image> _image;
        kor::Resource<kor::Image> _helperImage;
        kor::Resource<kor::ImageView> _helperImageView;
        kor::Resource<kor::Sampler> _helperSampler;

        std::vector<VkDescriptorSet> _descriptorSets;
    };

    class GUI
    {
    public:
        static void Init();
        static void NewFrame();
        static void Render(kor::CommandBuffer& commandBuffer, ImDrawData* draw_data);
        static void Shutdown();

    private:
        static kor::vk::DescriptorPool* _descriptorPool;
    };
}
