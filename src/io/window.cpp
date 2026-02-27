//
// Created by radue on 10/13/2024.
//
#include <GL/glew.h>
#include "core/resources/framebuffer.h"

#include "window.h"
#include "input.h"

#include <iostream>
#include <stb_image.h>

#include "manager.h"

namespace gfx::io {
    Window::Window(const Builder& createInfo) :
        _title(createInfo.title),
        _extent(createInfo.extent),
        _resizable(createInfo.resizable),
        _fullscreen(createInfo.fullscreen),
        _api(createInfo.api)
    {
        glfwWindowHint(GLFW_CLIENT_API, createInfo.api == API::OpenGL ? GLFW_OPENGL_API : GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, createInfo.resizable);

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
            static_cast<glm::i32>(width),
            static_cast<glm::i32>(height),
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

    	glm::uvec2 extent;
    	glfwGetFramebufferSize(_window, reinterpret_cast<int*>(&extent.x), reinterpret_cast<int*>(&extent.y));
		_extent = extent;

    }

    void Window::initContext()
    {
        Context::_currentThreadLinkedWindow = this;
        _framebuffer = Framebuffer::CreateDefault();
        
        glfwMakeContextCurrent(_window);
        glewInit();
    }

    void Window::Builder::build() const
    {
        Manager::createWindow(*this);
    }

    Window::~Window() {
        glfwDestroyWindow(_window);
    }

    void Window::close()
    {
        glfwSetWindowShouldClose(_window, GLFW_TRUE);
        _closed = true;
    }

    // std::vector <const char*> window::GetRequiredExtensions() const {
    //     uint32_t glfwExtensionCount = 0;
    //     const auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    //     std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    //     return extensions;
    // }

    // vk::SurfaceKHR window::CreateSurface(const vk::Instance& instance) const {
	   //  VkSurfaceKHR surface;
    // 	if (const auto result = glfwCreateWindowSurface(instance, m_window, nullptr, &surface); result != VK_SUCCESS) {
    // 		std::cerr << "Failed to create window surface: " << vk::to_string(static_cast<vk::Result>(result)) << std::endl;
    // 	}
    //
    // 	return { surface };
    // }

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

    void Window::focus()
    {
        Context::setFocusedWindow(this);
    }

    void Window::framebufferResize(GLFWwindow* handle, const int width, const int height) {
    	const auto app = static_cast<Window*>(glfwGetWindowUserPointer(handle));

    	app->_extent = { static_cast<glm::u32>(width), static_cast<glm::u32>(height) };
    	if (width == 0 || height == 0) {
    		app->pause();
    	} else {
    		app->unPause();
    	}
    }
}
