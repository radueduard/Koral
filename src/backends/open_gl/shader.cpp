//
// Created by radue on 2/21/2026.
//

module;

#include <array>
#include "ogl_err_handling.h"
#include <spirv_cross/spirv_glsl.hpp>

module ogl.shader;

namespace gfx::ogl
{
    Shader::Shader(const gfx::Shader::Builder& createInfo) : gfx::Shader(createInfo)
    {
        _spirvCode = CompileToSPIRV(_path, _stage);
        _memoryLayout = fetchMemoryLayout(_spirvCode);
    }

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

    GLuint Shader::compile(const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& rebindings) const
    {
        spirv_cross::CompilerGLSL compiler(_spirvCode);
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

        // Set GLSL options
        spirv_cross::CompilerGLSL::Options options;
        options.version = 450; // Target GLSL version
        options.es = false; // Not targeting OpenGL ES
        options.emit_push_constant_as_uniform_buffer = true;
        options.emit_uniform_buffer_as_plain_uniforms = false;
        compiler.set_common_options(options);

        // Handle shader inputs, outputs, and uniforms as needed
        // For example, you can remap resource bindings here if necessary

        const auto shaderCode = compiler.compile();
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
