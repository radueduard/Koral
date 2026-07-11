//
// Created by radue on 2/21/2026.
//

#include "computePipeline.h"

#include <descriptorSetLayout.h>

#include "buffer.h"
#include "shader.h"
#include "ogl_err_handling.h"

namespace gfx::ogl
{
    ComputePipeline::ComputePipeline(const Builder& createInfo) : gfx::ComputePipeline(createInfo) {
        Setup();
    }

    ComputePipeline::~ComputePipeline()
    {
        Teardown();
    }

    void ComputePipeline::Setup()
    {
        const auto& shader = dynamic_cast<const Shader&>(**_shader);

        _setAndBindingToBindingPoint.clear();
        glm::u32 nextDescriptorBindingPoint = 0;
        for (const auto& [set, layout] : _setLayouts) {
            for (const auto& [binding, type, Count] : layout->getBindings()) {
                _setAndBindingToBindingPoint[{ set, binding }] = nextDescriptorBindingPoint++;
            }
        }

        // Reserve a UBO binding point for push constants (see graphics-pipeline Setup).
        _pushConstantSize = 0;
        for (const auto& [offset, pc] : _pushConstantRanges) {
            const glm::u32 end = pc.offset + pc.size;
            if (end > _pushConstantSize) _pushConstantSize = end;
        }
        std::optional<glm::u32> pushConstantBinding;
        if (_pushConstantSize > 0) {
            _pushConstantBindingPoint = nextDescriptorBindingPoint++;
            pushConstantBinding = _pushConstantBindingPoint;
            glCreateBuffers(1, &_pushConstantUBO);
            glNamedBufferData(_pushConstantUBO, _pushConstantSize, nullptr, GL_DYNAMIC_DRAW);
            glCheckError();
        }

        const GLuint shaderId = shader.compile(_setAndBindingToBindingPoint, pushConstantBinding);

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

    void ComputePipeline::Teardown()
    {
        if (_pushConstantUBO != 0) {
            glDeleteBuffers(1, &_pushConstantUBO);
            _pushConstantUBO = 0;
            glCheckError();
        }
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
