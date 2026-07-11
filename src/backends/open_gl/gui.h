//
// Created by radue on 3/17/2026.
//

#pragma once
#include <commandBuffer.h>

#include <imgui.h>

#include "image.h"
#include <gui.h>

namespace gfx::ogl
{
    class GUI
    {
    public:
        static void Init();
        static void NewFrame();
        static void Render(CommandBuffer& commandBuffer, ImDrawData* drawData);
        static void Shutdown();
    };

    class GUI_Image final : public gfx::GUI_Image
    {
    public:
        explicit GUI_Image(gfx::ResourceRef<const gfx::Image> image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;

        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(gfx::ResourceRef<const gfx::Image> image) override;

        ImTextureID operator*() const override;

    private:
        GLint _id;
        gfx::ResourceRef<const gfx::Image> _image;
    };
}
