//
// Created by radue on 2/21/2026.
//

#include "../backends/open_gl/shader.h"
#include "../backends/vulkan/shader.h"

#include <shader.h>
#include <window.h>
#include <context.h>
#include <file.h>
#include <framebuffer.h>
#include <surface.h>

#include <iostream>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#include "slangCompiler.h"
#include "fileWatcher.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include "structs.h"
#include "paths.h"


namespace kor {
    namespace {
        // Per-shader hot-reload watch state, owned by the Shader via an opaque shared_ptr<void>
        // (Shader::_watchContext). `watched` is the set of files already registered with the
        // FileWatcher; `onChange` reloads the shader and re-syncs `watched` against the current
        // dependency list so files added by an edit start being watched too.
        struct WatchContext {
            std::unordered_set<std::filesystem::path> watched;
            std::function<void()> onChange;
        };
    }

    namespace {
        // The stage tag in a shader filename, e.g. "geometryPass.vert.glsl" -> eVertex. Looks at
        // every extension so both "x.vert" and "x.vert.glsl" resolve. Used for GLSL and SPIR-V,
        // where the stage cannot be recovered from the source the way Slang recovers it from
        // [shader("...")]; nullopt means "no recognisable tag", which is a build error unless the
        // caller set the stage explicitly.
        std::optional<Shader::Stage> stageFromFilename(const std::filesystem::path& path)
        {
            static const std::unordered_map<std::string, Shader::Stage> tags = {
                {"vert", Shader::Stage::eVertex},
                {"tesc", Shader::Stage::eTessellationControl},
                {"tese", Shader::Stage::eTessellationEvaluation},
                {"geom", Shader::Stage::eGeometry},
                {"frag", Shader::Stage::eFragment},
                {"comp", Shader::Stage::eCompute},
                {"task", Shader::Stage::eTask},
                {"mesh", Shader::Stage::eMesh},
                {"rgen", Shader::Stage::eRaygen},
                {"rahit", Shader::Stage::eAnyHit},
                {"rchit", Shader::Stage::eClosestHit},
                {"rmiss", Shader::Stage::eMiss},
                {"rint", Shader::Stage::eIntersection},
                {"rcall", Shader::Stage::eCallable},
            };

            for (auto stem = path.filename(); stem.has_extension(); stem = stem.stem()) {
                auto ext = stem.extension().string();
                if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
                if (const auto it = tags.find(ext); it != tags.end()) return it->second;
            }
            return std::nullopt;
        }

        Shader::Lang langFromFilename(const std::filesystem::path& path)
        {
            const auto ext = path.extension().string();
            if (ext == ".slang") return Shader::Lang::eSlang;
            if (ext == ".spv")   return Shader::Lang::eSPIRV;
            return Shader::Lang::eGLSL;
        }
    }

    Shader::Builder Shader::Builder::resolved() const
    {
        Builder b = *this;

        // Slang's module+entry form names the source by module rather than by path; treat the
        // module as the path so one set of rules covers both spellings.
        if (b.path.empty() && !b.module.empty()) b.path = b.module;

        if (!b.langExplicit && !b.path.empty()) b.lang = langFromFilename(b.path);

        // A Slang module is resolved by name, so it keeps the bare stem; every other language
        // needs a real file, resolved against the shader search roots.
        if (b.lang == Lang::eSlang) {
            if (b.module.empty()) b.module = b.path.stem().string();
        } else if (!b.path.empty() && b.path.is_relative()) {
            b.path = kor::shaderPath(b.path);
        }

        if (b.entry.empty() && b.lang != Lang::eSlang) b.entry = "main";

        // Slang recovers the stage from the entry point at compile time, so leave it alone there.
        if (!b.stageExplicit && b.lang != Lang::eSlang) {
            if (const auto inferred = stageFromFilename(b.path)) b.stage = *inferred;
        }

        return b;
    }

    std::string Shader::Builder::defaultIdentifier() const
    {
        const Builder b = resolved();
        const std::string source = b.lang == Lang::eSlang ? b.module : b.path.string();
        // GLSL's entry is always "main", so it adds nothing to the key; Slang's distinguishes the
        // several shaders that share one module.
        return b.lang == Lang::eSlang ? std::format("{}:{}", source, b.entry) : source;
    }

    Result<std::unique_ptr<Shader>> Shader::Builder::create() const
    {
        beginAttempt();

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const Builder b = resolved();

        if (b.path.empty())
            return fail(ErrorCode::eInvalidArgument,
                        "No shader source: call setPath(\"file.glsl\") (or setEntryPoint(module, entry) for Slang).");

        if (b.lang == Lang::eSlang && b.entry.empty())
            return fail(ErrorCode::eInvalidArgument,
                        "Slang module '{}' needs an entry point: call setEntryPoint(\"name\").", b.module);

        // GLSL and SPIR-V cannot report their own stage, so an un-inferrable filename is fatal
        // rather than silently compiling as the eCompute default.
        if (b.lang != Lang::eSlang && !b.stageExplicit && !stageFromFilename(b.path))
            return fail(ErrorCode::eInvalidArgument,
                        "Cannot infer the shader stage from '{}': name it '<name>.vert.glsl' (vert/frag/comp/geom/"
                        "tesc/tese/task/mesh/rgen/rahit/rchit/rmiss/rint/rcall) or call setStage() explicitly.",
                        b.path.string());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        // Construction compiles the shader; a compile/parse failure throws and is
        // surfaced as a kor::Error (eShaderCompileFailed unless a more specific cause).
        // The backends read the builder's fields directly, so they must see the resolved one.
        return guard(ErrorCode::eShaderCompileFailed, [&]() -> std::unique_ptr<Shader> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<Shader, vk::Shader>(b)
                : kor::MakeBackendPtr<Shader, ogl::Shader>(b);
        });
    }

    kor::Resource<Shader> Shader::Builder::build(const std::source_location where) const
    {
        // Name it after whatever identifies the source, so a compile failure reads as
        // "shaders/forward.frag.glsl" rather than "Shader".
        return materialize<Shader>(*this, defaultIdentifier(), where);
    }

    ResourceRef<const Shader> Shader::Builder::getOrBuild(std::string identifierOrEmpty,
                                                          const std::source_location where) const
    {
        const std::string identifier = identifierOrEmpty.empty() ? defaultIdentifier() : std::move(identifierOrEmpty);

        // Get-or-create: a shader already registered under this identifier is reused as-is.
        // Rebuilding would recompile the shader (wasted work) and double-register its file
        // watcher; the identifier is the cache key and the file path drives hot-reload.
        //
        // A poisoned entry is reused too, deliberately: it is the same object every pipeline
        // built from this shader already refers to, and repairing it in place is what brings
        // all of them back at once.
        if (Context::Repository().contains<Shader>(identifier))
            return ResourceRef<const Shader>(Context::Repository().getRef<Shader>(identifier));

        // Registered whether or not it compiled. A shader that failed to compile has to stay
        // alive, registered and watched — otherwise the file watcher never learns about the
        // file, fixing the source raises no event, and neither the shader nor anything built
        // from it could ever recover. The poisoned resource is the recovery mechanism.
        ResourceRef<Shader> shaderRef = Context::Repository().add(identifier, build(where));

        auto ctx = std::make_shared<WatchContext>();
        std::weak_ptr<WatchContext> weakCtx = ctx;

        // Register a FileWatcher for every dependency not already watched: the primary source
        // plus any #include'd (GLSL) or import'ed (Slang) files. Called once now and again
        // after each reload, so files newly referenced by an edit start being watched too.
        //
        // A shader that failed to compile reports no dependencies (there was no successful
        // parse), so fall back to its declared source path — which is the very file the user
        // is about to fix, and thus the one event we cannot afford to miss.
        auto resync = [shaderRef, weakCtx, fallback = resolved().path] () mutable
        {
            const auto ctx = weakCtx.lock();
            if (!ctx || !shaderRef.alive()) return;

            std::vector<std::filesystem::path> dependencies;
            if (shaderRef.valid()) dependencies = shaderRef->getDependencies();
            else if (!fallback.empty()) dependencies.push_back(fallback);

            for (const auto& dependency : dependencies)
            {
                std::error_code ec;
                auto path = std::filesystem::weakly_canonical(dependency, ec);
                if (ec) path = dependency;
                if (ctx->watched.insert(path).second) FileWatcher::TrackFile(path, ctx->onChange);
            }
        };

        ctx->onChange = [shaderRef, resync] () mutable
        {
            if (!shaderRef.alive()) return; // shader destroyed; its watch callbacks safely no-op

            if (shaderRef.poisoned()) {
                // The shader never compiled, so there is no object to reload — it has to be rebuilt
                // from its builder. Ask for that rather than doing it here: we are on the file
                // watcher's thread, and a rebuild replaces the underlying object, which must not
                // race whatever is recording with it. Repository::repair() picks this up at the top
                // of the next frame, and brings back every pipeline built from this shader with it.
                shaderRef.requestRepair();
                return;
            }

            shaderRef->OnReload();

            for (const auto& callback : shaderRef->_reloadCallbacks | std::views::values)
            {
                callback();
            }
            resync(); // pick up any dependencies the edit added
        };

        // Tie the watch state to the resource *slot*, not to the shader object: a shader that
        // failed to compile has no object, and that is exactly when the watch must survive.
        shaderRef.attach(ctx);

        resync();  // initial registration

        return ResourceRef<const Shader>(shaderRef);
    }

    static EShLanguage shaderStageToEShLanguage(const Shader::Stage &stage) {
        switch (stage) {
        case Shader::Stage::eVertex: return EShLangVertex;
        case Shader::Stage::eGeometry: return EShLangGeometry;
        case Shader::Stage::eTessellationControl: return EShLangTessControl;
        case Shader::Stage::eTessellationEvaluation: return EShLangTessEvaluation;
        case Shader::Stage::eFragment: return EShLangFragment;
        case Shader::Stage::eCompute: return EShLangCompute;
        case Shader::Stage::eMesh: return EShLangMeshNV;
        case Shader::Stage::eTask: return EShLangTaskNV;
        case Shader::Stage::eRaygen: return EShLangRayGenNV;
        case Shader::Stage::eIntersection: return EShLangIntersectNV;
        case Shader::Stage::eAnyHit: return EShLangAnyHitNV;
        case Shader::Stage::eClosestHit: return EShLangClosestHitNV;
        case Shader::Stage::eMiss: return EShLangMissNV;
        case Shader::Stage::eCallable: return EShLangCallableNV;
        default: throw std::runtime_error("Unknown shader stage!");
        }
    }

    static std::vector<std::filesystem::path>& shaderSearchPathsStorage()
    {
        // Seeded with wherever Koral's own shaders/ actually landed — beside the installed library, or
        // in the source tree for a dev build. Never a bare compile-time absolute path: that would be
        // the build machine's, and meaningless on a user's. Projects add their roots via addSearchPath.
        static std::vector<std::filesystem::path> paths =
            detail::dataRoots("shaders", "KORAL_SHADERS_DIR", SHADERS_PATH);
        return paths;
    }

    void Shader::addSearchPath(const std::filesystem::path& dir, const bool front)
    {
        auto& paths = shaderSearchPathsStorage();
        if (dir.empty() || std::ranges::find(paths, dir) != paths.end()) return;
        if (front) paths.insert(paths.begin(), dir);
        else       paths.push_back(dir);
    }

    const std::vector<std::filesystem::path>& Shader::searchPaths() { return shaderSearchPathsStorage(); }

    namespace {
        // glslang includer that resolves `#include`d files across the shader search roots
        // (and, for quoted includes, relative to the including file) and records every file
        // it opens so the shader can watch them for hot-reload. Requires the source to enable
        // `#extension GL_GOOGLE_include_directive : require`.
        class ShaderIncluder final : public glslang::TShader::Includer {
        public:
            explicit ShaderIncluder(std::filesystem::path sourceDir)
            {
                _directoryStack.push_back(std::move(sourceDir));
            }

            IncludeResult* includeLocal(const char* headerName, const char*, size_t) override
            {
                return resolve(headerName, /*local=*/true);
            }
            IncludeResult* includeSystem(const char* headerName, const char*, size_t) override
            {
                return resolve(headerName, /*local=*/false);
            }
            void releaseInclude(IncludeResult* result) override
            {
                if (!result) return;
                _directoryStack.pop_back(); // matches the push in a successful resolve()
                delete[] static_cast<char*>(result->userData);
                delete result;
            }

            [[nodiscard]] const std::unordered_set<std::filesystem::path>& dependencies() const { return _dependencies; }

        private:
            IncludeResult* resolve(const std::string& headerName, const bool local)
            {
                // Quoted includes look next to the including file first, then the search roots;
                // angle-bracket includes only look in the search roots.
                std::vector<std::filesystem::path> roots;
                if (local && !_directoryStack.empty()) roots.push_back(_directoryStack.back());
                for (const auto& root : Shader::searchPaths()) roots.push_back(root);

                for (const auto& root : roots) {
                    std::error_code ec;
                    auto candidate = std::filesystem::weakly_canonical(root / headerName, ec);
                    if (ec || !std::filesystem::exists(candidate, ec)) continue;

                    std::ifstream file(candidate, std::ios::binary);
                    if (!file) continue;
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                    _dependencies.insert(candidate);
                    _directoryStack.push_back(candidate.parent_path()); // popped in releaseInclude

                    auto* data = new char[content.size()];
                    std::memcpy(data, content.data(), content.size());
                    return new IncludeResult(candidate.string(), data, content.size(), data);
                }
                return nullptr;
            }

            std::unordered_set<std::filesystem::path> _dependencies;
            std::vector<std::filesystem::path> _directoryStack;
        };
    }

    void Shader::Compile()
    {
        if (!_modified) return;
        _modified = false;

        switch (_lang) {
        case Lang::eSPIRV:
            _spirvCode = utils::ReadFileToUIntVector(_path);
            _dependencies = { _path };
            if (_spirvCode.empty())
                throw BackendException(Error{
                    .code = ErrorCode::eShaderCompileFailed,
                    .message = std::format("SPIR-V module '{}' is missing or empty.", _path.string()),
                });
            _valid = true;
            return;
        case Lang::eSlang: {
            auto result = SlangCompiler::Compile(_module, _entry, searchPaths());
            _spirvCode = std::move(result.spirv);
            _stage = result.stage;                          // auto-detected from [shader(...)]
            if (!result.resolvedPath.empty()) _path = result.resolvedPath; // for hot-reload
            _dependencies = std::move(result.dependencies); // module + imports, for hot-reload
            _fieldSemantics.clear();                        // [module("SEMANTIC")] annotations
            for (auto& [field, annotation] : result.fieldSemantics) {
                _fieldSemantics.emplace(field, FieldSemantic{ std::move(annotation.moduleName),
                                                              std::move(annotation.semantic) });
            }
            _inputSemantics.clear();                        // `: SEMANTIC` on the stage inputs
            for (auto& [location, annotation] : result.varyingSemantics) {
                _inputSemantics.emplace(location, FieldSemantic{ std::move(annotation.moduleName),
                                                                 std::move(annotation.semantic) });
            }
            if (_spirvCode.empty())
                throw BackendException(Error{
                    .code = ErrorCode::eShaderCompileFailed,
                    .message = std::format("Slang compilation of '{}:{}' produced no code.", _module, _entry),
                });
            _valid = true;
            return;
        }
        case Lang::eGLSL:
        default:
            break; // fall through to the glslang path below
        }

        const auto source = utils::ReadFileAsString(_path);
        glslang::InitializeProcess();
        const auto eShStage = shaderStageToEShLanguage(_stage);

        const auto shader = new glslang::TShader(eShStage);
        const auto shaderStrings = new std::string(source);
        const auto shaderStringsPointer = shaderStrings->c_str();
        shader->setStrings(&shaderStringsPointer, 1);

        // Enable `#include` without requiring every shader to declare the extension itself.
        // A preamble is processed ahead of the source but doesn't disturb #version ordering.
        shader->setPreamble("#extension GL_GOOGLE_include_directive : require\n");

        shader->setEnvInput(glslang::EShSourceGlsl, eShStage, glslang::EShClientVulkan, 450);
        shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

        constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgDefault | EShMsgDebugInfo | EShMsgEnhanced);

        // Resolve #include directives across the search roots and record the opened files.
        ShaderIncluder includer(_path.parent_path());

        // The primary source is always a dependency; #include'd files are added on success.
        _dependencies = { _path };

        if (!shader->parse(GetDefaultResources(), 450, false, messages, includer)) {
        	// Carry glslang's own diagnostics out as the error: they name the file and line the
        	// user has to fix, and they become the root cause of every pipeline built from this.
        	std::string info = shader->getInfoLog();
        	delete shader;
        	throw BackendException(Error{
        		.code = ErrorCode::eShaderCompileFailed,
        		.message = std::format("GLSL compilation failed for '{}':\n{}", _path.string(), info),
        	});
        }
        _dependencies.insert(_dependencies.end(), includer.dependencies().begin(), includer.dependencies().end());

        const auto program = new glslang::TProgram;
        program->addShader(shader);

        if (!program->link(messages)) {
        	std::string info = program->getInfoLog();
        	delete program;
        	delete shader;
        	throw BackendException(Error{
        		.code = ErrorCode::eShaderCompileFailed,
        		.message = std::format("GLSL linking failed for '{}':\n{}", _path.string(), info),
        	});
        }

        std::vector<uint32_t> spirV;
        GlslangToSpv(*program->getIntermediate(eShStage), spirV);

        program->dumpReflection();

        delete program;
        delete shader;
        delete shaderStrings;

        glslang::FinalizeProcess();

    	_valid = true;
    	_spirvCode = spirV;
    }

	std::pair<ChannelType, glm::u32> SPIRTypeConverter(const spirv_cross::SPIRType& type)
    {
    	auto rows = type.vecsize;
    	const auto columns = type.columns;
    	const auto base = type.basetype;

    	if (columns > 1) {
    		throw std::runtime_error("Matrices are not supported as shader inputs/outputs!");
    	}

    	switch (base) {
			case spirv_cross::SPIRType::BaseType::Float: return {ChannelType::eFloat, rows};
			case spirv_cross::SPIRType::BaseType::Double: return {ChannelType::eDouble, rows};
			case spirv_cross::SPIRType::BaseType::SByte: return {ChannelType::eByte, rows};
			case spirv_cross::SPIRType::BaseType::UByte: return {ChannelType::eUByte, rows};
			case spirv_cross::SPIRType::BaseType::Short: return {ChannelType::eShort, rows};
			case spirv_cross::SPIRType::BaseType::UShort: return {ChannelType::eUShort, rows};
			case spirv_cross::SPIRType::BaseType::Int: return {ChannelType::eInt, rows};
			case spirv_cross::SPIRType::BaseType::UInt: return {ChannelType::eUInt, rows};
			default: throw std::runtime_error("Unknown base type for shader input/output!");
		}
    }

	glm::u32 GetCount(const spirv_cross::SPIRType& type) {
    	uint32_t count = 1;
    	for (const auto& arraySize : type.array) {
    		count *= arraySize;
    	}
    	return count;
    }

	// The shape the shader declared an image binding with. Only the shader knows this: six array
	// layers are equally a cube map and a 2D array, and picking wrong gives a `samplerCube` a view
	// it cannot sample. Kept so that binding an Image directly can build the right view.
	// @see Shader::ImageShape
	Shader::ImageShape ShapeOf(const spirv_cross::SPIRType& type) {
		using Shape = Shader::ImageShape;
		const bool arrayed = type.image.arrayed;
		switch (type.image.dim) {
		case spv::Dim1D:     return arrayed ? Shape::e1DArray : Shape::e1D;
		case spv::Dim2D:     return arrayed ? Shape::e2DArray : Shape::e2D;
		case spv::Dim3D:     return Shape::e3D;   // a 3D image has no array form
		case spv::DimCube:   return arrayed ? Shape::eCubeArray : Shape::eCube;
		case spv::DimBuffer: return Shape::eBuffer;
		// DimRect and DimSubpassData reach no path that builds a view for the caller, so naming
		// them here would only invite one to be built. Left unknown, which binding an Image
		// reports rather than guesses at.
		default:             return Shape::eUnknown;
		}
	}

	// Read/write intent for a resource that can be written, from the NonReadable/NonWritable
	// decorations. `flags` must come from get_buffer_block_flags for a storage *buffer* (the
	// decorations sit on the block's members and have to be aggregated) and from
	// get_decoration_bitset for a storage *image* (they sit on the variable itself).
	//
	// SPIR-V does not require either decoration, so an absent pair means "assume both", which
	// over-synchronises rather than under-synchronises. Slang and GLSL both emit them for the
	// declarations you would actually write (StructuredBuffer vs RWStructuredBuffer, readonly
	// vs writeonly), so hand-written SPIR-V is the only realistic source of a miss.
	Shader::AccessKind AccessFrom(const spirv_cross::Bitset& flags) {
		const bool readable = !flags.get(spv::DecorationNonReadable);
		const bool writable = !flags.get(spv::DecorationNonWritable);
		if (readable && writable) return Shader::AccessKind::eReadWrite;
		if (writable)             return Shader::AccessKind::eWrite;
		return Shader::AccessKind::eRead;
	}

	Shader::Stage StageFrom(const spv::ExecutionModel executionModel) {
	    switch (executionModel) {
	    	case spv::ExecutionModelVertex: return Shader::Stage::eVertex;
	    	case spv::ExecutionModelTessellationControl: return Shader::Stage::eTessellationControl;
	    	case spv::ExecutionModelTessellationEvaluation: return Shader::Stage::eTessellationEvaluation;
	    	case spv::ExecutionModelGeometry: return Shader::Stage::eGeometry;
	    	case spv::ExecutionModelFragment: return Shader::Stage::eFragment;
	    	case spv::ExecutionModelGLCompute: return Shader::Stage::eCompute;
	    	case spv::ExecutionModelMeshNV: return Shader::Stage::eMesh;
	    	case spv::ExecutionModelTaskNV: return Shader::Stage::eTask;
	    	case spv::ExecutionModelRayGenerationNV: return Shader::Stage::eRaygen;
	    	case spv::ExecutionModelIntersectionNV: return Shader::Stage::eIntersection;
	    	case spv::ExecutionModelAnyHitNV: return Shader::Stage::eAnyHit;
	    	case spv::ExecutionModelClosestHitNV: return Shader::Stage::eClosestHit;
	    	case spv::ExecutionModelMissNV: return Shader::Stage::eMiss;
	    	case spv::ExecutionModelCallableNV: return Shader::Stage::eCallable;
	    	default: throw std::runtime_error("Unknown execution model in SPIR-V module!");
	    }
    }

    // `#pragma module(SEMANTIC)` on one line, the field it decorates on the next. GLSL has no
    // attribute syntax, and a pragma is the one thing that can be written inside a block and still
    // be a directive rather than a comment — glslang accepts and ignores it, so the text is ours
    // to read. Which is what this does: the compiler discards it, so the annotation is recovered
    // from the source rather than from the SPIR-V.
    //
    // Includes are followed, because a project is expected to keep its shared blocks in a header.
    void Shader::fetchFieldSemantics(const std::string& rawSource)
    {
        // The pragmas that mean something to a compiler rather than to us. Everything else of the
        // form name(ARG) is read as an annotation, which is what makes the module's own name the
        // directive: `#pragma camera(VIEW_MATRIX)` needs nothing registered anywhere.
        static constexpr std::string_view kReserved[] {
            "once", "optimize", "debug", "STDGL", "pack", "warning", "message", "region", "endregion",
        };

        // Comments go first, replaced by spaces so every offset still lines up. Without this a
        // *description* of a pragma — in the header explaining it, or in a project's own shaders —
        // is read as one, and the semantic it appears to declare is nonsense. Found exactly that way.
        std::string source = rawSource;
        for (std::size_t i = 0; i + 1 < source.size(); ++i) {
            if (source[i] != '/') continue;

            if (source[i + 1] == '/') {
                while (i < source.size() && source[i] != '\n') source[i++] = ' ';
            } else if (source[i + 1] == '*') {
                const std::size_t end = source.find("*/", i + 2);
                const std::size_t stop = end == std::string::npos ? source.size() : end + 2;
                for (; i < stop; ++i) if (source[i] != '\n') source[i] = ' ';
            }
        }

        static constexpr std::string_view kPragma = "#pragma";

        std::size_t at = 0;
        while ((at = source.find(kPragma, at)) != std::string::npos) {
            const std::size_t lineEnd = source.find('\n', at);
            const std::size_t limit = lineEnd == std::string::npos ? source.size() : lineEnd;

            // module ( SEMANTIC )
            std::size_t cursor = source.find_first_not_of(" \t", at + kPragma.size());
            const std::size_t open = source.find('(', cursor);
            const std::size_t close = source.find(')', open == std::string::npos ? cursor : open);
            if (cursor == std::string::npos || open == std::string::npos ||
                close == std::string::npos || open > limit || close > limit) {
                at = limit;
                continue;   // not of the annotation shape; some other pragma's business
            }

            std::string moduleName(source, cursor, open - cursor);
            std::erase_if(moduleName, [](const unsigned char c) { return std::isspace(c); });
            if (std::ranges::find(kReserved, moduleName) != std::end(kReserved)) {
                at = limit;
                continue;
            }

            std::string semantic(source, open + 1, close - open - 1);
            std::erase(semantic, '"');   // accepted so both languages can be written alike
            std::erase_if(semantic, [](const unsigned char c) { return std::isspace(c); });
            if (moduleName.empty() || semantic.empty()) { at = limit; continue; }

            // The declaration it decorates: the next line with something on it. Its field name is
            // the last identifier before the ';' or the '[' of an array.
            cursor = limit == source.size() ? source.size() : limit + 1;
            const std::size_t statementEnd = source.find_first_of(";[", cursor);
            if (statementEnd == std::string::npos) break;

            const std::string declaration(source, cursor, statementEnd - cursor);
            const std::size_t nameEnd = declaration.find_last_not_of(" \t\r\n");
            if (nameEnd == std::string::npos) { at = statementEnd; continue; }
            std::size_t nameStart = declaration.find_last_of(" \t\r\n*&", nameEnd);
            nameStart = nameStart == std::string::npos ? 0 : nameStart + 1;

            if (std::string field = declaration.substr(nameStart, nameEnd - nameStart + 1); !field.empty()) {
                _fieldSemantics.emplace(std::move(field), FieldSemantic{ std::move(moduleName), std::move(semantic) });
            }
            at = statementEnd;
        }
    }

    // The block's fields, as the compiled shader lays them out, plus whatever semantic each was
    // annotated with. Offsets and types come from the SPIR-V, so they are the same however the
    // shader was written; only the annotation is language-specific, and that arrives in
    // _fieldSemantics from whichever front end read it. @see semantics.h
    void Shader::fetchBlockMembers(const spirv_cross::Compiler& module,
                                   const spirv_cross::Resource& resource,
                                   std::vector<BlockMember>& members,
                                   glm::u32& blockSize) const
    {
    	const auto& blockType = module.get_type(resource.base_type_id);
    	if (blockType.basetype != spirv_cross::SPIRType::Struct) return;

    	blockSize = static_cast<glm::u32>(module.get_declared_struct_size(blockType));
    	members.reserve(blockType.member_types.size());

    	for (glm::u32 i = 0; i < blockType.member_types.size(); ++i) {
    		const auto& memberType = module.get_type(blockType.member_types[i]);

    		BlockMember member;
    		member.name = module.get_member_name(resource.base_type_id, i);
    		member.offset = module.type_struct_member_offset(blockType, i);
    		member.size = static_cast<glm::u32>(module.get_declared_struct_member_size(blockType, i));
    		member.rows = static_cast<glm::u8>(memberType.vecsize);
    		member.columns = static_cast<glm::u8>(memberType.columns);

    		// Mirrors SemanticSlot::Scalar. Kept as a number here so shader.h does not have to
    		// include semantics.h, which includes resource.h, which would be a cycle.
    		switch (memberType.basetype) {
    		case spirv_cross::SPIRType::Float:  member.scalar = 0; break;
    		case spirv_cross::SPIRType::Int:    member.scalar = 1; break;
    		case spirv_cross::SPIRType::UInt:   member.scalar = 2; break;
    		case spirv_cross::SPIRType::Boolean:member.scalar = 3; break;
    		case spirv_cross::SPIRType::Double: member.scalar = 4; break;
    		default:                            member.scalar = 5; break;
    		}

    		if (const auto it = _fieldSemantics.find(member.name); it != _fieldSemantics.end()) {
    			member.semanticNamespace = it->second.moduleName;
    			member.semantic = it->second.semantic;
    		}
    		members.push_back(std::move(member));
    	}
    }


    namespace {
        // Whether a block's declared offsets and strides are the ones a packing standard
        // prescribes. spirv-cross implements those rules already and keeps them right across
        // corner cases (vec3 in arrays, nested struct alignment, matrix columns); the method is
        // merely protected, so this two-line subclass reaches it rather than writing the rules
        // out a second time and getting one of them subtly wrong.
        struct PackingProbe final : spirv_cross::CompilerGLSL {
            explicit PackingProbe(const std::vector<glm::u32>& spirv) : CompilerGLSL(spirv) {}
            using CompilerGLSL::buffer_is_packing_standard;
        };
    }

    // Every path inside a push-constant block that can be written by itself: each member, each
    // field of a nested struct, each element of an array. The offsets are the compiler's, so a
    // value written through one of these lands where the shader reads it however the two languages
    // disagree about padding — which is the whole point of walking down to the leaves.
    void Shader::flattenPushConstant(const spirv_cross::Compiler& module,
                                     const spirv_cross::SPIRType& type,
                                     const std::string& prefix, const glm::u32 baseOffset,
                                     std::vector<PushConstantField>& out)
    {
    	if (type.basetype != spirv_cross::SPIRType::Struct) return;

    	for (glm::u32 i = 0; i < type.member_types.size(); ++i) {
    		const auto& memberType = module.get_type(type.member_types[i]);
    		const auto name = prefix + module.get_member_name(type.self, i);
    		const auto offset = baseOffset + module.type_struct_member_offset(type, i);
    		const auto size = static_cast<glm::u32>(module.get_declared_struct_member_size(type, i));

    		PushConstantField field;
    		field.name = name;
    		field.offset = offset;
    		field.size = size;
    		field.rows = static_cast<glm::u8>(memberType.vecsize);
    		field.columns = static_cast<glm::u8>(memberType.columns);

    		switch (memberType.basetype) {
    		case spirv_cross::SPIRType::Float:  field.scalar = 0; break;
    		case spirv_cross::SPIRType::Int:    field.scalar = 1; break;
    		case spirv_cross::SPIRType::UInt:   field.scalar = 2; break;
    		case spirv_cross::SPIRType::Boolean:field.scalar = 3; break;
    		case spirv_cross::SPIRType::Double: field.scalar = 4; break;
    		default:                            field.scalar = 5; break;
    		}

    		const bool isArray = !memberType.array.empty();
    		const bool isStruct = memberType.basetype == spirv_cross::SPIRType::Struct;

    		if (field.columns > 1)
    			field.matrixStride = module.type_struct_member_matrix_stride(type, i);

    		if (isArray) {
    			// The array as a whole first, so it can still be written in one go when the C++
    			// side happens to be laid out identically, then each element on its own.
    			field.count = memberType.array[0];
    			field.arrayStride = module.type_struct_member_array_stride(type, i);
    			field.aggregate = isStruct;
    			out.push_back(field);

    			auto elementType = memberType;
    			elementType.array.clear();
    			elementType.array_size_literal.clear();

    			for (glm::u32 element = 0; element < field.count; ++element) {
    				const auto elementOffset = offset + element * field.arrayStride;
    				const auto elementName = std::format("{}[{}]", name, element);

    				if (isStruct) {
    					PushConstantField aggregate = field;
    					aggregate.name = elementName;
    					aggregate.offset = elementOffset;
    					aggregate.size = field.arrayStride;
    					aggregate.count = 1;
    					aggregate.arrayStride = 0;
    					aggregate.aggregate = true;
    					out.push_back(aggregate);
    					flattenPushConstant(module, elementType, elementName + ".", elementOffset, out);
    					continue;
    				}

    				PushConstantField leaf = field;
    				leaf.name = elementName;
    				leaf.offset = elementOffset;
    				leaf.size = field.arrayStride;
    				leaf.count = 1;
    				leaf.arrayStride = 0;
    				out.push_back(leaf);
    			}
    			continue;
    		}

    		if (isStruct) {
    			// Writable whole when the sizes agree, and field by field when they do not.
    			field.aggregate = true;
    			out.push_back(field);
    			flattenPushConstant(module, memberType, name + ".", offset, out);
    			continue;
    		}

    		out.push_back(field);
    	}
    }

    void Shader::fetchMemoryLayout()
    {
    	if (!_valid) return;

    	// GLSL's annotations live in the source, so they are read once the include set is known —
    	// a project keeps its shared blocks in a header, and the pragma travels with them. Slang's
    	// arrive through reflection and are already in place by now.
    	if (_lang == Lang::eGLSL) {
    		_fieldSemantics.clear();
    		for (const auto& dependency : _dependencies) {
    			if (const auto text = utils::ReadFileAsString(dependency); !text.empty())
    				fetchFieldSemantics(text);
    		}
    	}

        const auto module = spirv_cross::Compiler(_spirvCode);
        const auto resources = module.get_shader_resources();
    	const auto stage = StageFrom(module.get_execution_model());

    	// Which globals the entry point actually reaches. Recorded per descriptor rather than
    	// used to filter: an unused binding must stay in the layout (see Descriptor::active).
    	const auto activeVariables = module.get_active_interface_variables();
    	const auto isActive = [&](const spirv_cross::Resource& resource) {
    		return activeVariables.contains(resource.id);
    	};

    	// Raw-pointer access, which reflection can detect but never attribute to a buffer.
    	_usesDeviceAddresses = false;
    	for (const auto capability : module.get_declared_capabilities()) {
    		if (capability == spv::CapabilityPhysicalStorageBufferAddresses) {
    			_usesDeviceAddresses = true;
    			break;
    		}
    	}

    	MemoryLayout memoryLayout;

        for (const auto& input : resources.stage_inputs) {
			auto location = module.get_decoration(input.id, spv::DecorationLocation);
        	auto locationSpan = 1; // TODO: handle location spans for arrays and structs
			const auto& name = module.get_name(input.id);
			const auto& type = module.get_type(input.type_id);
        	const auto[channelType, channelCount] = SPIRTypeConverter(type);

        	// What the input was annotated with, from whichever half of the language knows: GLSL's
        	// `#pragma mesh(POSITION)` is read out of the source and keyed by name, Slang's
        	// `: POSITION` comes through its reflection and is keyed by location, since what Slang
        	// calls a varying rarely survives into the SPIR-V. @see vertexLayout.h
        	std::string semanticNamespace;
        	std::string semantic;
        	if (const auto byName = _fieldSemantics.find(name); byName != _fieldSemantics.end()) {
        		semanticNamespace = byName->second.moduleName;
        		semantic = byName->second.semantic;
        	} else if (const auto byLocation = _inputSemantics.find(location); byLocation != _inputSemantics.end()) {
        		semanticNamespace = byLocation->second.moduleName;
        		semantic = byLocation->second.semantic;
        	}

			memoryLayout.inputs.emplace(location, locationSpan, name, channelType, channelCount,
			                            std::move(semanticNamespace), std::move(semantic));
		} // inputs
		for (const auto& output : resources.stage_outputs) {
			auto location = module.get_decoration(output.id, spv::DecorationLocation);
			auto locationSpan = 1; // TODO: handle location spans for arrays and structs
			auto name = module.get_name(output.id);
			const auto& type = module.get_type(output.type_id);
			const auto[channelType, channelCount] = SPIRTypeConverter(type);

			memoryLayout.outputs.emplace(location, locationSpan, name, channelType, channelCount);
		} // outputs
		for (const auto& sampler : resources.separate_samplers) {
			const uint32_t set = module.get_decoration(sampler.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampler.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampler.type_id));
			const auto& name = module.get_name(sampler.id);

			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eSampler, name, count, stage, AccessKind::eRead, isActive(sampler) });
		} // eSampler
		for (const auto& sampledImage : resources.separate_images) {
			const uint32_t set = module.get_decoration(sampledImage.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampledImage.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampledImage.type_id));
			const auto& name = module.get_name(sampledImage.id);
			const auto shape = ShapeOf(module.get_type(sampledImage.type_id));

			if (module.get_type(sampledImage.type_id).image.dim == spv::DimBuffer) {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eUniformTexelBuffer, name, count, stage, AccessKind::eRead, isActive(sampledImage), {}, 0, {}, shape });
			}
			else {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eSampledImage, name, count, stage, AccessKind::eRead, isActive(sampledImage), {}, 0, {}, shape });
			}
		} // eSampledImage and eUniformTexelBuffer
    	for (const auto& sampledImage : resources.sampled_images) {
			const uint32_t set = module.get_decoration(sampledImage.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampledImage.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampledImage.type_id));
			const auto& name = module.get_name(sampledImage.id);
			const auto shape = ShapeOf(module.get_type(sampledImage.type_id));
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eCombinedImageSampler, name, count, stage, AccessKind::eRead, isActive(sampledImage), {}, 0, {}, shape });
		} // eCombinedImageSampler
		for (const auto& image : resources.storage_images) {
			const uint32_t set = module.get_decoration(image.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(image.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(image.type_id));
			const auto& name = module.get_name(image.id);
			// The variable's own decorations here, unlike storage buffers: an image is not a
			// block, so readonly/writeonly are attached directly to it.
			const auto access = AccessFrom(module.get_decoration_bitset(image.id));
			const bool active = isActive(image);
			const auto shape = ShapeOf(module.get_type(image.type_id));
			if (module.get_type(image.type_id).image.dim == spv::DimBuffer) {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eStorageTexelBuffer, name, count, stage, access, active, {}, 0, {}, shape });
			}
			else {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eStorageImage, name, count, stage, access, active, {}, 0, {}, shape });
			}
		} // eStorageImage and eStorageTexelBuffer
		for (const auto& buffer : resources.uniform_buffers) {
			const uint32_t set = module.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(buffer.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(buffer.type_id));
			const auto& name = module.get_name(buffer.id);
			auto descriptor = Descriptor { DescriptorType::eUniformBuffer, name, count, stage, AccessKind::eRead, isActive(buffer) };
			descriptor.blockName = module.get_name(buffer.base_type_id);
			fetchBlockMembers(module, buffer, descriptor.members, descriptor.blockSize);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::move(descriptor));
		} // eUniformBuffer
		for (const auto& buffer : resources.storage_buffers) {
			const uint32_t set = module.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(buffer.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(buffer.type_id));
			const auto& name = module.get_name(buffer.id);
			// Block flags, not the variable's: NonWritable/NonReadable land on the members.
			const auto access = AccessFrom(module.get_buffer_block_flags(buffer.id));
			auto descriptor = Descriptor { DescriptorType::eStorageBuffer, name, count, stage, access, isActive(buffer) };
			descriptor.blockName = module.get_name(buffer.base_type_id);
			fetchBlockMembers(module, buffer, descriptor.members, descriptor.blockSize);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, std::move(descriptor));
		} // eStorageBuffer
		for (const auto& accelerationStructure : resources.acceleration_structures) {
			const uint32_t set = module.get_decoration(accelerationStructure.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(accelerationStructure.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(accelerationStructure.type_id));
			const auto& name = module.get_name(accelerationStructure.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eAccelerationStructure, name, count, stage, AccessKind::eRead, isActive(accelerationStructure) });
		} // eAccelerationStructure

    	for (const auto& pushConstant : resources.push_constant_buffers) {
			const auto& name = module.get_name(pushConstant.id);
			const auto& type = module.get_type(pushConstant.type_id);
    		// const auto offset = module.get_decoration(pushConstant.id, spv::DecorationOffset);
    		glm::u32 start = 0xFFFFFFFF;
    		glm::u32 end = 0;
    		auto memberCount = type.member_types.size();
			for (uint32_t i = 0; i < memberCount; i++) {
				const auto memberOffset = module.get_member_decoration(type.self, i, spv::DecorationOffset);
				const auto memberSize = module.get_declared_struct_member_size(type, i);
				if (memberOffset < start) start = memberOffset;
				if (memberOffset + memberSize > end) end = memberOffset + memberSize;
			}
    		const auto offset = start;
    		const auto size = end - start;

			// Push-constant blocks are std430 and nothing else. It is the default in Vulkan GLSL
			// and what Slang emits, so this only ever catches a block that asked for something
			// else — and that is worth catching, because the whole point of knowing the layout is
			// that a C++ struct can be written to match it. Under std140 an array of floats
			// strides sixteen bytes instead of four, and a matching struct silently writes one
			// value in four.
			{
				PackingProbe probe(_spirvCode);
				glm::u32 failedIndex = 0;
				if (const auto& probeType = probe.get_type(pushConstant.base_type_id);
					!probe.buffer_is_packing_standard(probeType, spirv_cross::BufferPackingStd430, &failedIndex))
				{
					const auto member = failedIndex < probeType.member_types.size()
						? probe.get_member_name(pushConstant.base_type_id, failedIndex)
						: std::string("<unknown>");
					// Thrown, not logged and shrugged off: at construction guard() turns this into a
					// poisoned shader — and so a poisoned pipeline — while a *reload* that
					// introduces it keeps the last working version, which is the same treatment a
					// syntax error gets.
					throw BackendException(Error{
						.code = ErrorCode::ePushConstantMismatch,
						.message = std::format(
							"Push-constant block '{}' in {} is not laid out as std430 — '{}' does not sit "
							"where std430 puts it. Push constants must be std430, which is the default: "
							"drop any explicit std140 or scalar qualifier on the block.",
							name, _path.filename().string(), member),
					});
				}
			}

			// The same members the range was computed from, kept this time and flattened all the
			// way down: their paths are what CommandBuffer::PushConstant looks a constant up by,
			// and their offsets are absolute within the range, so each one can be pushed on its own.
			PushConstant pushConstantBlock { name, size, offset, stage };
			flattenPushConstant(module, type, {}, 0, pushConstantBlock.members);

			memoryLayout.pushConstants.emplace(offset, std::move(pushConstantBlock));
		} // push constants

    	_memoryLayout = memoryLayout;
    }

    void Shader::OnReload()
    {
    	kor::log::info("Reloading shader: {}", _path.string());

    	_modified = true;

    	// A reload runs on the file-watcher thread, and Compile() throws when the source does not
    	// compile — so an edit that breaks a working shader has to be caught here, or it would tear
    	// the process down. Keep the last good SPIR-V instead: the shader, and everything built from
    	// it, goes on working while the user fixes the error we just printed, and the next save
    	// re-enters here and picks the fix up.
    	//
    	// Only a shader that has *never* compiled becomes poisoned, and that is decided at
    	// construction — where the throw is exactly what guard() turns into the poison.
    	try {
    		Compile();
    		fetchMemoryLayout();
    	} catch (const BackendException& e) {
    		kor::log::error("Reload of '{}' failed; keeping the last working version:\n{}",
    		                _path.string(), e.error.history());
    		_modified = true; // still stale, so the next save retries
    	} catch (const std::exception& e) {
    		kor::log::error("Reload of '{}' failed; keeping the last working version: {}",
    		                _path.string(), e.what());
    		_modified = true;
    	}
    }

    Shader::Shader(const Builder& createInfo) :
        _stage(createInfo.stage),
        _lang(createInfo.lang),
        _path(createInfo.path),
        _module(createInfo.module),
        _entry(createInfo.entry)
    {
        if (_lang == Lang::eSlang) {
            if (_module.empty() || _entry.empty())
                throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                    .message = "A Slang shader requires a module and an entry point." });
        } else if (_path.empty()) {
            throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                .message = "Shader path cannot be empty." });
        }

    	Compile();
    	fetchMemoryLayout();
    }
}
