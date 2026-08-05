//
// Created by radue on 28.07.2026.
//

#include "module.h"

#include <algorithm>
#include <format>
#include <memory>
#include <ranges>
#include <unordered_map>

#include "commandBuffer.h"
#include "log.h"
#include "paths.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace kor
{
    namespace
    {
        // One known module: what it said about itself, how to construct it, where it came from if
        // the runtime went looking for it, and the instance once there is one.
        //
        // A library handle is kept but never closed. A module's types outlive the module object —
        // an ECS module holding a Resource<Camera> owns something whose vtable lives in the camera
        // module's image — so unloading any of them while another still has a pointer into it turns
        // a clean shutdown into a crash. Leaving them mapped until the process exits costs nothing.
        struct LoadedModule
        {
            std::string name;                       // as written in the config, for diagnostics
            std::filesystem::path path;             // empty for a module that registered itself
            void* handle = nullptr;
            const ModuleDescriptor* descriptor = nullptr;
            CreateModuleFn create = nullptr;
            bool conflicted = false;                // two different libraries claim this id
            std::unique_ptr<Module> instance;       // null until Resolve() constructs it
        };

        // Process-wide state, in the .cpp behind exported accessors rather than inline in the
        // header: an inline static would give the executable and every loaded library its own copy,
        // and a registry that every module sees differently is no registry at all.
        //
        // Registration happens during another library's static initialization, so this has to be a
        // function-local static: a namespace-scope vector here might not be constructed yet.
        std::vector<LoadedModule>& modules()
        {
            static std::vector<LoadedModule> instances;
            return instances;
        }

        // id -> index into modules(). Filled as instances are constructed, in Resolve().
        std::unordered_map<std::string_view, std::size_t>& moduleIndex()
        {
            static std::unordered_map<std::string_view, std::size_t> index;
            return index;
        }

        // Set by Resolve(). What makes a late registration — a library loaded after startup —
        // something to warn about rather than something that silently never runs.
        bool& resolved()
        {
            static bool done = false;
            return done;
        }

        // The entry that already claims @p id, or nullptr.
        LoadedModule* findEntry(const std::string_view id)
        {
            for (auto& entry : modules()) {
                if (entry.descriptor && entry.descriptor->id == id) return &entry;
            }
            return nullptr;
        }

        VoidResult loadError(const std::string& message)
        {
            return std::unexpected(Error{ .code = ErrorCode::eModuleLoadFailed, .message = message });
        }

        // ---- platform shims -------------------------------------------------------------------

        void* openLibrary(const std::filesystem::path& path, std::string& error)
        {
#if defined(_WIN32)
            const HMODULE handle = LoadLibraryW(path.wstring().c_str());
            if (!handle) error = std::format("LoadLibrary failed with error {}", GetLastError());
            return handle;
#else
            void* handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (!handle) {
                const char* message = dlerror();
                error = message ? message : "dlopen failed";
            }
            return handle;
#endif
        }

        void* findSymbol(void* handle, const char* symbol)
        {
#if defined(_WIN32)
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol));
#else
            return dlsym(handle, symbol);
#endif
        }

        // ---- name resolution ------------------------------------------------------------------

        // Where a module named (not pathed) in the config is looked for, most specific first: the
        // project's own directories, then Koral's. A project therefore overrides a shipped module by
        // dropping its own build beside the project, without editing anything.
        std::vector<std::filesystem::path> searchRoots(
            const std::span<const std::filesystem::path> configured)
        {
            std::vector<std::filesystem::path> roots(configured.begin(), configured.end());

            // Beside Koral itself — where the modules that ship with the engine are installed, and
            // where this build tree puts them. Not the executable's directory: modules belong to
            // the library, exactly like its shaders and assets do.
            if (const auto koralDir = detail::moduleDirectory(); !koralDir.empty()) {
                roots.push_back(koralDir / "modules");
                roots.push_back(koralDir);
            }
            return roots;
        }

        // "camera" -> libcamera.so / camera.dll / libcamera.dylib. A name is decorated because the
        // decoration differs per platform and a project should not have to write three of them; a
        // path is taken as given, because at that point the user has said exactly what they mean.
        std::filesystem::path decorate(const std::string_view name)
        {
            std::filesystem::path file;
#if defined(_WIN32)
            file = std::string(name) + ".dll";
#elif defined(__APPLE__)
            file = "lib" + std::string(name) + ".dylib";
#else
            file = "lib" + std::string(name) + ".so";
#endif
            return file;
        }

        std::filesystem::path resolve(const std::string& name,
                                      const std::span<const std::filesystem::path> roots)
        {
            std::error_code ec;

            // Anything with a directory in it, or that already exists as written, is a path and is
            // used as written — no searching, no decoration.
            const std::filesystem::path asWritten(name);
            if (asWritten.has_parent_path() || asWritten.is_absolute()) {
                if (std::filesystem::is_regular_file(asWritten, ec)) return asWritten;
                if (const auto decorated = asWritten.parent_path() / decorate(asWritten.filename().string());
                    std::filesystem::is_regular_file(decorated, ec)) return decorated;
                return {};
            }

            const auto file = decorate(name);
            for (const auto& root : roots) {
                if (auto candidate = root / file; std::filesystem::is_regular_file(candidate, ec))
                    return candidate;
                // A library named exactly as written, for a module whose file is not decorated the
                // way this platform's convention says it should be.
                if (auto candidate = root / name; std::filesystem::is_regular_file(candidate, ec))
                    return candidate;
            }
            return {};
        }

        std::string rootList(const std::span<const std::filesystem::path> roots)
        {
            std::string list;
            for (const auto& root : roots) {
                if (!list.empty()) list += ", ";
                list += '\'' + root.string() + '\'';
            }
            return list.empty() ? "<no search directories>" : list;
        }

        // ---- ordering -------------------------------------------------------------------------

        // Depth-first topological sort over the declared dependencies. Produces the order every
        // lifecycle hook runs in, so a module always runs after the ones it depends on — and, since
        // shutdown walks it backwards, is destroyed before them.
        VoidResult topologicalOrder(std::vector<LoadedModule>& loaded, std::vector<std::size_t>& order)
        {
            std::unordered_map<std::string_view, std::size_t> byId;
            for (std::size_t i = 0; i < loaded.size(); ++i) byId[loaded[i].descriptor->id] = i;

            enum class Mark : std::uint8_t { eUnvisited, eInProgress, eDone };
            std::vector<Mark> marks(loaded.size(), Mark::eUnvisited);
            std::vector<std::string_view> stack;

            // Explicit recursion, with the module id kept on a side stack, so a cycle can be
            // reported as the actual chain ("a -> b -> a") rather than as "there is a cycle".
            const auto visit = [&](auto&& self, const std::size_t index) -> VoidResult {
                if (marks[index] == Mark::eDone) return {};
                if (marks[index] == Mark::eInProgress) {
                    std::string chain;
                    for (const auto& id : stack) chain += std::string(id) + " -> ";
                    chain += std::string(loaded[index].descriptor->id);
                    return loadError(std::format("modules form a dependency cycle: {}", chain));
                }

                marks[index] = Mark::eInProgress;
                stack.emplace_back(loaded[index].descriptor->id);

                const auto& descriptor = *loaded[index].descriptor;
                for (std::size_t d = 0; d < descriptor.dependencyCount; ++d) {
                    const auto& dependency = descriptor.dependencies[d];
                    if (dependency.id.empty()) continue;   // KORAL_DECLARE_MODULE's padding entry

                    const auto it = byId.find(dependency.id);
                    if (it == byId.end()) {
                        if (dependency.kind == Dependency::eOptional) continue;
                        return loadError(std::format(
                            "module '{}' requires module '{}', which is not loaded — add it to "
                            "\"modules\" in koral.json",
                            descriptor.id, dependency.id));
                    }

                    if (const auto& provider = *loaded[it->second].descriptor;
                        provider.version != dependency.version)
                    {
                        if (dependency.kind == Dependency::eOptional) {
                            log::warn("[module] '{}' was built against '{}' v{}, but v{} is loaded; "
                                      "treating the optional dependency as absent",
                                      descriptor.id, dependency.id, dependency.version, provider.version);
                            continue;
                        }
                        return loadError(std::format(
                            "module '{}' requires '{}' v{}, but v{} is loaded",
                            descriptor.id, dependency.id, dependency.version, provider.version));
                    }

                    if (auto result = self(self, it->second); !result) return result;
                }

                stack.pop_back();
                marks[index] = Mark::eDone;
                order.push_back(index);
                return {};
            };

            for (std::size_t i = 0; i < loaded.size(); ++i) {
                if (auto result = visit(visit, i); !result) return result;
            }
            return {};
        }

        // Dispatch one hook across every module in load order. Wrapped so the six per-frame
        // entry points below are one line each and cannot get their iteration order wrong.
        template<typename Fn>
        void forEach(Fn&& fn)
        {
            for (auto& loaded : modules()) {
                if (loaded.instance) fn(*loaded.instance);
            }
        }
    }

    void ModuleHost::Register(const ModuleDescriptor* descriptor, const CreateModuleFn create)
    {
        if (!descriptor || descriptor->id.empty() || !create) {
            log::error("[module] a module registered itself with no descriptor or no factory; "
                       "ignoring it");
            return;
        }

        if (LoadedModule* existing = findEntry(descriptor->id)) {
            // The same library arriving twice — linked by the scene *and* named in koral.json — is
            // the ordinary case, and the second arrival has nothing new to say. Two different
            // libraries claiming one id is a real conflict, but it is reported from Resolve(), with
            // both paths known, rather than from inside a loader callback.
            if (existing->descriptor != descriptor) existing->conflicted = true;
            return;
        }

        modules().push_back({ .name = std::string(descriptor->id),
                              .descriptor = descriptor,
                              .create = create });

        if (resolved()) {
            log::warn("[module] '{}' registered after startup; its lifecycle hooks will not run. "
                      "A module has to be loaded before the scene is initialized.", descriptor->id);
        }
    }

    VoidResult ModuleHost::Load(const std::span<const std::string> requested,
                                const std::span<const std::filesystem::path> searchDirectories)
    {
        if (requested.empty()) return {};

        const auto roots = searchRoots(searchDirectories);

        for (const auto& name : requested) {
            const auto path = resolve(name, roots);
            if (path.empty())
                return loadError(std::format("module '{}' was not found (searched {})",
                                             name, rootList(roots)));

            // Opening the library is what runs its initializers, so by the time this returns the
            // module has already registered itself — the symbols below are the fallback for a
            // module built against an older Koral, and the way we learn which entry is this one.
            std::string error;
            void* handle = openLibrary(path, error);
            if (!handle)
                return loadError(std::format("module '{}' ('{}') could not be loaded: {}",
                                             name, path.string(), error));

            const auto describe = reinterpret_cast<ModuleDescriptorFn>(
                findSymbol(handle, "korModuleDescriptor"));
            const auto create = reinterpret_cast<CreateModuleFn>(
                findSymbol(handle, "korCreateModule"));

            if (!describe || !create)
                return loadError(std::format(
                    "'{}' ('{}') is not a Koral module: it exports no korModuleDescriptor/"
                    "korCreateModule. Did the module forget KORAL_DECLARE_MODULE?",
                    name, path.string()));

            const ModuleDescriptor* descriptor = describe();
            if (!descriptor || descriptor->id.empty())
                return loadError(std::format("module '{}' ('{}') returned no descriptor",
                                             name, path.string()));

            Register(descriptor, create);

            // Give the entry the provenance only this path knows about, whether it was just created
            // above or had already registered itself.
            if (LoadedModule* entry = findEntry(descriptor->id)) {
                entry->name = name;
                entry->path = path;
                entry->handle = handle;
            }
        }

        return {};
    }

    VoidResult ModuleHost::Resolve()
    {
        auto& registry = modules();
        if (resolved() || registry.empty()) {
            resolved() = true;
            return {};
        }

        for (const auto& entry : registry) {
            if (entry.conflicted)
                return loadError(std::format(
                    "module id '{}' is claimed by two different libraries; one of them has to go "
                    "(it is loaded both by linking and by name, from different builds)",
                    entry.descriptor->id));
        }

        // Order before constructing anything: a dependency problem should be reported as a startup
        // error, with nothing half-built to tear down.
        std::vector<std::size_t> order;
        if (auto result = topologicalOrder(registry, order); !result) return result;

        std::vector<LoadedModule> ordered;
        ordered.reserve(order.size());
        for (const std::size_t index : order) ordered.push_back(std::move(registry[index]));
        registry = std::move(ordered);

        for (std::size_t i = 0; i < registry.size(); ++i) {
            auto& entry = registry[i];
            entry.instance.reset(entry.create());
            if (!entry.instance)
                return loadError(std::format("module '{}' failed to construct", entry.descriptor->id));

            // Indexed as we go — and before any Initialize() runs — so that a module's
            // Initialize() can reach any other module, in any direction.
            moduleIndex()[entry.descriptor->id] = i;

            log::info("[module] loaded '{}' v{} ({})", entry.descriptor->id, entry.descriptor->version,
                      entry.path.empty() ? "linked" : entry.path.string());
        }

        resolved() = true;
        return {};
    }

    void ModuleHost::Initialize()
    {
        if (!resolved()) {
            if (const auto result = Resolve(); !result)
                log::error("[module] {}", result.error().message);
        }
        forEach([](Module& m) { m.Initialize(); });
    }
    void ModuleHost::Update()     { forEach([](Module& m) { m.Update(); }); }
    void ModuleHost::LateUpdate() { forEach([](Module& m) { m.LateUpdate(); }); }
    void ModuleHost::RenderUI()   { forEach([](Module& m) { m.RenderUI(); }); }

    void ModuleHost::Render(CommandBuffer& commandBuffer)
    {
        forEach([&](Module& m) { m.Render(commandBuffer); });
    }

    void ModuleHost::RenderOverlay(CommandBuffer& commandBuffer)
    {
        forEach([&](Module& m) { m.RenderOverlay(commandBuffer); });
    }

    void ModuleHost::OnResize(const glm::uvec2 extent)
    {
        forEach([extent](Module& m) { m.OnResize(extent); });
    }

    void ModuleHost::Shutdown()
    {
        auto& registry = modules();

        // Reverse dependency order, in two passes. Every module gets to shut down while all of its
        // dependencies are still alive and usable; only then does anything get destroyed. Doing it
        // in one pass would let a module's Shutdown() reach a dependency that had already been
        // deleted out from under it.
        for (auto& loaded : std::views::reverse(registry)) {
            if (loaded.instance) loaded.instance->Shutdown();
        }
        for (auto& loaded : std::views::reverse(registry)) {
            loaded.instance.reset();
        }

        moduleIndex().clear();
        registry.clear();
        resolved() = false;
        // The libraries themselves stay mapped; see LoadedModule. A module that registered itself
        // is gone from the registry now and will not come back — its library's initializers have
        // already run, and they only run once.
    }

    std::vector<std::string_view> ModuleHost::LoadedModules()
    {
        std::vector<std::string_view> ids;
        ids.reserve(modules().size());
        for (const auto& loaded : modules()) ids.push_back(loaded.descriptor->id);
        return ids;
    }
}
