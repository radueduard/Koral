//
// Created by radue on 2/21/2026.
//
#include <computePipeline.h>
#include <array>
#include <descriptorSetLayout.h>
#include <framebuffer.h>
#include <log.h>
#include <surface.h>

#include "../backends/open_gl/computePipeline.h"
#include "../backends/vulkan/computePipeline.h"

#include "context.h"
#include "shader.h"
#include "../../include/window.h"

namespace gfx
{
    ComputePipeline::Builder& ComputePipeline::Builder::setComputeShader(ResourceRef<const Shader> computeShader)
    {
        this->computeShader = computeShader;
        return *this;
    }

    gfx::Result<std::unique_ptr<ComputePipeline>> ComputePipeline::Builder::create() const
    {
        beginAttempt();
        if (computeShader) adopt(*computeShader, "compute shader");

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<ComputePipeline> {
            return (api == API::eVulkan)
                ? gfx::MakeBackendPtr<ComputePipeline, vk::ComputePipeline>(*this)
                : gfx::MakeBackendPtr<ComputePipeline, ogl::ComputePipeline>(*this);
        });
    }

    gfx::Resource<ComputePipeline> ComputePipeline::Builder::build() const
    {
        auto pipeline = materialize<ComputePipeline>(*this, "ComputePipeline");
        // Registered even when poisoned: the Repository is what drives the retry that brings it
        // back once its shader compiles again.
        Context::Repository().addRef(ResourceRef<const ComputePipeline>(pipeline));
        return pipeline;
    }

    ComputePipeline::~ComputePipeline()
    {
        if (_shader.has_value()) unsubscribeReload(*_shader);
    }

    void ComputePipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        _bound = true;
    }

    void ComputePipeline::Unbind() const
    {
        _bound = false;
    }

    VoidResult ComputePipeline::Validate()
    {
        if (!_shader.has_value())
            return fail(ErrorCode::eMissingShaderStage, "A compute pipeline must have a compute shader.");
        if ((*_shader)->getStage() != Shader::Stage::eCompute)
            return fail(ErrorCode::eShaderStageMismatch, "The shader provided to a compute pipeline must be a compute shader.");

        const std::array shaders = { *_shader };
        if (!buildLayouts(shaders))
            return fail(ErrorCode::eDescriptorConflict, "Descriptor declarations conflict in the compute pipeline.");
        return {};
    }

    ComputePipeline::ComputePipeline(const Builder& createInfo)
        : _shader(createInfo.computeShader),
          _specConstantsMetadata(createInfo.specConstantsMetadata),
          _specConstantsData(createInfo.specConstantsData)
    {
        if (auto v = Validate(); !v) throw BackendException(v.error());

        if (_shader.has_value()) subscribeReload(*_shader);
    }
}
