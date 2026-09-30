//
// Created by radue on 10/22/2024.
//

#include "input.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <ranges>
#include <unordered_map>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <magic_enum/magic_enum.hpp>

#include <imgui_internal.h>

#include "window.h"

// kor::Key's values are GLFW's key codes, which run to 348 — well past magic_enum's default
// [-128, 128] window, outside which an enumerator reflects as an empty name and is missing from
// enum_values() *silently*. Describe() below would name every modifier "?" without this. Note that
// the state machine deliberately does not reflect at all; see InputState::update.
template<>
struct magic_enum::customize::enum_range<kor::Key>
{
    static constexpr int min = 0;
    static constexpr int max = 349;
};


namespace kor {
	struct Input::State {
		std::unordered_map<Key, KeyState> keys;
		std::unordered_map<MouseButton, KeyState> buttons;

		glm::vec2 lastMousePosition {};
		glm::vec2 mousePosition {};        ///< In the scene's own window's client space.
		glm::vec2 mouseDelta {};
		glm::vec2 scrollDelta {};

		/// The cursor in virtual-desktop coordinates, the only space every window shares and so the
		/// only one a delta can be taken in — the pointer may cross into an undocked panel.
		glm::vec2 globalMousePosition {};
		bool hasMousePosition = false;

		std::vector<GLFWwindow*> windows;   ///< the scene's own first
		CursorMode cursorMode = CursorMode::eNormal;
		ImGuiContext* interface = nullptr;

		/// Fed from elsewhere, applied with the next Update so the scene sees it next frame. @see FeedKey
		std::vector<std::function<void(State&)>> fed;
	};

	namespace {
		// Which scene's input each OS window feeds. Process-wide, because GLFW's callbacks are plain
		// functions and a window is all they are handed.
		std::unordered_map<GLFWwindow*, Input*>& routes() {
			static std::unordered_map<GLFWwindow*, Input*> table;
			return table;
		}

		Input* routeOf(GLFWwindow* handle) {
			const auto it = routes().find(handle);
			return it == routes().end() ? nullptr : it->second;
		}

		// The cursor is a per-window setting in GLFW, so a mode is applied to each window the input
		// reads from — otherwise it would come back the moment the pointer crossed into an undocked panel.
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

		// Events over a scene's windows go to *its* interface. ImGui's GLFW backend keeps its state per
		// context, so the scene's context has to be the current one while the event is forwarded.
		struct InterfaceScope {
			ImGuiContext* previous = nullptr;
			bool active = false;
			explicit InterfaceScope(const Input* input, ImGuiContext* context) {
				if (!input || !context) return;
				previous = ImGui::GetCurrentContext();
				ImGui::SetCurrentContext(context);
				active = true;
			}
			~InterfaceScope() { if (active) ImGui::SetCurrentContext(previous); }
		};
	}

	Input::Input() : _state(new State) {}

	Input::~Input()
	{
		for (auto* window : _state->windows) routes().erase(window);
		delete _state;
	}

	void Input::SetInterfaceContext(ImGuiContext* context) { _state->interface = context; }

	// Installed on every window input is read from. The engine's callbacks forward each event to ImGui
	// themselves (see below), which is why they *replace* rather than chain.
	void Input::InstallCallbacks(GLFWwindow* window)
	{
		glfwSetKeyCallback(window, Callbacks::KeyCallback);
		glfwSetCursorPosCallback(window, Callbacks::MouseMoveCallback);
		glfwSetMouseButtonCallback(window, Callbacks::MouseButtonCallback);
		glfwSetScrollCallback(window, Callbacks::ScrollCallback);
		glfwSetWindowFocusCallback(window, Callbacks::FocusCallback);
		glfwSetCharCallback(window, Callbacks::CharCallback);
		glfwSetCursorEnterCallback(window, Callbacks::CursorEnterCallback);
	}

	void Input::AttachTo(GLFWwindow* window)
	{
		if (window == nullptr) return;
		auto& windows = _state->windows;
		if (std::ranges::find(windows, window) == windows.end()) windows.push_back(window);
		// The newest attachment wins: a window handed from one scene to the next (a scene replaced in
		// it) is simply attached to the new scene's input.
		routes()[window] = this;
		InstallCallbacks(window);
		// A window that appears mid-capture — an undocked panel — has to arrive in the same mode as
		// the rest, or the cursor would reappear as soon as the pointer entered it.
		applyCursorMode(window, _state->cursorMode);

		if (windows.size() == 1) {
			// The scene's own window: the starting position, so the first frame reports no movement.
			double cx = 0.0, cy = 0.0;
			glfwGetCursorPos(window, &cx, &cy);
			int windowX = 0, windowY = 0;
			glfwGetWindowPos(window, &windowX, &windowY);
			_state->mousePosition = { static_cast<float>(cx), static_cast<float>(cy) };
			_state->lastMousePosition = _state->mousePosition;
			_state->globalMousePosition = { static_cast<float>(windowX + cx), static_cast<float>(windowY + cy) };
			_state->hasMousePosition = true;
		}
	}

	void Input::DetachFrom(GLFWwindow* window)
	{
		std::erase(_state->windows, window);
		if (routeOf(window) == this) routes().erase(window);
		// The callbacks are not cleared: this is called for a window ImGui is about to destroy, and
		// touching a window mid-destruction is worse than leaving pointers on something about to go.
	}

	const std::vector<GLFWwindow*>& Input::AttachedWindows() const { return _state->windows; }

	void Input::SetCursorMode(const CursorMode mode)
	{
		if (_state->cursorMode == mode) return;
		_state->cursorMode = mode;
		for (auto* window : _state->windows) applyCursorMode(window, mode);
		// Capturing warps the cursor and releasing it puts it back, and either would arrive as an
		// enormous delta on the next report. Dropping the baseline makes the next one re-establish it.
		_state->hasMousePosition = false;
		_state->mouseDelta = { 0.f, 0.f };
	}

	Input::CursorMode Input::CurrentCursorMode() const { return _state->cursorMode; }

	void Input::Update()
	{
		// Over what is *in* the maps, not over what magic_enum can see of the enumerations: it only
		// reflects [-128, 128] unless told otherwise, and kor::Key runs to 348. @see reference_magic_enum_range
		const auto advance = [](auto& states) {
			for (auto& state : states | std::views::values) {
				if (state == KeyState::ePressed)       state = KeyState::eHeld;
				else if (state == KeyState::eReleased) state = KeyState::eNotPressed;
			}
		};
		advance(_state->keys);
		advance(_state->buttons);
		_state->lastMousePosition = _state->mousePosition;
		_state->mouseDelta  = { 0.0f, 0.0f };
		_state->scrollDelta = { 0.0f, 0.0f };
	}

	void Input::ApplyFed()
	{
		auto fed = std::move(_state->fed);
		_state->fed.clear();
		for (const auto& event : fed) event(*_state);
	}

	namespace {
		void press(KeyState& state, const bool down) {
			if (down) { if (state != KeyState::eHeld) state = KeyState::ePressed; }
			else if (state == KeyState::ePressed || state == KeyState::eHeld) state = KeyState::eReleased;
		}
	}

	void Input::FeedKey(const Key key, const bool down) {
		_state->fed.emplace_back([=](State& s) { press(s.keys[key], down); });
	}

	void Input::FeedMouseButton(const MouseButton button, const bool down) {
		_state->fed.emplace_back([=](State& s) { press(s.buttons[button], down); });
	}

	void Input::FeedMousePosition(const glm::vec2 position) {
		_state->fed.emplace_back([=](State& s) { s.mousePosition = position; });
	}

	void Input::FeedMouseDelta(const glm::vec2 delta) {
		_state->fed.emplace_back([=](State& s) { s.mouseDelta += delta; });
	}

	void Input::FeedScroll(const glm::vec2 delta) {
		_state->fed.emplace_back([=](State& s) { s.scrollDelta += delta; });
	}

	void Input::ReleaseAll() {
		_state->fed.emplace_back([](State& s) {
			for (auto& state : s.keys | std::views::values) press(state, false);
			for (auto& state : s.buttons | std::views::values) press(state, false);
		});
	}

	void Input::FeedFrom(const Input& source, const bool keyboard, const bool mouse) {
		if (&source == this) return;
		const auto& from = *source._state;
		if (keyboard) {
			for (const auto& [key, state] : from.keys) {
				if (state == KeyState::ePressed) FeedKey(key, true);
				else if (state == KeyState::eReleased) FeedKey(key, false);
			}
		}
		if (mouse) {
			for (const auto& [button, state] : from.buttons) {
				if (state == KeyState::ePressed) FeedMouseButton(button, true);
				else if (state == KeyState::eReleased) FeedMouseButton(button, false);
			}
			if (from.mouseDelta != glm::vec2(0.f)) FeedMouseDelta(from.mouseDelta);
			if (from.scrollDelta != glm::vec2(0.f)) FeedScroll(from.scrollDelta);
		}
	}

	namespace {
		KeyState stateIn(const auto& states, const auto key) {
			const auto it = states.find(key);
			return it == states.end() ? KeyState::eNotPressed : it->second;
		}
	}

    KeyState Input::StateOf(const Key key) const { return stateIn(_state->keys, key); }
    KeyState Input::MouseButtonState(const MouseButton button) const { return stateIn(_state->buttons, button); }
    bool Input::IsKeyPressed(const Key key) const { return StateOf(key) == KeyState::ePressed; }
    bool Input::IsKeyHeld(const Key key) const { return StateOf(key) == KeyState::eHeld; }
    bool Input::IsKeyReleased(const Key key) const { return StateOf(key) == KeyState::eReleased; }
    bool Input::IsMouseButtonPressed(const MouseButton button) const { return MouseButtonState(button) == KeyState::ePressed; }
    bool Input::IsMouseButtonHeld(const MouseButton button) const { return MouseButtonState(button) == KeyState::eHeld; }
    bool Input::IsMouseButtonReleased(const MouseButton button) const { return MouseButtonState(button) == KeyState::eReleased; }

    std::string Input::Describe(const Key key) {
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

    std::string Input::Describe(const MouseButton button) {
        switch (button) {
        case MouseButton::eLeft:   return "Left Mouse";
        case MouseButton::eRight:  return "Right Mouse";
        case MouseButton::eMiddle: return "Middle Mouse";
        default: break;
        }
        // e4 upwards have no names, only positions, so say which one it is.
        return "Mouse " + std::to_string(static_cast<int>(button) + 1);
    }

    std::optional<Key> Input::FirstKeyPressed() const {
        for (const auto& [key, state] : _state->keys)
            if (state == KeyState::ePressed) return key;
        return std::nullopt;
    }

    std::optional<MouseButton> Input::FirstMouseButtonPressed() const {
        for (const auto& [button, state] : _state->buttons)
            if (state == KeyState::ePressed) return button;
        return std::nullopt;
    }

    bool Input::InterfaceWantsMouse() const {
        // Read straight off the scene's own context: another scene's may be the current one.
        return _state->interface != nullptr && _state->interface->IO.WantCaptureMouse;
    }

    bool Input::InterfaceWantsKeyboard() const {
        return _state->interface != nullptr && _state->interface->IO.WantCaptureKeyboard;
    }

    const glm::vec2& Input::MousePosition() const { return _state->mousePosition; }
    const glm::vec2& Input::MousePositionDelta() const { return _state->mouseDelta; }
    const glm::vec2& Input::MouseScrollDelta() const { return _state->scrollDelta; }
    const glm::vec2& Input::LastMousePosition() const { return _state->lastMousePosition; }

    // ---- callbacks ----------------------------------------------------------------------------------
    // Each finds the scene the window feeds, forwards the event to that scene's interface (if it has
    // one), and records it in that scene's input.

    void Input::Callbacks::KeyCallback(GLFWwindow* handle, const int key, const int scancode, const int action, const int mods) {
        Input* input = routeOf(handle);
        if (!input) return;
        {
            const InterfaceScope scope(input, input->_state->interface);
            if (scope.active) ImGui_ImplGlfw_KeyCallback(handle, key, scancode, action, mods);
        }
        auto& state = input->_state->keys[static_cast<Key>(key)];
        switch (action) {
        case GLFW_PRESS:   state = KeyState::ePressed; break;
        case GLFW_RELEASE: state = KeyState::eReleased; break;
        default: break;   // GLFW_REPEAT: still held
        }
    }

    void Input::Callbacks::MouseMoveCallback(GLFWwindow* handle, const double x, const double y) {
        Input* input = routeOf(handle);
        if (!input) return;
        {
            const InterfaceScope scope(input, input->_state->interface);
            if (scope.active) ImGui_ImplGlfw_CursorPosCallback(handle, x, y);
        }
        auto& state = *input->_state;
        // Converted to virtual-desktop coordinates, which every window shares, so a delta taken as the
        // pointer crosses from the scene's window into one of its undocked panels means the same thing.
        int windowX = 0, windowY = 0;
        glfwGetWindowPos(handle, &windowX, &windowY);
        const glm::vec2 global = { static_cast<float>(windowX) + static_cast<float>(x),
                                   static_cast<float>(windowY) + static_cast<float>(y) };
        if (state.hasMousePosition) state.mouseDelta += global - state.globalMousePosition;
        state.globalMousePosition = global;
        state.hasMousePosition = true;
        // Window-local, and only from the scene's own window: where the cursor is in the picture it drew.
        if (!state.windows.empty() && handle == state.windows.front())
            state.mousePosition = { static_cast<float>(x), static_cast<float>(y) };
    }

    void Input::Callbacks::MouseButtonCallback(GLFWwindow* handle, const int button, const int action, const int mods) {
        Input* input = routeOf(handle);
        if (!input) return;
        {
            const InterfaceScope scope(input, input->_state->interface);
            if (scope.active) ImGui_ImplGlfw_MouseButtonCallback(handle, button, action, mods);
        }
        auto& state = input->_state->buttons[static_cast<MouseButton>(button)];
        if (action == GLFW_PRESS) state = KeyState::ePressed;
        else if (action == GLFW_RELEASE) state = KeyState::eReleased;
    }

    void Input::Callbacks::ScrollCallback(GLFWwindow* handle, const double x, const double y) {
        Input* input = routeOf(handle);
        if (!input) return;
        {
            const InterfaceScope scope(input, input->_state->interface);
            if (scope.active) ImGui_ImplGlfw_ScrollCallback(handle, x, y);
        }
        input->_state->scrollDelta += glm::vec2 { x, y };
    }

    void Input::Callbacks::FocusCallback(GLFWwindow* handle, const int focus) {
        Input* input = routeOf(handle);
        if (!input) return;
        {
            const InterfaceScope scope(input, input->_state->interface);
            if (scope.active) ImGui_ImplGlfw_WindowFocusCallback(handle, focus);
        }
        // Only a scene's own window carries a kor::Window in its user pointer; an undocked panel's
        // holds ImGui's data, and reading a kor::Window out of that was a segfault once.
        if (!input->_state->windows.empty() && handle == input->_state->windows.front()) {
            if (auto* window = static_cast<Window*>(glfwGetWindowUserPointer(handle))) window->_focused = focus;
        }
    }

    void Input::Callbacks::CharCallback(GLFWwindow* handle, const unsigned int codepoint) {
        Input* input = routeOf(handle);
        const InterfaceScope scope(input, input ? input->_state->interface : nullptr);
        if (scope.active) ImGui_ImplGlfw_CharCallback(handle, codepoint);
    }

    void Input::Callbacks::CursorEnterCallback(GLFWwindow* handle, const int entered) {
        Input* input = routeOf(handle);
        const InterfaceScope scope(input, input ? input->_state->interface : nullptr);
        if (scope.active) ImGui_ImplGlfw_CursorEnterCallback(handle, entered);
    }
}
