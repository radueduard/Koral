//
// Created by radue on 2/17/2026.
//
#include <GL/glew.h>

#include "manager.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <stb_image_write.h>
#include <magic_enum/magic_enum.hpp>

#include "input.h"

namespace gfx::io
{
    Window& Manager::createWindow(const Window::Builder& createInfo)
    {
        auto window = new Window(createInfo);
        auto windowRef = std::ref(*window);

        _windows.emplace_back(
            [windowRef, createInfo] ()
            {
                auto& w = windowRef.get();
                w.initContext();
                w._inputState.setup();
                w._timeState.setup();
                auto& scene = createInfo.scene.get();

                scene.Initialize();

                while (!w.shouldClose()) {
                    w._inputState.update();
                    w._timeState.update();
                    scene.Update();
                    scene.Render();
                    glfwSwapBuffers(*w);
                }
                std::cout << Context::Window().getTitle() << " is closing." << std::endl;
                w.close();
            }, window);
        return *window;
    }

    void Manager::removeWindow(Window* window)
    {
        const auto it = std::ranges::find_if(_windows, [window](const auto& w) {
            return w.second.get() == window;
        });

        if (it != _windows.end()) {
            _windows.erase(it);
        } else {
            std::cerr << "Attempted to remove a window that does not exist!" << std::endl;
        }
    }

    Manager::Manager()
    {
        if (const auto result = glfwInit(); result == GLFW_FALSE) {
            throw std::runtime_error("Failed to initialize GLFW!");
        }
    }

    Manager::~Manager()
    {
        glfwTerminate();
    }

    void Manager::update()
    {
        glfwPollEvents();

        _windows.erase(std::ranges::remove_if(_windows, [](const auto& window) {
            return window.second->_closed;
        }).begin(), _windows.end());
    }
}
