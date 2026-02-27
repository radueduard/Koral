//
// Created by radue on 2/21/2026.
//

#include "shader.h"

#include "utils/file.h"
#include "utils/ogl_err_handling.h"

namespace gfx::ogl
{
    Shader::Shader(const gfx::Shader::Builder& createInfo) : gfx::Shader(createInfo)
    {
        if (createInfo.lang != Lang::eGLSL) {
            throw std::runtime_error("Only GLSL shaders are supported in OpenGL!");
        }

        const std::string shaderSource = utils::ReadFileAsString(createInfo.path);
        const GLenum shaderType = GetShaderTypeFromStage(createInfo.stage);

        _id = glCreateShader(shaderType);
        glCheckError();


        const char* shaderSourceCStr = shaderSource.c_str();
        glShaderSource(_id, 1, &shaderSourceCStr, nullptr);
        glCheckError();

        glCompileShader(_id);
        glCheckError();

        // Check for compilation errors
        GLint success;
        glGetShaderiv(_id, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(_id, 512, nullptr, infoLog);
            std::cerr << "Error compiling shader at path " << createInfo.path << ": " << infoLog << std::endl;
        }
    }

    Shader::~Shader()
    {
        glDeleteShader(_id);
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
