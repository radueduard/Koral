//
// Created by radue on 28.07.2026.
//

/**
 * @file koralCamera.h
 * @brief The camera module's public face: what a scene (or another module) may call.
 *
 * A project links this module and uses it like any other part of the API — there is no service
 * object to look up and no handle to keep:
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyScene PRIVATE Koral koral-camera)
 *
 * _player = kcam::PerspectiveCamera::Builder{}
 *     .setName("player")
 *     .setFovY(glm::radians(70.f))
 *     .setPosition({ 0.f, 1.5f, 5.f })
 *     .lookAt({ 0.f, 0.f, 0.f })
 *     .setController({ .kind = kcam::Controller::Kind::eFly })  // the runtime moves it, before the scene runs
 *     .setFollowWindowAspect(true)                              // and keeps its aspect matched to the window
 *     .build();                                                 // (followAspectOf(target) for a viewport)
 *
 * _minimap = kcam::OrthographicCamera::Builder{}
 *     .setBounds(-50.f, 50.f, -50.f, 50.f)
 *     .build();                                // no per-frame behaviour: entirely the scene's
 *
 * // ... later, in Render()
 * commandBuffer.PushConstants(Push{ _player->viewProjection() });
 * @endcode
 *
 * Linking the module is what loads it: the library registers itself with the runtime as it is
 * loaded, so nothing has to be named in koral.json. @see kor::Module
 *
 * A camera is an ordinary kor::Resource — the project owns it, and destroying it destroys the
 * camera, controller and all. There is deliberately no "main camera": which camera renders what is
 * the project's business, and the module has no opinion.
 *
 * @section camera_updates What updates when
 *
 * Matrices are lazy: set the position twenty times and pay for one view matrix when it is next
 * read, so "updating on change" needs no opt-in and the setters cost nothing to repeat.
 *
 * *Per-frame* behaviour is the opt-in part. Asking for a controller or for window-aspect tracking
 * hands the camera to the runtime, which drives it at the top of each frame — before the scene's
 * Update, so a scene always reads an already-moved camera. Both can be changed at any time
 * afterwards; a camera with neither is entirely manual, and the module never touches it.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <builder.h>
#include <error.h>
#include <framebuffer.h>
#include <image.h>
#include <input.h>
#include <resource.h>
#include <semantics.h>

/**
 * @brief Marks what crosses out of the module's library.
 *
 * Koral and its modules build with hidden visibility, so a class a consumer *calls* — rather than
 * only reaches through the engine — has to say so. Exported here, imported everywhere else; the
 * module's own build defines KORAL_CAMERA_EXPORTS to pick the first branch.
 */
#if defined(_WIN32)
#  if defined(KORAL_CAMERA_EXPORTS)
#    define KCAM_API __declspec(dllexport)
#  else
#    define KCAM_API __declspec(dllimport)
#  endif
#else
#  define KCAM_API __attribute__((visibility("default")))
#endif

namespace kcam
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view kModuleId = "koral.camera";
    inline constexpr std::uint32_t    kModuleVersion = 1;

    /**
     * @brief What a shader can ask a camera to fill in, written `camera(NAME)`.
     *
     * @code{.glsl}
     * #pragma camera(VIEW_PROJECTION_MATRIX)
     * mat4 viewProjection;
     * @endcode
     * @code{.slang}
     * import camera;
     * [camera("VIEW_PROJECTION_MATRIX")] float4x4 viewProjection;
     * @endcode
     *
     * The vocabulary belongs to this module rather than to the engine — `camera` is the name the
     * annotation uses, and these are the semantics that name answers for. @see kor::semantics.h
     */
    namespace semantics
    {
        /** @brief The name a shader annotates with: the `camera` of `camera(VIEW_MATRIX)`. */
        inline constexpr std::string_view kNamespace = "camera";

        inline constexpr std::string_view kViewMatrix                  = "VIEW_MATRIX";
        inline constexpr std::string_view kProjectionMatrix            = "PROJECTION_MATRIX";
        inline constexpr std::string_view kViewProjectionMatrix        = "VIEW_PROJECTION_MATRIX";
        inline constexpr std::string_view kInverseViewMatrix           = "INVERSE_VIEW_MATRIX";
        inline constexpr std::string_view kInverseProjectionMatrix     = "INVERSE_PROJECTION_MATRIX";
        inline constexpr std::string_view kInverseViewProjectionMatrix = "INVERSE_VIEW_PROJECTION_MATRIX";

        inline constexpr std::string_view kPosition = "POSITION";
        inline constexpr std::string_view kForward  = "FORWARD";
        inline constexpr std::string_view kUp       = "UP";
        inline constexpr std::string_view kRight    = "RIGHT";

        inline constexpr std::string_view kNearPlane  = "NEAR_PLANE";    ///< float
        inline constexpr std::string_view kFarPlane   = "FAR_PLANE";     ///< float
        /** @brief vec4: near, far, far - near, 1 / (far - near). */
        inline constexpr std::string_view kDepthRange = "DEPTH_RANGE";
    }

    /**
     * @brief One thing a controller can be asked to do — the key half of @ref Bindings.
     *
     * eCount is the number of actions, not an action. It is what a rebinding interface iterates
     * over: `for (auto a = Action{}; a < Action::eCount; ...) bindings[a]`.
     */
    enum class Action : std::uint8_t
    {
        eMoveForward,
        eMoveBack,
        eMoveLeft,
        eMoveRight,
        eMoveUp,
        eMoveDown,
        eBoost,         ///< Held, multiplies the fly speed by @ref Bindings::boostFactor.
        eCount,
    };

    /**
     * @brief Modifier keys an @ref Input can additionally require, so a binding can be a chord.
     *
     * Left and right count the same: a binding asking for eCtrl is satisfied by either control key,
     * which is what a user pressing "ctrl" means. Extra modifiers that were not asked for are
     * ignored, so a chord fires whether or not something else is being held down with it.
     */
    enum class Modifier : std::uint8_t
    {
        eNone  = 0,
        eCtrl  = 1 << 0,
        eShift = 1 << 1,
        eAlt   = 1 << 2,
        eSuper = 1 << 3,   ///< The Windows / Command key.
    };

    /** @brief Both modifiers, as one requirement: `Modifier::eCtrl | Modifier::eShift`. */
    [[nodiscard]] constexpr Modifier operator|(const Modifier a, const Modifier b)
    {
        return static_cast<Modifier>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    /** @brief Whether @p a includes @p b. Reads as a test, so it returns bool rather than a set. */
    [[nodiscard]] constexpr bool operator&(const Modifier a, const Modifier b)
    {
        return (static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b)) != 0;
    }

    /**
     * @brief One thing that is either held or not: a key, a mouse button, always, or never.
     *
     * A controller reads every digital input through this, so anything bound to a key can just as
     * well be bound to a mouse button — which is the whole reason it exists rather than a
     * kor::Key.
     *
     * The two constant states are what make a gate expressible without a second convention:
     * @ref always is held on every frame, and an unbound input — which is what default
     * construction gives — is held on none. So `enable = Input::always()` is a controller that is
     * simply on, and `moveUp = {}` is an action nothing triggers, and neither reading has to be
     * inferred from the field it sits in.
     *
     * @code
     * .enable      = kcam::Input::always(),                        // on, no gate at all
     * .look        = kcam::Input::mouse(kor::MouseButton::eRight), // only while held
     * .moveForward = kcam::Input::key(kor::Key::eUp),
     * .moveUp      = {},                                           // nothing moves the camera up
     * @endcode
     *
     * A binding may also require modifiers, which is what makes a chord: @ref Modifier.
     */
    struct Input
    {
        enum class Type : std::uint8_t
        {
            eNone,          ///< Never held. An action that never fires, a gate that never opens.
            eAlways,        ///< Held on every frame. A gate that is simply open.
            eKey,
            eMouseButton,
        };

        Type type = Type::eNone;
        std::uint16_t code = 0;         ///< A kor::Key or a kor::MouseButton, per @ref type.
        Modifier modifiers = Modifier::eNone;   ///< Also held for this to count. @see Modifier

        [[nodiscard]] static constexpr Input key(const kor::Key key,
                                                 const Modifier modifiers = Modifier::eNone)
        {
            return { Type::eKey, static_cast<std::uint16_t>(key), modifiers };
        }

        [[nodiscard]] static constexpr Input mouse(const kor::MouseButton button,
                                                   const Modifier modifiers = Modifier::eNone)
        {
            return { Type::eMouseButton, static_cast<std::uint16_t>(button), modifiers };
        }

        /** @brief Held on every frame, with nothing to press. */
        [[nodiscard]] static constexpr Input always() { return { Type::eAlways }; }

        /** @brief Whether anything can ever make this fire. */
        [[nodiscard]] constexpr bool bound() const { return type != Type::eNone; }

        [[nodiscard]] constexpr bool operator==(const Input&) const = default;
    };

    /** @brief Where an @ref Axis reads its value from, once per frame. */
    enum class AxisSource : std::uint8_t
    {
        eNone,      ///< Reads zero. The axis does nothing.
        eMouseX,    ///< How far the pointer moved right this frame, in pixels.
        eMouseY,    ///< How far the pointer moved down this frame, in pixels.
        eScrollX,   ///< Horizontal wheel or trackpad scroll this frame.
        eScrollY,   ///< Vertical wheel scroll this frame, in notches.
    };

    /**
     * @brief An analog input, scaled: what the mouse does to the camera, as data.
     *
     * The controller reads @ref source each frame and multiplies by @ref sensitivity, negating if
     * @ref invert is set. Which axis drives what — and how hard — is therefore a value a project
     * sets, not something baked into the controller.
     *
     * @code
     * // twice as fast, and inverted the way flight sims do it
     * .pitch = { .source = kcam::AxisSource::eMouseY, .sensitivity = 0.01f, .invert = true },
     * @endcode
     */
    struct Axis
    {
        AxisSource source = AxisSource::eNone;
        float sensitivity = 1.f;    ///< What one unit of the source is worth. Units depend on the use.
        bool invert = false;        ///< Flips the direction.
    };

    /**
     * @brief Which key drives which action, and which mouse button turns the view.
     *
     * One of the fields of @ref Controller, so every camera has its own set and two cameras can
     * answer to different keys.
     *
     * @code
     * // arrow keys instead of WASD, and only while the right mouse button is down
     * camera->setController({
     *     .kind = kcam::Controller::Kind::eFly,
     *     .bindings = {
     *         .enable      = kcam::Input::mouse(kor::MouseButton::eRight),
     *         .moveForward = kcam::Input::key(kor::Key::eUp),
     *         .moveBack    = kcam::Input::key(kor::Key::eDown),
     *         .moveLeft    = kcam::Input::key(kor::Key::eLeft),
     *         .moveRight   = kcam::Input::key(kor::Key::eRight),
     *     },
     * });
     *
     * // or one action at a time, which is what a rebinding interface does
     * auto controller = camera->controller();
     * controller.bindings[kcam::Action::eMoveUp] = kcam::Input::key(kor::Key::eSpace);
     * camera->setController(controller);
     * @endcode
     *
     * Keys are physical positions, not the characters they print (@see kor::Key), so the WASD
     * defaults are the same four keys on an AZERTY keyboard — where they print ZQSD. Rebinding is
     * for a *different* arrangement, not for a different layout.
     */
    struct Bindings
    {
        /**
         * @brief Held for the controller to do anything at all. On by default.
         *
         * Bind it to the right mouse button for the editor-style camera: the keys then mean
         * nothing to it the rest of the time, and are free to mean something else. Leaving it
         * unbound is the opposite extreme — a controller that never runs, which is a way to park
         * one without forgetting how it was set up.
         */
        Input enable = Input::always();

        /**
         * @brief Held for @ref yaw and @ref pitch to turn the camera.
         *
         * Separate from @ref enable so "always drivable, but only looks around while the button is
         * down" — which is what the defaults do — is expressible. Set it to Input::always() for a
         * camera that turns with the pointer the whole time, as a first-person game does.
         */
        Input look = Input::mouse(kor::MouseButton::eRight);

        /**
         * @brief Pressed to let the cursor go. Ctrl+Shift+O by default.
         *
         * The way out of a captured cursor. With @ref enable and @ref look both bound to a button,
         * releasing that button ends the look and hands the pointer back — but a first-person camera
         * sets both to Input::always(), and then there is nothing to release: the cursor would stay
         * captured for as long as the scene runs, with no way to reach a menu or another window.
         *
         * A chord rather than a single key, and deliberately: this fires while the scene has the
         * keyboard, so anything a game might plausibly bind — Escape, Tab, a letter — would be taken
         * away from it. @ref engage is what takes the cursor back, so the two are not the same
         * binding pressed twice.
         *
         * While released the controller reads nothing at all and the cursor is the user's again; it
         * keeps its angles, so taking the grip back resumes rather than snaps. Unbind it (`= {}`)
         * for a camera that never lets go.
         *
         * @see Camera::released, which is the same state a scene can read and write directly — that
         * is how a menu opening releases the cursor without the user pressing anything.
         */
        Input release = Input::key(kor::Key::eO, Modifier::eCtrl | Modifier::eShift);

        /**
         * @brief Pressed over the scene to take the cursor back afterwards. Left mouse by default.
         *
         * Only counts where the controller is allowed to read the mouse — over the viewport for a
         * scene that says so, or anywhere ImGui is not using the pointer for one that does not. That
         * is what makes clicking a panel while released stay a click on that panel: the camera takes
         * the pointer back when you click *it*, and not before. @see Controller::Input
         */
        Input engage = Input::mouse(kor::MouseButton::eLeft);

        Input moveForward = Input::key(kor::Key::eW);
        Input moveBack    = Input::key(kor::Key::eS);
        Input moveLeft    = Input::key(kor::Key::eA);
        Input moveRight   = Input::key(kor::Key::eD);
        Input moveUp      = Input::key(kor::Key::eE);
        Input moveDown    = Input::key(kor::Key::eQ);
        Input boost       = Input::key(kor::Key::eLeftShift);

        /** @brief What the boost input multiplies the fly speed by while it is held. */
        float boostFactor = 4.f;

        /** @brief What turns the camera left and right, in radians per unit of the source. */
        Axis yaw { .source = AxisSource::eMouseX, .sensitivity = 0.005f };

        /** @brief What tilts the camera up and down, in radians per unit of the source. */
        Axis pitch { .source = AxisSource::eMouseY, .sensitivity = 0.005f };

        /**
         * @brief What moves an orbiting camera towards and away from its target. Fly ignores it.
         *
         * Applied exponentially — each unit scales the distance rather than subtracting from it —
         * so it feels the same whether the camera is 2 units out or 200.
         */
        Axis zoom { .source = AxisSource::eScrollY, .sensitivity = 0.105f };

        /** @brief The input bound to @p action. Assignable, so a rebinding interface can write it. */
        [[nodiscard]] constexpr Input& operator[](const Action action)
        {
            switch (action) {
            case Action::eMoveForward: return moveForward;
            case Action::eMoveBack:    return moveBack;
            case Action::eMoveLeft:    return moveLeft;
            case Action::eMoveRight:   return moveRight;
            case Action::eMoveUp:      return moveUp;
            case Action::eMoveDown:    return moveDown;
            case Action::eBoost:       return boost;
            case Action::eCount:       break;
            }
            // eCount is not an action. Returning a real reference keeps the operator total — a
            // rebinding loop that runs one step too far writes to a bit bucket instead of off the
            // end of the object. Static, so Bindings stays an aggregate and designated
            // initialisers keep working.
            return discarded;
        }

        [[nodiscard]] constexpr Input operator[](const Action action) const
        {
            return const_cast<Bindings&>(*this)[action];
        }

        /** @brief Where a write to Action::eCount goes. Never read. */
        static inline Input discarded {};
    };

    /**
     * @brief What drives a camera each frame, and everything that drive needs.
     *
     * One value describing the whole behaviour: which one, how fast, around what, and off which
     * keys. A camera is handed one at build time and can be handed a different one at any point —
     * there is no separate setter per knob, so "the camera flies twice as fast now" and "the
     * camera orbits instead" are the same kind of change.
     *
     * @code
     * .setController({ .kind = kcam::Controller::Kind::eFly, .speed = 12.f })
     * @endcode
     *
     * A default-constructed Controller drives nothing, which is what a camera starts with.
     */
    struct Controller
    {
        /** @brief How the runtime drives the camera, if at all. */
        enum class Kind : std::uint8_t
        {
            eNone,  ///< Nothing touches the camera. The default.
            eFly,   ///< WASD + QE moves, holding the right mouse button looks. Shift speeds up.
            eOrbit, ///< Right-drag orbits a target point, the scroll wheel moves closer or farther.
        };
        // Those are the default keys; every one of them is rebindable. @see bindings

        /**
         * @brief Where a controller's input is allowed to come from.
         *
         * A controller reading the mouse has to know when the mouse is someone else's — a slider being
         * dragged, a menu open. Left alone it works that out from ImGui, which is right for a scene
         * drawn straight to the screen.
         *
         * It is *wrong* for a scene shown inside an ImGui window, a viewport above all: the pointer is
         * over a window there by definition, so the automatic answer is always "someone else's" and the
         * camera never moves. Such a scene decides for itself — `eEnabled` while the viewport is
         * hovered, `eDisabled` otherwise. @see kgui::Viewport::isHovered
         */
        enum class Input : std::uint8_t
        {
            eAutomatic, ///< Take input unless ImGui wants it. The default.
            eEnabled,   ///< Take input regardless of what ImGui is doing.
            eDisabled,  ///< Take none at all.
        };

        /**
         * @brief What the cursor does while the camera is being aimed.
         *
         * Aiming a camera with a *visible, moving* pointer is the wrong shape: the pointer walks off
         * the window, stops at the edge of the desktop and takes the look with it, and lands on
         * whatever is underneath when the button comes up. Capturing it — hidden and locked, with only
         * the movement reported — is what the look is for, and it is restored the moment the look ends.
         */
        enum class Cursor : std::uint8_t
        {
            eCapture,   ///< Hide and lock it while looking. The default, and what a fly camera wants.
            eHide,      ///< Hide it while looking, but let it keep moving.
            eLeaveAlone,///< Do not touch it. For an application that manages the cursor itself.
        };

        Kind kind = Kind::eNone;

        /** @brief Where its input may come from. @see Input */
        Input input = Input::eAutomatic;

        /** @brief What the cursor does while looking. @see Cursor */
        Cursor cursor = Cursor::eCapture;

        /** @brief Fly: movement speed, in units per second. Ignored by the others. */
        float speed = 5.f;

        /** @brief Orbit: the point circled. Ignored by the others. */
        glm::vec3 orbitTarget { 0.f, 0.f, 0.f };

        /** @brief The keys and mouse button this controller answers to. */
        Bindings bindings;
    };

    /**
     * @brief One camera: a pose in the world, and the matrices derived from it.
     *
     * Built through PerspectiveCamera::Builder or OrthographicCamera::Builder, and owned by whoever
     * asked. Matrices are recomputed on access when something changed, so the getters are always
     * current.
     */
    class KCAM_API Camera : public kor::AutoUpdatable, public kor::SemanticSerializer
    {
    public:
        /** @brief World-space position of the eye. */
        [[nodiscard]] virtual glm::vec3 position() const = 0;

        /** @brief World-space orientation. Identity looks down -Z with +Y up. */
        [[nodiscard]] virtual glm::quat rotation() const = 0;

        /** @brief The direction the camera looks along, derived from @ref rotation. */
        [[nodiscard]] virtual glm::vec3 forward() const = 0;

        virtual void setPosition(glm::vec3 position) = 0;
        virtual void setRotation(glm::quat rotation) = 0;

        /** @brief Turns the camera (from wherever it stands) to look at @p target. */
        virtual void lookAt(glm::vec3 target, glm::vec3 up = { 0.f, 1.f, 0.f }) = 0;

        /** @brief World → view. */
        [[nodiscard]] virtual const glm::mat4& view() const = 0;

        /** @brief View → clip, in the engine's clip conventions (zero-to-one depth, Y down). */
        [[nodiscard]] virtual const glm::mat4& projection() const = 0;

        /** @brief projection() * view(), cached — the matrix a draw usually wants. */
        [[nodiscard]] virtual const glm::mat4& viewProjection() const = 0;

        /** @brief The name given at build time, for interfaces and logs. */
        [[nodiscard]] virtual std::string_view name() const = 0;

        // ---- per-frame behaviour ----------------------------------------------------------------

        /**
         * @brief Sets what drives this camera each frame, replacing whatever drove it before.
         *
         * Takes effect on the next frame, and is safe to call while a controller is running — a
         * key held across the change simply stops or starts driving its action. Switching kind
         * never snaps the view: the new controller picks up whatever direction the camera is
         * pointing at the time. `Kind::eNone` returns the camera to fully manual control.
         *
         * To change one knob, read the current controller, edit, and set it back — the value is a
         * plain struct, so that is a copy and two lines.
         */
        virtual void setController(const Controller& controller) = 0;
        [[nodiscard]] virtual const Controller& controller() const = 0;

        /**
         * @brief Whether the controller has let go: the cursor is the user's and no input is read.
         *
         * The state @ref Bindings::release sets and @ref Bindings::engage clears, readable and
         * writable from the scene as well — so a menu opening can hand the pointer back without the
         * user pressing anything, and closing it can take it again. Unlike `Kind::eNone` the
         * controller keeps its angles, so a camera that is released and taken back resumes exactly
         * where it was.
         *
         * **A camera starts released**, so nothing takes the pointer before the user has asked it
         * to: click the scene and it is yours. `Builder::setReleased(false)` opts out.
         */
        [[nodiscard]] virtual bool released() const = 0;
        virtual void setReleased(bool released) = 0;

        // ---- the GPU copy -----------------------------------------------------------------------

        /**
         * @brief Fills one field a shader asked a camera for. Called by the engine, not by projects.
         *
         * Answers for every KOR_VIEW_*, KOR_PROJECTION_*, KOR_CAMERA_* and depth-range semantic;
         * a shader takes the ones it wants and leaves the rest. @see kor::semantics
         */
        bool serialize(std::string_view semantic, kor::SemanticSlot& slot) const override = 0;

        /** @brief "camera" — what a shader writes in `camera(VIEW_MATRIX)`. */
        [[nodiscard]] std::string_view semanticNamespace() const override { return semantics::kNamespace; }

        /** @brief The blocks this camera has been asked to fill. @see kor::SemanticBuffers */
        kor::SemanticBuffers& semanticBuffers() override = 0;
    };

    /**
     * @brief What a camera matches its aspect ratio to, so that resizing it needs no code.
     *
     * The window is the obvious answer, and the wrong one as soon as the scene is not drawn straight
     * to the screen. A scene rendered into an image and shown in a viewport has the *image's* shape,
     * which is not the window's — matching the window there stretches everything, and by exactly the
     * amount the panels around the viewport take up. So the source is named rather than assumed:
     * point the camera at whatever it renders into.
     *
     * @code
     * // a full-screen scene
     * camera->setFollowWindowAspect(true);
     *
     * // a scene rendered into a viewport's target instead
     * camera->followAspectOf(_viewportColor);        // a kor::Resource<kor::Image>
     * camera->followAspectOf(_viewportFramebuffer);  // or the framebuffer it belongs to
     * @endcode
     *
     * The reference does not own anything: a source that has been destroyed simply stops being read,
     * leaving the aspect as it last was — and says so once, since a camera that quietly stopped
     * tracking is otherwise indistinguishable from one that was never asked to.
     */
    struct AspectSource
    {
        /** @brief Which of the fields below is the one to read. */
        enum class Kind : std::uint8_t
        {
            eNone,          ///< Nothing. The aspect is whatever it was set to.
            eWindow,        ///< The window the scene is presented to. Nothing to read in a headless run.
            eFramebuffer,   ///< A framebuffer's extent.
            eImage,         ///< An image's extent, ignoring depth.
        };

        Kind kind = Kind::eNone;
        kor::ResourceRef<const kor::Framebuffer> framebuffer;   ///< Read when @ref kind is eFramebuffer.
        kor::ResourceRef<const kor::Image> image;               ///< Read when @ref kind is eImage.

        /** @brief Follows nothing. */
        [[nodiscard]] static AspectSource none() { return { }; }

        /** @brief Follows the window. */
        [[nodiscard]] static AspectSource window() { return { .kind = Kind::eWindow }; }

        /** @brief Follows a framebuffer — the usual answer for a scene rendered off-screen. */
        [[nodiscard]] static AspectSource of(kor::ResourceRef<const kor::Framebuffer> framebuffer)
        { return { .kind = Kind::eFramebuffer, .framebuffer = std::move(framebuffer) }; }

        /** @brief Follows one image, for a scene whose target is not a whole framebuffer. */
        [[nodiscard]] static AspectSource of(kor::ResourceRef<const kor::Image> image)
        { return { .kind = Kind::eImage, .image = std::move(image) }; }

        /** @brief The extent to match, or nothing when there is nothing to read. */
        [[nodiscard]] KCAM_API std::optional<glm::uvec2> extent() const;

        /**
         * @brief Whether this names a resource that cannot be read — destroyed, or poisoned.
         *
         * The difference between "following nothing" and "following something that is gone", which
         * is what makes the second one reportable. @see extent
         */
        [[nodiscard]] KCAM_API bool dangling() const;
    };

    /** @brief A camera with a perspective projection: things farther away draw smaller. */
    class KCAM_API PerspectiveCamera : public Camera
    {
    public:
        /**
         * @brief Everything a perspective camera starts from. Every field has a usable default, so
         *        `PerspectiveCamera::Builder{}.build()` is already a working camera.
         */
        struct KCAM_API Builder : ::Builder
        {
            std::string name = "camera";            ///< Label for interfaces and logs.
            float fovY = glm::radians(60.f);        ///< Vertical field of view, in radians.
            float aspect = 16.f / 9.f;              ///< Width over height. @see setFollowWindowAspect
            float zNear = 0.1f;                     ///< Near plane. Must be > 0.
            float zFar = 1000.f;                    ///< Far plane. Must be > zNear.
            glm::vec3 position = { 0.f, 0.f, 3.f }; ///< Starting position.
            glm::quat rotation = { 1.f, 0.f, 0.f, 0.f };  ///< Starting orientation. @see lookAt

            Controller controller;                  ///< What drives it each frame, if anything.
            AspectSource aspectSource;              ///< What its aspect is kept matched to, if anything.
            bool released = true;                   ///< Whether it starts with the cursor let go. @see setReleased

            /** @brief Names the camera, for interfaces, logs and resource diagnostics. */
            Builder& setName(std::string name) { this->name = std::move(name); return *this; }

            /** @brief Sets the vertical field of view, in radians. Narrower zooms in. */
            Builder& setFovY(const float fovY) { this->fovY = fovY; return *this; }

            /** @brief Sets width over height. Pointless alongside @ref setFollowWindowAspect. */
            Builder& setAspect(const float aspect) { this->aspect = aspect; return *this; }

            /** @brief Sets the depth range. Keep zNear as large as the scene allows; it is what depth precision costs. */
            Builder& setNearFar(const float zNear, const float zFar)
            { this->zNear = zNear; this->zFar = zFar; return *this; }

            /** @brief Sets where the camera starts. */
            Builder& setPosition(const glm::vec3 position) { this->position = position; return *this; }

            /** @brief Sets which way the camera starts out facing. */
            Builder& setRotation(const glm::quat rotation) { this->rotation = rotation; return *this; }

            /** @brief Points the camera at @p target from wherever @ref setPosition put it. */
            Builder& lookAt(glm::vec3 target, glm::vec3 up = { 0.f, 1.f, 0.f });

            /**
             * @brief Hands the camera to the runtime, which moves it from input every frame.
             *
             * The scene then contains no camera input handling at all. @see Controller
             */
            Builder& setController(const Controller& controller)
            { this->controller = controller; return *this; }

            /**
             * @brief Whether the camera starts having let go of the cursor. It does by default.
             *
             * Nothing should take the user's pointer before the user has asked it to, so a camera
             * begins parked: it reads no input and leaves the cursor alone until @ref Bindings::engage
             * — a left click over the scene — hands it over. Pass false for a camera that is in
             * control from the first frame, which is what a game that opens straight into play wants.
             *
             * @see Camera::released
             */
            Builder& setReleased(const bool released = true)
            { this->released = released; return *this; }

            /**
             * @brief Has the runtime keep the aspect matched to the window, across every resize.
             *
             * Applied as soon as the camera exists, so @ref setAspect is unnecessary with this on.
             * A headless run has no window; the aspect then stays as configured.
             *
             * For a scene that does not fill the window — one rendered into a viewport — follow what
             * it renders into instead. @see followAspectOf
             */
            Builder& setFollowWindowAspect(const bool follow = true)
            { aspectSource = follow ? AspectSource::window() : AspectSource::none(); return *this; }

            /** @brief Keeps the aspect matched to a framebuffer, across every resize. @see AspectSource */
            Builder& followAspectOf(kor::ResourceRef<const kor::Framebuffer> framebuffer)
            { aspectSource = AspectSource::of(std::move(framebuffer)); return *this; }

            /** @brief Keeps the aspect matched to one image. @see AspectSource */
            Builder& followAspectOf(kor::ResourceRef<const kor::Image> image)
            { aspectSource = AspectSource::of(std::move(image)); return *this; }

            /** @brief Sets what the aspect follows, whatever that is. @see AspectSource */
            Builder& setAspectSource(AspectSource source)
            { aspectSource = std::move(source); return *this; }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] kor::Result<std::unique_ptr<PerspectiveCamera>> create() const;

            /** @brief Creates the camera, poisoned rather than thrown if the configuration is impossible. */
            [[nodiscard]] kor::Resource<PerspectiveCamera> build(
                std::source_location where = std::source_location::current()) const;
        };

        [[nodiscard]] virtual float fovY() const = 0;
        [[nodiscard]] virtual float aspect() const = 0;
        [[nodiscard]] virtual float zNear() const = 0;
        [[nodiscard]] virtual float zFar() const = 0;

        virtual void setFovY(float fovY) = 0;
        virtual void setAspect(float aspect) = 0;
        virtual void setNearFar(float zNear, float zFar) = 0;

        /** @brief Turns window-aspect tracking on or off. @see Builder::setFollowWindowAspect */
        virtual void setFollowWindowAspect(bool follow) = 0;
        [[nodiscard]] virtual bool followsWindowAspect() const = 0;

        /** @brief Sets what the aspect is kept matched to, from now on. @see AspectSource */
        virtual void setAspectSource(AspectSource source) = 0;
        [[nodiscard]] virtual const AspectSource& aspectSource() const = 0;

        /** @brief Matches the aspect to a framebuffer, across every resize. @see AspectSource */
        void followAspectOf(kor::ResourceRef<const kor::Framebuffer> framebuffer)
        { setAspectSource(AspectSource::of(std::move(framebuffer))); }

        /** @brief Matches the aspect to one image. @see AspectSource */
        void followAspectOf(kor::ResourceRef<const kor::Image> image)
        { setAspectSource(AspectSource::of(std::move(image))); }
    };

    /** @brief A camera with an orthographic projection: size on screen ignores distance. */
    class KCAM_API OrthographicCamera : public Camera
    {
    public:
        /** @brief Everything an orthographic camera starts from. */
        struct KCAM_API Builder : ::Builder
        {
            std::string name = "camera";            ///< Label for interfaces and logs.
            float left = -1.f, right = 1.f;         ///< Horizontal extent of the view volume.
            float bottom = -1.f, top = 1.f;         ///< Vertical extent of the view volume.
            float zNear = 0.1f;                     ///< Near plane.
            float zFar = 1000.f;                    ///< Far plane. Must be > zNear.
            glm::vec3 position = { 0.f, 0.f, 3.f }; ///< Starting position.
            glm::quat rotation = { 1.f, 0.f, 0.f, 0.f };  ///< Starting orientation. @see lookAt

            Controller controller;                  ///< What drives it each frame, if anything.
            bool released = true;                   ///< Whether it starts with the cursor let go. @see setReleased

            /** @brief Names the camera, for interfaces, logs and resource diagnostics. */
            Builder& setName(std::string name) { this->name = std::move(name); return *this; }

            /** @brief Sets the view volume's extent. Its shape is the image's shape — match the viewport's. */
            Builder& setBounds(const float left, const float right, const float bottom, const float top)
            { this->left = left; this->right = right; this->bottom = bottom; this->top = top; return *this; }

            /** @brief Sets the depth range. */
            Builder& setNearFar(const float zNear, const float zFar)
            { this->zNear = zNear; this->zFar = zFar; return *this; }

            /** @brief Sets where the camera starts. */
            Builder& setPosition(const glm::vec3 position) { this->position = position; return *this; }

            /** @brief Sets which way the camera starts out facing. */
            Builder& setRotation(const glm::quat rotation) { this->rotation = rotation; return *this; }

            /** @brief Points the camera at @p target from wherever @ref setPosition put it. */
            Builder& lookAt(glm::vec3 target, glm::vec3 up = { 0.f, 1.f, 0.f });

            /** @brief Hands the camera to the runtime, which moves it from input every frame. */
            Builder& setController(const Controller& controller)
            { this->controller = controller; return *this; }

            /** @brief Whether it starts having let go of the cursor. It does. @see Camera::released */
            Builder& setReleased(const bool released = true)
            { this->released = released; return *this; }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] kor::Result<std::unique_ptr<OrthographicCamera>> create() const;

            /** @brief Creates the camera, poisoned rather than thrown if the configuration is impossible. */
            [[nodiscard]] kor::Resource<OrthographicCamera> build(
                std::source_location where = std::source_location::current()) const;
        };

        [[nodiscard]] virtual glm::vec4 bounds() const = 0;   ///< left, right, bottom, top.
        [[nodiscard]] virtual float zNear() const = 0;
        [[nodiscard]] virtual float zFar() const = 0;

        virtual void setBounds(float left, float right, float bottom, float top) = 0;
        virtual void setNearFar(float zNear, float zFar) = 0;
    };

}
