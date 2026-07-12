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

namespace kor
{
    class Scene;

    class KORAL_API GUI_Image
    {
    public:
        virtual ~GUI_Image() = default;

        virtual void setLayerAndLevel(glm::u32 layer, glm::u32 level) = 0;
        virtual void setImage(kor::ResourceRef<const Image> image) = 0;

        virtual ImTextureID operator*() const = 0;

        static kor::Resource<GUI_Image> Create(kor::ResourceRef<const kor::Image> image, glm::u32 layer = 0, glm::u32 level = 0);
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
        KORAL_API static void Render(kor::CommandBuffer& commandBuffer, Scene& scene);
        static void Shutdown();

        KORAL_API static ImFont* GetFont(Font font);

    private:
        static void DefineStyle();
        inline static std::map<Font, ImFont*> _fonts {};
    };


}
