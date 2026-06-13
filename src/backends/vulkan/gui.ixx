//
// Created by radue on 3/17/2026.
//

module;

#include <imgui.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

export module gfx:vk_gui;
import :vk_types;
import :vk_descriptorPool;

import resource;
import :gui;
import :commandBuffer;

namespace gfx::vk
{
    class GUI_Image final : public gfx::GUI_Image
    {
    public:
        explicit GUI_Image(ResourceRef<const gfx::Image> image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;
        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(ResourceRef<const gfx::Image> image) override;

        ImTextureID operator*() const override;

    private:
        ResourceRef<const gfx::Image> _image;
        Resource<gfx::Image> _helperImage;
        Resource<gfx::ImageView> _helperImageView;
        Resource<gfx::Sampler> _helperSampler;

        std::vector<VkDescriptorSet> _descriptorSets;
    };

    export class GUI
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
