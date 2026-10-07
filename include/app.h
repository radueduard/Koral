//
// The application: every scene, every window, and the loop that runs them.
//

#pragma once

#include <concepts>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "api.h"
#include "context.h"
#include "error.h"
#include "scene.h"
#include "window.h"

// <windows.h> defines LoadLibrary as LoadLibraryA/W, which would rename App::LoadLibrary below in any
// file that included it first — and then no longer match the library's own. Not in this header.
#pragma push_macro("LoadLibrary")
#undef LoadLibrary

namespace kor
{
    /** @brief What the application brings up once, for every scene it will run. */
    struct AppSettings {
        API api = API::eVulkan;
        /**
         * The Linux windowing system to open windows on. WindowPlatform::eNone opens none at all —
         * only offscreen windows (OpenOffscreen) — and needs no display: a server, a test, a batch job.
         */
        WindowPlatform platform = WindowPlatform::eAuto;
        /** How many frames the CPU may run ahead of the GPU: how many copies a per-frame resource has. */
        glm::u32 framesInFlight = 2;
        /** A GPU to prefer by name (Vulkan only); empty lets the runtime pick. */
        std::string gpu {};
        /**
         * Features of the GPU the application cannot run without, and features it uses where they are there:
         * the device is made with them. Libraries ask for theirs with KORAL_REQUIRE_FEATURES. @see Feature
         */
        Flags<Feature> requiredFeatures {};
        Flags<Feature> optionalFeatures {};
        /**
         * Libraries to load before the device is made — what the runtime loads its project through — so that
         * the features they ask for are the device's. LoadLibrary them later as usual; each is loaded once.
         */
        std::vector<std::filesystem::path> libraries {};
    };

    /** @brief What makes a scene: given the arguments it is opened with. */
    using SceneFactory = std::function<std::unique_ptr<Scene>(const SceneArgs&)>;

    /**
     * @brief The application: the device and everything shared, the scenes it runs — each in its own
     *        window — and the loop that runs them.
     *
     * Usable without the runtime, from a program's own `main`, which is what makes Koral a framework
     * as well as an engine:
     *
     * @code
     * int main() {
     *     kor::App app;                                   // the device, the frames, the modules
     *     app.Register<MenuScene>("Menu");
     *     app.Register<LevelScene>("Level");
     *     app.Open("Menu", {.title = "My Game"});
     *     return app.Run();                               // until the last window closes
     * }
     * @endcode
     *
     * The runtime (Koral_Runtime) is one such program: it reads koral.json, loads the project's
     * library (LoadLibrary) and opens the scene the project names.
     *
     * One per process. Everything here runs on the thread that made it.
     */
    class KORAL_API App {
    public:
        explicit App(AppSettings settings = {});
        /** @brief Shuts every scene down, then everything the application brought up. */
        ~App();
        App(const App&) = delete;
        App& operator=(const App&) = delete;

        /** @brief The application. Throws if there is none. */
        [[nodiscard]] static App& Current();
        [[nodiscard]] static bool Exists();

        [[nodiscard]] const AppSettings& Settings() const;

        // ---- what can be opened -----------------------------------------------------------------

        /** @brief Makes @p factory what opening @p name runs. A later registration of a name replaces the earlier one. */
        void Register(std::string name, SceneFactory factory);

        /** @brief Registers @p S under @p name: made with the arguments when it takes them, default-made otherwise. */
        template<std::derived_from<Scene> S>
        void Register(std::string name) {
            Register(std::move(name), [](const SceneArgs& arguments) -> std::unique_ptr<Scene> {
                if constexpr (std::is_constructible_v<S, const SceneArgs&>) return std::make_unique<S>(arguments);
                else return std::make_unique<S>();
            });
        }

        /**
         * @brief Loads a scene library and registers every scene it offers.
         * @return The names registered. @see sceneLibrary.h
         */
        Result<std::vector<std::string>> LoadLibrary(const std::filesystem::path& path);

        /** @brief Closes every window showing one of the library's scenes, then unloads it. */
        VoidResult UnloadLibrary(const std::filesystem::path& path);

        /**
         * @brief Loads the library again — rebuilt, say — and reopens the scenes that were open from
         *        it, with the arguments and window settings they had, and each with its Scene::State()
         *        as it was.
         */
        VoidResult ReloadLibrary(const std::filesystem::path& path);

        /**
         * @brief Closes every window showing one of the scenes registered as @p names and opens it
         *        again from what is registered under that name *now* — each scene with its
         *        Scene::State() as it was. Between frames.
         *
         * What reloading scenes whose code is not a library does: scenes written in another
         * language, recompiled in-process and registered again under the same names.
         */
        VoidResult ReloadScenes(const std::vector<std::string>& names);

        /** @brief Every scene name that can be opened. */
        [[nodiscard]] std::vector<std::string> SceneNames() const;

        // ---- opening --------------------------------------------------------------------------------

        /**
         * @brief Opens the scene registered as @p name in a new window.
         * @return The scene, initialised; null (having said why) when there is no such scene or its
         *         window could not be made.
         *
         * Works from inside a frame too: the window shows from the next one.
         */
        Scene* Open(std::string_view name, const WindowSettings& window = {}, const SceneArgs& arguments = {});

        /** @brief Opens a scene the caller made, under @p name, in a new window. */
        Scene* Open(std::string name, std::unique_ptr<Scene> scene, const WindowSettings& window = {});

        /** @brief Makes a @p S from @p arguments and opens it in a new window. */
        template<std::derived_from<Scene> S, typename... Args>
        S* Open(const WindowSettings& window, Args&&... arguments) {
            return static_cast<S*>(Open(std::string(window.title), std::make_unique<S>(std::forward<Args>(arguments)...), window));
        }

        /**
         * @brief Opens the scene registered as @p name in an offscreen window: an image rather than
         *        an OS window. @see Window::IsOffscreen
         *
         * For a scene another shows — an editor's game view, a preview, a thumbnail — or one nobody
         * shows: a server, a batch render, a test. It is drawn every frame it is not paused, before
         * the scenes in OS windows, so one that shows its Window::Image() shows this frame's.
         */
        Scene* OpenOffscreen(std::string_view name, const OffscreenSettings& target = {}, const SceneArgs& arguments = {});

        /** @brief Opens a scene the caller made, under @p name, offscreen. */
        Scene* OpenOffscreen(std::string name, std::unique_ptr<Scene> scene, const OffscreenSettings& target = {});

        /** @brief Makes a @p S from @p arguments and opens it offscreen. */
        template<std::derived_from<Scene> S, typename... Args>
        S* OpenOffscreen(const OffscreenSettings& target, Args&&... arguments) {
            return static_cast<S*>(OpenOffscreen(std::string(target.title), std::make_unique<S>(std::forward<Args>(arguments)...), target));
        }

        // ---- state scenes share --------------------------------------------------------------------

        /**
         * @brief The object shared under @p key — made from @p arguments by the first to ask, and the
         *        same one for everyone after, for as long as anyone holds it.
         *
         * How scenes share state without globals: an editor and the game it runs offscreen looking at
         * one world, a menu and a level sharing the player's profile.
         *
         * @code
         * // in both scenes' Initialize
         * _world = kor::App::Current().Shared<World>("level");
         * @endcode
         *
         * Held by nobody, it goes; the next to ask makes a new one. Asking for a key as a different
         * type than it was made as throws std::logic_error.
         */
        template<typename T, typename... Args>
        std::shared_ptr<T> Shared(const std::string& key, Args&&... arguments) {
            if (auto existing = FindShared(key, typeid(T))) return std::static_pointer_cast<T>(existing);
            auto made = std::make_shared<T>(std::forward<Args>(arguments)...);
            KeepShared(key, typeid(T), made);
            return made;
        }

        /** @brief Whether something is shared under @p key and still held. */
        [[nodiscard]] bool IsShared(const std::string& key) const;

        /** @brief The scene each window shows, in the order the windows were opened. */
        [[nodiscard]] std::vector<Scene*> Scenes() const;

        /**
         * @brief Whether @p scene is open: shown, or suspended under another. A Scene* from before a
         *        reload is not — the reload opened a new scene in its place; find that in Scenes().
         */
        [[nodiscard]] bool IsOpen(const Scene* scene) const;

        // ---- running -------------------------------------------------------------------------------

        /** @brief Runs frames until no window is left, or Quit(). @return The process's exit code. */
        int Run();

        /** @brief Runs one frame of every scene. @return false once no window is left. */
        bool Frame();

        /** @brief Closes every window after the frame, which ends Run(). */
        void Quit();

        // ---- changing what a window shows (what Navigator does for the current scene) -------------
        // All of these take effect after the frame, never in the middle of one.

        void Replace(Scene& shown, std::string_view name, const SceneArgs& arguments = {});
        void Push(Scene& over, std::string_view name, const SceneArgs& arguments = {});
        void Pop(Scene& shown);
        void Close(Scene& shown);

    private:
        std::shared_ptr<void> FindShared(const std::string& key, const std::type_info& type);
        void KeepShared(const std::string& key, const std::type_info& type, std::shared_ptr<void> object);

        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    /**
     * @brief Changes what the current scene's window shows — from inside a scene.
     *
     * @code
     * if (Input::IsKeyPressed(kor::Key::eEnter)) kor::Navigator::Replace("Level", {{"map", "courtyard"}});
     * if (Input::IsKeyPressed(kor::Key::eEscape)) kor::Navigator::Push("Pause");   // Pop() from the pause menu
     * kor::Navigator::Open("Map", {.title = "Map", .extent = {512, 512}});         // another window, another scene
     * @endcode
     *
     * Replace, Push, Pop and Close take effect after the frame; the scene asking finishes the frame it
     * is in. Open is immediate.
     */
    struct KORAL_API Navigator {
        /** @brief Opens @p name in a window of its own. @see App::Open */
        static Scene* Open(std::string_view name, const WindowSettings& window = {}, const SceneArgs& arguments = {});
        /** @brief Opens @p name offscreen. @see App::OpenOffscreen */
        static Scene* OpenOffscreen(std::string_view name, const OffscreenSettings& target = {}, const SceneArgs& arguments = {});
        /** @brief Shows @p name in this window instead of the current scene, which is shut down. */
        static void Replace(std::string_view name, const SceneArgs& arguments = {});
        /** @brief Shows @p name in this window over the current scene, which is suspended until Pop. */
        static void Push(std::string_view name, const SceneArgs& arguments = {});
        /** @brief Shuts the current scene down and shows the one under it; closes the window if there is none. */
        static void Pop();
        /** @brief Closes this window, and every scene in it. */
        static void Close();
        /** @brief Closes every window. */
        static void Quit();
    };
}

#pragma pop_macro("LoadLibrary")
