//
// Created by radue on 2/21/2026.
//

#pragma once

#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "api.h"
#include "commandBuffer.h"
#include "debugDraw.h"
#include "frameGraph.h"
#include "gtime.h"
#include "input.h"
#include "reflect.h"
#include "window.h"

namespace kor
{
    class App;
    class Scene;

    namespace detail
    {
        /** @brief Whether a scene is still alive, for whoever outlives it (a coroutine, a camera). */
        struct SceneLife {
            Scene* scene = nullptr;
        };

        /**
         * @brief Makes a scene the current one on this thread for as long as it lives.
         *
         * What Scene::Current() — and so every `Window::`, `Input::` and `Time::` inside a scene —
         * answers from. The application sets one around every call into a scene; the frame graph
         * around every pass it records; a kor::Task around every resumption of a coroutine started
         * inside one.
         */
        class KORAL_API SceneScope {
        public:
            explicit SceneScope(Scene* scene);
            explicit SceneScope(const std::shared_ptr<SceneLife>& life);
            ~SceneScope();
            SceneScope(const SceneScope&) = delete;
            SceneScope& operator=(const SceneScope&) = delete;
        private:
            std::shared_ptr<SceneLife> _previous;
        };

        /** @brief The current scene's life, to carry into work that runs later or elsewhere. Null outside one. */
        [[nodiscard]] KORAL_API std::shared_ptr<SceneLife> CurrentSceneLife();

        /**
         * @brief Makes @p window the one `Window::` — and a render pass opened without a framebuffer —
         *        means on this thread, instead of the current scene's own. Null changes nothing.
         *
         * What a View's frame graph records under: its target is the window its passes draw into.
         */
        class KORAL_API WindowScope {
        public:
            explicit WindowScope(kor::Window* window);
            ~WindowScope();
            WindowScope(const WindowScope&) = delete;
            WindowScope& operator=(const WindowScope&) = delete;
        private:
            kor::Window* _previous = nullptr;
            bool _set = false;
        };

        /** @brief The window a WindowScope set on this thread, if any. */
        [[nodiscard]] KORAL_API kor::Window* CurrentWindowOverride();

        kor::Window* CurrentWindowOrNull();
    }

    /**
     * @brief Named values a scene is opened with.
     *
     * Strings underneath — so a scene can be opened by name from a menu, a command line, a config
     * file or another language — read back as what they are:
     *
     * @code
     * kor::Navigator::Replace("Level", {{"map", "courtyard"}, {"difficulty", "2"}});
     *
     * LevelScene::LevelScene(const kor::SceneArgs& args)
     *     : map(args.String("map", "default")), difficulty(args.Integer("difficulty", 1)) {}
     * @endcode
     */
    class KORAL_API SceneArgs {
    public:
        SceneArgs() = default;
        SceneArgs(std::initializer_list<std::pair<const std::string, std::string>> values) : _values(values) {}

        SceneArgs& Set(std::string key, std::string value) { _values.insert_or_assign(std::move(key), std::move(value)); return *this; }
        SceneArgs& Set(std::string key, double value);
        SceneArgs& Set(std::string key, bool value) { return Set(std::move(key), std::string(value ? "true" : "false")); }

        [[nodiscard]] bool Has(std::string_view key) const { return _values.contains(std::string(key)); }
        [[nodiscard]] std::string String(std::string_view key, std::string fallback = {}) const;
        /** @brief The value as a number; @p fallback when it is missing or is not one. */
        [[nodiscard]] double Number(std::string_view key, double fallback = 0.0) const;
        [[nodiscard]] long long Integer(std::string_view key, long long fallback = 0) const;
        /** @brief "true", "1", "yes" and "on" are true; anything else present is false. */
        [[nodiscard]] bool Flag(std::string_view key, bool fallback = false) const;

        [[nodiscard]] const std::map<std::string, std::string>& Values() const { return _values; }

    private:
        std::map<std::string, std::string> _values;
    };

    /**
     * @brief Another way of looking at a scene: a frame graph of its own, drawing into an image of its
     *        own, over the same scene state. @see Scene::AddView
     *
     * An editor's scene view and game view, a preview, a picture-in-picture, split screen: one scene
     * — one world, updated once — drawn several times. Each view's graph has its own passes and its
     * own Screen (the view's image); inside its passes `Window::` is the view's target, so its size
     * is the view's. Input and Time stay the scene's.
     *
     * @code
     * void Editor::Initialize() {
     *     auto& game = AddView("Game", {.extent = {1280, 720}});
     *     game.Graph().Add<ForwardPass>(_world, _gameCamera);
     * }
     * // and in the interface: kgui::SceneView(*FindView("Game")) shows it in a panel
     * @endcode
     *
     * Views are drawn after the scene's Update and LateUpdate and before its own Render and graph,
     * so what the scene draws — its interface included — can show this frame's view images.
     */
    class KORAL_API View {
    public:
        ~View();
        View(const View&) = delete;
        View& operator=(const View&) = delete;

        [[nodiscard]] const std::string& Name() const { return _name; }
        /** @brief The view's render passes. Their FrameGraph::Screen is Image(). */
        [[nodiscard]] kor::FrameGraph& Graph() { return _graph; }
        /** @brief The offscreen window the view draws into: its size, its framebuffer, its image. */
        [[nodiscard]] kor::Window& Target() { return *_window; }
        [[nodiscard]] const kor::Window& Target() const { return *_window; }
        /** @brief What the view drew. @see Window::Image */
        [[nodiscard]] ResourceRef<const kor::Image> Image() const;
        /** @brief Asks for a new size, taken at the start of the next frame. @see Window::Resize */
        void Resize(glm::uvec2 extent);
        /** @brief Whether it is drawn at all. A view nobody is looking at can be switched off. */
        [[nodiscard]] bool Enabled() const { return _enabled; }
        void SetEnabled(bool enabled) { _enabled = enabled; }

    private:
        friend class Scene;
        friend class App;
        View(Scene& scene, std::string name, const OffscreenSettings& target);

        std::string _name;
        std::unique_ptr<kor::Window> _window;
        kor::FrameGraph _graph;
        bool _enabled = true;
    };

    /**
     * @brief One screen of an application: what a window shows, and everything that belongs to it.
     *
     * A scene owns its window, its input, its clock and its frame graph. An application runs any number of them, each in its own window (App::Open,
     * Navigator::Open), and switches the one a window shows (Navigator::Replace, Push, Pop).
     *
     * Inside a scene, `Window::`, `Input::` and `Time::` are the scene's own — nested classes of this
     * one, which C++ finds before anything of the same name outside:
     *
     * @code
     * class MyScene final : public kor::Scene {
     *     void Initialize() override { Window::SetTitle("Hello"); }
     *     void Update() override {
     *         if (Input::IsKeyPressed(kor::Key::eEscape)) kor::Navigator::Push("Pause");
     *         angle += Time::FrameTime();
     *     }
     * };
     *
     * KORAL_SCENES(KORAL_SCENE("Main", MyScene))
     * @endcode
     *
     * They find the scene through Scene::Current(), which the application sets around every call into
     * a scene, the frame graph around every pass it records, and a kor::Task around every resumption
     * of a coroutine started in one. Code outside a scene uses SceneWindow(), SceneInput() and
     * SceneTime() on the scene itself.
     *
     * Everything happens on the application's thread, except render passes recording and coroutines
     * sent to the background pool, so a scene needs no locking of its own.
     */
    class KORAL_API Scene {
    public:
        Scene();
        /**
         * @brief Destroyed by the application after Shutdown(), once no frame in flight uses it.
         *
         * Members go with it, so releasing resources needs no code. A derived scene's members are
         * destroyed before the base's frame graph (and the passes in it).
         */
        virtual ~Scene();

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        // ---- lifecycle --------------------------------------------------------------------------

        /**
         * @brief Called once, after the scene has its window and before its first frame.
         *
         * Where resources are made: buffers, images, pipelines, passes. The constructor is too early
         * for anything that needs the window — `Window::`, `Input::` and `Time::` work from here on.
         */
        virtual void Initialize() {}

        /**
         * @brief Called at a steady rate — once per Time::FixedDeltaTime() of scene time, before
         *        Update: physics, and any simulation that must not depend on the frame rate.
         *
         * Zero times on a frame shorter than a step, several on a longer one; not at all while the
         * scene's time scale is 0. `Time::FrameTime()` is the step here. A key press lasts one frame,
         * which may have no fixed step in it: read presses in Update, and hand the step what they mean.
         */
        virtual void FixedUpdate() {}

        /** @brief Called once per frame, before Render: input, animation, cameras. */
        virtual void Update() {}

        /** @brief Called once per frame after Update and after every module's own late update. */
        virtual void LateUpdate() {}

        /**
         * @brief Called once per frame to record GPU work into the frame's command buffer.
         *
         * Do not begin, end or submit it. A scene built from render passes (Graph()) may leave this
         * empty: the passes run ahead of it.
         */
        virtual void Render(kor::CommandBuffer& commandBuffer) {}

        /** @brief Called the frame after the window's drawable area changed size. */
        virtual void OnResize(glm::uvec2 extent) {}

        /** @brief Called when another scene is pushed over this one in its window. It is neither updated nor drawn until OnResume. */
        virtual void OnSuspend() {}

        /** @brief Called when the scene pushed over this one was popped, making this one shown again. */
        virtual void OnResume() {}

        /**
         * @brief The window was asked to close — its close button, or Window::Close().
         * @return Whether to close it. False keeps it open: a scene asking to save first, say.
         */
        virtual bool OnCloseRequested() { return true; }

        /**
         * @brief The scene's state that outlives the scene: a reflected member (KORAL_REFLECT), or
         *        nothing — the default.
         *
         * What App::ReloadLibrary carries across a rebuild of the scene's library: saved (as
         * JSON, by field name) before the old scene goes and loaded into the new one after its
         * constructor, *before* Initialize — which sees it, and should not reset it. A field added
         * in the rebuild keeps its default, one removed is dropped. Also what SaveState and
         * LoadState read and write, for saving a game or an editor's play-mode snapshot.
         *
         * @code
         * struct Progress { glm::vec3 camera; int level = 1; };
         * KORAL_REFLECT(Progress, camera, level)
         *
         * class Game final : public kor::Scene {
         *     Progress _progress;
         *     kor::Ref State() override { return _progress; }
         * };
         * @endcode
         */
        virtual kor::Ref State() { return {}; }

        /**
         * @brief Called once, before the scene is destroyed: the GPU has finished with it and
         *        everything — members, frame graph, modules — is still alive.
         *
         * For stopping background work that would otherwise resume into members already gone.
         */
        virtual void Shutdown() {}

        // ---- what the scene owns ----------------------------------------------------------------

        /** @brief The scene's render passes, run every frame ahead of Render. */
        [[nodiscard]] kor::FrameGraph& Graph() { return _graph; }

        /**
         * @brief Adds a view: a frame graph drawing this scene into an image of its own. @see View
         *
         * From Initialize on (it needs the device). A view by the same name is replaced.
         */
        View& AddView(std::string name, const OffscreenSettings& target = {});
        /** @brief Removes a view, between frames. Its image goes once no frame in flight uses it. */
        void RemoveView(std::string_view name);
        /** @brief The view named @p name, or null. */
        [[nodiscard]] View* FindView(std::string_view name);
        /** @brief Every view, in the order they were added — the order they are drawn in. */
        [[nodiscard]] const std::vector<std::unique_ptr<View>>& Views() const { return _views; }

        [[nodiscard]] kor::Window& SceneWindow() const;
        [[nodiscard]] kor::Input& SceneInput() const { return *_input; }
        [[nodiscard]] kor::Time& SceneTime() { return _time; }
        /** @brief The scene's debug lines. @see Debug */
        [[nodiscard]] kor::DebugDraw& SceneDebug() { return _debug; }
        [[nodiscard]] const kor::Time& SceneTime() const { return _time; }

        /** @brief The name the scene was opened under. */
        [[nodiscard]] const std::string& Name() const { return _name; }

        /**
         * @brief The scene's state as JSON: State(), by default; "null" for a scene with none.
         *        Overridden by a scene whose state is not one reflected object — one written in
         *        another language keeps its own.
         */
        [[nodiscard]] virtual std::string SaveState();
        /** @brief Loads JSON saved by SaveState: into State(), by default. @see FromJson */
        virtual VoidResult LoadState(std::string_view json);

        /** @brief The scene whose code is running on this thread, or null outside any. */
        [[nodiscard]] static Scene* Current();

        /** @brief Whether this scene is still alive — for code holding a reference that may outlive it. */
        [[nodiscard]] std::weak_ptr<detail::SceneLife> Life() const { return _life; }

        // ---- the scene's own window, input and clock, as `Window::`, `Input::`, `Time::` ---------

        /** @brief The current scene's window. @see kor::Window */
        struct KORAL_API Window {
            [[nodiscard]] static kor::Window& Get();
            [[nodiscard]] static glm::uvec2 Extent();
            [[nodiscard]] static bool HasResized();
            [[nodiscard]] static bool IsPaused();
            [[nodiscard]] static bool IsFocused();
            [[nodiscard]] static const std::string& Title();
            static void SetTitle(const std::string& title);
            static void SetIcon(const std::filesystem::path& icon);
            /** @brief Asks the window to close; Scene::OnCloseRequested decides. */
            static void Close();
            [[nodiscard]] static kor::ResourceRef<kor::Framebuffer> DefaultFramebuffer();
        };

        /** @brief The current scene's input. @see kor::Input */
        struct KORAL_API Input {
            using CursorMode = kor::Input::CursorMode;
            [[nodiscard]] static kor::Input& Get();
            [[nodiscard]] static KeyState StateOf(Key key);
            [[nodiscard]] static KeyState MouseButtonState(MouseButton button);
            [[nodiscard]] static bool IsKeyPressed(Key key);
            [[nodiscard]] static bool IsKeyHeld(Key key);
            [[nodiscard]] static bool IsKeyReleased(Key key);
            [[nodiscard]] static bool IsMouseButtonPressed(MouseButton button);
            [[nodiscard]] static bool IsMouseButtonHeld(MouseButton button);
            [[nodiscard]] static bool IsMouseButtonReleased(MouseButton button);
            [[nodiscard]] static std::optional<Key> FirstKeyPressed();
            [[nodiscard]] static std::optional<MouseButton> FirstMouseButtonPressed();
            [[nodiscard]] static bool InterfaceWantsMouse();
            [[nodiscard]] static bool InterfaceWantsKeyboard();
            [[nodiscard]] static const glm::vec2& MousePosition();
            [[nodiscard]] static const glm::vec2& MousePositionDelta();
            [[nodiscard]] static const glm::vec2& MouseScrollDelta();
            [[nodiscard]] static const glm::vec2& LastMousePosition();
            static void SetCursorMode(CursorMode mode);
            [[nodiscard]] static CursorMode CurrentCursorMode();
            [[nodiscard]] static std::string Describe(Key key) { return kor::Input::Describe(key); }
            [[nodiscard]] static std::string Describe(MouseButton button) { return kor::Input::Describe(button); }
            [[nodiscard]] static bool IsGamepadConnected(int pad = 0);
            [[nodiscard]] static KeyState GamepadButtonState(GamepadButton button, int pad = 0);
            [[nodiscard]] static bool IsGamepadButtonPressed(GamepadButton button, int pad = 0);
            [[nodiscard]] static bool IsGamepadButtonHeld(GamepadButton button, int pad = 0);
            [[nodiscard]] static float GamepadAxisValue(GamepadAxis axis, int pad = 0);
            static void BindAction(std::string action, std::vector<InputSource> sources);
            static void BindAxis(std::string axis, std::vector<InputSource> sources);
            [[nodiscard]] static KeyState ActionState(std::string_view action);
            [[nodiscard]] static bool IsActionPressed(std::string_view action);
            [[nodiscard]] static bool IsActionHeld(std::string_view action);
            [[nodiscard]] static bool IsActionReleased(std::string_view action);
            [[nodiscard]] static float Axis(std::string_view axis);
            [[nodiscard]] static glm::vec2 Axis2D(std::string_view x, std::string_view y);
        };

        /** @brief The current scene's debug lines. @see kor::DebugDraw */
        struct KORAL_API Debug {
            using Style = kor::DebugDraw::Style;
            [[nodiscard]] static kor::DebugDraw& Get();
            static void Line(glm::vec3 from, glm::vec3 to, const Style& style = {});
            static void Box(glm::vec3 min, glm::vec3 max, const Style& style = {});
            static void Box(const glm::mat4& transform, const Style& style = {});
            static void Circle(glm::vec3 center, glm::vec3 normal, float radius, const Style& style = {});
            static void Sphere(glm::vec3 center, float radius, const Style& style = {});
            static void Arrow(glm::vec3 from, glm::vec3 to, const Style& style = {});
            static void Point(glm::vec3 position, float size = 0.1f, const Style& style = {});
            static void Axes(const glm::mat4& transform, float size = 1.f, float duration = 0.f);
            static void Grid(glm::vec3 center, float size, int cells, const Style& style = {});
            static void Frustum(const glm::mat4& viewProjection, const Style& style = {});
            static void Triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const Style& style = {});
            static void Quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const Style& style = {});
            static void Plane(glm::vec3 center, glm::vec3 normal, glm::vec2 size, const Style& style = {});
            static void Cylinder(glm::vec3 from, glm::vec3 to, float radius, const Style& style = {});
            static void Cone(glm::vec3 base, glm::vec3 tip, float radius, const Style& style = {});
            static void Capsule(glm::vec3 from, glm::vec3 to, float radius, const Style& style = {});
            static void Camera(const glm::mat4& view, const glm::mat4& projection, float size = 1.f, const Style& style = {});
            static void PointLight(glm::vec3 position, float range, const Style& style = {});
            static void SpotLight(glm::vec3 position, glm::vec3 direction, float range, float outerAngle, float innerAngle = 0.f,
                                  const Style& style = {});
            static void DirectionalLight(glm::vec3 position, glm::vec3 direction, float size = 1.f, const Style& style = {});
            /**
             * @brief kor::DebugDraw::Gizmo, used with the scene's own mouse — its left button — over its
             *        window. The pointer is the interface's while it wants the mouse and no handle is held.
             *        For a camera drawing into something else (an editor's viewport), call DebugDraw::Gizmo
             *        with a pointer of your own.
             */
            static bool Gizmo(GizmoMode mode, glm::mat4& transform, const glm::mat4& viewProjection,
                              const GizmoOptions& options = {}, std::uint64_t id = 0);
            [[nodiscard]] static bool GizmoActive();
            [[nodiscard]] static bool GizmoHovered();
        };

        /** @brief The current scene's clock. @see kor::Time */
        struct KORAL_API Time {
            [[nodiscard]] static kor::Time& Get();
            [[nodiscard]] static float FrameTime();
            [[nodiscard]] static float UnscaledFrameTime();
            [[nodiscard]] static float FixedDeltaTime();
            static void SetFixedDeltaTime(float seconds);
            [[nodiscard]] static float FixedStepFraction();
            [[nodiscard]] static float Elapsed();
            [[nodiscard]] static std::uint64_t FrameCount();
            [[nodiscard]] static float TimeScale();
            static void SetTimeScale(float scale);
        };

    private:
        friend class App;
        friend kor::Window* detail::CurrentWindowOrNull();

        std::string _name;
        kor::Window* _window = nullptr;               // the application's; shared by a window's scene stack
        std::unique_ptr<kor::Input> _input;
        kor::Time _time;
        kor::DebugDraw _debug;
        std::shared_ptr<detail::SceneLife> _life;
        kor::FrameGraph _graph;
        std::vector<std::unique_ptr<View>> _views;   // after the graph: destroyed first
    };
}
