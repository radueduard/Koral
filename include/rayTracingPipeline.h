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
#include <source_location>

#include "builder.h"
#include "pipeline.h"
#include "resource.h"
#include "shader.h"

namespace kor
{
    class Shader;
    class CommandBuffer;

    /**
     * @brief A compiled ray-tracing pipeline and its shader binding table.
     *
     * Unlike a graphics pipeline, which runs one shader per stage, this holds a *set* of shaders
     * and the GPU picks between them per ray: the raygen shader casts, a miss shader runs when a
     * ray hits nothing, and the hit group matching the geometry runs when it does.
     *
     * @code
     * kor::RayTracingPipeline::Builder builder;
     * auto pipeline = builder
     *     .setRaygenShader(raygen)
     *     .addMissShader(miss)
     *     .addHitGroup({ .closestHitShader = closestHit })
     *     .setMaxRecursionDepth(2)
     *     .build();
     *
     * commandBuffer.BindRayTracingPipeline(pipeline).TraceRays(width, height);
     * @endcode
     *
     * Requires a device with ray-tracing support — check Context::supportsRayTracing(). Without it
     * the build fails into a poisoned resource rather than crashing. Vulkan only.
     */
    class KORAL_API RayTracingPipeline : public Pipeline
    {
    public:
        /**
         * @brief A hit group: the shaders invoked when a ray hits geometry.
         *
         * For triangle geometry provide a closest-hit (and optionally any-hit)
         * shader. For procedural geometry also provide an intersection shader.
         */
        struct KORAL_API HitGroup
        {
            std::optional<ResourceRef<const Shader>> closestHitShader = std::nullopt;   ///< Runs for the nearest hit along the ray. The usual place to shade a surface.
            std::optional<ResourceRef<const Shader>> anyHitShader = std::nullopt;       ///< Runs for every candidate hit, in no particular order. Where alpha-tested geometry rejects a hit.
            std::optional<ResourceRef<const Shader>> intersectionShader = std::nullopt; ///< Computes intersections for procedural geometry. Omit it for triangles.
        };

        /** @brief Collects the shaders a ray-tracing pipeline is assembled from. */
        struct KORAL_API Builder : kor::Builder
        {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            std::optional<ResourceRef<const Shader>> raygenShader = std::nullopt;  ///< The entry point, run once per ray of the TraceRays grid.
            std::vector<ResourceRef<const Shader>> missShaders = {};                ///< Run when a ray hits nothing; the shader index is chosen by the trace call.
            std::vector<HitGroup> hitGroups = {};                                   ///< Run when a ray hits geometry; the group is chosen by the instance it hit.
            std::vector<ResourceRef<const Shader>> callableShaders = {};            ///< Invoked explicitly by other ray-tracing shaders.
            glm::u32 maxRecursionDepth = 1;                                         ///< How deep rays may recurse.

            /** @brief Sets the raygen shader — the entry point run once per ray. Required. */
            Builder& setRaygenShader(ResourceRef<const Shader> raygenShader);

            /** @brief Appends a miss shader. Its position is the index a trace call selects it by. */
            Builder& addMissShader(ResourceRef<const Shader> missShader);

            /** @brief Appends a hit group. Its position is what AccelerationStructure::Instance::hitGroupIndex refers to. */
            Builder& addHitGroup(const HitGroup& hitGroup);

            /** @brief Appends a callable shader, which other ray-tracing shaders may invoke by index. */
            Builder& addCallableShader(ResourceRef<const Shader> callableShader);

            /**
             * @brief Sets how deep rays may recurse — a shader casting a ray that casts another.
             * @param maxRecursionDepth The limit. Keep it as low as the effect allows; devices cap
             *        it, and deeper recursion costs stack memory per ray.
             */
            Builder& setMaxRecursionDepth(glm::u32 maxRecursionDepth);

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<RayTracingPipeline>> create() const;

            /**
             * @brief Compiles the pipeline and its shader binding table.
             * @return It as a Resource; poisoned rather than thrown when a shader fails to compile
             *         or the device has no ray-tracing support.
             */
            [[nodiscard]] kor::Resource<RayTracingPipeline> build(std::source_location where = std::source_location::current()) const;
        };

        /** @brief Virtual destructor for polymorphic ownership. */
        ~RayTracingPipeline() override;

        /** @brief Maximum ray recursion depth this pipeline was created with. */
        [[nodiscard]] glm::u32 maxRecursionDepth() const { return _maxRecursionDepth; }

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
