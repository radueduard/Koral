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

namespace kor
{
    ComputePipeline::Builder& ComputePipeline::Builder::SetComputeShader(ResourceRef<const Shader> computeShader)
    {
        this->computeShader = computeShader;
        return *this;
    }

    kor::Result<std::unique_ptr<ComputePipeline>> ComputePipeline::Builder::Create() const
    {
        BeginAttempt();
        if (computeShader) Adopt(*computeShader, "compute shader");

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eOpenGL && api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<ComputePipeline> {
            return (api == API::eVulkan)
                ? kor::MakeBackendPtr<ComputePipeline, vk::ComputePipeline>(*this)
                : kor::MakeBackendPtr<ComputePipeline, ogl::ComputePipeline>(*this);
        });
    }

    kor::Resource<ComputePipeline> ComputePipeline::Builder::Build(const std::source_location where) const
    {
        auto pipeline = Materialize<ComputePipeline>(*this, "ComputePipeline", where);
        // Registered even when poisoned: the Repository is what drives the retry that brings it
        // back once its shader compiles again.
        Context::Repository().AddRef(ResourceRef<const ComputePipeline>(pipeline));
        return pipeline;
    }

    ComputePipeline::~ComputePipeline()
    {
        if (_shader.has_value()) UnsubscribeReload(*_shader);
    }

    void ComputePipeline::Bind(const kor::CommandBuffer& commandBuffer) const
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
            return Fail(ErrorCode::eMissingShaderStage, "A compute pipeline must have a compute shader.");
        if ((*_shader)->ShaderStage() != Shader::Stage::eCompute)
            return Fail(ErrorCode::eShaderStageMismatch, "The shader provided to a compute pipeline must be a compute shader.");

        const std::array shaders = { *_shader };
        if (auto merged = BuildLayouts(shaders); !merged)
            return std::unexpected(merged.error());
        return {};
    }

    ComputePipeline::ComputePipeline(const Builder& createInfo)
        : _shader(createInfo.computeShader),
          _specConstantsMetadata(createInfo.specConstantsMetadata),
          _specConstantsData(createInfo.specConstantsData)
    {
        if (auto v = Validate(); !v) throw BackendException(v.error());

        if (_shader.has_value()) SubscribeReload(*_shader);
    }
}
