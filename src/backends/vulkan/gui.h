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
        // Blits the source into this frame's copy of the helper image and leaves it shader-readable.
        // @see kor::GUI_Image::refresh
        void refresh(kor::CommandBuffer& commandBuffer) override;

        // Records that blit into whichever command buffer is given. The frame's, from refresh(); a
        // single-time one when a handle is first built and there is no frame to record into.
        void recordBlit(kor::CommandBuffer& commandBuffer) const;

        kor::ResourceRef<const kor::Image> _image;
        kor::Resource<kor::Image> _helperImage;
        kor::Resource<kor::ImageView> _helperImageView;
        kor::Resource<kor::Sampler> _helperSampler;

        std::vector<VkDescriptorSet> _descriptorSets;

        // Which layer and mip level of the source the helper holds. Kept because the per-frame refresh
        // has to blit the same one the caller asked for, not the one it was created with.
        glm::u32 _layer = 0;
        glm::u32 _level = 0;

        // Whether ImGui samples the image itself rather than a copy of it. @see setImage
        bool _direct = false;

        // Which generation of the image the descriptors were written against. A resize replaces the
        // image, so they have to be written again. @see kor::Image::generation
        glm::u64 _boundGeneration = 0;
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
