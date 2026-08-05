//
// Created by radue on 28.06.2026.
//

#include "slangCompiler.h"

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
                if (SLANG_FAILED(slang::createGlobalSession(s.writeRef()))) {
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

    SlangCompileResult SlangCompiler::Compile(const std::string& module,
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
        const auto* code = static_cast<const glm::u32*>(spirvBlob->getBufferPointer());
        const auto words = spirvBlob->getBufferSize() / sizeof(glm::u32);
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
            const auto collect = [&](auto&& self, slang::TypeLayoutReflection* typeLayout) -> void {
                if (!typeLayout) return;

                // Unwrap the container a block arrives in — ConstantBuffer<T>, ParameterBlock<T> —
                // until the struct itself is in hand.
                while (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
                       typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock) {
                    typeLayout = typeLayout->getElementTypeLayout();
                    if (!typeLayout) return;
                }
                if (typeLayout->getKind() != slang::TypeReflection::Kind::Struct) return;

                for (unsigned f = 0; f < typeLayout->getFieldCount(); ++f) {
                    slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(f);
                    if (!field || !field->getName()) continue;

                    if (slang::VariableReflection* variable = field->getVariable()) {
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
                    }
                    self(self, field->getTypeLayout());   // nested structs annotate too
                }
            };

            for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
                if (slang::VariableLayoutReflection* parameter = layout->getParameterByIndex(i))
                    collect(collect, parameter->getTypeLayout());
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
                                         const glm::u32 baseLocation) -> void {
                    if (!variable) return;

                    const auto location = baseLocation + static_cast<glm::u32>(
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
