//
// Created by radue on 10/13/2024.
//

#pragma once

#include <string>

// #define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// #include <vulkan/vulkan.hpp>

#include <filesystem>
#include <glm/glm.hpp>

#include "input.h"
#include "context.h"
#include "io/time.h"
#include "../scenes/scene.h"

namespace gfx::io {
    class Window {
        friend class Input;
        friend class Input::Callbacks;
        friend class Manager;
        friend class Time;
    public:
        struct Builder {
            std::string title = "GFXFramework";
            glm::uvec2 extent = { 1280, 720 };
            bool resizable = true;
            bool fullscreen = false;
            API api = API::OpenGL;
            std::reference_wrapper<Scene> scene;

            explicit Builder(Scene& scene) : scene(scene) {}

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

            Builder& setAPI(API api) {
                this->api = api;
                return *this;
            }

            [[nodiscard]] void build() const;
        };

        ~Window();

        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        Window(Window &&) = default;
        Window &operator=(Window &&) = default;

        [[nodiscard]] bool shouldClose() const { return glfwWindowShouldClose(_window); }
        void close();

        [[nodiscard]] GLFWwindow* operator*() const { return _window; }
        [[nodiscard]] glm::uvec2 getExtent() const { return _extent; }
        // [[nodiscard]] std::vector<const char*> getRequiredExtensions() const;
        // [[nodiscard]] vk::SurfaceKHR createSurface(const vk::Instance&) const;

        [[nodiscard]] bool isPaused() const { return _paused; }

        void pause() { _paused = true; }
        void unPause() { _paused = false; }

        [[nodiscard]] const std::string& getTitle() const { return this->_title; }

        void setTitle(const std::string &title) {
            _title = title;
            glfwSetWindowTitle(_window, title.c_str());
        }

        [[nodiscard]] API getAPI() const { return _api; }
        [[nodiscard]] const gfx::Framebuffer* getFramebuffer() const { return _framebuffer.get(); }

        void setIcon(const std::filesystem::path& iconPath);

        bool isFocused() const { return _focused; }


    private:
        explicit Window(const Builder&);

        void initContext();

        void focus();
    	static void framebufferResize(GLFWwindow* handle, int width, int height);

        GLFWwindow* _window = nullptr;
        GLFWmonitor* _monitor = nullptr;
        GLFWimage* _icon = nullptr;
        const GLFWvidmode *_videoMode = nullptr;

        std::string _title;
        glm::uvec2 _extent;
        bool _resizable;
        bool _fullscreen;
        API _api;

        Input::State _inputState;
        Time::State _timeState;
        std::unique_ptr<gfx::Framebuffer> _framebuffer;

        bool _paused = false;
        bool _closed = false;
        bool _focused = true;
    };
}
