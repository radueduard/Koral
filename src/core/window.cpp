//
// Created by radue on 10/13/2024.
//

module;

#include <GL/glew.h>
#include <stb_image.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

module gfx;
import :window;
import :context;
import :scheduler;
import :framebuffer;

import std;
import :vk_context;
import :vk_scheduler;
import :vk_surface;
import :vk_swapChain;

namespace gfx {
    struct WindowSceneBox {
        std::unique_ptr<Scene> scene;
    };

    Window::Builder::Builder(std::unique_ptr<Scene> scene)
        : sceneBox(std::make_unique<WindowSceneBox>(WindowSceneBox{std::move(scene)})) {}
    Window::Builder::~Builder() = default;

    Window::Window(Builder& createInfo) :
        _title(createInfo.title),
        _extent(createInfo.extent),
        _resizable(createInfo.resizable),
        _fullscreen(createInfo.fullscreen),
        _decorated(createInfo.decorated),
        _transparentFramebuffer(createInfo.transparentFramebuffer),
        _api(createInfo.api),
        _scene(std::move(createInfo.sceneBox->scene))
    {
        Context::_window = this;

        // If you want to force a backend on Linux, do it here BEFORE glfwInit().
        // Uncomment one of these to pin the platform; otherwise GLFW auto-picks via XDG_SESSION_TYPE.
        // glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        // glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);

        if (const auto result = glfwInit(); result != GLFW_TRUE) {
            std::cerr << "Failed to initialize GLFW: " << result << std::endl;
        }

#ifndef NDEBUG
        const int platform = glfwGetPlatform();
        std::cerr << "GLFW platform: "
                  << (platform == GLFW_PLATFORM_WAYLAND ? "Wayland"
                    : platform == GLFW_PLATFORM_X11     ? "X11"
                    : platform == GLFW_PLATFORM_WIN32   ? "Win32"
                    : platform == GLFW_PLATFORM_COCOA   ? "Cocoa"
                                                        : "Unknown")
                  << std::endl;
#endif

        glfwWindowHint(GLFW_CLIENT_API, createInfo.api == API::eOpenGL ? GLFW_OPENGL_API : GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, createInfo.resizable);
        glfwWindowHint(GLFW_DECORATED, createInfo.decorated);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, createInfo.transparentFramebuffer);

        // Be explicit about HiDPI behaviour. true (the GLFW 3.4 default) means the
        // framebuffer is in physical pixels; flip to false if you'd rather render at
        // logical size and let the compositor scale.
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);

        const glm::u32 width = createInfo.extent.x;
        const glm::u32 height = createInfo.extent.y;

        if (createInfo.fullscreen) {
            _monitor = glfwGetPrimaryMonitor();
            if (_monitor == nullptr) {
                std::cerr << "Failed to get primary monitor" << std::endl;
            }

            _videoMode = glfwGetVideoMode(_monitor);
            _extent = {
                static_cast<glm::u32>(_videoMode->width),
                static_cast<glm::u32>(_videoMode->height)
            };
        } else {
            _monitor = nullptr;
            _videoMode = nullptr;
        }

        _window = glfwCreateWindow(
            static_cast<glm::i32>(_extent.x),
            static_cast<glm::i32>(_extent.y),
            createInfo.title.c_str(),
            _monitor,
            nullptr);

        if (_window == nullptr) {
            std::cerr << "Failed to create window" << std::endl;
        }

        glfwSetWindowUserPointer(_window, this);

        glfwSetKeyCallback(_window, Input::Callbacks::keyCallback);
        glfwSetCursorPosCallback(_window, Input::Callbacks::mouseMoveCallback);
        glfwSetMouseButtonCallback(_window, Input::Callbacks::mouseButtonCallback);
        glfwSetScrollCallback(_window, Input::Callbacks::scrollCallback);
        glfwSetFramebufferSizeCallback(_window, framebufferResize);
        glfwSetWindowFocusCallback(_window, Input::Callbacks::focusCallback);
        glfwSetWindowCloseCallback(_window, Input::Callbacks::closeCallback);

        // On Wayland the framebuffer size isn't valid until the compositor has sent
        // at least one xdg_surface.configure. Pump events and wait until we have a
        // non-zero size before doing anything that consumes it (Surface, Scheduler).
        {
            int fbw = 0, fbh = 0;
            glfwPollEvents();
            glfwGetFramebufferSize(_window, &fbw, &fbh);
            while ((fbw == 0 || fbh == 0) && !glfwWindowShouldClose(_window)) {
                glfwWaitEvents();
                glfwGetFramebufferSize(_window, &fbw, &fbh);
            }
            _extent = { static_cast<glm::u32>(fbw), static_cast<glm::u32>(fbh) };
        }

        // glfwMakeContextCurrent is invalid on a GLFW_NO_API (Vulkan) window;
        // it would emit GLFW_NO_WINDOW_CONTEXT and pollute the error state.
        if (_api == API::eOpenGL) {
            glfwMakeContextCurrent(_window);
        }

        // glfwSetWindowPos is unsupported on Wayland (compositors deny client
        // positioning). Skip the centering dance entirely there.
        if (!_fullscreen && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
            if (const auto primaryMonitor = glfwGetPrimaryMonitor()) {
                if (const auto videoMode = glfwGetVideoMode(primaryMonitor)) {
                    int monitorX, monitorY;
                    glfwGetMonitorPos(primaryMonitor, &monitorX, &monitorY);
                    const int windowX = monitorX + (videoMode->width - static_cast<int>(width)) / 2;
                    const int windowY = monitorY + (videoMode->height - static_cast<int>(height)) / 2;
                    glfwSetWindowPos(_window, windowX, windowY);
                } else {
                    std::cerr << "Failed to get video mode of primary monitor" << std::endl;
                }
            } else {
                std::cerr << "Failed to get primary monitor" << std::endl;
            }
        }

        if (_api == API::eOpenGL) {
            glewExperimental = GL_TRUE;
            if (const auto result = glewInit(); result != GLEW_OK) {
                std::cerr << "Failed to initialize GLEW: " << glewGetErrorString(result) << std::endl;
            }
        } else if (_api == API::eVulkan) {
            gfx::vk::Context::Init();
        }

        _surface = gfx::Surface::Create(*this);
        Context::_scheduler = Scheduler::Builder()
            .setMinImageCount(2)
            .setImageCount(3)
            .build();
        Context::_scheduler->Initialize();
        _framebuffer = Framebuffer::CreateDefault();
        gfx::GUI::Init();

        _timeState.setup();
        _inputState.setup();

        Context::_mainThreadExecutor = new MainThreadExecutor();
        Context::_backgroundExecutor = new BackgroundExecutor();


        _scene->Initialize();
    }

    std::unique_ptr<Window> Window::Builder::build()
    {
        return std::make_unique<Window>(*this);
    }

    Window::~Window() {
        Context::GetScheduler().WaitIdle();
        _scene.reset();
        GUI::Shutdown();
        _framebuffer.reset();
        delete Context::_scheduler;
        delete Context::_mainThreadExecutor;
        delete Context::_backgroundExecutor;
        // delete Context::_repository;
        _surface.reset();
        if (_api == API::eVulkan) {
            vk::Context::Destroy();
        }
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    bool Window::shouldClose() const
    { return glfwWindowShouldClose(_window); }

    void Window::close()
    {
        glfwSetWindowShouldClose(_window, GLFW_TRUE);
        _closed = true;
    }

    void Window::setTitle(const std::string& title)
    {
        _title = title;
        glfwSetWindowTitle(_window, title.c_str());
    }

    void Window::setIcon(const std::filesystem::path& iconPath)
    {
        const auto image = new GLFWimage;
        const std::string iconPathStr = iconPath.string();
        image->pixels = stbi_load(iconPathStr.c_str(), &image->width, &image->height, nullptr, 4);
        if (image->pixels == nullptr) {
            std::cerr << "Failed to load window icon" << std::endl;
        }
        glfwSetWindowIcon(_window, 1, image);

        if (_icon != nullptr)
        {
            stbi_image_free(_icon->pixels);
            delete _icon;
        }
        _icon = image;
    }

    void Window::Update() {
        glfwPollEvents();
        _timeState.update();
    }

    void Window::LateUpdate() {
        _inputState.update();
        _hasResized = false;
    }

    void Window::focus()
    {
        Context::setFocusedWindow(this);
    }

    void Window::framebufferResize(GLFWwindow* handle, const int width, const int height) {
        const auto app = static_cast<Window*>(glfwGetWindowUserPointer(handle));

        app->_hasResized = true;

        app->_extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
        if (width == 0 || height == 0) {
            app->pause();
        } else {
            app->unPause();
        }
    }
}
