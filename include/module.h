//
// Created by radue on 28.07.2026.
//

/**
 * @file module.h
 * @brief Optional, reusable pieces of a project, loaded from shared libraries at startup.
 *
 * A module is a shared library loaded before the scene runs. It has two faces, and keeping them
 * apart is the whole idea:
 *
 *   - **Runtime-facing** — the virtual lifecycle below (Initialize, Update, Render, ...). Only the
 *     runtime calls these, and only through a kor::Module*. A module may declare them private,
 *     which puts the split beyond a consumer's reach entirely.
 *   - **Consumer-facing** — whatever the module publishes in its own header for a scene, or another
 *     module, to call.
 *
 * The consumer-facing half is an ordinary library API — classes with builders, free functions —
 * published from the module's header and *linked* by whoever uses it. There is no service object to
 * find and no handle to thread through a scene:
 *
 * @code
 * // in the module's public header, exported with the module's own API macro
 * class KCAM_API PerspectiveCamera : public kor::AutoUpdatable {
 * public:
 *     struct KCAM_API Builder : kor::Builder { ... };
 * };
 *
 * // in the module's implementation, once, at namespace scope
 * KORAL_DECLARE_MODULE(CameraModule)
 *
 * // in a scene, which links the module like any other library
 * _camera = kcam::PerspectiveCamera::Builder{}.setFovY(glm::radians(70.f)).build();
 * @endcode
 *
 * @section module_loading How a module gets into the process
 *
 * Two ways, and they meet in the same registry:
 *
 *  - **By linking.** A scene that links the module pulls the library in as a dependency, and
 *    @ref KORAL_DECLARE_MODULE's registrar hands the module to the runtime as the library loads.
 *    Nothing has to be configured — using a module's API *is* asking for the module.
 *  - **By name.** Modules listed under `"modules"` in koral.json are loaded by the runtime itself.
 *    This is for a module nothing links against — one that only observes, or whose whole job is a
 *    lifecycle hook. @see ProjectConfig
 *
 * Both are keyed on the module id, so a module that is linked *and* listed is loaded once. A
 * header-only module needs neither: there is no library, and nothing to dispatch.
 *
 * @section module_abi Crossing the boundary
 *
 * A module is a separate binary, so what crosses between it and its consumers has to be either
 * exported or plain: functions and classes marked with the module's export macro, plain structs,
 * and kor::Resource / kor::ResourceRef. Koral builds with hidden visibility, so a type whose
 * methods a consumer calls — or whose identity it relies on for `dynamic_cast` — has to say so.
 *
 * A module *may* link another module, exactly as it links Koral; its consumers then load both. The
 * dependency is also declared in the descriptor (@ref KORAL_DECLARE_MODULE_DEPS), which is what
 * orders their lifecycle hooks and what turns a missing module into a startup error naming both
 * sides rather than a link failure.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "api.h"
#include "error.h"

/**
 * @brief Marks a module's two entry points as exported.
 *
 * Distinct from KORAL_API, which is *imported* by anything that is not Koral itself. A module has
 * to export its own symbols, so it needs the export half unconditionally.
 */
#if defined(_WIN32)
#  define KORAL_MODULE_EXPORT __declspec(dllexport)
#else
#  define KORAL_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace kor
{
    class CommandBuffer;

    /**
     * @brief The runtime-facing half of a module: one frame's worth of hooks.
     *
     * Every hook is optional. Their positions relative to the scene are fixed — there are no
     * priorities to tune — and modules run among themselves in dependency order, so a module always
     * runs after the modules it depends on.
     *
     * A concrete module is free to declare these `private`. Virtual dispatch still reaches them
     * from the runtime, while a scene holding the module's *public* interface cannot call them,
     * which turns the two-surface split into something the compiler enforces.
     */
    class KORAL_API Module
    {
    public:
        virtual ~Module() = default;

        /**
         * @brief Called once, after every module exists and the graphics device is up, before the
         *        scene's Initialize().
         *
         * Every other module is already constructed and findable by the time this runs, so a module
         * may look up the ones it depends on here. Resources may be created; the device exists.
         */
        virtual void Initialize() {}

        /** @brief Called once per frame, before Scene::Update. Where input-driven work belongs. */
        virtual void Update() {}

        /**
         * @brief Called once per frame, after Scene::Update.
         *
         * Where work that must observe everything the scene did this frame belongs — recomputing
         * derived state, uploading it to the GPU.
         */
        virtual void LateUpdate() {}

        /** @brief Called once per frame, before Scene::Render, with the frame's command buffer. */
        virtual void Render(CommandBuffer& commandBuffer) {}

        /** @brief Called once per frame, after Scene::Render, with the frame's command buffer. */
        virtual void RenderOverlay(CommandBuffer& commandBuffer) {}

        /** @brief Called once per frame, after Scene::RenderUI. Call ImGui functions directly. */
        virtual void RenderUI() {}

        /** @brief Called when the window's drawable area changed size, before Scene::OnResize. */
        virtual void OnResize(glm::uvec2 extent) {}

        /**
         * @brief Called once, before the module is destroyed and before the device goes away.
         *
         * Modules shut down in reverse dependency order, so a module's dependencies are still alive
         * here. Resources held as members need no explicit release.
         */
        virtual void Shutdown() {}
    };

    /** @brief One module's declared need for another. */
    struct Dependency
    {
        /** @brief Whether the dependent module can run without it. */
        enum class Kind : std::uint8_t
        {
            eRequired,  ///< Startup fails, naming both modules, if it is missing.
            eOptional,  ///< Absence is fine; the dependent module copes without it.
        };

        std::string_view id;                ///< The other module's ModuleId.
        std::uint32_t version = 1;          ///< The other module's ModuleVersion, as compiled against.
        Kind kind = Kind::eRequired;        ///< Whether it may be absent.
    };

    /**
     * @brief What a module says about itself, read before it is instantiated.
     *
     * Deliberately a plain aggregate of scalars and a pointer/count pair rather than anything with
     * a container in it: the runtime reads this out of a freshly loaded library, so its layout has
     * to be something both sides agree on without sharing an allocator.
     */
    struct ModuleDescriptor
    {
        std::string_view id;                        ///< Unique identifier, e.g. "koral.camera".
        std::uint32_t version = 1;                  ///< Bumped whenever the public interface changes.
        const Dependency* dependencies = nullptr;   ///< The modules this one needs.
        std::size_t dependencyCount = 0;
    };

    /** @brief The symbol a module library exports to describe itself: `korModuleDescriptor`. */
    using ModuleDescriptorFn = const ModuleDescriptor* (*)();

    /** @brief The symbol a module library exports to construct itself: `korCreateModule`. */
    using CreateModuleFn = Module* (*)();

    /**
     * @brief Loads modules, orders them, and drives their lifecycle. The runtime's business.
     *
     * Of interest only when embedding Koral without the runtime; a project never calls any of this.
     * The order matters and is not negotiable:
     *
     *   1. Load() — the modules koral.json names, before the device exists.
     *   2. the scene (or job) library is loaded, which is when every module it *links* registers
     *      itself, and so has to happen before...
     *   3. Resolve() — orders everything registered by either route and constructs it, still
     *      before the device, so a module's constructor can influence how the device is created.
     *   4. Initialize() — once there is a device to build resources against.
     */
    class ModuleHost
    {
    public:
        /**
         * @brief Registers a module with the runtime. Called by @ref KORAL_DECLARE_MODULE's
         *        registrar as the module's library is loaded; never called by hand.
         * @param descriptor The module's descriptor, which must outlive the process (it is a
         *        constant in the module's own image).
         * @param create The module's factory.
         *
         * Registering the same module twice — once because a scene links it, once because
         * koral.json also names it — is the normal case and keeps the first registration. A
         * *different* library claiming an id that is already taken is an error, and is reported
         * when the modules are resolved.
         */
        static KORAL_API void Register(const ModuleDescriptor* descriptor, CreateModuleFn create);

        /**
         * @brief Loads every module named in @p modules, resolving each against @p searchDirectories.
         * @param modules Module names or paths, as they appear under "modules" in koral.json.
         * @param searchDirectories Where to look for a name that is not already a path, most
         *        specific first. Koral's own module directory is appended to these.
         * @return The first failure, or success. A module that cannot be found stops startup rather
         *         than failing later in a frame.
         *
         * Only for modules nothing links against: a linked module is already registered by the time
         * this runs, and naming it here as well changes nothing.
         */
        static KORAL_API VoidResult Load(std::span<const std::string> modules,
                                         std::span<const std::filesystem::path> searchDirectories);

        /**
         * @brief Orders every registered module by its declared dependencies and constructs them.
         * @return The first failure — a dependency cycle, a missing required module, a version
         *         mismatch or an id claimed twice — or success.
         *
         * Must run after the scene library is loaded (that is what registers the modules the scene
         * links) and before the device is created. Every module is constructed before any of them
         * is initialized, so a module's Initialize() may reach any other module.
         */
        static KORAL_API VoidResult Resolve();

        /**
         * @brief Calls Initialize() on every module, in dependency order.
         *
         * Resolves first if that has not happened yet, logging rather than returning any failure —
         * an embedder that skipped Resolve() still gets working modules, but the runtime calls it
         * itself so that a bad module set fails startup with a proper exit code.
         */
        static KORAL_API void Initialize();

        /** @brief Calls Shutdown() on every module and destroys them, in reverse dependency order. */
        static KORAL_API void Shutdown();

        /** @brief Names of the loaded modules, in dependency order. For diagnostics and the GUI. */
        [[nodiscard]] static KORAL_API std::vector<std::string_view> loadedModules();

        // ---- per-frame dispatch, in the order the run loop calls them ----
        static KORAL_API void Update();
        static KORAL_API void LateUpdate();
        static KORAL_API void Render(CommandBuffer& commandBuffer);
        static KORAL_API void RenderOverlay(CommandBuffer& commandBuffer);
        static KORAL_API void RenderUI();
        static KORAL_API void OnResize(glm::uvec2 extent);
    };

    /**
     * @brief A namespace-scope object whose construction registers a module. @see KORAL_DECLARE_MODULE
     *
     * This is the whole mechanism behind "linking a module is asking for it": one of these lives in
     * the module's library, so the module announces itself when the dynamic loader runs that
     * library's initializers — which happens when whatever links it is loaded.
     */
    struct ModuleRegistrar
    {
        ModuleRegistrar(const ModuleDescriptor* descriptor, const CreateModuleFn create)
        {
            ModuleHost::Register(descriptor, create);
        }
    };
}

/**
 * @brief Declares a module: how it announces itself, and how the runtime constructs it.
 * @param ImplType The concrete class, which must derive from kor::Module and carry
 *        `ModuleId` / `ModuleVersion` (inherited from a base is fine).
 *
 * Expands to three things: a descriptor, a registrar that hands both to the runtime when the
 * library is loaded, and the pair of exported entry points the runtime uses when it loads a module
 * *by name* instead. Write it once, at namespace scope, in one of the module's translation units.
 * Use @ref KORAL_DECLARE_MODULE_DEPS when the module depends on others.
 */
#define KORAL_DECLARE_MODULE(ImplType)                                                  \
    KORAL_DECLARE_MODULE_DEPS(ImplType, kor::Dependency{})

/**
 * @brief Declares a module, listing the modules it depends on. @see KORAL_DECLARE_MODULE
 * @param ImplType The concrete class.
 * @param ... One kor::Dependency per module needed, each naming that module's id and version, and
 *        whether it is required. A module's public header publishes those two constants precisely
 *        so that a dependent can name them here.
 *
 * @code
 * KORAL_DECLARE_MODULE_DEPS(ECSModule,
 *     kor::Dependency{ kcam::ModuleId,  kcam::ModuleVersion },
 *     kor::Dependency{ kphys::ModuleId, kphys::ModuleVersion, kor::Dependency::Kind::eOptional })
 * @endcode
 */
#define KORAL_DECLARE_MODULE_DEPS(ImplType, ...)                                        \
    namespace {                                                                         \
        /* An empty id marks the padding entry KORAL_DECLARE_MODULE passes, since a  */ \
        /* zero-length array is not a thing; the loader skips those.                 */ \
        constexpr kor::Dependency korModuleDependencies_[] { __VA_ARGS__ };             \
        constexpr kor::ModuleDescriptor korModuleDescriptor_ {                          \
            ImplType::ModuleId,                                                        \
            ImplType::ModuleVersion,                                                   \
            korModuleDependencies_,                                                     \
            sizeof(korModuleDependencies_) / sizeof(kor::Dependency),                   \
        };                                                                              \
        kor::Module* korCreateModuleImpl_() { return new ImplType(); }                  \
        /* Constant-initialized descriptor, dynamically-initialized registrar: the   */ \
        /* descriptor is therefore complete before anything can observe it, whatever */ \
        /* order the library's initializers run in.                                  */ \
        const kor::ModuleRegistrar korModuleRegistrar_ {                                \
            &korModuleDescriptor_, &korCreateModuleImpl_ };                             \
    }                                                                                   \
    extern "C" KORAL_MODULE_EXPORT const kor::ModuleDescriptor* korModuleDescriptor()   \
    { return &korModuleDescriptor_; }                                                   \
    extern "C" KORAL_MODULE_EXPORT kor::Module* korCreateModule()                       \
    { return korCreateModuleImpl_(); }
