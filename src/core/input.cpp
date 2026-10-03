//
// Created by radue on 10/22/2024.
//

#include "input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <ranges>
#include <unordered_map>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <magic_enum/magic_enum.hpp>

#include <imgui_internal.h>

#include "window.h"
#include "log.h"
#include "parseNumber.h"

#include <format>

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
		std::u32string typed;                ///< This frame's text. @see TypedText
		std::vector<Key> repeated;           ///< Keys that repeated this frame.
		/// Interfaces other than ImGui that say they are using the pointer or the keyboard.
		std::unordered_map<const void*, std::pair<bool, bool>> claims;

		/// The cursor in virtual-desktop coordinates, the only space every window shares and so the
		/// only one a delta can be taken in — the pointer may cross into an undocked panel.
		glm::vec2 globalMousePosition {};
		bool hasMousePosition = false;

		std::vector<GLFWwindow*> windows;   ///< the scene's own first
		CursorMode cursorMode = CursorMode::eNormal;
		ImGuiContext* interface = nullptr;

		/// Fed from elsewhere, applied with the next Update so the scene sees it next frame. @see FeedKey
		std::vector<std::function<void(State&)>> fed;

		struct Pad {
			bool connected = false;
			std::string name;
			std::array<KeyState, 15> buttons {};
			std::array<float, 6> axes {};   ///< Sticks -1..1, triggers 0..1; raw, before the dead zone
		};
		std::array<Pad, MaxGamepads> pads {};
		float deadZone = 0.15f;

		struct Action {
			std::vector<InputSource> sources;
			KeyState state = KeyState::eNotPressed;
		};
		std::map<std::string, Action, std::less<>> actions;
		std::map<std::string, std::vector<InputSource>, std::less<>> axes;
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
		for (auto& pad : _state->pads) {
			for (auto& state : pad.buttons) {
				if (state == KeyState::ePressed)       state = KeyState::eHeld;
				else if (state == KeyState::eReleased) state = KeyState::eNotPressed;
			}
		}
		for (auto& action : _state->actions | std::views::values) {
			if (action.state == KeyState::ePressed)       action.state = KeyState::eHeld;
			else if (action.state == KeyState::eReleased) action.state = KeyState::eNotPressed;
		}
		_state->lastMousePosition = _state->mousePosition;
		_state->mouseDelta  = { 0.0f, 0.0f };
		_state->scrollDelta = { 0.0f, 0.0f };
		_state->typed.clear();
		_state->repeated.clear();
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

	void Input::FeedText(const std::u32string_view text) {
		_state->fed.emplace_back([text = std::u32string(text)](State& s) { s.typed += text; });
	}

	void Input::FeedKeyRepeat(const Key key) {
		_state->fed.emplace_back([=](State& s) { s.repeated.push_back(key); });
	}

	void Input::ReleaseAll() {
		_state->fed.emplace_back([](State& s) {
			for (auto& state : s.keys | std::views::values) press(state, false);
			for (auto& state : s.buttons | std::views::values) press(state, false);
			for (auto& pad : s.pads) {
				for (auto& state : pad.buttons) press(state, false);
				pad.axes = {};
			}
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
			if (!from.typed.empty()) FeedText(from.typed);
			for (const Key key : from.repeated) FeedKeyRepeat(key);
			// The gamepads go where the keys do: to whatever has focus.
			for (int pad = 0; pad < MaxGamepads; ++pad) {
				const auto& p = from.pads[pad];
				if (!p.connected) continue;
				for (std::size_t b = 0; b < p.buttons.size(); ++b) {
					if (p.buttons[b] == KeyState::ePressed) FeedGamepadButton(static_cast<GamepadButton>(b), true, pad);
					else if (p.buttons[b] == KeyState::eReleased) FeedGamepadButton(static_cast<GamepadButton>(b), false, pad);
				}
				for (std::size_t a = 0; a < p.axes.size(); ++a) FeedGamepadAxis(static_cast<GamepadAxis>(a), p.axes[a], pad);
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
        for (const auto& [mouse, keyboard] : _state->claims | std::views::values)
            if (mouse) return true;
        // Read straight off the scene's own context: another scene's may be the current one.
        return _state->interface != nullptr && _state->interface->IO.WantCaptureMouse;
    }

    bool Input::InterfaceWantsKeyboard() const {
        for (const auto& [mouse, keyboard] : _state->claims | std::views::values)
            if (keyboard) return true;
        return _state->interface != nullptr && _state->interface->IO.WantCaptureKeyboard;
    }

    void Input::ClaimInterface(const void* claimer, const bool mouse, const bool keyboard) {
        if (!mouse && !keyboard) _state->claims.erase(claimer);
        else _state->claims[claimer] = {mouse, keyboard};
    }

    std::u32string_view Input::TypedText() const { return _state->typed; }

    bool Input::IsKeyRepeated(const Key key) const {
        return std::ranges::find(_state->repeated, key) != _state->repeated.end();
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
        case GLFW_REPEAT:  input->_state->repeated.push_back(static_cast<Key>(key)); break;   // still held
        default: break;
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
        {
            const InterfaceScope scope(input, input ? input->_state->interface : nullptr);
            if (scope.active) ImGui_ImplGlfw_CharCallback(handle, codepoint);
        }
        if (input) input->_state->typed.push_back(static_cast<char32_t>(codepoint));
    }

    void Input::Callbacks::CursorEnterCallback(GLFWwindow* handle, const int entered) {
        Input* input = routeOf(handle);
        const InterfaceScope scope(input, input ? input->_state->interface : nullptr);
        if (scope.active) ImGui_ImplGlfw_CursorEnterCallback(handle, entered);
    }

    // ---- gamepads -----------------------------------------------------------------------------------

    namespace {
        // Every gamepad, read once a frame for every scene: what GLFW says, numbered in the order found.
        struct Polled {
            bool connected = false;
            std::string name;
            std::array<bool, 15> buttons {};
            std::array<float, 6> axes {};
        };
        std::array<Polled, Input::MaxGamepads>& polled() {
            static std::array<Polled, Input::MaxGamepads> pads {};
            return pads;
        }

        bool isDown(const KeyState state) { return state == KeyState::ePressed || state == KeyState::eHeld; }
    }

    void Input::PollGamepads()
    {
        auto& pads = polled();
        pads = {};
        int next = 0;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST && next < MaxGamepads; ++jid) {
            if (!glfwJoystickIsGamepad(jid)) continue;
            GLFWgamepadstate state {};
            if (!glfwGetGamepadState(jid, &state)) continue;
            auto& pad = pads[next++];
            pad.connected = true;
            if (const char* name = glfwGetGamepadName(jid)) pad.name = name;
            for (std::size_t b = 0; b < pad.buttons.size(); ++b) pad.buttons[b] = state.buttons[b] == GLFW_PRESS;
            for (std::size_t a = 0; a < pad.axes.size(); ++a) pad.axes[a] = state.axes[a];
            // Triggers rest at -1 in GLFW's mapping; 0 to 1 reads as how far it is pulled.
            for (const auto trigger : {GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER})
                pad.axes[trigger] = (pad.axes[trigger] + 1.f) * 0.5f;
        }
    }

    void Input::ApplyGamepads(const bool focused)
    {
        const auto& pads = polled();
        for (int i = 0; i < MaxGamepads; ++i) {
            auto& pad = _state->pads[i];
            const auto& from = pads[i];
            pad.connected = from.connected;
            pad.name = from.name;
            // Out of focus, the pad does nothing here: what was down comes up.
            for (std::size_t b = 0; b < pad.buttons.size(); ++b) press(pad.buttons[b], focused && from.buttons[b]);
            for (std::size_t a = 0; a < pad.axes.size(); ++a) pad.axes[a] = focused ? from.axes[a] : 0.f;
        }
    }

    bool Input::IsGamepadConnected(const int pad) const { return pad >= 0 && pad < MaxGamepads && _state->pads[pad].connected; }
    std::string Input::GamepadName(const int pad) const { return IsGamepadConnected(pad) ? _state->pads[pad].name : std::string(); }

    KeyState Input::GamepadButtonState(const GamepadButton button, const int pad) const
    {
        if (pad < 0 || pad >= MaxGamepads) return KeyState::eNotPressed;
        return _state->pads[pad].buttons[static_cast<std::size_t>(button)];
    }
    bool Input::IsGamepadButtonPressed(const GamepadButton b, const int pad) const { return GamepadButtonState(b, pad) == KeyState::ePressed; }
    bool Input::IsGamepadButtonHeld(const GamepadButton b, const int pad) const { return GamepadButtonState(b, pad) == KeyState::eHeld; }
    bool Input::IsGamepadButtonReleased(const GamepadButton b, const int pad) const { return GamepadButtonState(b, pad) == KeyState::eReleased; }

    float Input::GamepadAxisValue(const GamepadAxis axis, const int pad) const
    {
        if (pad < 0 || pad >= MaxGamepads) return 0.f;
        const float value = _state->pads[pad].axes[static_cast<std::size_t>(axis)];
        if (axis == GamepadAxis::eLeftTrigger || axis == GamepadAxis::eRightTrigger) return value;
        // A stick never rests at exactly 0: inside the dead zone it reads 0, and past it the rest of
        // its travel is stretched back over 0..1, so it does not jump as it leaves the zone.
        const float dead = _state->deadZone;
        const float magnitude = std::abs(value);
        if (magnitude <= dead) return 0.f;
        return std::copysign(std::min((magnitude - dead) / (1.f - dead), 1.f), value);
    }

    void Input::SetGamepadDeadZone(const float deadZone) { _state->deadZone = std::clamp(deadZone, 0.f, 0.95f); }

    void Input::FeedGamepadButton(const GamepadButton button, const bool down, const int pad)
    {
        if (pad < 0 || pad >= MaxGamepads) return;
        _state->fed.emplace_back([=](State& s) {
            s.pads[pad].connected = true;
            press(s.pads[pad].buttons[static_cast<std::size_t>(button)], down);
        });
    }

    void Input::FeedGamepadAxis(const GamepadAxis axis, const float value, const int pad)
    {
        if (pad < 0 || pad >= MaxGamepads) return;
        _state->fed.emplace_back([=](State& s) {
            s.pads[pad].connected = true;
            s.pads[pad].axes[static_cast<std::size_t>(axis)] = value;
        });
    }

    // ---- actions and axes ---------------------------------------------------------------------------

    namespace {
        // A source's value this frame: 1 for anything down, the axis's own for an axis — across every pad.
        float valueOf(const Input& input, const InputSource& source)
        {
            switch (source.kind) {
            case InputSource::Kind::eKey: return isDown(input.StateOf(static_cast<Key>(source.code))) ? 1.f : 0.f;
            case InputSource::Kind::eMouseButton: return isDown(input.MouseButtonState(static_cast<MouseButton>(source.code))) ? 1.f : 0.f;
            case InputSource::Kind::eGamepadButton:
                for (int pad = 0; pad < Input::MaxGamepads; ++pad)
                    if (isDown(input.GamepadButtonState(static_cast<GamepadButton>(source.code), pad))) return 1.f;
                return 0.f;
            case InputSource::Kind::eGamepadAxis: {
                float strongest = 0.f;
                for (int pad = 0; pad < Input::MaxGamepads; ++pad) {
                    const float v = input.GamepadAxisValue(static_cast<GamepadAxis>(source.code), pad);
                    if (std::abs(v) > std::abs(strongest)) strongest = v;
                }
                return strongest;
            }
            }
            return 0.f;
        }
    }

    void Input::BindAction(std::string action, std::vector<InputSource> sources)
    {
        _state->actions[std::move(action)].sources = std::move(sources);
    }

    void Input::BindAxis(std::string axis, std::vector<InputSource> sources)
    {
        _state->axes.insert_or_assign(std::move(axis), std::move(sources));
    }

    void Input::UpdateActions()
    {
        for (auto& action : _state->actions | std::views::values) {
            // An axis counts as down past halfway: a trigger pulled, a stick pushed.
            const bool down = std::ranges::any_of(action.sources, [&](const InputSource& source) {
                return std::abs(valueOf(*this, source) * source.scale) >= 0.5f;
            });
            press(action.state, down);
        }
    }

    KeyState Input::ActionState(const std::string_view action) const
    {
        const auto it = _state->actions.find(action);
        return it == _state->actions.end() ? KeyState::eNotPressed : it->second.state;
    }

    float Input::Axis(const std::string_view axis) const
    {
        const auto it = _state->axes.find(axis);
        if (it == _state->axes.end()) return 0.f;
        float sum = 0.f;
        for (const auto& source : it->second) sum += valueOf(*this, source) * source.scale;
        return std::clamp(sum, -1.f, 1.f);
    }

    glm::vec2 Input::Axis2D(const std::string_view x, const std::string_view y) const
    {
        const glm::vec2 direction { Axis(x), Axis(y) };
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
        return length > 1.f ? direction / length : direction;
    }

    InputBindings Input::Bindings() const
    {
        InputBindings bindings;
        const auto names = [](const std::vector<InputSource>& sources) {
            std::vector<std::string> out;
            for (const auto& source : sources) out.push_back(source.Name());
            return out;
        };
        for (const auto& [name, action] : _state->actions) bindings.actions.push_back({name, names(action.sources)});
        for (const auto& [name, sources] : _state->axes) bindings.axes.push_back({name, names(sources)});
        return bindings;
    }

    void Input::SetBindings(const InputBindings& bindings)
    {
        const auto parse = [](const InputBindings::Entry& entry) {
            std::vector<InputSource> sources;
            for (const auto& name : entry.sources) {
                if (const auto source = InputSource::Parse(name)) sources.push_back(*source);
                else log::Warn("[input] '{}' in the bindings of '{}' names nothing that can be pressed", name, entry.name);
            }
            return sources;
        };
        _state->actions.clear();
        _state->axes.clear();
        for (const auto& entry : bindings.actions) BindAction(entry.name, parse(entry));
        for (const auto& entry : bindings.axes) BindAxis(entry.name, parse(entry));
    }

    // ---- source names -------------------------------------------------------------------------------

    namespace {
        template<typename E>
        std::string_view bare(const E value) {
            const std::string_view name = magic_enum::enum_name(value);
            return name.empty() ? name : name.substr(1);   // without the leading 'e'
        }

        template<typename E>
        std::optional<E> named(const std::string_view name) {
            for (const E value : magic_enum::enum_values<E>())
                if (bare(value) == name) return value;
            return std::nullopt;
        }
    }

    std::string InputSource::Name() const
    {
        std::string name;
        switch (kind) {
        case Kind::eKey: name = "Key." + std::string(bare(static_cast<Key>(code))); break;
        case Kind::eMouseButton: {
            const auto button = static_cast<MouseButton>(code);
            name = button == MouseButton::eLeft ? "Mouse.Left" : button == MouseButton::eRight ? "Mouse.Right"
                 : button == MouseButton::eMiddle ? "Mouse.Middle" : "Mouse." + std::to_string(code + 1);
            break;
        }
        case Kind::eGamepadButton: name = "Gamepad." + std::string(bare(static_cast<GamepadButton>(code))); break;
        case Kind::eGamepadAxis: name = "GamepadAxis." + std::string(bare(static_cast<GamepadAxis>(code))); break;
        }
        if (scale == -1.f) return "-" + name;
        if (scale != 1.f) return name + "*" + std::format("{}", scale);
        return name;
    }

    std::optional<InputSource> InputSource::Parse(std::string_view name)
    {
        float scale = 1.f;
        if (name.starts_with('-')) { scale = -1.f; name.remove_prefix(1); }
        if (const auto star = name.find('*'); star != std::string_view::npos) {
            const auto factor = name.substr(star + 1);
            const auto parsed = detail::ParseFloating<float>(factor);
            if (!parsed) return std::nullopt;
            scale *= *parsed;
            name = name.substr(0, star);
        }
        const auto dot = name.find('.');
        if (dot == std::string_view::npos) return std::nullopt;
        const auto device = name.substr(0, dot);
        const auto what = name.substr(dot + 1);
        if (device == "Key") {
            if (const auto key = named<Key>(what)) return InputSource(*key, scale);
        } else if (device == "Mouse") {
            if (what == "Left") return InputSource(MouseButton::eLeft, scale);
            if (what == "Right") return InputSource(MouseButton::eRight, scale);
            if (what == "Middle") return InputSource(MouseButton::eMiddle, scale);
            int number = 0;
            const auto [end, ec] = std::from_chars(what.data(), what.data() + what.size(), number);
            if (ec == std::errc{} && end == what.data() + what.size() && number >= 1 && number <= 8)
                return InputSource(static_cast<MouseButton>(number - 1), scale);
        } else if (device == "Gamepad") {
            if (const auto button = named<GamepadButton>(what)) return InputSource(*button, scale);
        } else if (device == "GamepadAxis") {
            if (const auto axis = named<GamepadAxis>(what)) return InputSource(*axis, scale);
        }
        return std::nullopt;
    }
}
