//
// Created by radue on 10/22/2024.
//

#include "input.h"

#include <cstdio>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/fwd.hpp>
#include <glm/vec2.hpp>
#include <magic_enum/magic_enum.hpp>
#include <imgui_impl_glfw.h>

#include "context.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"


namespace gfx::io {
	void Input::State::setup(GLFWwindow* window)
	{
		for (const auto key : magic_enum::enum_values<Key>()) {
			keyboardKeyStates[key] = KeyState::eNotPressed;
		}

		for (const auto button : magic_enum::enum_values<MouseButton>()) {
			mouseButtonStates[button] = KeyState::eNotPressed;
		}

		// Initialize mouse position to the actual cursor position to avoid a
		// large spurious delta on the very first frame.
		double cx = 0.0, cy = 0.0;
		if (window) {
			glfwGetCursorPos(window, &cx, &cy);
		}
		mousePosition     = { static_cast<float>(cx), static_cast<float>(cy) };
		lastMousePosition = mousePosition;
		mouseDelta        = { 0.0f, 0.0f };
		scrollDelta       = { 0.0f, 0.0f };
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

		// mouseDelta is accumulated by mouseMoveCallback during the frame;
		// reset it here so the next frame starts from zero.
		lastMousePosition = mousePosition;
		mouseDelta  = { 0.0f, 0.0f };
		scrollDelta = { 0.0f, 0.0f };
	}

    KeyState Input::getKeyState(const Key key) {
        auto& state = Context::Window()._inputState;
        return state.keyboardKeyStates[key];
    }

    KeyState Input::getMouseButtonState(const MouseButton button) {
        return Context::Window()._inputState.mouseButtonStates[button];
    }

    bool Input::isKeyPressed(const Key key) {
        auto& state = Context::Window()._inputState;
        return state.keyboardKeyStates[key] == KeyState::ePressed;
    }

    bool Input::isKeyHeld(const Key key) {
        auto& state = Context::Window()._inputState;
        return state.keyboardKeyStates[key] == KeyState::eHeld;
    }

    bool Input::isKeyReleased(const Key key) {
        return Context::Window()._inputState.keyboardKeyStates[key] == KeyState::eReleased;
    }

    bool Input::isMouseButtonPressed(const MouseButton button) {
        const auto s = Context::Window()._inputState.mouseButtonStates[button];
        return s == KeyState::ePressed;
    }

    bool Input::isMouseButtonHeld(const MouseButton button) {
        const auto s = Context::Window()._inputState.mouseButtonStates[button];
        return s == KeyState::eHeld;
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

    void Input::Callbacks::keyCallback(GLFWwindow * handle, int key, int scancode, const int action, const int mods) {
    	ImGui_ImplGlfw_KeyCallback(handle, key, scancode, action, mods);

    	auto& state = Context::Window()._inputState;
    	const auto k = static_cast<Key>(key);

    	switch (action) {
    	case GLFW_PRESS:
    		state.keyboardKeyStates[k] = KeyState::ePressed;
    		break;
    	case GLFW_RELEASE:
    		state.keyboardKeyStates[k] = KeyState::eReleased;
    		break;
    	default:
    		break;
    	}

    	if (mods & GLFW_MOD_SHIFT) {
    		state.keyboardKeyStates[Key::eLeftShift] = KeyState::eHeld;
		} else {
			if (state.keyboardKeyStates[Key::eLeftShift] == KeyState::eHeld) {
				state.keyboardKeyStates[Key::eLeftShift] = KeyState::eNotPressed;
			}
		}
    	if (mods & GLFW_MOD_CONTROL) {
    		state.keyboardKeyStates[Key::eLeftControl] = KeyState::eHeld;
		} else {
			if (state.keyboardKeyStates[Key::eLeftControl] == KeyState::eHeld) {
				state.keyboardKeyStates[Key::eLeftControl] = KeyState::eNotPressed;
			}
		}
    	if (mods & GLFW_MOD_ALT) {
    		state.keyboardKeyStates[Key::eLeftAlt] = KeyState::eHeld;
		} else {
			if (state.keyboardKeyStates[Key::eLeftAlt] == KeyState::eHeld) {
				state.keyboardKeyStates[Key::eLeftAlt] = KeyState::eNotPressed;
			}
		}
    }

	void Input::Callbacks::mouseMoveCallback(GLFWwindow *handle, const double x, const double y) {
		ImGui_ImplGlfw_CursorPosCallback(handle, x, y);

		auto& state = Context::Window()._inputState;
		const glm::vec2 newPos = { static_cast<float>(x), static_cast<float>(y) };
		// Accumulate delta so scene.Update() sees the full movement for this frame.
		state.mouseDelta    += newPos - state.mousePosition;
    	state.mousePosition  = newPos;
    }

	void Input::Callbacks::mouseButtonCallback(GLFWwindow *handle, int button, const int action, int mods) {
		ImGui_ImplGlfw_MouseButtonCallback(handle, button, action, mods);

		auto& state = Context::Window()._inputState;
    	const auto b = static_cast<MouseButton>(button);
    	switch (action) {
    	case GLFW_PRESS:
    		state.mouseButtonStates[b] = KeyState::ePressed;
    		break;
    	case GLFW_RELEASE:
    		state.mouseButtonStates[b] = KeyState::eReleased;
    		break;
    	default:
    		break;
    	}
    }

	void Input::Callbacks::scrollCallback(GLFWwindow *handle, const double x, const double y) {
		ImGui_ImplGlfw_ScrollCallback(handle, x, y);

		Context::Window()._inputState.scrollDelta += glm::vec2 { x, y };
    }

	void Input::Callbacks::focusCallback(GLFWwindow* handle, int focus)
	{
		ImGui_ImplGlfw_WindowFocusCallback(handle, focus);

    	auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    	if (focus == GLFW_TRUE) {
			window->focus();
		}
		window->_focused = focus;
	}

	void Input::Callbacks::charCallback(GLFWwindow* handle, unsigned int codepoint)
	{
		ImGui_ImplGlfw_CharCallback(handle, codepoint);
	}

	void Input::Callbacks::cursorEnterCallback(GLFWwindow* handle, int entered)
	{
		ImGui_ImplGlfw_CursorEnterCallback(handle, entered);
	}

	void Input::Callbacks::closeCallback(GLFWwindow* handle)
	{
		glfwSetWindowShouldClose(handle, GLFW_TRUE);
	}
}
