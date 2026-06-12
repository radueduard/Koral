//
// Created by radue on 10/13/2024.
//

module;

#include <glm/glm.hpp>
#include "api.h"
#include <GLFW/glfw3.h>

export module gfx.window;

import std;
import gfx.input;
import gfx.time;
import gfx.framebuffer;
import gfx.context;
import gfx.scene;
import gfx.resource;
import gfx.surface;
import gfx.framebuffer;

namespace gfx
{
    class Engine;
}

namespace gfx::io {
    export class GFX_API Window {
        friend class Input;
        friend class Input::Callbacks;
        friend class gfx::Engine;
        friend class Time;
    public:
        struct GFX_API Builder {
            std::string title = "GFXFramework";
            glm::uvec2 extent = { 1280, 720 };
            bool resizable = true;
            bool fullscreen = false;
            bool decorated = true;
            bool transparentFramebuffer = false;
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

        Window(Window &&) = default;
        Window &operator=(Window &&) = default;

        [[nodiscard]] bool shouldClose() const;
        void close();

        [[nodiscard]] GLFWwindow* operator*() const { return _window; }
        [[nodiscard]] glm::uvec2 getExtent() const { return _extent; }

        [[nodiscard]] bool isPaused() const { return _paused; }
        [[nodiscard]] bool isResizable() const { return _resizable; }
        [[nodiscard]] bool isFullscreen() const { return _fullscreen; }
        [[nodiscard]] bool isDecorated() const { return _decorated; }
        [[nodiscard]] bool isFramebufferTransparent() const { return _transparentFramebuffer; }

        void pause() { _paused = true; }
        void unPause() { _paused = false; }

        void setTitle(const std::string &title);

        [[nodiscard]] API getAPI() const { return _api; }
        [[nodiscard]] const std::string& getTitle() const { return _title; }
        [[nodiscard]] ResourceRef<const gfx::Framebuffer> getFramebuffer() const { return _framebuffer; }
        [[nodiscard]] bool hasResized() const { return _hasResized; }
        [[nodiscard]] const gfx::Surface& getSurface() const { return *_surface; }

        void setIcon(const std::filesystem::path& iconPath);

        [[nodiscard]] bool isFocused() const { return _focused; }

        void Update();
        void LateUpdate();

    private:
        void focus();
    	static void framebufferResize(GLFWwindow* handle, int width, int height);

        GLFWwindow* _window = nullptr;
        GLFWmonitor* _monitor = nullptr;
        GLFWimage* _icon = nullptr;
        const GLFWvidmode *_videoMode = nullptr;
        std::unique_ptr<gfx::Surface> _surface;

        std::string _title;
        glm::uvec2 _extent;
        bool _resizable;
        bool _fullscreen;
        bool _decorated;
        bool _transparentFramebuffer;
        API _api;

        Input::State _inputState;
        Time::State _timeState;
        gfx::Resource<gfx::Framebuffer> _framebuffer;

        std::unique_ptr<Scene> _scene;

        bool _paused = false;
        bool _closed = false;
        bool _focused = true;
        bool _hasResized = false;
    };
}
