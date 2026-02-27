//
// Created by radue on 2/21/2026.
//

#include "computePipeline.h"

#include "buffer.h"
#include "shader.h"
#include "utils/ogl_err_handling.h"

namespace gfx::ogl
{
    ComputePipeline::ComputePipeline(const Builder& createInfo) : gfx::ComputePipeline(createInfo) {
        if (!createInfo.computeShader.has_value())
            throw std::runtime_error("You can not create a compute pipeline without a compute shader!");

        const auto& shader = dynamic_cast<const Shader&>(createInfo.computeShader.value().get());
        if (shader.getStage() != Shader::Stage::eCompute) {
            throw std::runtime_error("The shader provided to the compute pipeline must be a compute shader!");
        }

        _id = glCreateProgram();
        glCheckError();

        glAttachShader(_id, *shader);
        glCheckError();

        glLinkProgram(_id);
        glCheckError();

        // Check for linking errors
        GLint success;
        glGetProgramiv(_id, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(_id, 512, nullptr, infoLog);
            std::cerr << "Error linking compute pipeline: " << infoLog << std::endl;
        }
    }

    ComputePipeline::~ComputePipeline()
    {
        glDeleteProgram(_id);
        glCheckError();
    }

    void ComputePipeline::Bind() const
    {
        gfx::ComputePipeline::Bind();
        glUseProgram(_id);
        glCheckError();
    }

    void ComputePipeline::Unbind() const
    {
        gfx::ComputePipeline::Unbind();
        glUseProgram(0);
        glCheckError();
    }

    void ComputePipeline::Dispatch(const glm::u32 groupCountX, const glm::u32 groupCountY, const glm::u32 groupCountZ) const
    {
        gfx::ComputePipeline::Dispatch(groupCountX, groupCountY, groupCountZ);
        glDispatchCompute(groupCountX, groupCountY, groupCountZ);
        glCheckError();
    }

    void ComputePipeline::DispatchIndirect(const gfx::Buffer& indirectBuffer, const glm::i64 offset) const
    {
        gfx::ComputePipeline::DispatchIndirect(indirectBuffer, offset);
        const auto& indirectBufferGL = dynamic_cast<const Buffer&>(indirectBuffer);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, *indirectBufferGL);
        glCheckError();
        glDispatchComputeIndirect(offset);
        glCheckError();
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
        glCheckError();
    }
}
