//
// Created by radue on 28.06.2026.
//

#include "slangCompiler.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <atomic>
#include <cstring>
#include <optional>
#include <vector>

#include <slang.h>
#include <slang-com-ptr.h>

#include "error.h"
#include "log.h"

namespace kor
{
    using Slang::ComPtr;

    namespace
    {
        // One process-wide global session (expensive to create). Sessions (cheap) are made
        // per compile so search paths reflect the current registry.
        ComPtr<slang::IGlobalSession>& globalSession()
        {
            static ComPtr<slang::IGlobalSession> session = [] {
                ComPtr<slang::IGlobalSession> s;

                // GLSL-flavoured Slang — a shader whose first line is `#version` — resolves its
                // vec3/mat4/gl_Position against a `glsl` module that exists only when the *global*
                // session was created with GLSL enabled. SessionDesc::allowGLSLSyntax does not do
                // it and is not needed once this is set: without the flag below every such shader
                // fails with "'glsl' module not available", followed by an undefined identifier for
                // the first vec3 it meets. Enabled unconditionally because the global session is
                // built once per process, long before any shader has said which flavour it is.
                SlangGlobalSessionDesc globalDesc = {};
                globalDesc.enableGLSL = true;

                if (SLANG_FAILED(slang::createGlobalSession(&globalDesc, s.writeRef()))) {
                    throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                        .message = "Failed to create the Slang global session." });
                }
                return s;
            }();
            return session;
        }

        Shader::Stage toGfxStage(const SlangStage stage)
        {
            switch (stage) {
            case SLANG_STAGE_VERTEX:        return Shader::Stage::eVertex;
            case SLANG_STAGE_HULL:          return Shader::Stage::eTessellationControl;
            case SLANG_STAGE_DOMAIN:        return Shader::Stage::eTessellationEvaluation;
            case SLANG_STAGE_GEOMETRY:      return Shader::Stage::eGeometry;
            case SLANG_STAGE_FRAGMENT:      return Shader::Stage::eFragment;
            case SLANG_STAGE_COMPUTE:       return Shader::Stage::eCompute;
            case SLANG_STAGE_AMPLIFICATION: return Shader::Stage::eTask;
            case SLANG_STAGE_MESH:          return Shader::Stage::eMesh;
            case SLANG_STAGE_RAY_GENERATION:return Shader::Stage::eRaygen;
            case SLANG_STAGE_ANY_HIT:       return Shader::Stage::eAnyHit;
            case SLANG_STAGE_CLOSEST_HIT:   return Shader::Stage::eClosestHit;
            case SLANG_STAGE_MISS:          return Shader::Stage::eMiss;
            case SLANG_STAGE_INTERSECTION:  return Shader::Stage::eIntersection;
            case SLANG_STAGE_CALLABLE:      return Shader::Stage::eCallable;
            default:
                throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                    .message = "Slang entry point has an unsupported stage." });
            }
        }

        [[noreturn]] void fail(const std::string& what, slang::IBlob* diagnostics)
        {
            std::string message = what;
            if (diagnostics && diagnostics->getBufferSize() > 0) {
                message += ": ";
                message.append(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
            }
            throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed, .message = std::move(message) });
        }
    }

    // ---- what was compiled before, kept on disk ----------------------------------------------------
    //
    // Compiling a module parses it and everything it imports, every time: a scene of fifty pipelines took seconds to
    // open, each of them. A result is kept by its module, entry and search paths, with every file it was compiled from
    // and that file's size and time; it is taken again only while each is as it was, and while no file of the module's
    // name has come before it in the search paths. KORAL_SHADER_CACHE names where (`off`: nowhere).
    namespace
    {
        constexpr std::uint32_t CacheMagic = 0x4b534c43;    // "KSLC"
        constexpr std::uint32_t CacheVersion = 1;

        std::filesystem::path cacheDirectory()
        {
            if (const char* asked = std::getenv("KORAL_SHADER_CACHE"); asked && *asked)
                return std::string_view(asked) == "off" ? std::filesystem::path() : std::filesystem::path(asked);
#ifdef _WIN32
            if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) return std::filesystem::path(local) / "Koral" / "slang";
#else
            if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::filesystem::path(xdg) / "koral" / "slang";
            if (const char* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".cache" / "koral" / "slang";
#endif
            return {};
        }

        std::uint64_t fnv(const std::string_view text, std::uint64_t hash = 14695981039346656037ull)
        {
            for (const char c : text) { hash ^= static_cast<unsigned char>(c); hash *= 1099511628211ull; }
            return hash;
        }

        /** A file's size and time, as it is now; nothing for one that is not there. */
        std::optional<std::pair<std::uint64_t, std::int64_t>> stamp(const std::filesystem::path& file)
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(file, ec);
            if (ec) return std::nullopt;
            const auto time = std::filesystem::last_write_time(file, ec);
            if (ec) return std::nullopt;
            return std::pair { static_cast<std::uint64_t>(size), static_cast<std::int64_t>(time.time_since_epoch().count()) };
        }

        struct Out {
            std::string bytes;
            template <typename T> requires std::is_arithmetic_v<T> void put(const T value) { bytes.append(reinterpret_cast<const char*>(&value), sizeof value); }
            void text(const std::string_view text) { put<std::uint64_t>(text.size()); bytes.append(text); }
        };
        struct In {
            std::string_view bytes;
            bool ok = true;
            template <typename T> T get()
            {
                T value {};
                if (bytes.size() < sizeof value) { ok = false; return value; }
                std::memcpy(&value, bytes.data(), sizeof value);
                bytes.remove_prefix(sizeof value);
                return value;
            }
            std::string text()
            {
                const auto size = get<std::uint64_t>();
                if (!ok || bytes.size() < size) { ok = false; return {}; }
                std::string value(bytes.substr(0, size));
                bytes.remove_prefix(size);
                return value;
            }
        };

        std::string serialize(const SlangCompileResult& result)
        {
            Out out;
            out.put(CacheMagic);
            out.put(CacheVersion);
            out.put(static_cast<std::uint32_t>(result.stage));
            out.put<std::uint64_t>(result.spirv.size());
            out.bytes.append(reinterpret_cast<const char*>(result.spirv.data()), result.spirv.size() * sizeof(kor::u32));
            out.text(result.resolvedPath.string());
            out.put<std::uint64_t>(result.dependencies.size());
            for (const auto& dependency : result.dependencies) {
                const auto now = stamp(dependency);
                out.text(dependency.string());
                out.put<std::uint64_t>(now ? now->first : 0);
                out.put<std::int64_t>(now ? now->second : 0);
            }
            out.put<std::uint64_t>(result.fieldSemantics.size());
            for (const auto& [field, semantic] : result.fieldSemantics) { out.text(field); out.text(semantic.moduleName); out.text(semantic.semantic); }
            out.put<std::uint64_t>(result.varyingSemantics.size());
            for (const auto& [location, semantic] : result.varyingSemantics) { out.put(location); out.text(semantic.moduleName); out.text(semantic.semantic); }
            out.put<std::uint64_t>(result.pushHandles.size());
            for (const auto& [path, kind] : result.pushHandles) { out.text(path); out.put(static_cast<std::uint8_t>(kind)); }
            out.put<std::uint64_t>(result.entryParameters.size());
            for (const auto& name : result.entryParameters) out.text(name);
            return std::move(out.bytes);
        }

        /** A result kept, while every file it was compiled from is as it was; nothing otherwise. */
        std::optional<SlangCompileResult> deserialize(const std::string_view bytes)
        {
            In in { bytes };
            if (in.get<std::uint32_t>() != CacheMagic || in.get<std::uint32_t>() != CacheVersion) return std::nullopt;
            SlangCompileResult result;
            result.stage = static_cast<Shader::Stage>(in.get<std::uint32_t>());
            const auto words = in.get<std::uint64_t>();
            if (!in.ok || in.bytes.size() < words * sizeof(kor::u32)) return std::nullopt;
            result.spirv.resize(words);
            std::memcpy(result.spirv.data(), in.bytes.data(), words * sizeof(kor::u32));
            in.bytes.remove_prefix(words * sizeof(kor::u32));
            result.resolvedPath = in.text();
            for (auto count = in.get<std::uint64_t>(); in.ok && count > 0; --count) {
                std::filesystem::path dependency = in.text();
                const auto size = in.get<std::uint64_t>();
                const auto time = in.get<std::int64_t>();
                const auto now = stamp(dependency);
                if (!in.ok || !now || now->first != size || now->second != time) return std::nullopt;     // changed since
                result.dependencies.push_back(std::move(dependency));
            }
            for (auto count = in.get<std::uint64_t>(); in.ok && count > 0; --count) {
                std::string field = in.text(), module = in.text(), semantic = in.text();
                result.fieldSemantics.emplace(std::move(field), SlangCompileResult::FieldSemantic { std::move(module), std::move(semantic) });
            }
            for (auto count = in.get<std::uint64_t>(); in.ok && count > 0; --count) {
                const auto location = in.get<kor::u32>();
                std::string module = in.text(), semantic = in.text();
                result.varyingSemantics.emplace(location, SlangCompileResult::FieldSemantic { std::move(module), std::move(semantic) });
            }
            for (auto count = in.get<std::uint64_t>(); in.ok && count > 0; --count) {
                std::string path = in.text();
                result.pushHandles.emplace(std::move(path), static_cast<BindlessKind>(in.get<std::uint8_t>()));
            }
            for (auto count = in.get<std::uint64_t>(); in.ok && count > 0; --count) result.entryParameters.insert(in.text());
            if (!in.ok) return std::nullopt;
            return result;
        }

        /** Whether a file of the module's name is found before the one it was compiled from: then it would be that one. */
        bool shadowed(const std::string& module, const std::filesystem::path& resolved, const std::vector<std::filesystem::path>& searchPaths)
        {
            std::string file = module;
            std::ranges::replace(file, '.', '/');
            std::error_code ec;
            for (const auto& path : searchPaths) {
                for (const std::string& name : { file, [&] { std::string dashed = file; std::ranges::replace(dashed, '_', '-'); return dashed; }() }) {
                    const auto candidate = path / (name + ".slang");
                    if (!std::filesystem::exists(candidate, ec)) continue;
                    return !std::filesystem::equivalent(candidate, resolved, ec);
                }
            }
            return false;
        }
    }

    namespace { SlangCompileResult compileNow(const std::string& module, const std::string& entry, const std::vector<std::filesystem::path>& searchPaths); }

    SlangCompileResult SlangCompiler::Compile(const std::string& module,
                                              const std::string& entry,
                                              const std::vector<std::filesystem::path>& searchPaths)
    {
        const std::filesystem::path directory = cacheDirectory();
        if (directory.empty()) return compileNow(module, entry, searchPaths);
        std::string what = std::format("{}\n{}\n{}", globalSession()->getBuildTagString(), module, entry);
        for (const auto& path : searchPaths) what += "\n" + path.lexically_normal().string();
        const auto file = directory / std::format("{}-{:016x}.kslc", module, fnv(what));
        {
            std::ifstream in(file, std::ios::binary);
            if (in) {
                const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (auto kept = deserialize(bytes); kept && !shadowed(module, kept->resolvedPath, searchPaths)) return std::move(*kept);
            }
        }
        SlangCompileResult result = compileNow(module, entry, searchPaths);
        // Kept for the next time — written whole and then put in place, so that a reader never finds half of it.
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        static std::atomic<std::uint64_t> written { 0 };
        const auto partial = std::filesystem::path(file).concat(std::format(".{}.part", written++));
        {
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            const std::string bytes = serialize(result);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        std::filesystem::rename(partial, file, ec);
        if (ec) std::filesystem::remove(partial, ec);
        return result;
    }

    namespace {
    SlangCompileResult compileNow(const std::string& module,
                                  const std::string& entry,
                                  const std::vector<std::filesystem::path>& searchPaths)
    {
        // Per-compile session carrying the current search paths and a SPIR-V target.
        std::vector<std::string> pathStrings;
        pathStrings.reserve(searchPaths.size());
        for (const auto& p : searchPaths) pathStrings.push_back(p.string());
        std::vector<const char*> pathPtrs;
        pathPtrs.reserve(pathStrings.size());
        for (const auto& s : pathStrings) pathPtrs.push_back(s.c_str());

        slang::TargetDesc targetDesc = {};
        targetDesc.format  = SLANG_SPIRV;
        targetDesc.profile = globalSession()->findProfile("spirv_1_5");

        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets         = &targetDesc;
        sessionDesc.targetCount     = 1;
        sessionDesc.searchPaths     = pathPtrs.data();
        sessionDesc.searchPathCount = static_cast<SlangInt>(pathPtrs.size());

        ComPtr<slang::ISession> session;
        if (SLANG_FAILED(globalSession()->createSession(sessionDesc, session.writeRef()))) {
            throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                .message = "Failed to create a Slang session." });
        }

        ComPtr<slang::IBlob> diagnostics;
        slang::IModule* slangModule = session->loadModule(module.c_str(), diagnostics.writeRef());
        if (!slangModule) fail("Failed to load Slang module '" + module + "'", diagnostics);

        ComPtr<slang::IEntryPoint> entryPoint;
        if (SLANG_FAILED(slangModule->findEntryPointByName(entry.c_str(), entryPoint.writeRef())) || !entryPoint) {
            throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                .message = "Slang module '" + module + "' has no entry point '" + entry + "'." });
        }

        slang::IComponentType* components[] = { slangModule, entryPoint.get() };
        ComPtr<slang::IComponentType> composed;
        if (SLANG_FAILED(session->createCompositeComponentType(components, 2, composed.writeRef(), diagnostics.writeRef()))) {
            fail("Failed to compose Slang program", diagnostics);
        }

        ComPtr<slang::IComponentType> linked;
        if (SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef()))) {
            fail("Failed to link Slang program", diagnostics);
        }

        ComPtr<slang::IBlob> spirvBlob;
        if (SLANG_FAILED(linked->getEntryPointCode(0, 0, spirvBlob.writeRef(), diagnostics.writeRef())) || !spirvBlob) {
            fail("Failed to generate SPIR-V for '" + module + ":" + entry + "'", diagnostics);
        }

        SlangCompileResult result;
        const auto* code = static_cast<const kor::u32*>(spirvBlob->getBufferPointer());
        const auto words = spirvBlob->getBufferSize() / sizeof(kor::u32);
        result.spirv.assign(code, code + words);

        // Stage from reflection of the linked entry point.
        if (slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef())) {
            if (slang::EntryPointReflection* ep = layout->getEntryPointByIndex(0)) {
                result.stage = toGfxStage(ep->getStage());
            } else {
                throw BackendException(Error{ .code = ErrorCode::eShaderCompileFailed,
                    .message = "Could not reflect the Slang entry point '" + entry + "'." });
            }
        } else {
            fail("Failed to reflect Slang program", diagnostics);
        }

        // The [Kor("SEMANTIC")] annotations on block members.
        //
        // A user attribute, not a `: SEMANTIC` — Slang parses semantics on uniform members and then
        // drops them: reflection reports nothing for them, and nothing reaches the SPIR-V either.
        // User attributes are the one annotation it keeps, argument string and all. @see semantics.h
        if (slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef())) {
            const auto annotations = [&](slang::VariableLayoutReflection* field) {
                slang::VariableReflection* variable = field->getVariable();
                if (!variable) return;
                for (unsigned a = 0; a < variable->getUserAttributeCount(); ++a) {
                    slang::UserAttribute* attribute = variable->getUserAttributeByIndex(a);
                    const char* name = attribute ? attribute->getName() : nullptr;
                    if (!name) continue;

                    // The attribute's own name is the module: [camera("VIEW_MATRIX")] is
                    // the camera module's VIEW_MATRIX, and the module ships the attribute
                    // it is named by. Anything with a string argument is read this way, so
                    // a new module needs no change here.
                    std::size_t length = 0;
                    if (const char* value = attribute->getArgumentValueString(0, &length)) {
                        std::string semantic(value, length);
                        std::erase(semantic, '"');   // the argument arrives quoted
                        result.fieldSemantics.emplace(
                            field->getName(), SlangCompileResult::FieldSemantic{ name, std::move(semantic) });
                    }
                }
            };
            const auto collect = [&](auto&& self, slang::TypeLayoutReflection* typeLayout) -> void {
                if (!typeLayout) return;

                // Unwrap the container a block arrives in — ConstantBuffer<T>, ParameterBlock<T>, and the
                // element of a StructuredBuffer<T> — until the struct itself is in hand.
                while (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
                       typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock ||
                       typeLayout->getKind() == slang::TypeReflection::Kind::Resource) {
                    typeLayout = typeLayout->getElementTypeLayout();
                    if (!typeLayout) return;
                }
                if (typeLayout->getKind() != slang::TypeReflection::Kind::Struct) return;

                for (unsigned f = 0; f < typeLayout->getFieldCount(); ++f) {
                    slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(f);
                    if (!field || !field->getName()) continue;
                    annotations(field);
                    self(self, field->getTypeLayout());   // nested structs annotate too
                }
            };

            for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
                if (slang::VariableLayoutReflection* parameter = layout->getParameterByIndex(i)) {
                    // A parameter itself is annotated by name as a field is: [entity("PER_INSTANCE")] on a buffer.
                    if (parameter->getName()) annotations(parameter);
                    collect(collect, parameter->getTypeLayout());
                }
            }
        }

        // The `T.Handle` push constants, and which bindless table each indexes.
        //
        // A DescriptorHandle is laid out as a uint2 — that is all the SPIR-V, and the layout reflection,
        // keep of it. The field's declared type still names it, and the resource inside says the table.
        if (slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef())) {
            const auto kindOf = [](slang::TypeReflection* type) -> std::optional<BindlessKind> {
                if (!type) return std::nullopt;
                ComPtr<ISlangBlob> name;
                if (SLANG_FAILED(type->getFullName(name.writeRef())) || !name) return std::nullopt;
                const std::string_view full(static_cast<const char*>(name->getBufferPointer()), name->getBufferSize());
                constexpr std::string_view handle = "DescriptorHandle<";
                if (!full.starts_with(handle)) return std::nullopt;
                const auto inner = full.substr(handle.size());
                const auto is = [&](std::initializer_list<std::string_view> prefixes) {
                    return std::ranges::any_of(prefixes, [&](const std::string_view p) { return inner.starts_with(p); });
                };
                // Order matters: "RWTexture" before "Texture", and so on.
                if (is({ "SamplerState", "SamplerComparisonState" })) return BindlessKind::eSampler;
                if (is({ "RWTexture", "RasterizerOrderedTexture" })) return BindlessKind::eStorageImage;
                if (is({ "Texture" })) return BindlessKind::eSampledImage;
                if (is({ "ConstantBuffer" })) return BindlessKind::eUniformBuffer;
                if (is({ "StructuredBuffer", "RWStructuredBuffer", "ByteAddressBuffer", "RWByteAddressBuffer",
                         "AppendStructuredBuffer", "ConsumeStructuredBuffer", "RasterizerOrderedStructuredBuffer",
                         "RasterizerOrderedByteAddressBuffer" })) return BindlessKind::eStorageBuffer;
                if (is({ "RaytracingAccelerationStructure" })) return BindlessKind::eAccelerationStructure;
                return BindlessKind::eNone;   // a handle to something with no table: a texel buffer, a combined sampler
            };

            const auto collect = [&](auto&& self, slang::TypeLayoutReflection* typeLayout, const std::string& prefix) -> void {
                while (typeLayout && (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
                                      typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock))
                    typeLayout = typeLayout->getElementTypeLayout();
                if (!typeLayout || typeLayout->getKind() != slang::TypeReflection::Kind::Struct) return;

                for (unsigned f = 0; f < typeLayout->getFieldCount(); ++f) {
                    slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(f);
                    if (!field || !field->getName()) continue;
                    const std::string path = prefix + field->getName();
                    slang::TypeReflection* declared = field->getVariable() ? field->getVariable()->getType() : nullptr;
                    while (declared && declared->getKind() == slang::TypeReflection::Kind::Array)
                        declared = declared->getElementType();
                    if (const auto kind = kindOf(declared)) {
                        result.pushHandles.emplace(path, *kind);
                        continue;
                    }
                    slang::TypeLayoutReflection* inner = field->getTypeLayout();
                    while (inner && inner->getKind() == slang::TypeReflection::Kind::Array)
                        inner = inner->getElementTypeLayout();
                    self(self, inner, path + ".");
                }
            };

            // A push-constant block's fields are named bare ("output"); an entry point's `uniform` parameters
            // become one block of their own, its members named by the parameter ("push.output").
            for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
                if (slang::VariableLayoutReflection* parameter = layout->getParameterByIndex(i))
                    collect(collect, parameter->getTypeLayout(), "");
            }
            if (slang::EntryPointReflection* ep = layout->getEntryPointByIndex(0)) {
                for (unsigned p = 0; p < ep->getParameterCount(); ++p) {
                    slang::VariableLayoutReflection* parameter = ep->getParameterByIndex(p);
                    if (!parameter || !parameter->getName()) continue;
                    result.entryParameters.emplace(parameter->getName());
                    slang::TypeReflection* declared = parameter->getVariable() ? parameter->getVariable()->getType() : nullptr;
                    if (const auto kind = kindOf(declared)) { result.pushHandles.emplace(parameter->getName(), *kind); continue; }
                    collect(collect, parameter->getTypeLayout(), std::string(parameter->getName()) + ".");
                }
            }
        }

        // The `: SEMANTIC` on the entry point's stage inputs.
        //
        // Varyings are the one thing Slang does not drop the semantic from, so a vertex input needs
        // no user attribute: `float3 position : POSITION` is already the annotation. Keyed by
        // location, which is the only name shared with the SPIR-V — Slang's own parameter names do
        // not survive into it. @see vertexLayout.h
        if (slang::ProgramLayout* layout = linked->getLayout(0, diagnostics.writeRef())) {
            if (slang::EntryPointReflection* ep = layout->getEntryPointByIndex(0)) {
                const auto isVaryingInput = [](slang::VariableLayoutReflection* variable) {
                    for (unsigned c = 0; c < variable->getCategoryCount(); ++c) {
                        if (variable->getCategoryByIndex(c) == slang::ParameterCategory::VaryingInput)
                            return true;
                    }
                    return false;
                };

                // Locations nest: an input struct's fields are offset from where the struct itself
                // starts, so the walk carries the parent's location down with it.
                const auto collect = [&](auto&& self, slang::VariableLayoutReflection* variable,
                                         const kor::u32 baseLocation) -> void {
                    if (!variable) return;

                    const auto location = baseLocation + static_cast<kor::u32>(
                        variable->getOffset(SLANG_PARAMETER_CATEGORY_VARYING_INPUT));

                    if (slang::TypeLayoutReflection* typeLayout = variable->getTypeLayout();
                        typeLayout && typeLayout->getKind() == slang::TypeReflection::Kind::Struct) {
                        for (unsigned f = 0; f < typeLayout->getFieldCount(); ++f)
                            self(self, typeLayout->getFieldByIndex(f), location);
                        return;
                    }

                    // System values (SV_Position and the rest) carry semantics too, but they are
                    // not read from a vertex buffer and their offset is in another category
                    // entirely — recording them would claim location 0 for something the layout
                    // must never feed.
                    if (!isVaryingInput(variable)) return;

                    if (const char* semantic = variable->getSemanticName(); semantic && *semantic) {
                        result.varyingSemantics.emplace(
                            location, SlangCompileResult::FieldSemantic{ std::string(), semantic });
                    }
                };

                for (unsigned p = 0; p < ep->getParameterCount(); ++p)
                    collect(collect, ep->getParameterByIndex(p), 0);
            }
        }

        if (const char* filePath = slangModule->getFilePath()) {
            result.resolvedPath = filePath;
        }

        // Every file the module was built from (its own source plus any imported/included
        // modules), so the caller can watch all of them for hot-reload.
        const SlangInt32 dependencyCount = slangModule->getDependencyFileCount();
        result.dependencies.reserve(dependencyCount);
        for (SlangInt32 i = 0; i < dependencyCount; ++i) {
            if (const char* dependency = slangModule->getDependencyFilePath(i)) {
                result.dependencies.emplace_back(dependency);
            }
        }
        return result;
    }
    }
}
