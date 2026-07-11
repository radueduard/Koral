//
// Created by radue on 6/23/2026.
//

#include <rayTracingPipeline.h>

#include <log.h>

#include "../backends/vulkan/rayTracingPipeline.h"

#include "context.h"
#include "shader.h"
#include "window.h"

namespace gfx
{
    RayTracingPipeline::Builder& RayTracingPipeline::Builder::setRaygenShader(ResourceRef<const Shader> raygenShader)
    {
        this->raygenShader = raygenShader;
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::addMissShader(ResourceRef<const Shader> missShader)
    {
        this->missShaders.push_back(missShader);
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::addHitGroup(const HitGroup& hitGroup)
    {
        this->hitGroups.push_back(hitGroup);
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::addCallableShader(ResourceRef<const Shader> callableShader)
    {
        this->callableShaders.push_back(callableShader);
        return *this;
    }

    RayTracingPipeline::Builder& RayTracingPipeline::Builder::setMaxRecursionDepth(const glm::u32 maxRecursionDepth)
    {
        this->maxRecursionDepth = maxRecursionDepth;
        return *this;
    }

    gfx::Result<std::unique_ptr<RayTracingPipeline>> RayTracingPipeline::Builder::create() const
    {
        beginAttempt();

        if (raygenShader) adopt(*raygenShader, "raygen shader");
        for (const auto& miss : missShaders)        adopt(miss, "miss shader");
        for (const auto& callable : callableShaders) adopt(callable, "callable shader");
        for (const auto& group : hitGroups) {
            if (group.closestHitShader)  adopt(*group.closestHitShader,  "closest-hit shader");
            if (group.anyHitShader)      adopt(*group.anyHitShader,      "any-hit shader");
            if (group.intersectionShader) adopt(*group.intersectionShader, "intersection shader");
        }

        if (auto v = validate(); !v) return std::unexpected(v.error());

        const auto api = Context::activeAPI();
        if (api == API::eOpenGL)
            return fail(ErrorCode::eRayTracingUnsupported, "Ray tracing pipelines are not supported on the OpenGL backend.");
        if (api != API::eVulkan)
            return fail(ErrorCode::eUnknownApi, "Unknown graphics API!");

        return guard(ErrorCode::eBackend, [&]() -> std::unique_ptr<RayTracingPipeline> {
            return gfx::MakeBackendPtr<RayTracingPipeline, vk::RayTracingPipeline>(*this);
        });
    }

    gfx::Resource<RayTracingPipeline> RayTracingPipeline::Builder::build() const
    {
        auto pipeline = materialize<RayTracingPipeline>(*this, "RayTracingPipeline");
        Context::Repository().addRef(ResourceRef<const RayTracingPipeline>(pipeline));
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

        for (const auto& shader : collectShaders()) subscribeReload(shader);
    }

    RayTracingPipeline::~RayTracingPipeline()
    {
        for (const auto& shader : collectShaders()) unsubscribeReload(shader);
    }

    std::vector<ResourceRef<const Shader>> RayTracingPipeline::collectShaders() const
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
            return fail(ErrorCode::eMissingShaderStage, "A ray tracing pipeline must have a raygen shader.");
        if ((*_raygenShader)->getStage() != Shader::Stage::eRaygen)
            return fail(ErrorCode::eShaderStageMismatch, "The raygen shader must be a raygen-stage shader.");

        for (const auto& missShader : _missShaders)
            if (missShader->getStage() != Shader::Stage::eMiss)
                return fail(ErrorCode::eShaderStageMismatch, "A shader provided as a miss shader is not a miss-stage shader.");

        for (const auto& callableShader : _callableShaders)
            if (callableShader->getStage() != Shader::Stage::eCallable)
                return fail(ErrorCode::eShaderStageMismatch, "A shader provided as a callable shader is not a callable-stage shader.");

        for (const auto& hitGroup : _hitGroups) {
            if (!hitGroup.closestHitShader.has_value() && !hitGroup.intersectionShader.has_value())
                return fail(ErrorCode::eMissingShaderStage, "A hit group must have at least a closest-hit or an intersection shader.");
            if (hitGroup.closestHitShader.has_value() && (*hitGroup.closestHitShader)->getStage() != Shader::Stage::eClosestHit)
                return fail(ErrorCode::eShaderStageMismatch, "The closest-hit shader of a hit group is not a closest-hit-stage shader.");
            if (hitGroup.anyHitShader.has_value() && (*hitGroup.anyHitShader)->getStage() != Shader::Stage::eAnyHit)
                return fail(ErrorCode::eShaderStageMismatch, "The any-hit shader of a hit group is not an any-hit-stage shader.");
            if (hitGroup.intersectionShader.has_value() && (*hitGroup.intersectionShader)->getStage() != Shader::Stage::eIntersection)
                return fail(ErrorCode::eShaderStageMismatch, "The intersection shader of a hit group is not an intersection-stage shader.");
        }

        // Merge descriptor set layouts and push constants across all stages.
        const auto shaders = collectShaders();
        if (!buildLayouts(shaders))
            return fail(ErrorCode::eDescriptorConflict, "Descriptor declarations conflict across the ray-tracing pipeline's shader stages.");

        return {};
    }
}
