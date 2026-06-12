//
// Created by radue on 2/21/2026.
//

module;

#include "ogl_err_handling.h"

module ogl.computePipeline;

import std;
import gfx.computePipeline;
import gfx.shader;
import gfx.descriptorSetLayout;

namespace gfx::ogl
{
    ComputePipeline::ComputePipeline(const Builder& createInfo) : gfx::ComputePipeline(createInfo) {
        if (!createInfo.computeShader.has_value())
            throw std::runtime_error("You can not create a compute pipeline without a compute shader!");

        const auto& shader = dynamic_cast<const Shader&>(*createInfo.computeShader.value());
        if (shader.getStage() != Shader::Stage::eCompute) {
            throw std::runtime_error("The shader provided to the compute pipeline must be a compute shader!");
        }


        glm::u32 nextDescriptorBindingPoint = 0;
        for (const auto& [set, layout] : _setLayouts) {
            for (const auto& [binding, type, Count] : layout->getBindings()) {
                _setAndBindingToBindingPoint[{ set, binding }] = nextDescriptorBindingPoint++;
            }
        }

        const GLuint shaderId = shader.compile(_setAndBindingToBindingPoint);

        // Check shader compilation status first
        GLint compileStatus;
        glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compileStatus);
        if (!compileStatus) {
            char infoLog[1024];
            glGetShaderInfoLog(shaderId, 1024, nullptr, infoLog);
            throw std::runtime_error(std::string("Shader compilation failed: ") + infoLog);
        }

        _id = glCreateProgram();
        glCheckError();

        glAttachShader(_id, shaderId);
        glCheckError();

        glLinkProgram(_id);
        glCheckError();

        glDetachShader(_id, shaderId);
        glCheckError();

        GLint success;
        glGetProgramiv(_id, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[1024];
            glGetProgramInfoLog(_id, 1024, nullptr, infoLog);
            glDeleteProgram(_id);
            throw std::runtime_error(std::string("Error linking compute pipeline: ") + infoLog);
        }

        glDeleteShader(shaderId);
    }

    ComputePipeline::~ComputePipeline()
    {
        glDeleteProgram(_id);
        glCheckError();
    }

    void ComputePipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        gfx::ComputePipeline::Bind(commandBuffer);
        glUseProgram(_id);
        glCheckError();
    }

    void ComputePipeline::Unbind() const
    {
        gfx::ComputePipeline::Unbind();
        glUseProgram(0);
        glCheckError();
    }
}
