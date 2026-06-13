//
// Created by radue on 3/17/2026.
//

module;

#include <imgui.h>
#include <GL/gl.h>
#include <glm/glm.hpp>

export module gfx:ogl_gui;
import :ogl_types;

import :gui;
import resource;


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
        explicit GUI_Image(ResourceRef<const gfx::Image> image, glm::u32 layer, glm::u32 level);
        ~GUI_Image() override;

        void setLayerAndLevel(glm::u32 layer, glm::u32 level) override;
        void setImage(ResourceRef<const gfx::Image> image) override;

        ImTextureID operator*() const override;

    private:
        GLint _id;
        ResourceRef<const gfx::Image> _image;
    };
}
