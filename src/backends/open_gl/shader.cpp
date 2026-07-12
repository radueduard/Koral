//
// Created by radue on 2/21/2026.
//

#include "shader.h"

#include <array>
#include <unordered_set>

#include <file.h>
#include "ogl_err_handling.h"

#include <spirv_cross/spirv_glsl.hpp>

namespace kor::ogl
{
    namespace {
        // Vulkan bindless shaders declare descriptor arrays with no size
        // (`uniform texture2D maps[]`, an OpTypeRuntimeArray). OpenGL GLSL has no
        // runtime descriptor arrays, so spirv-cross throws when targeting GL. The
        // engine sizes these to a fixed maximum on Vulkan (descriptorSetLayout.cpp:
        // count == 0 ? kBindlessDescriptorCount), so here we rewrite each descriptor
        // runtime array to a fixed-size array of the same count before decompiling.
        //
        // Only arrays whose element is an opaque descriptor type (image / sampler /
        // sampled image) are rewritten — a runtime array of scalars/structs is the
        // legal trailing member of an SSBO block and must stay unsized.
        constexpr uint32_t kBindlessDescriptorCount = 256; // keep in sync with vk descriptorSetLayout

        std::vector<uint32_t> sizeRuntimeDescriptorArrays(const std::vector<uint32_t>& code, const uint32_t fixedSize)
        {
            if (code.size() < 5 || code[0] != spv::MagicNumber)
                return code;

            // Pass 1: collect opaque-descriptor type ids, and reuse an existing uint
            // type / matching constant if one is declared before the first array we
            // need to patch (guaranteeing correct define-before-use ordering).
            std::unordered_set<uint32_t> opaqueTypes;
            uint32_t uintTypeId = 0;
            uint32_t constId = 0;
            bool anyToPatch = false;

            for (size_t i = 5; i < code.size();) {
                const uint16_t wc = static_cast<uint16_t>(code[i] >> 16);
                const uint16_t op = static_cast<uint16_t>(code[i] & 0xFFFFu);
                if (wc == 0) break;
                switch (op) {
                    case spv::OpTypeImage:
                    case spv::OpTypeSampler:
                    case spv::OpTypeSampledImage:
                        opaqueTypes.insert(code[i + 1]);
                        break;
                    case spv::OpTypeInt:
                        if (!anyToPatch && uintTypeId == 0 && code[i + 2] == 32 && code[i + 3] == 0)
                            uintTypeId = code[i + 1];
                        break;
                    case spv::OpConstant:
                        if (!anyToPatch && uintTypeId != 0 && code[i + 1] == uintTypeId && code[i + 3] == fixedSize)
                            constId = code[i + 2];
                        break;
                    case spv::OpTypeRuntimeArray:
                        if (opaqueTypes.contains(code[i + 2]))
                            anyToPatch = true;
                        break;
                    default:
                        break;
                }
                i += wc;
            }
            if (!anyToPatch)
                return code;

            uint32_t bound = code[3];
            const bool needUintType = uintTypeId == 0;
            if (needUintType) uintTypeId = bound++;
            const bool needConst = constId == 0;
            if (needConst) constId = bound++;

            // Pass 2: rebuild, injecting the uint type + length constant right before
            // the first patched array and converting each descriptor runtime array to
            // a fixed-size array.
            std::vector<uint32_t> out;
            out.reserve(code.size() + 8);
            for (int k = 0; k < 5; ++k) out.push_back(code[k]);
            out[3] = bound;

            bool injected = false;
            for (size_t i = 5; i < code.size();) {
                const uint16_t wc = static_cast<uint16_t>(code[i] >> 16);
                const uint16_t op = static_cast<uint16_t>(code[i] & 0xFFFFu);
                const bool isDescRuntimeArray = op == spv::OpTypeRuntimeArray && opaqueTypes.contains(code[i + 2]);

                // Now that every descriptor runtime array is a fixed array, the
                // RuntimeDescriptorArray capability is unused. spirv-cross rejects it
                // outright for GL (it gates GL_EXT_nonuniform_qualifier), so drop it —
                // the ShaderNonUniform capability stays and maps to GL_NV_gpu_shader5.
                if (op == spv::OpCapability && code[i + 1] == spv::CapabilityRuntimeDescriptorArray) {
                    i += wc;
                    continue;
                }

                if (isDescRuntimeArray && !injected) {
                    if (needUintType) {
                        out.push_back((4u << 16) | spv::OpTypeInt);
                        out.push_back(uintTypeId);
                        out.push_back(32);
                        out.push_back(0);
                    }
                    if (needConst) {
                        out.push_back((4u << 16) | spv::OpConstant);
                        out.push_back(uintTypeId);
                        out.push_back(constId);
                        out.push_back(fixedSize);
                    }
                    injected = true;
                }

                if (isDescRuntimeArray) {
                    out.push_back((4u << 16) | spv::OpTypeArray);
                    out.push_back(code[i + 1]); // result id
                    out.push_back(code[i + 2]); // element type id
                    out.push_back(constId);     // length id
                } else {
                    for (uint16_t w = 0; w < wc; ++w) out.push_back(code[i + w]);
                }
                i += wc;
            }
            return out;
        }

        // SPIR-V 1.6 lowers `discard` to OpDemoteToHelperInvocation (execution
        // continues as a helper lane). spirv-cross rejects that for GL, so lower it
        // back to OpKill — classic GLSL `discard`, a block terminator. glslang emits
        // demote as the last non-terminator of its block, immediately followed by the
        // block's branch/return; OpKill subsumes that terminator, so we drop the
        // following branch. Also strip the now-unused capability.
        std::vector<uint32_t> lowerDemoteToKill(const std::vector<uint32_t>& code)
        {
            if (code.size() < 5 || code[0] != spv::MagicNumber)
                return code;

            bool hasDemote = false;
            for (size_t i = 5; i < code.size();) {
                const uint16_t wc = static_cast<uint16_t>(code[i] >> 16);
                if (wc == 0) break;
                if ((code[i] & 0xFFFFu) == spv::OpDemoteToHelperInvocation) { hasDemote = true; break; }
                i += wc;
            }
            if (!hasDemote)
                return code;

            const auto isTerminator = [](const uint16_t op) {
                return op == spv::OpBranch || op == spv::OpBranchConditional ||
                       op == spv::OpReturn || op == spv::OpReturnValue ||
                       op == spv::OpUnreachable || op == spv::OpKill;
            };

            std::vector<uint32_t> out;
            out.reserve(code.size());
            for (int k = 0; k < 5; ++k) out.push_back(code[k]);

            bool dropNextTerminator = false;
            for (size_t i = 5; i < code.size();) {
                const uint16_t wc = static_cast<uint16_t>(code[i] >> 16);
                const uint16_t op = static_cast<uint16_t>(code[i] & 0xFFFFu);

                if (op == spv::OpCapability && code[i + 1] == spv::CapabilityDemoteToHelperInvocation) {
                    i += wc;
                    continue;
                }
                if (op == spv::OpDemoteToHelperInvocation) {
                    out.push_back((1u << 16) | spv::OpKill);
                    dropNextTerminator = true;
                    i += wc;
                    continue;
                }
                if (dropNextTerminator) {
                    dropNextTerminator = false;
                    if (isTerminator(op)) { // subsumed by the OpKill we just emitted
                        i += wc;
                        continue;
                    }
                }
                for (uint16_t w = 0; w < wc; ++w) out.push_back(code[i + w]);
                i += wc;
            }
            return out;
        }

        // Remove the ShaderNonUniform capability and every NonUniform decoration.
        // spirv-cross maps ShaderNonUniform to GL_NV_gpu_shader5 (an NVIDIA-only
        // extension Mesa lacks). We index the material descriptor arrays through
        // GL_ARB_bindless_texture instead, where dynamic indexing needs no such
        // capability, so the qualifier is pure dead weight for the GL output.
        std::vector<uint32_t> stripNonUniform(const std::vector<uint32_t>& code)
        {
            if (code.size() < 5 || code[0] != spv::MagicNumber)
                return code;

            std::vector<uint32_t> out;
            out.reserve(code.size());
            for (int k = 0; k < 5; ++k) out.push_back(code[k]);

            for (size_t i = 5; i < code.size();) {
                const uint16_t wc = static_cast<uint16_t>(code[i] >> 16);
                const uint16_t op = static_cast<uint16_t>(code[i] & 0xFFFFu);
                if (wc == 0) break;

                const bool dropCap = op == spv::OpCapability && code[i + 1] == spv::CapabilityShaderNonUniform;
                const bool dropDec = (op == spv::OpDecorate || op == spv::OpDecorateId) &&
                                     code[i + 2] == spv::DecorationNonUniform;
                if (dropCap || dropDec) {
                    i += wc;
                    continue;
                }
                for (uint16_t w = 0; w < wc; ++w) out.push_back(code[i + w]);
                i += wc;
            }
            return out;
        }
    }

    Shader::Shader(const kor::Shader::Builder& createInfo) : kor::Shader(createInfo)
    {}

    std::string Shader::TranspileSPIRVToGLSL(const std::vector<uint32_t>& spirvCode, const Stage stage) {
        spirv_cross::CompilerGLSL compiler(spirvCode);
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        // Set GLSL options
        spirv_cross::CompilerGLSL::Options options;
        options.version = 450; // Target GLSL version
        options.es = false; // Not targeting OpenGL ES
        options.emit_push_constant_as_uniform_buffer = true;
        options.emit_uniform_buffer_as_plain_uniforms = false;
        compiler.set_common_options(options);

        // Handle shader inputs, outputs, and uniforms as needed
        // For example, you can remap resource bindings here if necessary

        return compiler.compile();
    }

    Shader::~Shader()
    {

    }

    GLuint Shader::compile(const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& rebindings,
                           const std::optional<glm::u32> pushConstantBinding,
                           std::vector<BindlessSamplerArray>* outBindless) const
    {
        // Vulkan-style separate texture2D + sampler (and bindless runtime arrays of
        // them) aren't expressible in OpenGL GLSL. Size the runtime descriptor arrays
        // to a fixed count, then have spirv-cross fold each (image, sampler) pair into
        // a combined sampler2D — the only form GL accepts.
        std::vector<uint32_t> patchedCode = sizeRuntimeDescriptorArrays(_spirvCode, kBindlessDescriptorCount);
        patchedCode = lowerDemoteToKill(patchedCode);
        patchedCode = stripNonUniform(patchedCode);
        spirv_cross::CompilerGLSL compiler(patchedCode);

        // texelFetch on a separate image needs a sampler to combine with; inject a
        // dummy one if required (must precede build_combined_image_samplers()).
        if (const auto dummySampler = compiler.build_dummy_sampler_for_combined_images(); dummySampler != 0) {
            compiler.set_decoration(dummySampler, spv::DecorationDescriptorSet, 0);
            compiler.set_decoration(dummySampler, spv::DecorationBinding, 0);
        }
        compiler.build_combined_image_samplers();

        // The generated combined samplers carry no decorations. Give each one the
        // set/binding of the image it came from so it remaps to the same GL binding
        // point the application binds that sampled-image descriptor to. Array combined
        // samplers are the bindless material tables: they can't fit in texture units,
        // so instead of a binding they become GL_ARB_bindless_texture arrays — record
        // them, and leave them undecorated so they emit as a plain (handle-settable)
        // uniform. (The separate sampler descriptor is folded away; its point is unused.)
        std::vector<std::string> bindlessUniformNames;
        for (const auto& combined : compiler.get_combined_image_samplers()) {
            const auto& type = compiler.get_type_from_variable(combined.combined_id);
            const bool isArray = !type.array.empty();
            if (isArray && outBindless != nullptr) {
                BindlessSamplerArray info;
                info.imageSet = compiler.get_decoration(combined.image_id, spv::DecorationDescriptorSet);
                info.imageBinding = compiler.get_decoration(combined.image_id, spv::DecorationBinding);
                info.samplerSet = compiler.get_decoration(combined.sampler_id, spv::DecorationDescriptorSet);
                info.samplerBinding = compiler.get_decoration(combined.sampler_id, spv::DecorationBinding);
                info.count = type.array[0] ? type.array[0] : kBindlessDescriptorCount;
                // Freshly-combined samplers have no name yet (get_name is empty), so
                // give them a deterministic one we can both find in the text and query
                // as a uniform location after link.
                info.uniformName = "gfxBindless_" + std::to_string(info.imageSet) + "_" + std::to_string(info.imageBinding);
                compiler.set_name(combined.combined_id, info.uniformName);
                bindlessUniformNames.push_back(info.uniformName);
                outBindless->push_back(std::move(info));
                continue; // leave undecorated → emitted as a bindless uniform below
            }
            compiler.set_decoration(combined.combined_id, spv::DecorationDescriptorSet,
                                    compiler.get_decoration(combined.image_id, spv::DecorationDescriptorSet));
            compiler.set_decoration(combined.combined_id, spv::DecorationBinding,
                                    compiler.get_decoration(combined.image_id, spv::DecorationBinding));
        }

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

        const std::array allResources = {
            resources.separate_samplers,
            resources.separate_images,
            resources.sampled_images,
            resources.storage_images,
            resources.uniform_buffers,
            resources.storage_buffers,
        };

        for (const auto resourceList : allResources) {
            for (const auto& resource : resourceList)
            {
                uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (auto it = rebindings.find({ set, binding }); it != rebindings.end()) {
                    compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 0); // OpenGL doesn't use descriptor sets
                    compiler.set_decoration(resource.id, spv::DecorationBinding, it->second);
                }
            }
        }

        // Push constants are emitted as a std140 uniform buffer; give that buffer the
        // binding point the pipeline reserved so glBindBufferRange(GL_UNIFORM_BUFFER)
        // in CommandBuffer::PushConstants targets the same slot the shader reads.
        if (pushConstantBinding.has_value()) {
            for (const auto& resource : resources.push_constant_buffers) {
                compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, 0);
                compiler.set_decoration(resource.id, spv::DecorationBinding, pushConstantBinding.value());
            }
        }

        // Set GLSL options
        spirv_cross::CompilerGLSL::Options options;
        options.version = 450; // Target GLSL version
        options.es = false; // Not targeting OpenGL ES
        options.emit_push_constant_as_uniform_buffer = true;
        options.emit_uniform_buffer_as_plain_uniforms = false;
        compiler.set_common_options(options);

        // Handle shader inputs, outputs, and uniforms as needed
        // For example, you can remap resource bindings here if necessary

        auto shaderCode = compiler.compile();

        // Bindless material tables are emitted as ordinary `uniform sampler2D NAME[N];`.
        // Turn each into a GL_ARB_bindless_texture array by prefixing the declaration
        // with the bindless_sampler layout, and require the extension. The sampler
        // handles are supplied per element by the descriptor bind (see DescriptorSet).
        if (!bindlessUniformNames.empty()) {
            const std::string ext = "#extension GL_ARB_bindless_texture : require\n";
            if (const auto versionEnd = shaderCode.find('\n'); versionEnd != std::string::npos)
                shaderCode.insert(versionEnd + 1, ext);
            else
                shaderCode.insert(0, ext);

            for (const auto& name : bindlessUniformNames) {
                const std::string decl = "uniform sampler2D " + name;
                if (const auto pos = shaderCode.find(decl); pos != std::string::npos)
                    shaderCode.insert(pos, "layout(bindless_sampler) ");
            }
        }

        if (const char* want = std::getenv("KORAL_DUMP_GLSL"); want && _path.string().find(want) != std::string::npos) {
            std::cerr << "===== GLSL " << _path << " =====\n" << shaderCode << "\n===== end =====" << std::endl;
        }

        const GLenum shaderType = GetShaderTypeFromStage(_stage);

        auto id = glCreateShader(shaderType);
        glCheckError();


        const char* shaderSourceCStr = shaderCode.c_str();
        glShaderSource(id, 1, &shaderSourceCStr, nullptr);
        glCheckError();

        glCompileShader(id);
        glCheckError();

        // Check for compilation errors
        GLint success;
        glGetShaderiv(id, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(id, 512, nullptr, infoLog);
            std::cerr << "Error compiling shader at path " << _path << ": " << infoLog << std::endl;
        }

        return id;
    }

    GLenum Shader::GetShaderTypeFromStage(const Stage stage)
    {
        switch (stage) {
        case Stage::eVertex: return GL_VERTEX_SHADER;
        case Stage::eTessellationControl: return GL_TESS_CONTROL_SHADER;
        case Stage::eTessellationEvaluation: return GL_TESS_EVALUATION_SHADER;
        case Stage::eGeometry: return GL_GEOMETRY_SHADER;
        case Stage::eFragment: return GL_FRAGMENT_SHADER;
        case Stage::eCompute: return GL_COMPUTE_SHADER;
        case Stage::eTask: return GL_TASK_SHADER_NV;
        case Stage::eMesh: return GL_MESH_SHADER_NV;
        case Stage::eRaygen:
        case Stage::eAnyHit:
        case Stage::eClosestHit:
        case Stage::eMiss:
        case Stage::eIntersection:
        case Stage::eCallable:
            throw std::runtime_error("Raytracing shaders are not supported in OpenGL!");
        default:
            throw std::runtime_error("Unknown shader stage!");
        }
    }
}
