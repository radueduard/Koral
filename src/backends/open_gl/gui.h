//
// Created by radue on 3/17/2026.
//

#pragma once
#include <commandBuffer.h>

#include <imgui.h>

#include "image.h"
#include <gui.h>

namespace kor::ogl
{
    class GUI
    {
    public:
        static void Init();
        static void NewFrame();
        static void Render(CommandBuffer& commandBuffer, ImDrawData* drawData);
        static void Shutdown();
    };

    class GUI_Image final : public kor::GUI_Image
    {
    public:
        explicit GUI_Image(kor::ResourceRef<const kor::Image> image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;

        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(kor::ResourceRef<const kor::Image> image) override;

        ImTextureID operator*() const override;

        // @see kor::GUI_Image::refresh — the copy this handle shows has to be retaken every frame,
        // or the viewport keeps displaying the frame the handle was created on.
        void refresh(kor::CommandBuffer& commandBuffer) override;

    private:
        // 0, not indeterminate: setImage tests this before deleting the previous texture, and it
        // runs from the constructor — where the only thing it could otherwise read is garbage.
        GLint _id = 0;
        kor::ResourceRef<const kor::Image> _image;

        // Which layer and mip level of the source this holds, so the per-frame refresh retakes the
        // same one rather than silently falling back to 0.
        glm::u32 _layer = 0;
        glm::u32 _level = 0;

        // The source's generation when the destination was last sized. A resize recreates the
        // source at a new extent, and this handle's copy has immutable storage at the old one.
        glm::u64 _generation = 0;
    };
}
