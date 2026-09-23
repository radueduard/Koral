//
// Created by radue on 10/22/2024.
//

#include "input.h"

#include <cctype>
#include <ranges>
#include <unordered_map>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <magic_enum/magic_enum.hpp>

#include "window.h"

// kor::Key's values are GLFW's key codes, which run to 348 — well past magic_enum's default
// [-128, 128] window, outside which an enumerator reflects as an empty name and is missing from
// enum_values() *silently*. describe() below would name every modifier "?" without this. Note that
// the state machine deliberately does not reflect at all; see InputState::update.
template<>
struct magic_enum::customize::enum_range<kor::Key>
{
    static constexpr int min = 0;
    static constexpr int max = 349;
};


namespace kor {
	namespace {
		// Process-wide input state. There is one *main* window, but not one window: an undocked
		// interface panel is an OS window of its own and its events have to reach the same state, or
		// the pointer stops existing the moment it leaves the main one. Encapsulated in this TU;
		// reached through Input's methods.
		struct InputState {
			std::unordered_map<Key, KeyState> keyboardKeyStates;
			std::unordered_map<MouseButton, KeyState> mouseButtonStates;

			glm::vec2 lastMousePosition;
			glm::vec2 mousePosition;        ///< In the main window's client space, as it always was.
			glm::vec2 mouseDelta;
			glm::vec2 scrollDelta;

			/// The cursor in virtual-desktop coordinates, which is the only space every window shares
			/// and therefore the only one a delta can be taken in. @see Callbacks::mouseMoveCallback
			glm::vec2 globalMousePosition {};
			bool hasMousePosition = false;

			GLFWwindow* mainWindow = nullptr;
			std::vector<GLFWwindow*> attachedWindows;
			Input::CursorMode cursorMode = Input::CursorMode::eNormal;

			void setup(GLFWwindow* window)
			{
				// Remembered so the callbacks can tell the engine's own window from the others they
				// are now installed on. @see Input::attachTo
				mainWindow = window;

				// Cleared rather than seeded key by key. A key's state is created by the callback
				// that first reports it, so there is nothing to pre-fill — and seeding by walking
				// the enumeration was worse than useless: see update().
				keyboardKeyStates.clear();
				mouseButtonStates.clear();

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

				// The same position in the space deltas are taken in, so the first movement after
				// startup is a movement rather than a jump from the origin.
				int windowX = 0, windowY = 0;
				if (window) glfwGetWindowPos(window, &windowX, &windowY);
				globalMousePosition = { static_cast<float>(windowX) + static_cast<float>(cx),
				                        static_cast<float>(windowY) + static_cast<float>(cy) };
				hasMousePosition = window != nullptr;
			}

			void update()
			{
				// Over what is *in* the maps, not over what magic_enum can see of the enumerations.
				// magic_enum only reflects values within [-128, 128] unless told otherwise, and
				// kor::Key runs to 348 — so Escape (256), the modifiers (340-347) and the whole
				// keypad were never advanced from ePressed to eHeld and never cleared from
				// eReleased. isKeyHeld() answered false for every one of them, for ever: Shift-to-
				// boost and any binding on Escape simply did nothing. @see reference_magic_enum_range
				const auto advance = [](auto& states) {
					for (auto& state : states | std::views::values) {
						if (state == KeyState::ePressed)       state = KeyState::eHeld;
						else if (state == KeyState::eReleased) state = KeyState::eNotPressed;
					}
				};
				advance(keyboardKeyStates);
				advance(mouseButtonStates);

				// mouseDelta is accumulated by mouseMoveCallback during the frame;
				// reset it here so the next frame starts from zero.
				lastMousePosition = mousePosition;
				mouseDelta  = { 0.0f, 0.0f };
				scrollDelta = { 0.0f, 0.0f };
			}
		};

		InputState g_input;
	}

	void Input::setup(GLFWwindow* window) { g_input.setup(window); }

	// Installed on every window input is read from. The engine's callbacks forward each event to ImGui
	// themselves (see below), which is why they *replace* rather than chain: ImGui installs equivalents
	// on the windows it creates, and running both would deliver every event twice.
	void Input::installCallbacks(GLFWwindow* window)
	{
		glfwSetKeyCallback(window, Input::Callbacks::keyCallback);
		glfwSetCursorPosCallback(window, Input::Callbacks::mouseMoveCallback);
		glfwSetMouseButtonCallback(window, Input::Callbacks::mouseButtonCallback);
		glfwSetScrollCallback(window, Input::Callbacks::scrollCallback);
		glfwSetWindowFocusCallback(window, Input::Callbacks::focusCallback);
		glfwSetCharCallback(window, Input::Callbacks::charCallback);
		glfwSetCursorEnterCallback(window, Input::Callbacks::cursorEnterCallback);
	}

	namespace
	{
		// The cursor is a per-window setting in GLFW, so a mode is applied to each window input is read
		// from — otherwise it would come back the moment the pointer crossed into an undocked panel.
		void applyCursorMode(GLFWwindow* window, const Input::CursorMode mode)
		{
			switch (mode) {
			case Input::CursorMode::eHidden:
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
				break;
			case Input::CursorMode::eCaptured:
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
				// Unaccelerated movement, where the platform has it: pointer acceleration is tuned for
				// reaching a menu quickly, which is the opposite of what aiming wants.
				if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
				break;
			case Input::CursorMode::eNormal:
			default:
				if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
				break;
			}
		}
	}

	void Input::setCursorMode(const CursorMode mode)
	{
		auto& state = g_input;
		if (state.cursorMode == mode) return;
		state.cursorMode = mode;

		for (auto* window : state.attachedWindows) applyCursorMode(window, mode);

		// Capturing warps the cursor and releasing it puts it back, and either would arrive as an
		// enormous delta on the next report. Dropping the baseline makes the next one re-establish it
		// instead, so the frame the mode changes reports no movement rather than a fling.
		state.hasMousePosition = false;
		state.mouseDelta = { 0.f, 0.f };
	}

	Input::CursorMode Input::cursorMode() { return g_input.cursorMode; }

	void Input::attachTo(GLFWwindow* window)
	{
		if (window == nullptr) return;
		auto& attached = g_input.attachedWindows;
		if (std::ranges::find(attached, window) != attached.end()) return;

		attached.push_back(window);
		installCallbacks(window);
		// A window that appears mid-capture — an undocked panel — has to arrive in the same mode as
		// the rest, or the cursor would reappear as soon as the pointer entered it.
		applyCursorMode(window, g_input.cursorMode);
	}

	void Input::detachFrom(GLFWwindow* window)
	{
		auto& attached = g_input.attachedWindows;
		std::erase(attached, window);
		// The callbacks are not cleared: this is called for a window ImGui is about to destroy, and
		// touching a window mid-destruction is worse than leaving pointers on something about to go.
	}

	std::vector<GLFWwindow*> Input::attachedWindows() { return g_input.attachedWindows; }
	void Input::update() { g_input.update(); }

    KeyState Input::keyState(const Key key) {
        return g_input.keyboardKeyStates[key];
    }

    KeyState Input::mouseButtonState(const MouseButton button) {
        return g_input.mouseButtonStates[button];
    }

    bool Input::isKeyPressed(const Key key) {
        return g_input.keyboardKeyStates[key] == KeyState::ePressed;
    }

    bool Input::isKeyHeld(const Key key) {
        return g_input.keyboardKeyStates[key] == KeyState::eHeld;
    }

    bool Input::isKeyReleased(const Key key) {
        return g_input.keyboardKeyStates[key] == KeyState::eReleased;
    }

    bool Input::isMouseButtonPressed(const MouseButton button) {
        return g_input.mouseButtonStates[button] == KeyState::ePressed;
    }

    bool Input::isMouseButtonHeld(const MouseButton button) {
        return g_input.mouseButtonStates[button] == KeyState::eHeld;
    }

    bool Input::isMouseButtonReleased(const MouseButton button) {
        return g_input.mouseButtonStates[button] == KeyState::eReleased;
    }

    std::string Input::describe(const Key key) {
        // "eLeftShift" -> "Left Shift". The enumerator's spelling is the only name a key has here;
        // splitting it on capitals makes it readable without a table that would fall out of step
        // with kor::Key the moment a key is added.
        const std::string_view enumerator = magic_enum::enum_name(key);
        if (enumerator.empty()) return "?";

        std::string name;
        for (const char c : enumerator.substr(1)) {   // drop the leading 'e'
            if (std::isupper(static_cast<unsigned char>(c)) && !name.empty()) name += ' ';
            name += c;
        }
        return name;
    }

    std::string Input::describe(const MouseButton button) {
        switch (button) {
        case MouseButton::eLeft:   return "Left Mouse";
        case MouseButton::eRight:  return "Right Mouse";
        case MouseButton::eMiddle: return "Middle Mouse";
        default: break;
        }
        // e4 upwards have no names, only positions, so say which one it is.
        return "Mouse " + std::to_string(static_cast<int>(button) + 1);
    }

    std::optional<Key> Input::firstKeyPressed() {
        // Over what has been reported, not over the enumeration: the map holds exactly the keys the
        // callbacks have seen, which is a handful rather than 120. @see InputState::update
        for (const auto& [key, state] : g_input.keyboardKeyStates) {
            if (state == KeyState::ePressed) return key;
        }
        return std::nullopt;
    }

    std::optional<MouseButton> Input::firstMouseButtonPressed() {
        for (const auto& [button, state] : g_input.mouseButtonStates) {
            if (state == KeyState::ePressed) return button;
        }
        return std::nullopt;
    }

    bool Input::interfaceWantsMouse() {
        // GetIO() asserts outright with no context, and "no interface" is a state a job, a test and
        // an application without a GUI are all legitimately in — so it is answered, not crashed on.
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
    }

    bool Input::interfaceWantsKeyboard() {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
    }

	const glm::vec2& Input::mousePosition() {
        return g_input.mousePosition;
    }

    const glm::vec2& Input::mousePositionDelta() {
        return g_input.mouseDelta;
    }

    const glm::vec2& Input::mouseScrollDelta() {
        return g_input.scrollDelta;
    }

    const glm::vec2& Input::lastMousePosition()
    {
		return g_input.lastMousePosition;
    }

    void Input::Callbacks::keyCallback(GLFWwindow * handle, int key, int scancode, const int action, const int mods) {
    	ImGui_ImplGlfw_KeyCallback(handle, key, scancode, action, mods);

    	auto& state = g_input;
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

		auto& state = g_input;

		// The position arrives relative to whichever window the pointer is over, and once an interface
		// panel can be undocked that is not always the main one. Two windows' client coordinates are
		// different spaces, so a delta taken across them would be nonsense — the pointer appears to
		// jump by the distance between the windows. Converted here to virtual-desktop coordinates,
		// which every window shares, so the delta means the same thing whichever one is under it.
		int windowX = 0, windowY = 0;
		glfwGetWindowPos(handle, &windowX, &windowY);
		const glm::vec2 global = { static_cast<float>(windowX) + static_cast<float>(x),
		                           static_cast<float>(windowY) + static_cast<float>(y) };

		// Accumulate delta so scene.Update() sees the full movement for this frame. Skipped for the
		// first report from a newly attached window: there is no previous position in this space to
		// subtract, and taking one would spike the delta by wherever the window happens to be.
		if (state.hasMousePosition) state.mouseDelta += global - state.globalMousePosition;
		state.globalMousePosition = global;
		state.hasMousePosition = true;

		// Kept window-local, and only from the main window, because that is what it has always meant:
		// where the cursor is *in the window you drew*.
		if (handle == state.mainWindow) {
			state.mousePosition = { static_cast<float>(x), static_cast<float>(y) };
		}
    }

	void Input::Callbacks::mouseButtonCallback(GLFWwindow *handle, int button, const int action, int mods) {
		ImGui_ImplGlfw_MouseButtonCallback(handle, button, action, mods);

		auto& state = g_input;
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

		g_input.scrollDelta += glm::vec2 { x, y };
    }

	void Input::Callbacks::focusCallback(GLFWwindow* handle, int focus)
	{
		ImGui_ImplGlfw_WindowFocusCallback(handle, focus);

		// Only the engine's own window carries a kor::Window in its user pointer. These callbacks are
		// installed on other windows too now — an undocked interface panel is one — and ImGui keeps
		// *its* own data there, so reading a kor::Window out of it would be reading whatever ImGui put
		// there and writing through it. That was a straight segfault the first time a panel was
		// undocked. @see Input::attachTo
		if (handle != g_input.mainWindow) return;

		if (auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle))) {
			window->_focused = focus;
		}
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
