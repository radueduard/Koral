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

    void Shader::fetchMemoryLayout()
    {
    	if (!_valid) return;

        const auto module = spirv_cross::Compiler(_spirvCode);
        const auto resources = module.get_shader_resources();
    	const auto stage = StageFrom(module.get_execution_model());

    	MemoryLayout memoryLayout;

        for (const auto& input : resources.stage_inputs) {
			auto location = module.get_decoration(input.id, spv::DecorationLocation);
        	auto locationSpan = 1; // TODO: handle location spans for arrays and structs
			const auto& name = module.get_name(input.id);
			const auto& type = module.get_type(input.type_id);
        	const auto[channelType, channelCount] = SPIRTypeConverter(type);

			memoryLayout.inputs.emplace(location, locationSpan, name, channelType, channelCount);
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

			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eSampler, name, count, stage });
		} // eSampler
		for (const auto& sampledImage : resources.separate_images) {
			const uint32_t set = module.get_decoration(sampledImage.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampledImage.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampledImage.type_id));
			const auto& name = module.get_name(sampledImage.id);
			if (module.get_type(sampledImage.type_id).image.dim == spv::DimBuffer) {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eUniformTexelBuffer, name, count, stage });
			}
			else {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eSampledImage, name, count, stage });
			}
		} // eSampledImage and eUniformTexelBuffer
    	for (const auto& sampledImage : resources.sampled_images) {
			const uint32_t set = module.get_decoration(sampledImage.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(sampledImage.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(sampledImage.type_id));
			const auto& name = module.get_name(sampledImage.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eCombinedImageSampler, name, count, stage });
		} // eCombinedImageSampler
		for (const auto& image : resources.storage_images) {
			const uint32_t set = module.get_decoration(image.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(image.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(image.type_id));
			const auto& name = module.get_name(image.id);
			if (module.get_type(image.type_id).image.dim == spv::DimBuffer) {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eStorageTexelBuffer, name, count, stage });
			}
			else {
				memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eStorageImage, name, count, stage });
			}
		} // eStorageImage and eStorageTexelBuffer
		for (const auto& buffer : resources.uniform_buffers) {
			const uint32_t set = module.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(buffer.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(buffer.type_id));
			const auto& name = module.get_name(buffer.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eUniformBuffer, name, count, stage });
		} // eUniformBuffer
		for (const auto& buffer : resources.storage_buffers) {
			const uint32_t set = module.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(buffer.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(buffer.type_id));
			const auto& name = module.get_name(buffer.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eStorageBuffer, name, count, stage });
		} // eStorageBuffer
		for (const auto& accelerationStructure : resources.acceleration_structures) {
			const uint32_t set = module.get_decoration(accelerationStructure.id, spv::DecorationDescriptorSet);
			const uint32_t binding = module.get_decoration(accelerationStructure.id, spv::DecorationBinding);
			const uint32_t count = GetCount(module.get_type(accelerationStructure.type_id));
			const auto& name = module.get_name(accelerationStructure.id);
			memoryLayout.descriptorSets[set].descriptors.emplace(binding, Descriptor { DescriptorType::eAccelerationStructure, name, count, stage });
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

			memoryLayout.pushConstants.emplace(offset, PushConstant { name, size, offset, stage });
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
