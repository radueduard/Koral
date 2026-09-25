//
// Created by radue on 10/13/2024.
//
#include <window.h>
#include <framebuffer.h>

#include <iostream>
#include <stdexcept>
#include <string>

#include <stb_image.h>
#include <GLFW/glfw3.h>

#include "context.h"
#include "scheduler.h"
#include "surface.h"
#include "../backends/vulkan/surface.h"

namespace kor {
    Window::Window(const WindowSettings& settings) :
        _title(settings.title),
        _extent(settings.extent),
        _resizable(settings.resizable),
        _fullscreen(settings.fullscreen),
        _decorated(settings.decorated),
        _transparentFramebuffer(settings.transparentFramebuffer),
        _vsync(settings.vsync)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, _resizable);
        glfwWindowHint(GLFW_DECORATED, _decorated);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, _transparentFramebuffer);
        // The framebuffer is in physical pixels on a scaled display (GLFW 3.4's default, stated).
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);

        if (_fullscreen) {
            _monitor = glfwGetPrimaryMonitor();
            if (_monitor) {
                _videoMode = glfwGetVideoMode(_monitor);
                _extent = { static_cast<glm::u32>(_videoMode->width), static_cast<glm::u32>(_videoMode->height) };
            }
        }

        _window = glfwCreateWindow(static_cast<int>(_extent.x), static_cast<int>(_extent.y),
                                   _title.c_str(), _monitor, nullptr);
        if (_window == nullptr) {
            const char* description = nullptr;
            glfwGetError(&description);
            throw std::runtime_error(std::string("GLFW could not create the window: ")
                                     + (description ? description : "no reason given"));
        }

        glfwSetWindowUserPointer(_window, this);
        glfwSetFramebufferSizeCallback(_window, FramebufferResize);
        glfwSetWindowCloseCallback(_window, CloseRequested);

        // On Wayland the framebuffer size is not valid until the compositor has configured the
        // surface. Pumped briefly rather than waited out: a window opened from inside a running
        // application should not stall every other one. One that is still unsized simply is not
        // shown until it has a size.
        int width = 0, height = 0;
        glfwGetFramebufferSize(_window, &width, &height);
        for (int attempt = 0; (width == 0 || height == 0) && attempt < 50 && !glfwWindowShouldClose(_window); ++attempt) {
            glfwWaitEventsTimeout(0.01);
            glfwGetFramebufferSize(_window, &width, &height);
        }
        if (width == 0 || height == 0) {
            _paused = true;
        } else {
            _extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
        }
        _focused = glfwGetWindowAttrib(_window, GLFW_FOCUSED) == GLFW_TRUE;

        // Centred on the primary monitor, where the platform lets a client place its window at all.
        if (!_fullscreen && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
            if (const auto primaryMonitor = glfwGetPrimaryMonitor()) {
                if (const auto videoMode = glfwGetVideoMode(primaryMonitor)) {
                    int monitorX, monitorY;
                    glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
                    glfwSetWindowPos(_window, monitorX + (videoMode->width - static_cast<int>(settings.extent.x)) / 2,
                                     monitorY + (videoMode->height - static_cast<int>(settings.extent.y)) / 2);
                }
            }
        }
    }

    Window::~Window() {
        _framebuffer.Reset();
        _surface.reset();
        if (_window) glfwDestroyWindow(_window);
        if (_icon != nullptr) {
            stbi_image_free(_icon->pixels);
            delete _icon;
        }
    }

    void Window::Retire(std::shared_ptr<kor::Surface>& surface, GLFWwindow*& window) {
        _framebuffer.Reset();
        surface = std::move(_surface);
        window = _window;
        _window = nullptr;
    }

    // Out of line: returning the ResourceRef by value instantiates kor::Framebuffer's destructor,
    // which needs it complete, and window.h does not include framebuffer.h.
    ResourceRef<Framebuffer> Window::DefaultFramebuffer() const {
        return _framebuffer;
    }

    bool Window::ShouldClose() const { return _closeRequested || (_window && glfwWindowShouldClose(_window)); }

    void Window::Close() {
        _closeRequested = true;
    }

    void Window::SetTitle(const std::string& title)
    {
        _title = title;
        glfwSetWindowTitle(_window, title.c_str());
    }

    void Window::SetIcon(const std::filesystem::path& iconPath)
    {
        const auto image = new GLFWimage;
        const std::string iconPathStr = iconPath.string();
        image->pixels = stbi_load(iconPathStr.c_str(), &image->width, &image->height, nullptr, 4);
        if (image->pixels == nullptr) {
            std::cerr << "Failed to load window icon" << std::endl;
            delete image;
            return;
        }
        glfwSetWindowIcon(_window, 1, image);

        if (_icon != nullptr) {
            stbi_image_free(_icon->pixels);
            delete _icon;
        }
        _icon = image;
    }

    void Window::LateUpdate() {
        _hasResized = false;
    }

    void Window::FramebufferResize(GLFWwindow* handle, const int width, const int height) {
    	const auto window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (!window) return;
        window->_hasResized = true;
    	window->_extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
    	if (width == 0 || height == 0) window->Pause();
    	else window->Unpause();
    }

    void Window::CloseRequested(GLFWwindow* handle) {
        if (const auto window = static_cast<Window*>(glfwGetWindowUserPointer(handle))) window->_closeRequested = true;
    }
}
