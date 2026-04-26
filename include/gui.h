//
// Created by radue on 3/17/2026.
//

#pragma once
#include "commandBuffer.h"
#include "imageView.h"
#include "sampler.h"

#include <imgui.h>
#include <map>

struct ImGui_ImplVulkan_InitInfo;

struct GLFWwindow;

namespace gfx
{
    class Scene;

    class GFX_API GUI_Image
    {
    public:
        virtual ~GUI_Image() = default;

        virtual void setLayerAndLevel(glm::u32 layer, glm::u32 level) = 0;
        virtual void setImage(gfx::ResourceRef<Image> image) = 0;

        virtual ImTextureID operator*() const = 0;

        static gfx::Resource<GUI_Image> Create(gfx::ResourceRef<gfx::Image> image, glm::u32 layer = 0, glm::u32 level = 0);
    };

    enum class Font
    {
        Light,
        Regular,
        Bold,
        Italic,
        Black
    };

    class GUI
    {
    public:
        static void Init();
        GFX_API static void Render(gfx::CommandBuffer& commandBuffer, Scene& scene);
        static void Shutdown();

        GFX_API static ImFont* GetFont(Font font);

    private:
        static void DefineStyle();
        inline static std::map<Font, ImFont*> _fonts {};
    };


}
