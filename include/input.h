//
// Created by radue on 10/22/2024.
//

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

struct GLFWwindow;

#include "api.h"

namespace kor {
    class Window;
    class Engine;

    /**
     * @brief A physical key, identified by its position on a US layout.
     *
     * Keys are reported by position, not by the character they produce, so eW is the same key on a
     * QWERTY and an AZERTY keyboard even though it prints differently. For text entry, take the
     * characters from Dear ImGui rather than reading keys here.
     */
    enum class Key : unsigned short {
        eSpace = 32,
        eApostrophe = 39,
        eComma = 44,
        eMinus = 45,
        ePeriod = 46,
        eSlash = 47,
        eNum0 = 48,
        eNum1 = 49,
        eNum2 = 50,
        eNum3 = 51,
        eNum4 = 52,
        eNum5 = 53,
        eNum6 = 54,
        eNum7 = 55,
        eNum8 = 56,
        eNum9 = 57,
        eSemicolon = 59,
        eEqual = 61,
        eA = 65,
        eB = 66,
        eC = 67,
        eD = 68,
        eE = 69,
        eF = 70,
        eG = 71,
        eH = 72,
        eI = 73,
        eJ = 74,
        eK = 75,
        eL = 76,
        eM = 77,
        eN = 78,
        eO = 79,
        eP = 80,
        eQ = 81,
        eR = 82,
        eS = 83,
        eT = 84,
        eU = 85,
        eV = 86,
        eW = 87,
        eX = 88,
        eY = 89,
        eZ = 90,
        eLeftBracket = 91,
        eBackslash = 92,
        eRightBracket = 93,
        eGraveAccent = 96,
        eWorld1 = 161,
        eWorld2 = 162,
        eEsc = 256,
        eEnter = 257,
        eTab = 258,
        eBackspace = 259,
        eInsert = 260,
        eDelete = 261,
        eRight = 262,
        eLeft = 263,
        eDown = 264,
        eUp = 265,
        ePageUp = 266,
        ePageDown = 267,
        eHome = 268,
        eEnd = 269,
        eCapsLock = 280,
        eScrollLock = 281,
        eNumLock = 282,
        ePrintScreen = 283,
        ePause = 284,
        eF1 = 290,
        eF2 = 291,
        eF3 = 292,
        eF4 = 293,
        eF5 = 294,
        eF6 = 295,
        eF7 = 296,
        eF8 = 297,
        eF9 = 298,
        eF10 = 299,
        eF11 = 300,
        eF12 = 301,
        eF13 = 302,
        eF14 = 303,
        eF15 = 304,
        eF16 = 305,
        eF17 = 306,
        eF18 = 307,
        eF19 = 308,
        eF20 = 309,
        eF21 = 310,
        eF22 = 311,
        eF23 = 312,
        eF24 = 313,
        eF25 = 314,
        eKP0 = 320,
        eKP1 = 321,
        eKP2 = 322,
        eKP3 = 323,
        eKP4 = 324,
        eKP5 = 325,
        eKP6 = 326,
        eKP7 = 327,
        eKP8 = 328,
        eKP9 = 329,
        eKPDecimal = 330,
        eKPDivide = 331,
        eKPMultiply = 332,
        eKPSubtract = 333,
        eKPAdd = 334,
        eKPEnter = 335,
        eKPEqual = 336,
        eLeftShift = 340,
        eLeftControl = 341,
        eLeftAlt = 342,
        eLeftSuper = 343,
        eRightShift = 344,
        eRightControl = 345,
        eRightAlt = 346,
        eRightSuper = 347,
        eMenu = 348
    };

    /** @brief A mouse button. The first three have names; the rest are numbered. */
    enum class MouseButton {
        e1 = 0,
        e2 = 1,
        e3 = 2,
        e4 = 3,
        e5 = 4,
        e6 = 5,
        e7 = 6,
        e8 = 7,
        eLeft = e1,
        eRight = e2,
        eMiddle = e3
    };

    /** @brief Modifier keys and locks, as a set of bits. */
    enum class SpecialKey {
        eShift = 1 << 0,    ///< Either shift key is down.
        eCtrl = 1 << 1,     ///< Either control key is down.
        eAlt = 1 << 2,      ///< Either alt key is down.
        eSuper = 1 << 3,    ///< The command / Windows key is down.
        eCapsLock = 1 << 4, ///< Caps lock is on.
        eNumLock = 1 << 5,  ///< Num lock is on.
    };

    /**
     * @brief Where a key or button is in the press-hold-release cycle.
     *
     * ePressed and eReleased last for exactly one frame — the frame the transition happened in —
     * while eHeld persists for as long as the key stays down. Which is why "did the user just press
     * jump" and "is the user holding forward" are different questions with different answers.
     */
    enum class KeyState {
        eNotPressed,    ///< Up, and it was up last frame too.
        ePressed,       ///< Went down this frame.
        eHeld,          ///< Still down, having gone down on an earlier frame.
        eReleased,      ///< Came up this frame.
    };



    /**
     * @brief Keyboard and mouse state for the current frame.
     *
     * Polled, not event-driven: the run loop samples the devices once per frame, so every call
     * within a frame sees the same answer and nothing is missed between them.
     *
     * @code
     * void MyScene::Update() {
     *     if (kor::Input::isKeyHeld(kor::Key::eW)) camera.moveForward(kor::Time::FrameTime());
     *     if (kor::Input::isKeyPressed(kor::Key::eSpace)) jump();     // once per press
     * }
     * @endcode
     *
     * Everything is static: there is one window, so there is one input state.
     *
     * @note These report the raw device, whether or not Dear ImGui is using it. A scene that reacts
     *       to a click while the user is dragging an ImGui window should check ImGui's own
     *       WantCaptureMouse / WantCaptureKeyboard first.
     */
    class KORAL_API Input {
    	friend class Window;
    	friend class Engine;
    public:
        /** @brief Where @p key is in the press-hold-release cycle this frame. */
        static KeyState getKeyState(Key key);

        /** @brief Where @p button is in the press-hold-release cycle this frame. */
        static KeyState getMouseButtonState(MouseButton button);

        /** @brief Whether @p key went down this frame. True for one frame per press. */
        static bool isKeyPressed(Key key);

        /** @brief Whether @p key is being held, having gone down on an earlier frame. */
        static bool isKeyHeld(Key key);

        /** @brief Whether @p key came up this frame. True for one frame per release. */
        static bool isKeyReleased(Key key);

        /** @brief Whether @p button went down this frame. True for one frame per press. */
        static bool isMouseButtonPressed(MouseButton button);

        /** @brief Whether @p button is being held, having gone down on an earlier frame. */
        static bool isMouseButtonHeld(MouseButton button);

        /** @brief Whether @p button came up this frame. True for one frame per release. */
        static bool isMouseButtonReleased(MouseButton button);

        /**
         * @brief A readable name for @p key, as an interface would show it: "Left Shift", "F1", "A".
         *
         * Derived from the enumerator rather than a table, so a key added to kor::Key is named without
         * anything else being edited. It is here, out-of-line, because deriving it needs reflection
         * over an enumeration whose values run past the usual limit — a trap paid for once, in the
         * engine, instead of by everyone who writes a key-rebinding interface.
         */
        [[nodiscard]] static std::string describe(Key key);

        /** @brief A readable name for @p button: "Left Mouse", "Middle Mouse". @see describe(Key) */
        [[nodiscard]] static std::string describe(MouseButton button);

        /**
         * @brief The first key that went down this frame, if any.
         *
         * What completes a rebind: arm the control, then take whatever the user presses next. Which
         * key is "first" among several pressed in one frame is unspecified — pressing two at once is
         * not a thing a rebinding interface can honour anyway.
         */
        [[nodiscard]] static std::optional<Key> firstKeyPressed();

        /** @brief The first mouse button that went down this frame, if any. @see firstKeyPressed */
        [[nodiscard]] static std::optional<MouseButton> firstMouseButtonPressed();

        /**
         * @brief Whether the interface is using the pointer this frame — a panel hovered, a slider
         *        dragged, a menu open.
         *
         * What to ask before acting on the mouse yourself, so a camera does not fly off while an
         * ImGui window is being dragged. It is answered here, by the engine, rather than by each
         * caller asking ImGui: a module that only wants this one bool would otherwise have to be an
         * ImGui client — link it, and be handed the engine's context at load — for a question that
         * is really about input.
         *
         * False when there is no interface at all: a headless job, or a test. That is the answer
         * that lets the same code run in both.
         */
        [[nodiscard]] static bool interfaceWantsMouse();

        /** @brief Whether the interface is using the keyboard — text is being typed into it. @see interfaceWantsMouse */
        [[nodiscard]] static bool interfaceWantsKeyboard();

        /** @brief Cursor position in pixels, measured from the top-left of the drawable area. */
        static const glm::vec2& getMousePosition();

        /** @brief How far the cursor moved since the previous frame, in pixels. The value to drive a look-around camera with. */
        static const glm::vec2& getMousePositionDelta();

        /** @brief How far the wheel turned this frame. Y is the usual vertical wheel; X is horizontal scrolling where the device has it. */
        static const glm::vec2& getMouseScrollDelta();

        /** @brief Where the cursor was on the previous frame, in pixels. */
        static const glm::vec2& getLastMousePosition();

        /**
         * @brief What the cursor does while the application runs.
         *
         * @ref eCaptured is what a camera wants while it is being aimed: the pointer stops moving
         * across the screen, so it cannot leave the window, reach the edge of the desktop, or land on
         * something and click it — and the movement keeps arriving as deltas, without limit.
         */
        enum class CursorMode
        {
            eNormal,    ///< Visible, and free to move. The default.
            eHidden,    ///< Invisible, but still moving and still able to leave the window.
            eCaptured,  ///< Invisible and locked in place: only the movement is reported. Also called relative mode.
        };

        /**
         * @brief Sets what the cursor does.
         * @param mode The behaviour to switch to. Setting the mode it is already in does nothing.
         *
         * Applies to every attached window, so the behaviour does not change as the pointer crosses
         * from the main window to an undocked panel.
         *
         * The movement delta is *rebased* across the change rather than carried over: a mode switch
         * moves the cursor (capturing it warps it, releasing it puts it back), and reporting that as
         * movement would fling a camera the moment aiming began. The frame of the switch therefore
         * reports no movement at all.
         *
         * Raw, unaccelerated movement is used while captured where the platform has it, which is what
         * an aimed camera wants — desktop pointer acceleration is tuned for reaching menus.
         */
        static void setCursorMode(CursorMode mode);

        /** @brief What the cursor is currently doing. */
        [[nodiscard]] static CursorMode getCursorMode();

        /**
         * @brief Starts reading input from another window as well as the main one.
         * @param window The GLFW window to listen to. Attaching one twice does nothing.
         *
         * The engine listens to the window it created, and that is the whole story until something
         * else opens one — which is exactly what an *undocked* interface panel is: ImGui gives it its
         * own OS window, and events over it are delivered to that window, not to the main one. Without
         * this, the pointer moving over an undocked viewport produces no delta at all and a camera
         * driven by it simply stops responding.
         *
         * The GUI attaches and releases ImGui's windows as they come and go, so a project that uses
         * the interface needs to call none of this. It is public for the other case: an application
         * that opens a window of its own and wants the engine's input from it too.
         *
         * Positions are reported in *virtual desktop* coordinates from the moment more than one window
         * is attached, so a delta stays meaningful as the pointer crosses from one to another.
         */
        static void attachTo(GLFWwindow* window);

        /** @brief Stops reading input from @p window. Called for you when ImGui closes one. */
        static void detachFrom(GLFWwindow* window);

        /** @brief Every window input is currently read from, the main one first. */
        [[nodiscard]] static std::vector<GLFWwindow*> attachedWindows();

    private:
        static void setup(GLFWwindow* window);

        /** @brief Points a window's GLFW callbacks at the engine's. @see attachTo */
        static void installCallbacks(GLFWwindow* window);
        static void update();

    	struct KORAL_API Callbacks {
    		static void keyCallback(GLFWwindow*, int, int, int, int);
    		static void mouseMoveCallback(GLFWwindow*, double, double);
    		static void mouseButtonCallback(GLFWwindow*, int, int, int);
    		static void scrollCallback(GLFWwindow*, double, double);
    	    static void focusCallback(GLFWwindow*, int);
    	    static void charCallback(GLFWwindow*, unsigned int);
    	    static void cursorEnterCallback(GLFWwindow*, int);
    	    static void closeCallback(GLFWwindow*);
    	};
    };
}
