//
// Created by radue on 2/21/2026.
//

#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>
#include <glm/fwd.hpp>
#include <string>
#include "api.h"
#include <source_location>

#include "builder.h"
#include "structs.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class KORAL_API Shader {
    public:
        enum class Stage {
            // VTG Graphics Pipeline
            eVertex = 1 << 0,
            eTessellationControl = 1 << 1,
            eTessellationEvaluation = 1 << 2,
            eGeometry = 1 << 3,
            eFragment = 1 << 4,

            // Compute Pipeline
            eCompute = 1 << 5,

            // MT Graphics Pipeline
            eTask = 1 << 6,
            eMesh = 1 << 7,

            // Raytracing Pipeline
            eRaygen = 1 << 8,
            eAnyHit = 1 << 9,
            eClosestHit = 1 << 10,
            eMiss = 1 << 11,
            eIntersection = 1 << 12,

            // Callable
            eCallable = 1 << 13,
        };

        enum class Lang {
            eGLSL,
            eSlang,
            eSPIRV,
        };

        struct KORAL_API InputOutput
        {
            glm::u32 startingLocation;
            glm::u32 locationSpan;
            std::string name;
            ChannelType channelType;
            glm::u32 channelCount;

            auto operator<=>(const InputOutput& other) const {
                return startingLocation <=> other.startingLocation;
            }
        };

        struct KORAL_API Descriptor {
            DescriptorType type;
            std::string name;
            glm::u32 count;
            Flags<Stage> stages;

            auto operator<=>(const Descriptor& other) const {
                return std::tie(type, name, count) <=> std::tie(other.type, other.name, other.count);
            }

            auto operator==(const Descriptor& other) const {
                return std::tie(type, name, count) == std::tie(other.type, other.name, other.count);
            }
        };

        struct KORAL_API DescriptorSet
        {
            std::map<glm::u32, Descriptor> descriptors;
        };

        struct KORAL_API PushConstant {
            std::string name;
            glm::u32 size;
            glm::u32 offset;
            Flags<Stage> stages;

            auto operator<=>(const PushConstant& other) const {
                return std::tie(name, size, offset) <=> std::tie(other.name, other.size, other.offset);
            }
            auto operator==(const PushConstant& other) const {
                return std::tie(name, size, offset) == std::tie(other.name, other.size, other.offset);
            }
        };

        struct KORAL_API MemoryLayout
        {
            std::set<InputOutput> inputs;
            std::set<InputOutput> outputs;
            std::map<glm::u32, DescriptorSet> descriptorSets;
            std::map<glm::u32, PushConstant> pushConstants;
        };

        // The one shader builder, identical for every language. A shader is always named by
        // its source file and (optionally) an entry point:
        //
        //     Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild();
        //     Shader::Builder{}.setPath("sample.slang").setEntryPoint("vertexMain").getOrBuild();
        //
        // Everything that used to differ per language is now inferred, and every inference has
        // an explicit setter that overrides it:
        //
        //   language   — from the file extension (.slang / .spv / anything else = GLSL); setLang().
        //   stage      — Slang reads its entry point's [shader("...")] attribute; GLSL and SPIR-V
        //                read the filename's stage tag ("x.vert.glsl", "x.comp.glsl"); setStage().
        //   path       — a relative path is resolved against the shader search roots, so callers
        //                do not have to wrap it in kor::shaderPath() themselves.
        //   identifier — getOrBuild() defaults it to "path" or "path:entry".
        struct KORAL_API Builder : ::Builder {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            Stage stage = Stage::eCompute;
            Lang lang = Lang::eGLSL;
            std::filesystem::path path = std::filesystem::path(); // source file, any language
            std::string module;                                   // Slang module name (defaults to path stem)
            std::string entry;                                    // entry point ("main" for GLSL)

            // Whether the corresponding field was set explicitly rather than inferred. Inference
            // only fills in what the caller left alone.
            bool stageExplicit = false;
            bool langExplicit  = false;

            Builder& setStage(const Stage stage) {
                this->stage = stage;
                this->stageExplicit = true;
                return *this;
            }

            Builder& setPath(std::filesystem::path path) {
                this->path = std::move(path);
                return *this;
            }

            // The entry point to compile. Slang modules have many; GLSL has one, "main".
            Builder& setEntryPoint(std::string entry) {
                this->entry = std::move(entry);
                return *this;
            }

            // Slang's module + entry form. Equivalent to setPath(module).setEntryPoint(entry):
            // the module is resolved by name across the shader search roots either way.
            Builder& setEntryPoint(std::string module, std::string entry) {
                this->module = std::move(module);
                this->entry = std::move(entry);
                return *this;
            }

            Builder& setLang(const Lang lang) {
                this->lang = lang;
                this->langExplicit = true;
                return *this;
            }

            template<Lang L> Builder& setLang() { return setLang(L); }

            // Fill in whatever the caller left to inference (language, Slang module name, entry
            // point, stage) and resolve a relative path against the search roots. Idempotent.
            // Applied by create(); exposed because getOrBuild() needs the resolved identifier.
            [[nodiscard]] Builder resolved() const;

            // The cache key getOrBuild() uses when none is given: "path" or "path:entry".
            [[nodiscard]] std::string defaultIdentifier() const;

            /** @brief One compile attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Shader>> create() const;
            [[nodiscard]] kor::Resource<Shader> build(std::source_location where = std::source_location::current()) const;

            // Get-or-build: returns the shader registered under `identifier`, building and
            // registering it (with hot-reload tracking) on first use. Subsequent calls with
            // the same identifier reuse the cached shader instead of recompiling.
            //
            // A shader that fails to compile is registered too, as a poisoned resource that is
            // still watched. That is what makes recovery possible at all: an unregistered failure
            // would be invisible to the file watcher, so fixing the source could never bring it
            // (or the pipelines built from it) back.
            // Identifier defaults to defaultIdentifier() ("path" / "path:entry") when omitted.
            // `where` is defaulted at the call site so a compile failure is reported against the
            // caller's line rather than against shader.cpp; see Builder::materialize.
            [[nodiscard]] ResourceRef<const Shader> getOrBuild(std::string identifier = {},
                                                              std::source_location where = std::source_location::current()) const;
        };

        virtual ~Shader() = default;

        [[nodiscard]] Stage getStage() const { return _stage; }
        [[nodiscard]] Lang getLang() const { return _lang; }
        [[nodiscard]] const std::filesystem::path& getSourcePath() const { return _path; }

        // Every source file the compiled shader depends on: the primary source plus any
        // transitively #include'd (GLSL) or import'ed (Slang) files. Populated by Compile()
        // and used to register hot-reload watchers on all of them.
        [[nodiscard]] const std::vector<std::filesystem::path>& getDependencies() const { return _dependencies; }

        // Runtime shader search roots, shared by Slang module resolution and shaderPath().
        // The API's own shaders/ folder is always registered; projects add their own — usually by
        // declaring them in koral.json under "shaderDirectories" and letting the runtime do it.
        //
        // `front` searches the new root ahead of the ones already registered, which is what the
        // config uses: a project's shaders shadow the engine's same-named ones, without the project
        // ever losing access to the engine's built-in shaders.
        static void addSearchPath(const std::filesystem::path& dir, bool front = false);
        static const std::vector<std::filesystem::path>& searchPaths();

        // Compiles to SPIR-V (_spirvCode) according to _lang: glslang for GLSL, file read for
        // SPIR-V, the Slang compiler for Slang (which also fills in the auto-detected stage).
        void Compile();
        void fetchMemoryLayout();

        const MemoryLayout& getMemoryLayout() const { return _memoryLayout; }

        glm::u64 RegisterReloadCallback(const std::function<void()>& callback) {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            static std::uniform_int_distribution<glm::u64> dis(1, std::numeric_limits<glm::u64>::max());

            const glm::u64 callbackId = dis(gen);
            _reloadCallbacks[callbackId] = callback;
            return callbackId;
        }
        void UnregisterReloadCallback(const glm::u64 callbackId)
        {
            _reloadCallbacks.erase(callbackId);
        }

    protected:
        virtual void OnReload();
        std::unordered_map<glm::u64, std::function<void()>> _reloadCallbacks;

        explicit Shader(const Builder& createInfo);
        Stage _stage;
        Lang _lang;
        std::filesystem::path _path;
        std::string _module;
        std::string _entry;

        std::vector<std::filesystem::path> _dependencies;

        std::vector<glm::u32> _spirvCode;
        MemoryLayout _memoryLayout;

        // Opaque hot-reload watch state (its concrete type lives in core/shader.cpp). Held here
        // so it shares the shader's lifetime: when the shader dies, the watch state dies with it
        // and its file-watcher callbacks safely no-op via the weak ResourceRef.
        std::shared_ptr<void> _watchContext;

        bool _valid = false;
        bool _modified = true;
    };
}
