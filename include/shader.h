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
#include "builder.h"
#include "structs.h"
#include "resource.h"
#include "error.h"

namespace gfx
{
    class GFX_API Shader {
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

        struct GFX_API InputOutput
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

        struct GFX_API Descriptor {
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

        struct GFX_API DescriptorSet
        {
            std::map<glm::u32, Descriptor> descriptors;
        };

        struct GFX_API PushConstant {
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

        struct GFX_API MemoryLayout
        {
            std::set<InputOutput> inputs;
            std::set<InputOutput> outputs;
            std::map<glm::u32, DescriptorSet> descriptorSets;
            std::map<glm::u32, PushConstant> pushConstants;
        };

        // Unified shader descriptor and convergence point of the language-specific builders
        // below; the backends construct a Shader from this. Build it via the fluent
        // Shader::Builder API: pick the language with setLang<L>(), which returns the
        // matching language-specific builder.
        struct GFX_API Builder : ::Builder {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            Stage stage = Stage::eCompute;
            Lang lang = Lang::eGLSL;
            std::filesystem::path path = std::filesystem::path(); // GLSL / SPIR-V source
            std::string module;                                   // Slang module name
            std::string entry;                                    // Slang entry point name

            Builder& setStage(const Stage stage) {
                this->stage = stage;
                return *this;
            }

            // Language divergence: returns the builder for language L (GLSLBuilder /
            // SPIRVBuilder expose setPath; SlangBuilder exposes setEntryPoint). Returns by
            // value since the type changes. Defined out of line below the class.
            template<Lang L> auto setLang() const;

            /** @brief One compile attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Shader>> create() const;
            [[nodiscard]] gfx::Resource<Shader> build() const;

            // Get-or-build: returns the shader registered under `identifier`, building and
            // registering it (with hot-reload tracking) on first use. Subsequent calls with
            // the same identifier reuse the cached shader instead of recompiling.
            //
            // A shader that fails to compile is registered too, as a poisoned resource that is
            // still watched. That is what makes recovery possible at all: an unregistered failure
            // would be invisible to the file watcher, so fixing the source could never bring it
            // (or the pipelines built from it) back.
            [[nodiscard]] ResourceRef<const Shader> getOrBuild(const std::string& identifier) const;
        };

        // GLSL source referenced by file path.
        struct GFX_API GLSLBuilder {
            Builder _b;
            GLSLBuilder& setStage(const Stage stage) { _b.stage = stage; return *this; }
            GLSLBuilder& setPath(std::filesystem::path path) { _b.path = std::move(path); return *this; }
            [[nodiscard]] gfx::Resource<Shader> build() const { return _b.build(); }
            [[nodiscard]] ResourceRef<const Shader> getOrBuild(const std::string& identifier) const { return _b.getOrBuild(identifier); }
        };

        // Precompiled SPIR-V referenced by file path.
        struct GFX_API SPIRVBuilder {
            Builder _b;
            SPIRVBuilder& setStage(const Stage stage) { _b.stage = stage; return *this; }
            SPIRVBuilder& setPath(std::filesystem::path path) { _b.path = std::move(path); return *this; }
            [[nodiscard]] gfx::Resource<Shader> build() const { return _b.build(); }
            [[nodiscard]] ResourceRef<const Shader> getOrBuild(const std::string& identifier) const { return _b.getOrBuild(identifier); }
        };

        // Slang referenced by module + entry point; the stage is auto-detected from the
        // entry point's [shader("...")] attribute (override with setStage).
        struct GFX_API SlangBuilder {
            Builder _b;
            SlangBuilder& setStage(const Stage stage) { _b.stage = stage; return *this; }
            SlangBuilder& setEntryPoint(std::string module, std::string entry) {
                _b.module = std::move(module);
                _b.entry = std::move(entry);
                return *this;
            }
            [[nodiscard]] gfx::Resource<Shader> build() const { return _b.build(); }
            // Identifier defaults to "module:entry" when omitted.
            [[nodiscard]] ResourceRef<const Shader> getOrBuild(std::string identifier = {}) const {
                return _b.getOrBuild(identifier.empty() ? _b.module + ":" + _b.entry : identifier);
            }
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
        // The API's own shaders/ folder is always registered; projects add their own.
        static void addSearchPath(const std::filesystem::path& dir);
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

    template<Shader::Lang L>
    auto Shader::Builder::setLang() const
    {
        Builder copy = *this;
        copy.lang = L;
        if constexpr (L == Lang::eSlang)       return SlangBuilder{ copy };
        else if constexpr (L == Lang::eSPIRV)  return SPIRVBuilder{ copy };
        else                                   return GLSLBuilder{ copy };
    }
}
