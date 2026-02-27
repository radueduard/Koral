//
// Created by radue on 10/22/2024.
//

#include "input.h"

#include <iostream>


#include <glm/fwd.hpp>
#include <glm/vec2.hpp>
#include <magic_enum/magic_enum.hpp>

#include "window.h"
#include "../context.h"

namespace gfx::io {
	void Input::State::setup()
	{
		for (const auto key : magic_enum::enum_values<Key>()) {
			keyboardKeyStates[key] = KeyState::eNotPressed;
		}

		for (const auto button : magic_enum::enum_values<MouseButton>()) {
			mouseButtonStates[button] = KeyState::eNotPressed;
		}
	}

	void Input::State::update()
	{
		for (const auto key : magic_enum::enum_values<Key>()) {
			if (keyboardKeyStates[key] == KeyState::ePressed) {
				keyboardKeyStates[key] = KeyState::eHeld;
			} else if (keyboardKeyStates[key] == KeyState::eReleased) {
				keyboardKeyStates[key] = KeyState::eNotPressed;
			}
		}

		for (const auto button : magic_enum::enum_values<MouseButton>()) {
			if (mouseButtonStates[button] == KeyState::ePressed) {
				mouseButtonStates[button] = KeyState::eHeld;
			} else if (mouseButtonStates[button] == KeyState::eReleased) {
				mouseButtonStates[button] = KeyState::eNotPressed;
			}
		}

		mouseDelta = mousePosition - lastMousePosition;
		lastMousePosition = mousePosition;
		scrollDelta = { 0.0f, 0.0f };
	}

    KeyState Input::getKeyState(const Key key) {
        return Context::Window()._inputState.keyboardKeyStates[key];
    }

    KeyState Input::getMouseButtonState(const MouseButton button) {
        return Context::Window()._inputState.mouseButtonStates[button];
    }

    bool Input::isKeyPressed(const Key key) {
        return Context::Window()._inputState.keyboardKeyStates[key] == KeyState::ePressed;
    }

    bool Input::isKeyHeld(const Key key) {
        return Context::Window()._inputState.keyboardKeyStates[key] == KeyState::eHeld;
    }

    bool Input::isKeyReleased(const Key key) {
        return Context::Window()._inputState.keyboardKeyStates[key] == KeyState::eReleased;
    }

    bool Input::isMouseButtonPressed(const MouseButton button) {
        return Context::Window()._inputState.mouseButtonStates[button] == KeyState::ePressed;
    }

    bool Input::isMouseButtonHeld(const MouseButton button) {
        return Context::Window()._inputState.mouseButtonStates[button] == KeyState::eHeld;
    }

    bool Input::isMouseButtonReleased(const MouseButton button) {
        return Context::Window()._inputState.mouseButtonStates[button] == KeyState::eReleased;
    }

	const glm::vec2& Input::getMousePosition() {
        return Context::Window()._inputState.mousePosition;
    }

    const glm::vec2& Input::getMousePositionDelta() {
        return Context::Window()._inputState.mouseDelta;
    }

    const glm::vec2& Input::getMouseScrollDelta() {
        return Context::Window()._inputState.scrollDelta;
    }

    const glm::vec2& Input::getLastMousePosition()
    {
		return Context::Window()._inputState.lastMousePosition;
    }

    void Input::Callbacks::keyCallback(GLFWwindow * handle, int key, int, const int action, const int mods) {
		auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    	const auto k = static_cast<Key>(key);

    	switch (action) {
    	case GLFW_PRESS:
    		window->_inputState.keyboardKeyStates[k] = KeyState::ePressed;
    		break;
    	case GLFW_RELEASE:
    		window->_inputState.keyboardKeyStates[k] = KeyState::eReleased;
    		break;
    	default:
    		break;
    	}

    	if (mods & GLFW_MOD_SHIFT) {
			window->_inputState.keyboardKeyStates[Key::eLeftShift] = KeyState::eHeld;
		} else {
			if (window->_inputState.keyboardKeyStates[Key::eLeftShift] == KeyState::eHeld) {
				window->_inputState.keyboardKeyStates[Key::eLeftShift] = KeyState::eNotPressed;
			}
		}
    	if (mods & GLFW_MOD_CONTROL) {
			window->_inputState.keyboardKeyStates[Key::eLeftControl] = KeyState::eHeld;
		} else {
			if (window->_inputState.keyboardKeyStates[Key::eLeftControl] == KeyState::eHeld) {
				window->_inputState.keyboardKeyStates[Key::eLeftControl] = KeyState::eNotPressed;
			}
		}
    	if (mods & GLFW_MOD_ALT) {
			window->_inputState.keyboardKeyStates[Key::eLeftAlt] = KeyState::eHeld;
		} else {
			if (window->_inputState.keyboardKeyStates[Key::eLeftAlt] == KeyState::eHeld) {
				window->_inputState.keyboardKeyStates[Key::eLeftAlt] = KeyState::eNotPressed;
			}
		}
    }

	void Input::Callbacks::mouseMoveCallback(GLFWwindow *handle, const double x, const double y) {
		auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    	window->_inputState.mousePosition = glm::vec2 { static_cast<float>(x), static_cast<float>(y) };
    }

	void Input::Callbacks::mouseButtonCallback(GLFWwindow *handle, int button, const int action, int) {
		auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    	const auto b = static_cast<MouseButton>(button);
    	switch (action) {
    	case GLFW_PRESS:
    		window->_inputState.mouseButtonStates[b] = KeyState::ePressed;
    		break;
    	case GLFW_RELEASE:
    		window->_inputState.mouseButtonStates[b] = KeyState::eReleased;
    		break;
    	default:
    		break;
    	}
    }

	void Input::Callbacks::scrollCallback(GLFWwindow *handle, const double x, const double y) {
		auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    	window->_inputState.scrollDelta += glm::vec2 { x, y };
    }

	void Input::Callbacks::focusCallback(GLFWwindow* handle, int focus)
	{
    	auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    	if (focus == GLFW_TRUE) {
			window->focus();
		}
		window->_focused = focus;
	}

	void Input::Callbacks::closeCallback(GLFWwindow* handle)
	{
		glfwSetWindowShouldClose(handle, GLFW_TRUE);
	}
}
