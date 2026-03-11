//
// Created by radue on 10/13/2024.
//
#include <GL/glew.h>
#include "../core/framebuffer.h"

#include "window.h"
#include "input.h"

#include <iostream>
#include <stb_image.h>

#include "manager.h"
#include "impl/vulkan/device.h"
#include "impl/vulkan/vulkanContext.h"

namespace gfx::io {
    Window::Window(const Builder& createInfo) :
        _title(createInfo.title),
        _extent(createInfo.extent),
        _resizable(createInfo.resizable),
        _fullscreen(createInfo.fullscreen),
        _api(createInfo.api)
    {
        glfwWindowHint(GLFW_CLIENT_API, createInfo.api == API::eOpenGL ? GLFW_OPENGL_API : GLFW_NO_API);
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

        glfwMakeContextCurrent(_window);

        switch (_api)
        {
        case API::eOpenGL:
            {
                glewInit();
                auto globalVAO = 0u;
                glGenVertexArrays(1, &globalVAO);
                glBindVertexArray(globalVAO);
                break;
            }
        case API::eVulkan:
            {
                gfx::vk::Context::Device().createCommandPools();
                break;
            }
        }
        Context::InitScheduler();
        _framebuffer = Framebuffer::CreateDefault();
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
        Context::DestroyScheduler();
        if (_api == API::eVulkan) {
            gfx::vk::Context::Device().freeCommandPools();
        }
        glfwSetWindowShouldClose(_window, GLFW_TRUE);
        _closed = true;
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
