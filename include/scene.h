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

#include <glm/glm.hpp>

#include "api.h"
#include "commandBuffer.h"
#include "frameGraph.h"
#include "gtime.h"
#include "input.h"
#include "window.h"

namespace kor
{
    class App;
    class Interface;
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

    /** @brief How a scene's interface is set up. @see Scene::EnableInterface */
    struct InterfaceSettings {
        /** Where the layout is saved; empty keeps it in memory only. */
        std::filesystem::path iniFile {};
        /** Panels may be dragged out into windows of their own (not under Wayland, which cannot place them). */
        bool viewports = true;
        /** Panels may be docked into one another and into the window. */
        bool docking = true;
    };

    /**
     * @brief One screen of an application: what a window shows, and everything that belongs to it.
     *
     * A scene owns its window, its input, its clock, its frame graph and — if it asks for one — its
     * interface. An application runs any number of them, each in its own window (App::Open,
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

        /**
         * @brief Called once per frame to build the scene's interface — only when it has one
         *        (EnableInterface). Include <gui.h> to call Dear ImGui.
         */
        virtual void RenderUI() {}

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
         * @brief Called once, before the scene is destroyed: the GPU has finished with it and
         *        everything — members, frame graph, modules — is still alive.
         *
         * For stopping background work that would otherwise resume into members already gone.
         */
        virtual void Shutdown() {}

        // ---- what the scene owns ----------------------------------------------------------------

        /** @brief The scene's render passes, run every frame ahead of Render. */
        [[nodiscard]] kor::FrameGraph& Graph() { return _graph; }

        [[nodiscard]] kor::Window& SceneWindow() const;
        [[nodiscard]] kor::Input& SceneInput() const { return *_input; }
        [[nodiscard]] kor::Time& SceneTime() { return _time; }
        [[nodiscard]] const kor::Time& SceneTime() const { return _time; }

        /** @brief The name the scene was opened under. */
        [[nodiscard]] const std::string& Name() const { return _name; }

        /** @brief Whether the scene has an interface. @see EnableInterface */
        [[nodiscard]] bool HasInterface() const { return _interface != nullptr; }

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

    protected:
        /**
         * @brief Gives the scene an interface: a Dear ImGui context of its own, drawn over its window,
         *        with RenderUI() called every frame.
         *
         * Opt-in: a scene that never calls this has no ImGui at all, and needs none to build. Call it
         * from the constructor or from Initialize. A scene library calling ImGui includes <gui.h>,
         * which is what hands it the engine's ImGui.
         */
        void EnableInterface(InterfaceSettings settings = {});

    private:
        friend class App;
        friend class Interface;
        friend kor::Window* detail::CurrentWindowOrNull();

        std::string _name;
        kor::Window* _window = nullptr;               // the application's; shared by a window's scene stack
        std::unique_ptr<kor::Input> _input;
        kor::Time _time;
        std::unique_ptr<Interface> _interface;
        std::optional<InterfaceSettings> _interfaceRequest;
        std::shared_ptr<detail::SceneLife> _life;
        kor::FrameGraph _graph;
    };
}
