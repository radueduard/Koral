//
// Created by radue on 3/17/2026.
//

module;

#include <api.h>

#include <glm/glm.hpp>
#include <imgui.h>

export module gfx.gui;
import std;
import gfx.image;
import gfx.commandBuffer;
import gfx.resource;

struct ImGui_ImplVulkan_InitInfo;

struct GLFWwindow;

namespace gfx
{
    class Scene;

    export class GFX_API GUI_Image
    {
    public:
        virtual ~GUI_Image() = default;

        virtual void setLayerAndLevel(glm::u32 layer, glm::u32 level) = 0;
        virtual void setImage(ResourceRef<const Image> image) = 0;

        virtual ImTextureID operator*() const = 0;

        static gfx::Resource<GUI_Image> Create(gfx::ResourceRef<const gfx::Image> image, glm::u32 layer = 0, glm::u32 level = 0);
    };

    enum class Font
    {
        Light,
        Regular,
        Bold,
        Italic,
        Black
    };

    export class GUI
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
