//
// A scene's interface: its own Dear ImGui context, drawn over its window.
//

#pragma once

#include <map>
#include <string>
#include <vector>

#include "gui.h"
#include "scene.h"

struct ImGuiContext;
struct GLFWwindow;

namespace kor
{
    /**
     * @brief One scene's Dear ImGui context and the backends drawing it into the scene's window.
     *
     * Made when a scene asks for one (Scene::EnableInterface) and destroyed with the scene. Every
     * ImGui call happens with the scene's context current: the application makes it so around the
     * scene's hooks, and the input callbacks around every event they forward.
     */
    class Interface
    {
    public:
        Interface(Scene& scene, const InterfaceSettings& settings);
        ~Interface();
        Interface(const Interface&) = delete;
        Interface& operator=(const Interface&) = delete;

        /** @brief Makes this the current ImGui context — Koral's and every loaded library's. */
        void MakeCurrent() const;

        /** @brief Runs one ImGui frame — Scene::RenderUI, then the modules' — and records its draws. */
        void Render(CommandBuffer& commandBuffer);

        /** @brief Draws the panels undocked into windows of their own. After the frame is submitted. */
        void RenderPlatformWindows();

        [[nodiscard]] ImGuiContext* Context() const { return _context; }
        [[nodiscard]] ImFont* GetFont(Font font) const;

        /** @brief A handle made while this interface was current, refreshed with it every frame. */
        void Track(const ResourceRef<GuiImage>& image) { _images.push_back(image); }

        /** @brief The interface of the current scene, if it has one. */
        [[nodiscard]] static Interface* Current();

        /** @brief No ImGui context is current: what a scene without an interface runs with. */
        static void MakeNoneCurrent();

    private:
        void DefineStyle();
        void AttachPlatformWindows();

        Scene& _scene;
        ImGuiContext* _context = nullptr;
        std::string _iniFile;   // backs io.IniFilename, which keeps the pointer
        std::map<Font, ImFont*> _fonts;
        std::vector<ResourceRef<GuiImage>> _images;
        std::vector<GLFWwindow*> _platformWindows;
    };
}
