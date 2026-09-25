//
// Created by radue on 10/13/2024.
//
#include <GL/glew.h>

#include <window.h>
#include <framebuffer.h>

#include "input.h"

#include <format>
#include <iostream>
#include <stb_image.h>
#include <GLFW/glfw3.h>

#include "frameGraph.h"
#include "gui.h"
#include "scene.h"
#include "module.h"
#include "scheduler.h"
#include "surface.h"
#include "../backends/vulkan/surface.h"
#include "../backends/vulkan/vulkanContext.h"

#include "../executor/MainThreadExecutor.h"
#include "tokenState.h"
#include "../executor/BackgroundExecutor.h"

// initLibs.cpp — seeds GLFW's Vulkan loader; must run before glfwInit().
void initGlfwVulkanLoader();

namespace kor {
    // Here rather than in the header: window.h forward-declares Scene, so the unique_ptr member's
    // destructor can only be instantiated where Scene is complete.
    Window::Builder::Builder(std::unique_ptr<Scene> scene) : scene(std::move(scene)) {}
    Window::Builder::Builder() = default;
    Window::Builder::~Builder() = default;
    Window::Builder::Builder(Builder&&) noexcept = default;
    Window::Builder& Window::Builder::operator=(Builder&&) noexcept = default;

    Window::Window(Builder& createInfo) :
        _title(createInfo.title),
        _extent(createInfo.extent),
        _resizable(createInfo.resizable),
        _fullscreen(createInfo.fullscreen),
        _decorated(createInfo.decorated),
        _transparentFramebuffer(createInfo.transparentFramebuffer),
        _vsync(createInfo.vsync),
        _api(createInfo.api),
        _imguiIni(createInfo.imguiIni.string()),
        _scene(std::move(createInfo.scene))
    {
        try {
            if (_scene) BringUp(createInfo);
            else BringUpSecondary(createInfo);
        } catch (...) {
            // The destructor does not run for a constructor that threw: undo what was set up.
            Release();
            throw;
        }
    }

    void Window::BringUp(Builder& createInfo)
    {
        Context::_window = this;
        _screenName = std::string(FrameGraph::Screen);
        Context::WindowList().insert(Context::WindowList().begin(), this);
        Context::_activeAPI = createInfo.api; // backend selection keys off this, not the window

        // Choose the windowing platform on Linux (X11 vs Wayland). This is an init hint, so it must
        // be set BEFORE glfwInit(), which snapshots hint values. GLFW_ANY_PLATFORM leaves GLFW's
        // automatic selection in place; on Windows/macOS the X11/Wayland enums are unsupported and
        // the request quietly falls back to automatic.
        int requestedPlatform = GLFW_ANY_PLATFORM;
        switch (createInfo.platform) {
            case WindowPlatform::eX11:     requestedPlatform = GLFW_PLATFORM_X11;     break;
            case WindowPlatform::eWayland: requestedPlatform = GLFW_PLATFORM_WAYLAND; break;
            case WindowPlatform::eAuto:    break;
        }

        // OpenGL is the exception to the choice: GLEW (our OpenGL function loader) resolves entry
        // points through GLX, so a Wayland-platform GLFW window (EGL context) makes glewInit() fail
        // with "No GLX display". Pin OpenGL to X11/XWayland regardless of the request, and say so if
        // the user explicitly asked for Wayland.
        if (createInfo.api == API::eOpenGL) {
            if (createInfo.platform == WindowPlatform::eWayland)
                std::cerr << "[window] OpenGL requires X11/XWayland (GLEW is GLX-only); "
                             "ignoring the Wayland request" << std::endl;
            requestedPlatform = GLFW_PLATFORM_X11;
        }

        if (requestedPlatform != GLFW_ANY_PLATFORM) {
            if (glfwPlatformSupported(requestedPlatform))
                glfwInitHint(GLFW_PLATFORM, requestedPlatform);
            else
                std::cerr << "[window] the requested windowing platform is not available in this "
                             "GLFW build/session; using automatic selection" << std::endl;
        }

        // Point GLFW's Vulkan support at the loader Koral links and ships, instead of its own
        // dlopen-by-name (which fails on installed builds — see initLibs.cpp). Init hint: must
        // precede glfwInit(), which snapshots hint values.
        if (createInfo.api == API::eVulkan) {
            initGlfwVulkanLoader();
        }

        if (glfwInit() != GLFW_TRUE) {
            const char* description = nullptr;
            glfwGetError(&description);
            throw std::runtime_error(std::string("GLFW could not initialize: ")
                                     + (description ? description : "no reason given"));
        }
        _stage = Stage::eGlfw;

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
            const char* description = nullptr;
            glfwGetError(&description);
            throw std::runtime_error(std::string("GLFW could not create the window: ")
                                     + (description ? description : "no reason given"));
        }

        glfwSetWindowUserPointer(_window, this);

        // Register only the window-management callbacks now (before the
        // framebuffer-ready wait loop). Input callbacks are registered later,
        // after GUI::Init(), because they explicitly forward events to ImGui.
        glfwSetFramebufferSizeCallback(_window, FramebufferResize);
        glfwSetWindowCloseCallback(_window, Input::Callbacks::CloseCallback);

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
            // OpenGL presents through GLFW's buffer swap; map vsync onto the swap interval.
            glfwSwapInterval(_vsync ? 1 : 0);
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
                // Without a function loader every later GL call is a null pointer;
                // fail loudly here instead of crashing somewhere confusing.
                throw std::runtime_error(std::string("Failed to initialize GLEW: ")
                    + reinterpret_cast<const char*>(glewGetErrorString(result)));
            }
        } else if (_api == API::eVulkan) {
            kor::vk::Context::Init();
        }
        _stage = Stage::eDevice;

        _surface = kor::Surface::Create(*this);
        Context::_scheduler = Scheduler::Builder()
            // A floor, not a promise: the surface may require more and the driver may allocate
            // more still. Everything per-frame is sized to what was actually allocated, which
            // Scheduler::Initialize adopts before anything reads it.
            .SetImageCount(2)
            .Build();
        if (!Context::_scheduler.Valid()) throw std::runtime_error(Context::_scheduler.Failure()->message);
        Context::_scheduler->Initialize();
        _framebuffer = Framebuffer::CreateDefault();
        kor::GUI::Init();

        // Register input callbacks AFTER GUI::Init() so that our callbacks can
        // safely forward events to ImGui. We use install_callbacks=false in
        // ImGui's init (see vulkan/gui.cpp and open_gl/gui.cpp) so ImGui does
        // NOT install its own GLFW callbacks; our callbacks are the sole chain.
        Time::Setup();
        Input::Setup(_window);
        // Through the same path an undocked panel's window takes, so there is one way in rather than
        // two that can drift apart. @see Input::AttachTo
        Input::AttachTo(_window);

        Context::_mainThreadExecutor = new MainThreadExecutor();
        Context::_backgroundExecutor = new BackgroundExecutor();

        Context::_repository = new Repository();

        // Modules first: a scene's Initialize() is where it asks a module for the things it needs
        // (a camera, a light), so every module has to be ready before the scene runs. They were
        // constructed much earlier — before the device — and this is the point at which there is
        // finally something for them to build resources against.
        ModuleHost::Initialize();
        _stage = Stage::eRuntime;

        _scene->Initialize();
        _stage = Stage::eScene;
    }

    void Window::BringUpSecondary(const Builder& createInfo)
    {
        _secondary = true;
        if (Context::_window == nullptr)
            throw std::runtime_error("A window without a scene is a second window, and there is no first one: "
                                     "the application's window has to exist before a scene opens another.");
        if (Context::ActiveAPI() != API::eVulkan)
            throw std::runtime_error("Only the Vulkan backend opens more than one window so far.");
        _api = API::eVulkan;
        _imguiIni.clear();

        // Numbered, not named after the title: a title can repeat and can change, a screen name cannot.
        static glm::u32 opened = 0;
        _screenName = std::format("screen:{}", ++opened);

        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, createInfo.resizable);
        glfwWindowHint(GLFW_DECORATED, createInfo.decorated);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, createInfo.transparentFramebuffer);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
        _monitor = createInfo.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        if (_monitor) {
            _videoMode = glfwGetVideoMode(_monitor);
            _extent = { static_cast<glm::u32>(_videoMode->width), static_cast<glm::u32>(_videoMode->height) };
        }
        _window = glfwCreateWindow(static_cast<int>(_extent.x), static_cast<int>(_extent.y),
                                   createInfo.title.c_str(), _monitor, nullptr);
        if (_window == nullptr) {
            const char* description = nullptr;
            glfwGetError(&description);
            throw std::runtime_error(std::string("GLFW could not create the window: ")
                                     + (description ? description : "no reason given"));
        }
        glfwSetWindowUserPointer(_window, this);
        glfwSetFramebufferSizeCallback(_window, FramebufferResize);
        glfwSetWindowCloseCallback(_window, Input::Callbacks::CloseCallback);

        int width = 0, height = 0;
        glfwGetFramebufferSize(_window, &width, &height);
        _extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
        // Not waited for, unlike the main window's: a scene opens this from inside a frame, and a
        // compositor that has not sized it yet just means it is not shown until it has.
        if (width == 0 || height == 0) {
            _extent = glm::max(_extent, glm::uvec2(1));
            Pause();
        }

        _surface = kor::Surface::Create(*this);
        dynamic_cast<vk::Surface&>(*_surface).CreateSwapChain(*this, Context::Scheduler().ImageCount());
        _framebuffer = Framebuffer::CreateDefault(*this);

        // Its input goes where the main window's does — it is the same user at the same keyboard.
        Input::AttachEngineWindow(_window);
        _focused = glfwGetWindowAttrib(_window, GLFW_FOCUSED) == GLFW_TRUE;
        Context::WindowList().push_back(this);
        _stage = Stage::eScene;
    }

    Resource<Window> Window::Builder::Build()
    {
        try {
            return Resource<Window>(std::make_unique<Window>(*this), title);
        } catch (const std::exception& e) {
            log::Error("[window] could not be created: {}", e.what());
            return Resource<Window>::Failed(Error{.code = ErrorCode::eWindowCreationFailed, .message = e.what()}, title);
        }
    }

    // Out of line for the same reason, but for kor::Framebuffer: returning the ResourceRef by value
    // instantiates that type's destructor, which needs it complete. This file has it via
    // <framebuffer.h>; window.h does not (see context.h).
    ResourceRef<Framebuffer> Window::DefaultFramebuffer() const {
        return _framebuffer;
    }

    Window::~Window() { Release(); }

    void Window::Release() {
        std::erase(Context::WindowList(), this);
        if (_secondary) {
            // Only what is its own. The frame that last drew into it may still be on its way to the
            // GPU — the window may even be closing in the middle of the frame being built — so the
            // surface and the OS window go to the scheduler, which destroys them once that frame is done.
            if (_window) Input::DetachFrom(_window);
            _framebuffer.Reset();
            if (Context::_scheduler.Valid()) {
                Context::_scheduler->RetireWindow(std::move(_surface), _window);
            } else {
                _surface.reset();
                if (_window) glfwDestroyWindow(_window);
            }
            _window = nullptr;
            _stage = Stage::eNone;
            return;
        }
        // Torn down as far as it was brought up: all of it for a window that was built, only the
        // first steps for a constructor that threw part-way.
        if (_stage >= Stage::eRuntime) {
            Context::Scheduler().WaitIdle();
            // Everything is still alive and the GPU is done: the scene's last chance to stop its own
            // background work. What it submits from there is waited for too.
            if (_stage == Stage::eScene && _scene) {
                _scene->Shutdown();
                Context::Scheduler().WaitIdle();
            }
            // One-off submissions still held for the GPU, before the scene and modules whose resources
            // their records may keep alive.
            detail::collectRetired(/*all=*/true);
            // The scene goes first: it holds resources the modules created, and those have to be
            // released while the module that made them is still alive to release them properly.
            _scene.reset();
            ModuleHost::Shutdown();
            GUI::Shutdown();
        }
        _framebuffer.Reset();
        if (Context::_scheduler.Valid()) Context::_scheduler->WaitIdle();
        Context::_scheduler.Reset();
        if (_stage >= Stage::eDevice && _api == API::eVulkan) {
            vk::Context::StopTokens();
        }
        delete Context::_repository;
        delete Context::_mainThreadExecutor;
        delete Context::_backgroundExecutor;
        Context::_repository = nullptr;
        Context::_mainThreadExecutor = nullptr;
        Context::_backgroundExecutor = nullptr;
        _surface.reset();
        if (_stage >= Stage::eDevice && _api == API::eVulkan) {
            vk::Context::Destroy();
        }
        if (_window) glfwDestroyWindow(_window);
        _window = nullptr;
        if (_stage >= Stage::eGlfw) glfwTerminate();
        if (Context::_window == this) Context::_window = nullptr;
        _stage = Stage::eNone;
    }

    bool Window::ShouldClose() const
    { return glfwWindowShouldClose(_window); }

    void Window::Close()
    {
        glfwSetWindowShouldClose(_window, GLFW_TRUE);
        _closed = true;
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
        }
        glfwSetWindowIcon(_window, 1, image);

        if (_icon != nullptr)
        {
            stbi_image_free(_icon->pixels);
            delete _icon;
        }
        _icon = image;
    }

    void Window::LateUpdate() {
        _hasResized = false;
    }

    void Window::FramebufferResize(GLFWwindow* handle, const int width, const int height) {
    	const auto app = static_cast<Window*>(glfwGetWindowUserPointer(handle));

        app->_hasResized = true;

    	app->_extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
    	if (width == 0 || height == 0) {
    		app->Pause();
    	} else {
    		app->Unpause();
    	}
    }
}
