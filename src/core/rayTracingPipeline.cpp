//
// Created by radue on 6/23/2026.
//

#include <rayTracingPipeline.h>

#include <log.h>

#include "../backends/vulkan/rayTracingPipeline.h"

#include "context.h"
#include "shader.h"
#include "window.h"

namespace kor
{
    RayTracingPipeline::Builder& RayTracingPipeline::Builder::SetRaygenShader(ResourceRef<const Shader> raygenShader)
    {
        this->raygenShader = raygenShader;
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::AddMissShader(ResourceRef<const Shader> missShader)
    {
        this->missShaders.push_back(missShader);
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::AddHitGroup(const HitGroup& hitGroup)
    {
        this->hitGroups.push_back(hitGroup);
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::AddCallableShader(ResourceRef<const Shader> callableShader)
    {
        this->callableShaders.push_back(callableShader);
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::SetMaxRecursionDepth(const glm::u32 maxRecursionDepth)
    {
        this->maxRecursionDepth = maxRecursionDepth;
        return *this;
    }

    kor::Result<std::unique_ptr<RayTracingPipeline>> RayTracingPipeline::Builder::Create() const
    {
        BeginAttempt();

        if (raygenShader) Adopt(*raygenShader, "raygen shader");
        for (const auto& miss : missShaders)        Adopt(miss, "miss shader");
        for (const auto& callable : callableShaders) Adopt(callable, "callable shader");
        for (const auto& group : hitGroups) {
            if (group.closestHitShader)  Adopt(*group.closestHitShader,  "closest-hit shader");
            if (group.anyHitShader)      Adopt(*group.anyHitShader,      "any-hit shader");
            if (group.intersectionShader) Adopt(*group.intersectionShader, "intersection shader");
        }

        if (auto v = Validate(); !v) return std::unexpected(v.error());

        const auto api = Context::ActiveAPI();
        if (api != API::eVulkan)
            return Fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return Guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<RayTracingPipeline> {
            return kor::MakeBackendPtr<RayTracingPipeline, vk::RayTracingPipeline>(*this);
        });
    }

    kor::Resource<RayTracingPipeline> RayTracingPipeline::Builder::Build(const std::source_location where) const
    {
        auto pipeline = Materialize<RayTracingPipeline>(*this, "RayTracingPipeline", where);
        Context::Repository().AddRef(ResourceRef<const RayTracingPipeline>(pipeline));
        return pipeline;
    }

    RayTracingPipeline::RayTracingPipeline(const Builder& createInfo)
        : _raygenShader(createInfo.raygenShader),
          _missShaders(createInfo.missShaders),
          _hitGroups(createInfo.hitGroups),
          _callableShaders(createInfo.callableShaders),
          _maxRecursionDepth(createInfo.maxRecursionDepth)
    {
        if (auto v = Validate(); !v) throw BackendException(v.error());

        for (const auto& shader : CollectShaders()) SubscribeReload(shader);
    }

    RayTracingPipeline::~RayTracingPipeline()
    {
        for (const auto& shader : CollectShaders()) UnsubscribeReload(shader);
    }

    std::vector<ResourceRef<const Shader>> RayTracingPipeline::CollectShaders() const
    {
        std::vector<ResourceRef<const Shader>> shaders;
        if (_raygenShader.has_value()) shaders.push_back(*_raygenShader);
        for (const auto& missShader : _missShaders) shaders.push_back(missShader);
        for (const auto& hitGroup : _hitGroups) {
            if (hitGroup.closestHitShader.has_value()) shaders.push_back(*hitGroup.closestHitShader);
            if (hitGroup.anyHitShader.has_value()) shaders.push_back(*hitGroup.anyHitShader);
            if (hitGroup.intersectionShader.has_value()) shaders.push_back(*hitGroup.intersectionShader);
        }
        for (const auto& callableShader : _callableShaders) shaders.push_back(callableShader);
        return shaders;
    }

    VoidResult RayTracingPipeline::Validate()
    {
        if (!_raygenShader.has_value())
            return Fail(ErrorCode::eMissingShaderStage, "A ray tracing pipeline must have a raygen shader.");
        if ((*_raygenShader)->ShaderStage() != Shader::Stage::eRaygen)
            return Fail(ErrorCode::eShaderStageMismatch, "The raygen shader must be a raygen-stage shader.");

        for (const auto& missShader : _missShaders)
            if (missShader->ShaderStage() != Shader::Stage::eMiss)
                return Fail(ErrorCode::eShaderStageMismatch, "A shader provided as a miss shader is not a miss-stage shader.");

        for (const auto& callableShader : _callableShaders)
            if (callableShader->ShaderStage() != Shader::Stage::eCallable)
                return Fail(ErrorCode::eShaderStageMismatch, "A shader provided as a callable shader is not a callable-stage shader.");

        for (const auto& hitGroup : _hitGroups) {
            if (!hitGroup.closestHitShader.has_value() && !hitGroup.intersectionShader.has_value())
                return Fail(ErrorCode::eMissingShaderStage, "A hit group must have at least a closest-hit or an intersection shader.");
            if (hitGroup.closestHitShader.has_value() && (*hitGroup.closestHitShader)->ShaderStage() != Shader::Stage::eClosestHit)
                return Fail(ErrorCode::eShaderStageMismatch, "The closest-hit shader of a hit group is not a closest-hit-stage shader.");
            if (hitGroup.anyHitShader.has_value() && (*hitGroup.anyHitShader)->ShaderStage() != Shader::Stage::eAnyHit)
                return Fail(ErrorCode::eShaderStageMismatch, "The any-hit shader of a hit group is not an any-hit-stage shader.");
            if (hitGroup.intersectionShader.has_value() && (*hitGroup.intersectionShader)->ShaderStage() != Shader::Stage::eIntersection)
                return Fail(ErrorCode::eShaderStageMismatch, "The intersection shader of a hit group is not an intersection-stage shader.");
        }

        // Merge descriptor set layouts and push constants across all stages.
        const auto shaders = CollectShaders();
        if (auto merged = BuildLayouts(shaders); !merged)
            return std::unexpected(merged.error());

        return {};
    }
}
