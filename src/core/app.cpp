//
// The application: every scene, every window, and the loop that runs them.
//


#include "app.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <format>
#include <map>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <utility>

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "framebuffer.h"
#include "interface.h"
#include "log.h"
#include "module.h"
#include "sceneLibrary.h"
#include "scheduler.h"
#include "surface.h"
#include "tokenState.h"
#include "../backends/vulkan/gui.h"
#include "../backends/vulkan/surface.h"
#include "../backends/vulkan/vulkanContext.h"
#include "../executor/BackgroundExecutor.h"
#include "../executor/MainThreadExecutor.h"

// initLibs.cpp — seeds GLFW's Vulkan loader; must run before glfwInit().
void initGlfwVulkanLoader();

namespace kor
{
    namespace
    {
        App* g_app = nullptr;

        using Clock = std::chrono::steady_clock;

        // ---- shared libraries, the same on every platform ----------------------------------------

        void* openLibrary(const std::filesystem::path& path, std::string& error)
        {
#ifdef _WIN32
            HMODULE module = LoadLibraryW(path.wstring().c_str());
            if (!module) error = std::format("LoadLibrary failed ({})", GetLastError());
            return module;
#else
            void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!module) error = dlerror();
            return module;
#endif
        }

        void* symbolOf(void* library, const char* name)
        {
#ifdef _WIN32
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
            return dlsym(library, name);
#endif
        }

        void closeLibrary(void* library)
        {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(library));
#else
            dlclose(library);
#endif
        }

        std::string fileSafe(const std::string_view name)
        {
            std::string out;
            for (const char c : name) out += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
            return out;
        }
    }

    // ---- the application's state --------------------------------------------------------------------

    struct App::Impl {
        struct Library {
            std::filesystem::path path;
            void* handle = nullptr;
            std::vector<std::string> names;
        };

        /** @brief A scene the application made, and how to free it. */
        struct Hosted {
            std::string name;
            SceneArgs arguments;
            Scene* scene = nullptr;
            std::function<void(Scene*)> destroy;
            Library* library = nullptr;
        };

        struct Registration {
            std::function<Hosted(const SceneArgs&)> make;
            Library* library = nullptr;
        };

        /** @brief A window, and the scenes shown in it: the last one is the one shown. */
        struct Stage {
            std::unique_ptr<Window> window;
            WindowSettings settings;
            std::vector<Hosted> stack;
            [[nodiscard]] Scene& Top() const { return *stack.back().scene; }
        };

        struct Request {
            enum class Kind { eReplace, ePush, ePop, eClose, eQuit } kind;
            Scene* scene = nullptr;
            std::string name;
            SceneArgs arguments;
        };

        AppSettings settings;
        std::map<std::string, Registration, std::less<>> registry;
        std::vector<std::unique_ptr<Library>> libraries;
        std::vector<std::unique_ptr<Stage>> stages;
        std::vector<Request> requests;
        bool deviceUp = false;
        bool modulesUp = false;
        bool inFrame = false;
        std::optional<Clock::time_point> lastFrame;

        // ---- bringing things up ----------------------------------------------------------------------

        void StartDevice()
        {
            vk::Context::Init();
            Context::_scheduler = Scheduler::Builder().SetImageCount(std::max(settings.framesInFlight, 1u)).Build();
            if (!Context::_scheduler.Valid()) throw std::runtime_error(Context::_scheduler.Failure()->message);
            Context::_scheduler->Initialize();
            Context::_mainThreadExecutor = new MainThreadExecutor();
            Context::_backgroundExecutor = new BackgroundExecutor();
            Context::_repository = new Repository();
            deviceUp = true;
        }

        void StartModules()
        {
            if (modulesUp) return;
            // Here, at the first scene, rather than with the device: every library loaded by now has
            // registered the modules it links, and a module is ready before any scene asks it for anything.
            if (const auto resolved = ModuleHost::Resolve(); !resolved) log::Error("[app] {}", resolved.error().message);
            ModuleHost::Initialize();
            modulesUp = true;
        }

        std::unique_ptr<Window> OpenWindow(const WindowSettings& windowSettings)
        {
            std::unique_ptr<Window> window(new Window(windowSettings));
            window->_surface = Surface::Create(*window);
            dynamic_cast<vk::Surface&>(*window->_surface).CreateSwapChain(*window, Context::Scheduler().ImageCount());
            window->_framebuffer = Framebuffer::CreateDefault(*window);
            return window;
        }

        // ---- scenes -------------------------------------------------------------------------------------

        static void MakeInterfaceCurrent(const Scene& scene)
        {
            if (scene._interface) scene._interface->MakeCurrent();
            else Interface::MakeNoneCurrent();
        }

        /** @brief Gives the scene its window, input and interface, then initialises it. */
        void Host(Stage& stage, Hosted& hosted)
        {
            Scene& scene = *hosted.scene;
            scene._window = stage.window.get();
            scene._name = hosted.name;
            scene._input->AttachTo(**stage.window);
            if (scene._interfaceRequest) {
                auto interfaceSettings = *scene._interfaceRequest;
                scene._interfaceRequest.reset();
                if (interfaceSettings.iniFile.empty()) interfaceSettings.iniFile = DefaultInterfaceFile(hosted.name);
                scene._interface = std::make_unique<Interface>(scene, interfaceSettings);
            }
            detail::SceneScope scope(&scene);
            MakeInterfaceCurrent(scene);
            scene.Initialize();
        }

        [[nodiscard]] std::filesystem::path DefaultInterfaceFile(const std::string_view name) const
        {
            if (settings.interfaceDirectory.empty()) return {};
            return settings.interfaceDirectory / std::format("imgui.{}.ini", fileSafe(name));
        }

        void Destroy(Hosted& hosted)
        {
            if (!hosted.scene) return;
            {
                detail::SceneScope scope(hosted.scene);
                MakeInterfaceCurrent(*hosted.scene);
                hosted.scene->Shutdown();
                hosted.destroy(hosted.scene);
            }
            Interface::MakeNoneCurrent();
            hosted.scene = nullptr;
        }

        Hosted Make(const std::string_view name, const SceneArgs& arguments)
        {
            const auto it = registry.find(name);
            if (it == registry.end()) {
                std::string known;
                for (const auto& registered : registry | std::views::keys) known += (known.empty() ? "" : ", ") + registered;
                throw std::runtime_error(std::format("No scene is registered as '{}'. Known: {}", name,
                                                     known.empty() ? "none" : known));
            }
            Hosted hosted = it->second.make(arguments);
            hosted.name = std::string(name);
            hosted.arguments = arguments;
            hosted.library = it->second.library;
            if (!hosted.scene) throw std::runtime_error(std::format("Scene '{}' could not be made", name));
            return hosted;
        }

        Scene* OpenStage(const WindowSettings& windowSettings, Hosted hosted)
        {
            StartModules();
            auto stage = std::make_unique<Stage>();
            try {
                stage->window = OpenWindow(windowSettings);
            } catch (const std::exception& e) {
                log::Error("[app] could not open a window for '{}': {}", hosted.name, e.what());
                if (hosted.scene) hosted.destroy(hosted.scene);
                return nullptr;
            }
            stage->settings = windowSettings;
            stage->stack.push_back(std::move(hosted));
            Stage& added = *stages.emplace_back(std::move(stage));
            Host(added, added.stack.back());
            return added.stack.back().scene;
        }

        Stage* StageOf(const Scene* scene)
        {
            for (auto& stage : stages)
                for (const auto& hosted : stage->stack)
                    if (hosted.scene == scene) return stage.get();
            return nullptr;
        }

        /** @brief Every scene in the window shut down and destroyed, then the window itself retired. */
        void CloseStage(Stage& stage)
        {
            while (!stage.stack.empty()) {
                Destroy(stage.stack.back());
                stage.stack.pop_back();
            }
            RetireWindow(stage);
            std::erase_if(stages, [&](const auto& s) { return s.get() == &stage; });
        }

        void RetireWindow(Stage& stage)
        {
            if (!stage.window) return;
            std::shared_ptr<Surface> surface;
            GLFWwindow* handle = nullptr;
            stage.window->Retire(surface, handle);
            // Destroyed once no frame in flight uses it — possibly the next one, still to present it.
            if (Context::_scheduler.Valid()) Context::_scheduler->RetireWindow(std::move(surface), handle);
            else { surface.reset(); if (handle) glfwDestroyWindow(handle); }
            stage.window.reset();
        }

        // ---- what scenes asked for, applied between frames -------------------------------------------

        void ApplyRequests()
        {
            if (requests.empty()) return;
            // Scenes are about to go, and the frames in flight may still be using what they own.
            if (Context::_scheduler.Valid()) Context::_scheduler->WaitIdle();
            auto pending = std::move(requests);
            requests.clear();
            for (auto& request : pending) {
                if (request.kind == Request::Kind::eQuit) {
                    while (!stages.empty()) CloseStage(*stages.back());
                    continue;
                }
                Stage* stage = StageOf(request.scene);
                if (!stage) continue;   // already closed by an earlier request
                switch (request.kind) {
                case Request::Kind::eClose:
                    CloseStage(*stage);
                    break;
                case Request::Kind::ePop: {
                    Destroy(stage->stack.back());
                    stage->stack.pop_back();
                    if (stage->stack.empty()) { CloseStage(*stage); break; }
                    Scene& shown = stage->Top();
                    shown._input->AttachTo(**stage->window);
                    detail::SceneScope scope(&shown);
                    MakeInterfaceCurrent(shown);
                    shown.OnResume();
                    break;
                }
                case Request::Kind::eReplace:
                case Request::Kind::ePush: {
                    Hosted made;
                    try {
                        made = Make(request.name, request.arguments);
                    } catch (const std::exception& e) {
                        log::Error("[navigator] {}", e.what());
                        break;
                    }
                    if (request.kind == Request::Kind::eReplace) {
                        Destroy(stage->stack.back());
                        stage->stack.pop_back();
                    } else {
                        Scene& covered = stage->Top();
                        detail::SceneScope scope(&covered);
                        MakeInterfaceCurrent(covered);
                        covered.OnSuspend();
                    }
                    stage->stack.push_back(std::move(made));
                    Host(*stage, stage->stack.back());
                    break;
                }
                default:
                    break;
                }
            }
            Interface::MakeNoneCurrent();
        }

        void Request_(Request request)
        {
            requests.push_back(std::move(request));
        }
    };

    // ---- construction ------------------------------------------------------------------------------

    App::App(AppSettings settings) : _impl(std::make_unique<Impl>())
    {
        if (g_app) throw std::logic_error("There is already a kor::App: one per process.");
        _impl->settings = std::move(settings);
        const auto& s = _impl->settings;
        Context::_activeAPI = s.api;
        if (!s.gpu.empty()) SetPreferredGpu(s.gpu);

        // The windowing platform (X11 or Wayland) is an init hint, so it is chosen before glfwInit.
        int platform = GLFW_ANY_PLATFORM;
        switch (s.platform) {
            case WindowPlatform::eX11:     platform = GLFW_PLATFORM_X11;     break;
            case WindowPlatform::eWayland: platform = GLFW_PLATFORM_WAYLAND; break;
            case WindowPlatform::eAuto:    break;
        }
        if (platform != GLFW_ANY_PLATFORM) {
            if (glfwPlatformSupported(platform)) glfwInitHint(GLFW_PLATFORM, platform);
            else log::Warn("[app] the requested windowing platform is not available; using automatic selection");
        }
        // Point GLFW's Vulkan support at the loader Koral ships, before glfwInit snapshots it.
        initGlfwVulkanLoader();

        if (glfwInit() != GLFW_TRUE) {
            const char* description = nullptr;
            glfwGetError(&description);
            throw std::runtime_error(std::string("GLFW could not initialize: ") + (description ? description : "no reason given"));
        }
        g_app = this;

        // The device comes up before any window: a scene's window is made against it.
        try {
            _impl->StartDevice();
        } catch (...) {
            g_app = nullptr;
            glfwTerminate();
            throw;
        }
    }

    App::~App()
    {
        auto& impl = *_impl;
        if (Context::_scheduler.Valid()) Context::_scheduler->WaitIdle();

        // Every scene, while everything it may use is still up.
        std::vector<std::unique_ptr<Window>> windows;
        for (auto& stage : impl.stages) {
            while (!stage->stack.empty()) {
                impl.Destroy(stage->stack.back());
                stage->stack.pop_back();
            }
            windows.push_back(std::move(stage->window));
        }
        impl.stages.clear();
        detail::collectRetired(/*all=*/true);

        if (impl.modulesUp) ModuleHost::Shutdown();
        windows.clear();
        if (impl.deviceUp) vk::GUI::ReleaseShared();
        Context::_scheduler.Reset();
        if (impl.deviceUp) vk::Context::StopTokens();
        delete Context::_repository;
        delete Context::_mainThreadExecutor;
        delete Context::_backgroundExecutor;
        Context::_repository = nullptr;
        Context::_mainThreadExecutor = nullptr;
        Context::_backgroundExecutor = nullptr;
        if (impl.deviceUp) vk::Context::Destroy();
        glfwTerminate();
        // The libraries stay loaded: the process is ending, and static destructors in them may still
        // reach into what they linked.
        g_app = nullptr;
    }

    App& App::Current()
    {
        if (!g_app) throw std::logic_error("There is no kor::App");
        return *g_app;
    }

    bool App::Exists() { return g_app != nullptr; }

    const AppSettings& App::Settings() const { return _impl->settings; }

    // ---- registry -------------------------------------------------------------------------------------

    void App::Register(std::string name, SceneFactory factory)
    {
        _impl->registry.insert_or_assign(std::move(name), Impl::Registration{
            .make = [factory = std::move(factory)](const SceneArgs& arguments) {
                return Impl::Hosted{.scene = factory(arguments).release(),
                                    .destroy = [](Scene* scene) { delete scene; }};
            }});
    }

    std::vector<std::string> App::SceneNames() const
    {
        std::vector<std::string> names;
        for (const auto& name : _impl->registry | std::views::keys) names.push_back(name);
        return names;
    }

    Result<std::vector<std::string>> App::LoadLibrary(const std::filesystem::path& path)
    {
        auto& impl = *_impl;
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (std::ranges::any_of(impl.libraries, [&](const auto& l) { return l->path == canonical; }))
            return std::unexpected(Error{.code = ErrorCode::eInvalidArgument,
                                         .message = std::format("'{}' is already loaded; ReloadLibrary loads it again", path.string())});
        if (!std::filesystem::exists(canonical))
            return std::unexpected(Error{.code = ErrorCode::eFileNotReadable, .message = std::format("'{}' does not exist", path.string())});

        std::string error;
        void* handle = openLibrary(canonical, error);
        if (!handle)
            return std::unexpected(Error{.code = ErrorCode::eModuleLoadFailed,
                                         .message = std::format("could not load '{}': {}", path.string(), error)});

        auto library = std::make_unique<Impl::Library>(Impl::Library{.path = canonical, .handle = handle});
        Impl::Library* owner = library.get();
        std::vector<std::string> names;

        if (const auto getScenes = reinterpret_cast<KoralGetScenesFn>(symbolOf(handle, "KoralGetScenes"))) {
            const KoralSceneTable* table = getScenes();
            if (!table || table->abiVersion != KORAL_SCENE_ABI_VERSION) {
                closeLibrary(handle);
                return std::unexpected(Error{.code = ErrorCode::eModuleLoadFailed, .message = std::format(
                    "'{}' was built against scene interface version {}, and this runtime speaks {}; rebuild it",
                    path.string(), table ? table->abiVersion : 0u, KORAL_SCENE_ABI_VERSION)});
            }
            for (std::uint32_t i = 0; i < table->count; ++i) {
                const KoralSceneEntry entry = table->entries[i];
                if (!entry.name || !entry.create || !entry.destroy) continue;
                names.emplace_back(entry.name);
                impl.registry.insert_or_assign(std::string(entry.name), Impl::Registration{
                    .make = [entry](const SceneArgs& arguments) {
                        return Impl::Hosted{.scene = entry.create(&arguments), .destroy = entry.destroy};
                    },
                    .library = owner});
            }
        } else if (const auto createScene = reinterpret_cast<Scene* (*)()>(symbolOf(handle, "CreateScene"))) {
            // The older single-scene library: one scene, named after the file.
            names.push_back(canonical.stem().string());
            impl.registry.insert_or_assign(names.back(), Impl::Registration{
                .make = [createScene](const SceneArgs&) {
                    return Impl::Hosted{.scene = createScene(), .destroy = [](Scene* scene) { delete scene; }};
                },
                .library = owner});
        } else {
            closeLibrary(handle);
            return std::unexpected(Error{.code = ErrorCode::eModuleLoadFailed, .message = std::format(
                "'{}' exports no scenes: neither KoralGetScenes (KORAL_SCENES) nor CreateScene", path.string())});
        }

        library->names = names;
        impl.libraries.push_back(std::move(library));
        return names;
    }

    VoidResult App::UnloadLibrary(const std::filesystem::path& path)
    {
        auto& impl = *_impl;
        if (impl.inFrame)
            return std::unexpected(Error{.code = ErrorCode::eInvalidArgument, .message = "UnloadLibrary is for between frames"});
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        const auto it = std::ranges::find_if(impl.libraries, [&](const auto& l) { return l->path == canonical; });
        if (it == impl.libraries.end())
            return std::unexpected(Error{.code = ErrorCode::eInvalidArgument, .message = std::format("'{}' is not loaded", path.string())});
        Impl::Library* library = it->get();

        if (Context::_scheduler.Valid()) Context::_scheduler->WaitIdle();
        // Every window showing one of its scenes goes, whole: a stack cannot be left with a hole in it.
        for (bool closed = true; closed;) {
            closed = false;
            for (auto& stage : impl.stages) {
                if (std::ranges::any_of(stage->stack, [&](const auto& h) { return h.library == library; })) {
                    impl.CloseStage(*stage);
                    closed = true;
                    break;
                }
            }
        }
        if (Context::_scheduler.Valid()) Context::_scheduler->WaitIdle();
        detail::collectRetired(/*all=*/true);
        std::erase_if(impl.registry, [&](const auto& entry) { return entry.second.library == library; });
        closeLibrary(library->handle);
        impl.libraries.erase(it);
        return {};
    }

    VoidResult App::ReloadLibrary(const std::filesystem::path& path)
    {
        auto& impl = *_impl;
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(path, ec);
        const auto it = std::ranges::find_if(impl.libraries, [&](const auto& l) { return l->path == canonical; });
        if (it == impl.libraries.end()) return LoadLibrary(path).transform([](auto&&) {});

        // What to open again: each affected window's settings (at its current size) and its stack.
        struct Reopen {
            WindowSettings settings;
            std::vector<std::pair<std::string, SceneArgs>> stack;
        };
        std::vector<Reopen> reopen;
        for (const auto& stage : impl.stages) {
            if (!std::ranges::any_of(stage->stack, [&](const auto& h) { return h.library == it->get(); })) continue;
            Reopen entry{.settings = stage->settings};
            entry.settings.extent = stage->window->Extent();
            entry.settings.title = stage->window->Title();
            for (const auto& hosted : stage->stack) entry.stack.emplace_back(hosted.name, hosted.arguments);
            reopen.push_back(std::move(entry));
        }

        if (const auto unloaded = UnloadLibrary(path); !unloaded) return unloaded;
        if (const auto loaded = LoadLibrary(path); !loaded) return std::unexpected(loaded.error());

        for (const auto& [settings, stack] : reopen) {
            if (stack.empty()) continue;
            Scene* bottom = nullptr;
            try {
                bottom = impl.OpenStage(settings, impl.Make(stack.front().first, stack.front().second));
            } catch (const std::exception& e) {
                log::Error("[app] reopening '{}': {}", stack.front().first, e.what());
            }
            Impl::Stage* stage = bottom ? impl.StageOf(bottom) : nullptr;
            for (std::size_t i = 1; stage && i < stack.size(); ++i) {
                try {
                    Scene& covered = stage->Top();
                    { detail::SceneScope scope(&covered); covered.OnSuspend(); }
                    stage->stack.push_back(impl.Make(stack[i].first, stack[i].second));
                    impl.Host(*stage, stage->stack.back());
                } catch (const std::exception& e) {
                    log::Error("[app] reopening '{}': {}", stack[i].first, e.what());
                    break;
                }
            }
        }
        Interface::MakeNoneCurrent();
        return {};
    }

    // ---- opening --------------------------------------------------------------------------------------

    Scene* App::Open(const std::string_view name, const WindowSettings& window, const SceneArgs& arguments)
    {
        auto& impl = *_impl;
        Impl::Hosted hosted;
        try {
            hosted = impl.Make(name, arguments);
        } catch (const std::exception& e) {
            log::Error("[app] {}", e.what());
            return nullptr;
        }
        return impl.OpenStage(window, std::move(hosted));
    }

    Scene* App::Open(std::string name, std::unique_ptr<Scene> scene, const WindowSettings& window)
    {
        if (!scene) return nullptr;
        return _impl->OpenStage(window, Impl::Hosted{.name = std::move(name), .scene = scene.release(),
                                                     .destroy = [](Scene* s) { delete s; }});
    }

    std::vector<Scene*> App::Scenes() const
    {
        std::vector<Scene*> scenes;
        for (const auto& stage : _impl->stages)
            if (!stage->stack.empty()) scenes.push_back(&stage->Top());
        return scenes;
    }

    // ---- running --------------------------------------------------------------------------------------

    int App::Run()
    {
        while (Frame()) {}
        return EXIT_SUCCESS;
    }

    void App::Quit() { _impl->Request_({.kind = Impl::Request::Kind::eQuit}); }

    bool App::Frame()
    {
        auto& impl = *_impl;
        glfwPollEvents();
        if (impl.deviceUp) Context::DrainMainThread();

        // A window asked to close: its scene decides.
        for (const auto& stage : impl.stages) {
            if (!stage->window->ShouldClose()) continue;
            Scene& shown = stage->Top();
            bool close = true;
            {
                detail::SceneScope scope(&shown);
                Impl::MakeInterfaceCurrent(shown);
                close = shown.OnCloseRequested();
            }
            if (close) {
                impl.Request_({.kind = Impl::Request::Kind::eClose, .scene = &shown});
            } else {
                glfwSetWindowShouldClose(**stage->window, GLFW_FALSE);
                stage->window->_closeRequested = false;
            }
        }
        impl.ApplyRequests();
        if (impl.stages.empty()) return false;

        // The frame's clock: one real delta, which each scene scales by its own time scale.
        const auto now = Clock::now();
        const float frameTime = impl.lastFrame ? std::chrono::duration<float>(now - *impl.lastFrame).count() : 0.f;
        impl.lastFrame = now;

        std::vector<Impl::Stage*> active;
        std::vector<Window*> windows;
        for (const auto& stage : impl.stages) {
            stage->Top()._time.Advance(frameTime);
            stage->window->_shownThisFrame = false;   // until the scheduler gives it an image
            if (stage->window->IsPaused()) continue;
            active.push_back(stage.get());
            windows.push_back(stage->window.get());
        }

        if (active.empty()) {
            // Every window minimized: nothing to draw, and no reason to spin.
            for (const auto& stage : impl.stages) stage->Top()._input->Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return true;
        }

        impl.inFrame = true;
        Context::Scheduler().Draw(windows, [&](CommandBuffer& commandBuffer) {
            Context::Repository().Update();
            for (Impl::Stage* stage : active) {
                Window& window = *stage->window;
                if (!window.IsShownThisFrame()) continue;   // nothing to draw into this frame
                Scene& scene = stage->Top();
                detail::SceneScope scope(&scene);
                Impl::MakeInterfaceCurrent(scene);

                if (window.HasResized()) {
                    // Modules first, so anything the scene reads from one in its own OnResize — a
                    // camera's projection, say — already reflects the new size.
                    ModuleHost::OnResize(window.Extent());
                    scene.OnResize(window.Extent());
                }
                // The fixed steps the scene's time has moved on by, ahead of the frame's own update.
                for (std::uint32_t steps = scene._time.TakeFixedSteps(); steps > 0; --steps) {
                    scene._time._inFixedStep = true;
                    ModuleHost::FixedUpdate();
                    scene.FixedUpdate();
                    scene._time._inFixedStep = false;
                }

                // The fixed order every module is written against: modules move things, the scene
                // reacts, modules settle what the scene changed, the scene sees the settled state.
                ModuleHost::Update();
                scene.Update();
                ModuleHost::LateUpdate();
                scene.LateUpdate();

                ModuleHost::Render(commandBuffer);
                scene.Render(commandBuffer);
                // The scene's render passes, recorded in parallel and run ahead of this command buffer.
                const bool graphTouchedScreen = scene.Graph().Execute();
                ModuleHost::RenderOverlay(commandBuffer);

                // A window nothing drew into is cleared to its framebuffer's colour, rather than showing
                // whatever its swap-chain image held — before the interface, which would otherwise be
                // wiped: for a scene that shows everything through its interface, it is all there is.
                if (const auto framebuffer = window.DefaultFramebuffer();
                    framebuffer.Valid() && !framebuffer->ColorAttachments().empty()) {
                    if (const auto screen = framebuffer->ColorImage(0);
                        !commandBuffer.HasTouched(screen) && !graphTouchedScreen
                        && !Context::Scheduler().QueuedWorkTouches(screen)) {
                        commandBuffer.BeginRendering(RenderInfo(framebuffer));
                        commandBuffer.EndRendering();
                    }
                }

                if (scene._interface) scene._interface->Render(commandBuffer);
            }
        });
        impl.inFrame = false;

        // After the frame's submission: the panels floating in windows of their own submit command
        // buffers of their own, and those must follow the frame's.
        for (Impl::Stage* stage : active) {
            Scene& scene = stage->Top();
            if (!scene._interface || !stage->window->IsShownThisFrame()) continue;
            detail::SceneScope scope(&scene);
            scene._interface->RenderPlatformWindows();
        }
        Interface::MakeNoneCurrent();

        for (const auto& stage : impl.stages) {
            stage->Top()._input->Update();
            stage->window->LateUpdate();
        }
        return true;
    }

    // ---- navigation ---------------------------------------------------------------------------------

    void App::Replace(Scene& shown, const std::string_view name, const SceneArgs& arguments)
    {
        _impl->Request_({.kind = Impl::Request::Kind::eReplace, .scene = &shown, .name = std::string(name), .arguments = arguments});
    }

    void App::Push(Scene& over, const std::string_view name, const SceneArgs& arguments)
    {
        _impl->Request_({.kind = Impl::Request::Kind::ePush, .scene = &over, .name = std::string(name), .arguments = arguments});
    }

    void App::Pop(Scene& shown) { _impl->Request_({.kind = Impl::Request::Kind::ePop, .scene = &shown}); }
    void App::Close(Scene& shown) { _impl->Request_({.kind = Impl::Request::Kind::eClose, .scene = &shown}); }

    namespace
    {
        Scene& currentFor(const char* what)
        {
            Scene* scene = Scene::Current();
            if (!scene) throw std::logic_error(std::format("Navigator::{} with no scene current: call it from a scene", what));
            return *scene;
        }
    }

    Scene* Navigator::Open(const std::string_view name, const WindowSettings& window, const SceneArgs& arguments)
    {
        return App::Current().Open(name, window, arguments);
    }

    void Navigator::Replace(const std::string_view name, const SceneArgs& arguments)
    {
        App::Current().Replace(currentFor("Replace"), name, arguments);
    }

    void Navigator::Push(const std::string_view name, const SceneArgs& arguments)
    {
        App::Current().Push(currentFor("Push"), name, arguments);
    }

    void Navigator::Pop() { App::Current().Pop(currentFor("Pop")); }
    void Navigator::Close() { App::Current().Close(currentFor("Close")); }
    void Navigator::Quit() { App::Current().Quit(); }
}
