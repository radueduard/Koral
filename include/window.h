//
// Created by radue on 10/13/2024.
//

#pragma once

#include <string>

#include <filesystem>
#include <glm/glm.hpp>

#include "input.h"
#include "context.h"
#include "gtime.h"
#include "api.h"
#include "scene.h"

namespace kor
{
    class Surface;
    class Framebuffer;
    class Engine;
}

struct GLFWmonitor;
struct GLFWwindow;
struct GLFWimage;
struct GLFWvidmode;

namespace kor {
    class KORAL_API Window {
        friend class Input;
        friend class Input::Callbacks;
        friend class kor::Engine;
        friend class Time;
    public:
        struct KORAL_API Builder {
            std::string title = "GFXFramework";
            glm::uvec2 extent = { 1280, 720 };
            bool resizable = true;
            bool fullscreen = false;
            bool decorated = true;
            bool transparentFramebuffer = false;
            bool vsync = true;
            API api = API::eVulkan;
            std::unique_ptr<Scene> scene = nullptr;

            explicit Builder(std::unique_ptr<Scene> scene) : scene(std::move(scene)) {}

            Builder& setTitle(const std::string& title) {
                this->title = title;
                return *this;
            }

            Builder& setExtent(const glm::uvec2& extent) {
                this->extent = extent;
                return *this;
            }

            Builder& setResizable(bool resizable) {
                this->resizable = resizable;
                return *this;
            }

            Builder& setFullscreen(bool fullscreen) {
                this->fullscreen = fullscreen;
                return *this;
            }

            Builder& setDecorated(bool decorated) {
                this->decorated = decorated;
                return *this;
            }

            Builder& setTransparentFramebuffer(bool transparent) {
                this->transparentFramebuffer = transparent;
                return *this;
            }

            // Vertical sync. When true (default) frames are presented without tearing
            // and capped to the display's refresh rate; when false, frames present as
            // fast as possible (uncapped, may tear).
            Builder& setVSync(bool vsync) {
                this->vsync = vsync;
                return *this;
            }

            Builder& setAPI(API api) {
                this->api = api;
                return *this;
            }

            std::unique_ptr<Window> build();
        };

        explicit Window(Builder&);
        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        // Declared here, defaulted in window.cpp — deliberately not `= default` inline.
        //
        // _surface is a std::unique_ptr to a forward-declared kor::Surface, so anything that has to
        // destroy it needs the complete type. Move-assignment destroys the object being overwritten,
        // and the move constructor needs the destructor available for unwinding, so defaulting
        // either one *here* instantiates std::default_delete<Surface> against an incomplete type.
        // The constructor and destructor above are already out-of-line for exactly this reason;
        // these two were the ones left behind.
        //
        // GCC and Clang happened not to instantiate the deleter for these and compiled fine, so
        // this only ever surfaced on MSVC ("can't delete an incomplete type"), the first time the
        // Windows leg of the release was run.
        Window(Window &&);
        Window &operator=(Window &&);

        [[nodiscard]] bool shouldClose() const;
        void close();

        [[nodiscard]] GLFWwindow* operator*() const { return _window; }
        [[nodiscard]] glm::uvec2 getExtent() const { return _extent; }

        [[nodiscard]] bool isPaused() const { return _paused; }
        [[nodiscard]] bool isResizable() const { return _resizable; }
        [[nodiscard]] bool isFullscreen() const { return _fullscreen; }
        [[nodiscard]] bool isDecorated() const { return _decorated; }
        [[nodiscard]] bool isVSync() const { return _vsync; }
        [[nodiscard]] bool isFramebufferTransparent() const { return _transparentFramebuffer; }

        void pause() { _paused = true; }
        void unPause() { _paused = false; }

        void setTitle(const std::string &title);

        [[nodiscard]] API getAPI() const { return _api; }
        [[nodiscard]] const std::string& getTitle() const { return _title; }
        [[nodiscard]] kor::ResourceRef<kor::Framebuffer> getFramebuffer() const { return _framebuffer; }
        [[nodiscard]] bool hasResized() const { return _hasResized; }
        [[nodiscard]] const kor::Surface& getSurface() const { return *_surface; }

        void setIcon(const std::filesystem::path& iconPath);

        [[nodiscard]] bool isFocused() const { return _focused; }

        void LateUpdate();

    private:
    	static void framebufferResize(GLFWwindow* handle, int width, int height);

        GLFWwindow* _window = nullptr;
        GLFWmonitor* _monitor = nullptr;
        GLFWimage* _icon = nullptr;
        const GLFWvidmode *_videoMode = nullptr;
        std::unique_ptr<kor::Surface> _surface;

        std::string _title;
        glm::uvec2 _extent;
        bool _resizable;
        bool _fullscreen;
        bool _decorated;
        bool _transparentFramebuffer;
        bool _vsync;
        API _api;

        kor::Resource<kor::Framebuffer> _framebuffer;

        std::unique_ptr<Scene> _scene;

        bool _paused = false;
        bool _closed = false;
        bool _focused = true;
        bool _hasResized = false;
    };
}
