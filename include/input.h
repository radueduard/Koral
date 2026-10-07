//
// Created by radue on 10/22/2024.
//

#pragma once
#include <cstdint>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <string_view>
#include <kmath/vector.h>

struct GLFWwindow;

#include "api.h"
#include "reflect.h"


namespace kor {
    class Window;
    class App;
    class Scene;

    /**
     * @brief A physical key, identified by its position on a US layout.
     *
     * Keys are reported by position, not by the character they produce, so eW is the same key on a
     * QWERTY and an AZERTY keyboard even though it prints differently. For text entry, take the
     * characters from Input::TypedText() rather than reading keys here.
     */
    enum class Key : std::uint16_t {
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
    enum class MouseButton : std::uint8_t {
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

    /** @brief A gamepad's buttons, laid out as on an Xbox controller (GLFW's gamepad mapping). */
    enum class GamepadButton : std::uint8_t {
        eA, eB, eX, eY,
        eLeftBumper, eRightBumper,
        eBack, eStart, eGuide,
        eLeftThumb, eRightThumb,
        eDpadUp, eDpadRight, eDpadDown, eDpadLeft,
    };

    /** @brief A gamepad's axes: sticks from -1 to 1 (down is +1 on Y), triggers from 0 to 1. */
    enum class GamepadAxis : std::uint8_t {
        eLeftX, eLeftY, eRightX, eRightY, eLeftTrigger, eRightTrigger,
    };

    /**
     * @brief Something an action or an axis is bound to: a key, a mouse button, a gamepad button or
     *        a gamepad axis. @see Input::BindAction
     *
     * Each has a name, which is what bindings are saved as and what a rebinding screen shows:
     * "Key.Space", "Key.W", "Mouse.Left", "Gamepad.A", "Gamepad.DpadUp", "GamepadAxis.LeftX".
     */
    struct KORAL_API InputSource {
        enum class Kind : std::uint8_t { eKey, eMouseButton, eGamepadButton, eGamepadAxis };
        Kind kind = Kind::eKey;
        std::uint16_t code = 0;
        /** For an axis: what it contributes, times the source's value — a key, fully down, is 1. -1 turns it round. */
        float scale = 1.f;

        InputSource() = default;
        InputSource(Key key, float scale = 1.f) : kind(Kind::eKey), code(static_cast<std::uint16_t>(key)), scale(scale) {}                       // NOLINT(*-explicit-constructor)
        InputSource(MouseButton button, float scale = 1.f) : kind(Kind::eMouseButton), code(static_cast<std::uint16_t>(button)), scale(scale) {} // NOLINT(*-explicit-constructor)
        InputSource(GamepadButton button, float scale = 1.f) : kind(Kind::eGamepadButton), code(static_cast<std::uint16_t>(button)), scale(scale) {} // NOLINT(*-explicit-constructor)
        InputSource(GamepadAxis axis, float scale = 1.f) : kind(Kind::eGamepadAxis), code(static_cast<std::uint16_t>(axis)), scale(scale) {}    // NOLINT(*-explicit-constructor)

        /** @brief Its name: "Key.Space", "Gamepad.A"; an axis scaled by -1 is "-GamepadAxis.LeftY". */
        [[nodiscard]] std::string Name() const;
        /** @brief The source a name names. */
        [[nodiscard]] static std::optional<InputSource> Parse(std::string_view name);
        bool operator==(const InputSource&) const = default;
    };

    /** @brief What a scene's actions and axes are bound to, by name: what a settings file saves. @see Input::Bindings */
    struct KORAL_API InputBindings {
        struct Entry {
            std::string name;
            std::vector<std::string> sources;   ///< InputSource names.
        };
        std::vector<Entry> actions;
        std::vector<Entry> axes;
    };
    KORAL_REFLECT(InputBindings::Entry, name, sources)
    KORAL_REFLECT(InputBindings, actions, axes)

    /** @brief Modifier keys and locks, as a set of bits. */
    enum class SpecialKey : std::uint8_t {
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
    enum class KeyState : std::uint8_t {
        eNotPressed,    ///< Up, and it was up last frame too.
        ePressed,       ///< Went down this frame.
        eHeld,          ///< Still down, having gone down on an earlier frame.
        eReleased,      ///< Came up this frame.
    };



    /**
     * @brief One scene's keyboard and mouse: what they did this frame, while its window had them.
     *
     * Every scene has its own, reached inside it as `Input::` (Scene::Input) and from outside as
     * Scene::SceneInput(). Events go to the scene whose window they happen in — or one of the windows
     * its interface opened for an undocked panel — so two scenes in two windows never see each other's
     * keys, and the one the user is typing into is the one that hears it.
     *
     * Polled, not event-driven: the application samples the devices once per frame, so every call
     * within a frame sees the same answer and nothing is missed between them.
     *
     * @code
     * void MyScene::Update() {
     *     if (Input::IsKeyHeld(kor::Key::eW)) camera.MoveForward(Time::FrameTime());
     *     if (Input::IsKeyPressed(kor::Key::eSpace)) Jump();     // once per press
     * }
     * @endcode
     *
     * @note These report the raw device, whether or not the scene's interface is using it. A scene
     *       that reacts to a click while the user is dragging a panel checks InterfaceWantsMouse().
     */
    class KORAL_API Input {
    public:
        Input();
        ~Input();
        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;

        /** @brief Where @p key is in the press-hold-release cycle this frame. */
        [[nodiscard]] KeyState StateOf(Key key) const;

        /** @brief Where @p button is in the press-hold-release cycle this frame. */
        [[nodiscard]] KeyState MouseButtonState(MouseButton button) const;

        /** @brief Whether @p key went down this frame. True for one frame per press. */
        [[nodiscard]] bool IsKeyPressed(Key key) const;

        /** @brief Whether @p key is being held, having gone down on an earlier frame. */
        [[nodiscard]] bool IsKeyHeld(Key key) const;

        /** @brief Whether @p key came up this frame. True for one frame per release. */
        [[nodiscard]] bool IsKeyReleased(Key key) const;

        /** @brief Whether @p button went down this frame. True for one frame per press. */
        [[nodiscard]] bool IsMouseButtonPressed(MouseButton button) const;

        /** @brief Whether @p button is being held, having gone down on an earlier frame. */
        [[nodiscard]] bool IsMouseButtonHeld(MouseButton button) const;

        /** @brief Whether @p button came up this frame. True for one frame per release. */
        [[nodiscard]] bool IsMouseButtonReleased(MouseButton button) const;

        /**
         * @brief A readable name for @p key, as an interface would show it: "Left Shift", "F1", "A".
         *
         * Derived from the enumerator rather than a table, so a key added to kor::Key is named without
         * anything else being edited.
         */
        [[nodiscard]] static std::string Describe(Key key);

        /** @brief A readable name for @p button: "Left Mouse", "Middle Mouse". @see Describe(Key) */
        [[nodiscard]] static std::string Describe(MouseButton button);

        /** @brief The first key that went down this frame, if any: what completes a rebind. */
        [[nodiscard]] std::optional<Key> FirstKeyPressed() const;

        /** @brief The first mouse button that went down this frame, if any. @see FirstKeyPressed */
        [[nodiscard]] std::optional<MouseButton> FirstMouseButtonPressed() const;

        /**
         * @brief Whether an interface over the scene's window is using the pointer this frame — a panel
         *        hovered, a slider dragged, a menu open: whatever one has claimed (ClaimInterface).
         *
         * What to ask before acting on the mouse yourself, so a camera does not fly off while a panel
         * is being dragged.
         */
        [[nodiscard]] bool InterfaceWantsMouse() const;

        /** @brief Whether an interface over the scene's window is using the keyboard — text is being typed into it. */
        [[nodiscard]] bool InterfaceWantsKeyboard() const;

        /**
         * @brief Says that an interface — a koral-ui one, say — is using the pointer or the keyboard.
         *        InterfaceWantsMouse/Keyboard answer true while it does, so a camera stays put under a
         *        panel whichever library drew it.
         *
         * Sticky: it holds until the claimer says otherwise. Each @p claimer is counted on its own, so
         * two interfaces over one window do not release each other's claim.
         */
        void ClaimInterface(const void* claimer, bool mouse, bool keyboard);

        /**
         * @brief The text typed this frame, as Unicode code points in the order they were typed —
         *        what a text field inserts. Not keys: a shifted 'a' arrives here as 'A', and a dead
         *        key followed by 'e' as 'é'.
         */
        [[nodiscard]] std::u32string_view TypedText() const;

        /**
         * @brief Whether @p key repeated this frame, as a key held down does after a moment — what
         *        makes holding backspace delete more than one character. The press itself is not a repeat.
         */
        [[nodiscard]] bool IsKeyRepeated(Key key) const;

        /** @brief Cursor position in pixels, from the top-left of the scene's window. */
        [[nodiscard]] const kor::Vec2& MousePosition() const;

        /** @brief How far the cursor moved since the previous frame, in pixels. What drives a look-around camera. */
        [[nodiscard]] const kor::Vec2& MousePositionDelta() const;

        /** @brief How far the wheel turned this frame. Y is the usual vertical wheel. */
        [[nodiscard]] const kor::Vec2& MouseScrollDelta() const;

        /** @brief Where the cursor was on the previous frame, in pixels. */
        [[nodiscard]] const kor::Vec2& LastMousePosition() const;

        /**
         * @brief What the cursor does over this scene's windows.
         *
         * @ref eCaptured is what a camera wants while it is being aimed: the pointer stops moving, so
         * it cannot leave the window or land on something and click it, and the movement keeps
         * arriving as deltas, without limit.
         */
        enum class CursorMode : std::uint8_t {
            eNormal,    ///< Visible, and free to move. The default.
            eHidden,    ///< Invisible, but still moving and still able to leave the window.
            eCaptured,  ///< Invisible and locked in place: only the movement is reported. Also called relative mode.
        };

        /**
         * @brief Sets what the cursor does over this scene's windows — its own and any panel its
         *        interface undocked.
         *
         * The frame the mode changes reports no movement: capturing warps the cursor and releasing it
         * puts it back, and reporting that as movement would fling a camera the moment aiming began.
         */
        void SetCursorMode(CursorMode mode);

        /** @brief What the cursor is currently doing. */
        [[nodiscard]] CursorMode CurrentCursorMode() const;

        /**
         * @brief Starts reading input from another OS window as well as the scene's own.
         *
         * The interface does this for the windows it opens for undocked panels, so a camera keeps
         * responding while the pointer is over one. Attaching one twice does nothing.
         */
        void AttachTo(GLFWwindow* window);

        /** @brief Stops reading input from @p window. */
        void DetachFrom(GLFWwindow* window);

        /** @brief Every window this reads from, the scene's own first. */
        [[nodiscard]] const std::vector<GLFWwindow*>& AttachedWindows() const;

        // ---- gamepads -----------------------------------------------------------------------------
        // Every connected gamepad, numbered from 0 in the order they were found. Only the scene whose
        // window has focus sees them — a scene in a window behind it is not steered by the pad — and
        // an offscreen scene gets them fed (FeedFrom, kgui::SceneView).

        static constexpr int MaxGamepads = 4;

        [[nodiscard]] bool IsGamepadConnected(int pad = 0) const;
        /** @brief The gamepad's name, as its driver reports it; empty when none is connected there. */
        [[nodiscard]] std::string GamepadName(int pad = 0) const;
        [[nodiscard]] KeyState GamepadButtonState(GamepadButton button, int pad = 0) const;
        [[nodiscard]] bool IsGamepadButtonPressed(GamepadButton button, int pad = 0) const;
        [[nodiscard]] bool IsGamepadButtonHeld(GamepadButton button, int pad = 0) const;
        [[nodiscard]] bool IsGamepadButtonReleased(GamepadButton button, int pad = 0) const;
        /** @brief An axis, with the dead zone taken off a stick's: a stick at rest reads 0. */
        [[nodiscard]] float GamepadAxisValue(GamepadAxis axis, int pad = 0) const;
        /** @brief How far a stick must move before it reads anything: 0.15 of its travel by default. */
        void SetGamepadDeadZone(float deadZone);

        // ---- actions and axes ---------------------------------------------------------------------
        // What the scene means rather than which key: "Jump" is Space, or A, or the left mouse
        // button — whichever the player uses, and whatever they rebind it to.
        //
        //     Input::Get().BindAction("Jump", {kor::Key::eSpace, kor::GamepadButton::eA});
        //     Input::Get().BindAxis("MoveX", {{kor::Key::eD, 1.f}, {kor::Key::eA, -1.f}, kor::GamepadAxis::eLeftX});
        //     if (Input::Get().IsActionPressed("Jump")) Jump();
        //     position.x += Input::Get().Axis("MoveX") * speed * Time::FrameTime();

        /** @brief Binds @p action to @p sources, replacing what it was bound to. */
        void BindAction(std::string action, std::vector<InputSource> sources);
        /** @brief Binds @p axis to @p sources: their values, times their scales, summed and kept in [-1, 1]. */
        void BindAxis(std::string axis, std::vector<InputSource> sources);
        /** @brief Down when any of its sources is down; pressed the frame the first goes down. */
        [[nodiscard]] KeyState ActionState(std::string_view action) const;
        [[nodiscard]] bool IsActionPressed(std::string_view action) const { return ActionState(action) == KeyState::ePressed; }
        [[nodiscard]] bool IsActionHeld(std::string_view action) const { return ActionState(action) == KeyState::eHeld; }
        [[nodiscard]] bool IsActionReleased(std::string_view action) const { return ActionState(action) == KeyState::eReleased; }
        [[nodiscard]] float Axis(std::string_view axis) const;
        /** @brief Two axes as one direction, no longer than 1: diagonal on a keyboard is not faster. */
        [[nodiscard]] kor::Vec2 Axis2D(std::string_view x, std::string_view y) const;
        /** @brief Every binding, by name: to save, or to show on a rebinding screen. */
        [[nodiscard]] InputBindings Bindings() const;
        /** @brief Replaces every binding. A source name it cannot read is reported and skipped. */
        void SetBindings(const InputBindings& bindings);

        // ---- input from elsewhere ----------------------------------------------------------------
        // For a scene with no OS window of its own to read — an offscreen one — whose host forwards
        // what happens over the view it shows it in (kgui::SceneView does). What is fed takes effect
        // at the start of the scene's next frame, as an OS window's events do: a key fed down reads
        // as pressed there, then held.

        /** @brief A key went down, or up. */
        void FeedKey(Key key, bool down);
        /** @brief A mouse button went down, or up. */
        void FeedMouseButton(MouseButton button, bool down);
        /** @brief Where the pointer is, in the scene's window's pixels. Moves nothing: see FeedMouseDelta. */
        void FeedMousePosition(kor::Vec2 position);
        /** @brief How far the pointer moved, in pixels: MousePositionDelta(). */
        void FeedMouseDelta(kor::Vec2 delta);
        /** @brief How far the wheel turned: MouseScrollDelta(). */
        void FeedScroll(kor::Vec2 delta);
        /** @brief Text typed: TypedText(). */
        void FeedText(std::u32string_view text);
        /** @brief A held key repeated: IsKeyRepeated(). */
        void FeedKeyRepeat(Key key);
        /** @brief A gamepad button went down, or up. */
        void FeedGamepadButton(GamepadButton button, bool down, int pad = 0);
        /** @brief Where a gamepad axis is now. */
        void FeedGamepadAxis(GamepadAxis axis, float value, int pad = 0);
        /** @brief Everything down released: for a host that stops feeding, so nothing is left held. */
        void ReleaseAll();
        /**
         * @brief Feeds what @p source saw this frame: the keys (with @p keyboard) and the buttons,
         *        movement and scroll (with @p mouse) that went down, came up or moved.
         */
        void FeedFrom(const Input& source, bool keyboard, bool mouse);

    private:
        friend class App;
        struct State;

        /** @brief End of frame: presses become holds, releases become nothing, deltas start over. */
        void Update();

        /** @brief Start of frame: what was fed since the last one arrives, as an OS window's events do. */
        void ApplyFed();

        /** @brief Start of frame: reads every gamepad, once for all scenes. */
        static void PollGamepads();
        /** @brief Start of frame: this scene's view of the gamepads — the polled one when @p focused, none otherwise. */
        void ApplyGamepads(bool focused);
        /** @brief Start of frame, after everything else arrived: what the actions are now. */
        void UpdateActions();

        static void InstallCallbacks(GLFWwindow* window);

    public:
        /** @brief The GLFW callbacks every attached window is given. Internal. */
        struct Callbacks {
            static void KeyCallback(GLFWwindow*, int, int, int, int);
            static void MouseMoveCallback(GLFWwindow*, double, double);
            static void MouseButtonCallback(GLFWwindow*, int, int, int);
            static void ScrollCallback(GLFWwindow*, double, double);
            static void FocusCallback(GLFWwindow*, int);
            static void CharCallback(GLFWwindow*, unsigned int);
        };

    private:
        State* _state;
    };
}
