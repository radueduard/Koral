//
// Created by radue on 6/23/2026.
//

/**
 * @file rayTracingPipeline.h
 * @brief Ray-tracing pipeline abstraction and builder configuration types.
 *
 * Defines the high-level ray-tracing pipeline interface used by the runtime. A
 * ray-tracing pipeline is built from a single raygen shader, any number of miss
 * and callable shaders, and a list of hit groups (closest-hit / any-hit /
 * intersection). The backend assembles these into a pipeline plus its shader
 * binding table.
 */

#pragma once
#include <optional>
#include <vector>
#include <glm/fwd.hpp>

#include "api.h"
#include "builder.h"
#include "pipeline.h"
#include "resource.h"
#include "shader.h"

namespace gfx
{
    class Shader;
    class CommandBuffer;

    class GFX_API RayTracingPipeline : public Pipeline
    {
    public:
        /**
         * @brief A hit group: the shaders invoked when a ray hits geometry.
         *
         * For triangle geometry provide a closest-hit (and optionally any-hit)
         * shader. For procedural geometry also provide an intersection shader.
         */
        struct GFX_API HitGroup
        {
            std::optional<ResourceRef<const Shader>> closestHitShader = std::nullopt;
            std::optional<ResourceRef<const Shader>> anyHitShader = std::nullopt;
            std::optional<ResourceRef<const Shader>> intersectionShader = std::nullopt;
        };

        struct GFX_API Builder : ::Builder
        {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            std::optional<ResourceRef<const Shader>> raygenShader = std::nullopt;
            std::vector<ResourceRef<const Shader>> missShaders = {};
            std::vector<HitGroup> hitGroups = {};
            std::vector<ResourceRef<const Shader>> callableShaders = {};
            glm::u32 maxRecursionDepth = 1;

            Builder& setRaygenShader(ResourceRef<const Shader> raygenShader);
            Builder& addMissShader(ResourceRef<const Shader> missShader);
            Builder& addHitGroup(const HitGroup& hitGroup);
            Builder& addCallableShader(ResourceRef<const Shader> callableShader);
            Builder& setMaxRecursionDepth(glm::u32 maxRecursionDepth);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<RayTracingPipeline>> create() const;
            [[nodiscard]] gfx::Resource<RayTracingPipeline> build() const;
        };

        /** @brief Virtual destructor for polymorphic ownership. */
        ~RayTracingPipeline() override;

        /** @brief Maximum ray recursion depth this pipeline was created with. */
        [[nodiscard]] glm::u32 getMaxRecursionDepth() const { return _maxRecursionDepth; }

    protected:
        explicit RayTracingPipeline(const Builder& createInfo);

        VoidResult Validate() override;

        /** @brief Every shader making up this pipeline, in shader-group order. */
        [[nodiscard]] std::vector<ResourceRef<const Shader>> collectShaders() const;

        std::optional<ResourceRef<const Shader>> _raygenShader;
        std::vector<ResourceRef<const Shader>> _missShaders;
        std::vector<HitGroup> _hitGroups;
        std::vector<ResourceRef<const Shader>> _callableShaders;
        glm::u32 _maxRecursionDepth = 1;
    };
}
