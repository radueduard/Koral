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

// Reflection types, forward-declared: SPIRV-Cross is an implementation detail of the compile step,
// and its headers are not something a project should acquire by including this one.
namespace spirv_cross { class Compiler; struct Resource; }

namespace kor
{
    /**
     * @brief A compiled shader stage, its reflected interface, and its hot-reload watch.
     *
     * Shaders are named by source file and, for Slang, an entry point. getOrBuild() is the usual
     * way in: it compiles on first use and hands back the cached shader afterwards.
     *
     * @code
     * auto vert = kor::Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild();
     * auto frag = kor::Shader::Builder{}.setPath("sample.slang").setEntryPoint("fragmentMain").getOrBuild();
     * @endcode
     *
     * Compiling also reflects the shader: the descriptor sets, push constants and vertex inputs it
     * declares are read out of the SPIR-V, which is how a pipeline knows its layouts without being
     * told. Every source it depends on — includes and imports too — is watched, so editing one
     * recompiles the shader and rebuilds the pipelines built from it while the application runs. A
     * shader that fails to compile stays registered as a poisoned resource and is still watched,
     * which is what lets fixing the source bring it back.
     */
    class KORAL_API Shader {
    public:
        /** @brief Which stage of which pipeline a shader runs at. */
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

        /** @brief The source language, inferred from the file extension unless set explicitly. */
        enum class Lang {
            eGLSL,      ///< GLSL source, compiled with glslang. One entry point, "main".
            eSlang,     ///< Slang source, compiled with the Slang compiler. A module may hold several entry points.
            eSPIRV,     ///< Already-compiled SPIR-V, read straight from the file.
        };

        /** @brief One input or output variable of a stage, as reflected out of the compiled code. */
        struct KORAL_API InputOutput
        {
            glm::u32 startingLocation;  ///< The location it is bound at.
            glm::u32 locationSpan;      ///< How many consecutive locations it occupies; a matrix takes several.
            std::string name;           ///< Its name in the source.
            ChannelType channelType;    ///< The element type of one channel.
            glm::u32 channelCount;      ///< How many channels — 3 for a vec3.

            /// Which vocabulary the semantic below is from — the `mesh` of `mesh(POSITION)`. Empty
            /// for a Slang `: POSITION`, which names no module, and for an unannotated input.
            /// Last, and defaulted, so the positional brace-initialisers reflection builds these
            /// with keep working. @see vertexLayout.h
            std::string semanticNamespace;
            /// What the input was annotated with, or empty if it was not. A vertex shader's inputs
            /// are matched to a vertex layout by this rather than by their location.
            std::string semantic;

            auto operator<=>(const InputOutput& other) const {
                return startingLocation <=> other.startingLocation;
            }
        };

        /**
         * @brief How a shader reaches a descriptor's resource, which decides how it is synchronised.
         *
         * Uniform buffers, sampled images and acceleration structures are read-only by
         * construction; only storage buffers and storage images can be written. Which of the three
         * they are is carried by the NonReadable/NonWritable decorations SPIR-V puts on them, and a
         * producer is allowed to omit those — in which case this reads eReadWrite: conservative, so
         * the barrier that comes out is stronger than necessary but never weaker.
         */
        enum class AccessKind : glm::u8 {
            eRead,      ///< Only read.
            eWrite,     ///< Only written.
            eReadWrite, ///< Both, or not declared precisely enough to tell.
        };

        /**
         * @brief One field of a uniform or storage block, as the compiled shader lays it out.
         *
         * What lets the engine fill a block the shader designed rather than one it dictated.
         * @see semantics.h
         */
        struct KORAL_API BlockMember {
            std::string name;       ///< Its name in the source.
            /// Which module was asked to fill it — the `camera` of `camera(VIEW_MATRIX)`. Empty
            /// when the field carries no annotation at all.
            std::string semanticNamespace;
            std::string semantic;   ///< What it was annotated with, or empty if it was not.
            glm::u32 offset = 0;    ///< Byte offset within the block.
            glm::u32 size = 0;      ///< Its size in bytes, as declared.
            glm::u8 scalar = 0;     ///< SemanticSlot::Scalar, kept as a plain byte to avoid the include.
            glm::u8 rows = 1;       ///< Vector components, or matrix rows.
            glm::u8 columns = 1;    ///< Matrix columns; 1 for scalars and vectors.

            auto operator<=>(const BlockMember& other) const = default;
        };

        /** @brief One binding a shader declares, as reflected out of the compiled code. */
        struct KORAL_API Descriptor {
            DescriptorType type;                    ///< What kind of resource it expects.
            std::string name;                       ///< Its name in the source.
            glm::u32 count;                         ///< How many, for an array binding.
            Flags<Stage> stages;                    ///< Which stages reach it, unioned across the pipeline's shaders.
            AccessKind access = AccessKind::eRead;  ///< Whether the shader reads it, writes it, or both.

            /// Whether the entry point actually reaches this binding. A declared-but-unused binding
            /// still belongs in the layout — pruning it would change the descriptor set layout and
            /// orphan any set that writes to it — but it needs no barrier, so the resolver skips it.
            bool active = true;

            /// The block's fields, for a uniform or storage buffer binding; empty for anything
            /// else. Last, and defaulted, so the positional brace-initialisers that build a
            /// Descriptor from reflection keep working unchanged.
            ///
            /// Deliberately outside operator<=>: two shaders declaring one binding differently is
            /// a layout conflict, but a *reload* that only renames a field must not orphan every
            /// descriptor set built from the old layout. @see Descriptor::operator<=>
            std::vector<BlockMember> members;
            glm::u32 blockSize = 0;                 ///< The block's total size in bytes.

            // Identity is the *interface* only: type, name, count. Access is a function of
            // the type and adds nothing to compare, and stages are unioned across shaders
            // rather than compared. Keeping this narrow is what lets Pipeline::buildLayouts
            // recognise an unchanged set across a shader reload and keep the existing
            // layout object alive, along with every descriptor set built from it.
            auto operator<=>(const Descriptor& other) const {
                return std::tie(type, name, count) <=> std::tie(other.type, other.name, other.count);
            }

            auto operator==(const Descriptor& other) const {
                return std::tie(type, name, count) == std::tie(other.type, other.name, other.count);
            }
        };

        /** @brief One set the shader declares, and what sits at each of its bindings. */
        struct KORAL_API DescriptorSet
        {
            std::map<glm::u32, Descriptor> descriptors; ///< Keyed by binding number.
        };

        /** @brief A push-constant block the shader declares. */
        struct KORAL_API PushConstant {
            std::string name;       ///< Its name in the source.
            glm::u32 size;          ///< Its size in bytes.
            glm::u32 offset;        ///< Its byte offset within the pipeline's push-constant range.
            Flags<Stage> stages;    ///< Which stages read it.

            auto operator<=>(const PushConstant& other) const {
                return std::tie(name, size, offset) <=> std::tie(other.name, other.size, other.offset);
            }
            auto operator==(const PushConstant& other) const {
                return std::tie(name, size, offset) == std::tie(other.name, other.size, other.offset);
            }
        };

        /**
         * @brief Everything a shader's interface consists of, reflected out of the compiled SPIR-V.
         *
         * What a pipeline uses to build its descriptor set layouts and push-constant ranges, so
         * none of it has to be restated in C++.
         */
        struct KORAL_API MemoryLayout
        {
            std::set<InputOutput> inputs;                       ///< Stage inputs, by location.
            std::set<InputOutput> outputs;                      ///< Stage outputs, by location.
            std::map<glm::u32, DescriptorSet> descriptorSets;   ///< Declared sets, keyed by set number.
            std::map<glm::u32, PushConstant> pushConstants;     ///< Declared push-constant blocks, keyed by offset.
        };

        /**
         * @brief Names a shader to compile. One builder, identical for every language.
         *
         * A shader is named by its source file and, where the language has more than one, an entry
         * point:
         *
         * @code
         * Shader::Builder{}.setPath("flatTriangle.vert.glsl").getOrBuild();
         * Shader::Builder{}.setPath("sample.slang").setEntryPoint("vertexMain").getOrBuild();
         * @endcode
         *
         * Everything else is inferred, and every inference has a setter that overrides it:
         *
         * - **language** — from the file extension (.slang / .spv / anything else GLSL); setLang().
         * - **stage** — Slang reads its entry point's `[shader("...")]` attribute; GLSL and SPIR-V
         *   read the filename's stage tag ("x.vert.glsl", "x.comp.glsl"); setStage().
         * - **path** — a relative path is resolved against the shader search roots, so callers do
         *   not have to wrap it in kor::shaderPath() themselves.
         * - **identifier** — getOrBuild() defaults it to "path" or "path:entry".
         */
        struct KORAL_API Builder : ::Builder {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            Stage stage = Stage::eCompute;                        ///< Which pipeline stage it is; inferred unless set.
            Lang lang = Lang::eGLSL;                              ///< Source language; inferred from the extension unless set.
            std::filesystem::path path = std::filesystem::path(); ///< Source file, any language.
            std::string module;                                   ///< Slang module name (defaults to the path stem).
            std::string entry;                                    ///< Entry point ("main" for GLSL).

            /// Whether the corresponding field was set explicitly rather than inferred. Inference
            /// only fills in what the caller left alone.
            bool stageExplicit = false;
            bool langExplicit  = false;

            /** @brief Sets the pipeline stage explicitly, overriding what would be inferred. */
            Builder& setStage(const Stage stage) {
                this->stage = stage;
                this->stageExplicit = true;
                return *this;
            }

            /** @brief Sets the source file. A relative path is resolved against the shader search roots. */
            Builder& setPath(std::filesystem::path path) {
                this->path = std::move(path);
                return *this;
            }

            /** @brief Sets the entry point to compile. Slang modules have many; GLSL has one, "main". */
            Builder& setEntryPoint(std::string entry) {
                this->entry = std::move(entry);
                return *this;
            }

            /**
             * @brief Names a Slang module and one of its entry points.
             *
             * Equivalent to setPath(module).setEntryPoint(entry): the module is resolved by name
             * across the shader search roots either way.
             */
            Builder& setEntryPoint(std::string module, std::string entry) {
                this->module = std::move(module);
                this->entry = std::move(entry);
                return *this;
            }

            /** @brief Sets the source language explicitly, overriding what the extension implies. */
            Builder& setLang(const Lang lang) {
                this->lang = lang;
                this->langExplicit = true;
                return *this;
            }

            /** @brief Sets the source language as a template argument, for use in a chain. */
            template<Lang L> Builder& setLang() { return setLang(L); }

            /**
             * @brief A copy of this builder with every inference applied.
             * @return The builder with language, Slang module name, entry point and stage filled
             *         in, and a relative path resolved against the search roots. Idempotent.
             */
            [[nodiscard]] Builder resolved() const;

            /** @brief The cache key getOrBuild() uses when none is given: "path" or "path:entry". */
            [[nodiscard]] std::string defaultIdentifier() const;

            /** @brief One compile attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<Shader>> create() const;

            /**
             * @brief Compiles the shader unconditionally, without consulting the cache.
             * @return It as a Resource; poisoned rather than thrown if compilation fails.
             *
             * Prefer getOrBuild(), which caches and enables hot reload.
             */
            [[nodiscard]] kor::Resource<Shader> build(std::source_location where = std::source_location::current()) const;

            /**
             * @brief Returns the shader registered under @p identifier, compiling it on first use.
             * @param identifier Cache key; defaults to "path" or "path:entry".
             * @param where Source location, so a compile failure is reported against your line
             *        rather than against shader.cpp. Leave it defaulted.
             * @return A reference to the cached shader. Subsequent calls with the same identifier
             *         reuse it instead of recompiling.
             *
             * This is what registers the shader for hot reload. A shader that fails to compile is
             * registered too, as a poisoned resource that is still watched — which is what makes
             * recovery possible at all, since an unregistered failure would be invisible to the
             * file watcher and fixing the source could never bring it, or the pipelines built from
             * it, back.
             */
            [[nodiscard]] ResourceRef<const Shader> getOrBuild(std::string identifier = {},
                                                              std::source_location where = std::source_location::current()) const;
        };

        virtual ~Shader() = default;

        /** @brief Which pipeline stage this shader runs at. */
        [[nodiscard]] Stage getStage() const { return _stage; }

        /** @brief The language it was compiled from. */
        [[nodiscard]] Lang getLang() const { return _lang; }

        /** @brief The resolved path of its source file. */
        [[nodiscard]] const std::filesystem::path& getSourcePath() const { return _path; }

        /**
         * @brief Every source file the compiled shader depends on.
         * @return The primary source plus everything transitively `#include`d (GLSL) or `import`ed
         *         (Slang). All of them are watched, so editing an included file reloads the shaders
         *         that include it.
         */
        [[nodiscard]] const std::vector<std::filesystem::path>& getDependencies() const { return _dependencies; }

        /**
         * @brief Registers a directory to resolve shader paths and Slang module names against.
         * @param dir The directory.
         * @param front Search it before the roots already registered. This is what the config uses:
         *        a project's shaders shadow the engine's same-named ones without ever losing access
         *        to the rest.
         *
         * Koral's own shaders/ is always a root. A project adds its own here — or, more usually,
         * declares them in koral.json under "shaderDirectories" and lets the runtime do it.
         */
        static void addSearchPath(const std::filesystem::path& dir, bool front = false);

        /** @brief The shader search roots, in the order they are consulted. */
        static const std::vector<std::filesystem::path>& searchPaths();

        /**
         * @brief Compiles the source to SPIR-V.
         *
         * glslang for GLSL, a file read for SPIR-V, the Slang compiler for Slang — which also fills
         * in the auto-detected stage. Called by the builder and by hot reload.
         */
        void Compile();

        /** @brief Reflects the compiled SPIR-V into the memory layout. Called after Compile(). */
        void fetchMemoryLayout();

        /** @brief One block binding's fields. @see semantics.h */
        void fetchBlockMembers(const spirv_cross::Compiler& module,
                               const spirv_cross::Resource& resource,
                               Descriptor& descriptor) const;

        /**
         * @brief Reads the semantic annotations out of the shader's own source.
         *
         * GLSL carries them as `#pragma kor(NAME)` before the member, since GLSL has no attribute
         * syntax and the pragma is the one directive that survives being written inside a block.
         * Slang carries them as `[Kor(NAME)]`, which its reflection reports directly, so that path
         * fills this from the compiler instead of re-reading the file.
         */
        void fetchFieldSemantics(const std::string& source);

        /** @brief The shader's reflected interface: its sets, push constants and stage inputs and outputs. */
        const MemoryLayout& getMemoryLayout() const { return _memoryLayout; }

        /**
         * @brief Whether this shader reaches buffers through raw device addresses.
         *
         * Declared via SPIR-V's PhysicalStorageBufferAddresses capability, which is what a
         * buffer_reference / BufferPointer in the source compiles to. Reflection can say that
         * such dereferences happen but not *which* buffer any of them lands on, so the engine
         * cannot synchronise them: this flag is what lets it say so out loud instead of
         * silently under-synchronising. See CommandBuffer::resolveBarriers.
         */
        [[nodiscard]] bool usesDeviceAddresses() const { return _usesDeviceAddresses; }

        /**
         * @brief Asks to be told when this shader is recompiled.
         * @param callback Invoked after a successful reload.
         * @return An id to pass to UnregisterReloadCallback. Registrations must be released before
         *         the callback's owner dies, or it will be called on a destroyed object.
         *
         * How pipelines rebuild themselves on a shader edit.
         */
        glm::u64 RegisterReloadCallback(const std::function<void()>& callback) {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            // The parentheses around the name are not noise: <windows.h> defines max() as a
            // function-like macro, which eats `numeric_limits<T>::max()` and yields an unreadable
            // C2589 at the point of use. Wrapping the name blocks macro expansion. This header ships
            // in the SDK, so it cannot rely on the consumer defining NOMINMAX.
            static std::uniform_int_distribution<glm::u64> dis(1, (std::numeric_limits<glm::u64>::max)());

            const glm::u64 callbackId = dis(gen);
            _reloadCallbacks[callbackId] = callback;
            return callbackId;
        }
        /** @brief Releases a registration made by RegisterReloadCallback. */
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

        /// One field's annotation: which module, and what for.
        struct FieldSemantic { std::string moduleName; std::string semantic; };

        /// Field name -> annotation, from the shader's source or its Slang reflection. Field names
        /// are unique enough within one shader that this needs no per-block scoping; two blocks
        /// that happen to share a field name want the same semantic for it anyway.
        std::map<std::string, FieldSemantic> _fieldSemantics;

        /// Location -> annotation, for stage inputs. Slang keeps `: POSITION` on varyings and
        /// reports it by location, and a location is the one name a varying is sure to have: what
        /// Slang calls the parameter rarely survives into the SPIR-V. GLSL's inputs are annotated
        /// like its block members are and arrive through _fieldSemantics instead. @see vertexLayout.h
        std::map<glm::u32, FieldSemantic> _inputSemantics;
        bool _usesDeviceAddresses = false;

        // Opaque hot-reload watch state (its concrete type lives in core/shader.cpp). Held here
        // so it shares the shader's lifetime: when the shader dies, the watch state dies with it
        // and its file-watcher callbacks safely no-op via the weak ResourceRef.
        std::shared_ptr<void> _watchContext;

        bool _valid = false;
        bool _modified = true;
    };
}
