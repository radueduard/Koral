//
// Created by radue on 6/23/2026.
//

/**
 * @file pipeline.h
 * @brief Common pipeline base shared by graphics, compute and (future) ray-tracing pipelines.
 *
 * Holds the state every pipeline type has in common: the descriptor set layouts
 * and push-constant ranges derived from its shaders, the bound flag, and the
 * shader hot-reload machinery. Backend pipelines implement @ref Setup / @ref Teardown
 * and @ref Bind / @ref Unbind; pipeline types implement @ref Validate.
 */

#pragma once
#include <map>
#include <memory>
#include <span>
#include <unordered_map>
#include <glm/fwd.hpp>

#include "api.h"
#include "descriptorSetLayout.h"
#include "resource.h"
#include "shader.h"

namespace kor
{
    class CommandBuffer;

    /**
     * @brief What every pipeline type has in common: its shaders' interface, and hot reload.
     *
     * A pipeline is the compiled state a draw or dispatch runs with. This base holds the parts that
     * do not depend on which kind it is — the descriptor set layouts and push-constant ranges
     * derived from the shaders by reflection, so a project never restates in C++ what the shader
     * already declares.
     *
     * It also watches its shaders. Edit a shader source while the application is running and it is
     * recompiled, the pipeline is rebuilt, and the descriptor set layouts are kept if the interface
     * did not change — so the sets already built against them stay valid. A shader that fails to
     * compile leaves the pipeline unusable rather than crashing: commands recorded with it fail and
     * name the shader, and the pipeline heals itself when the source is fixed.
     *
     * @see GraphicsPipeline, ComputePipeline, RayTracingPipeline
     */
    class KORAL_API Pipeline : public AutoUpdatable
    {
    public:
        /** @brief Virtual destructor for polymorphic ownership. */
        ~Pipeline() override;

        Pipeline(const Pipeline&) = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        /**
         * @brief Bind this pipeline on a command buffer.
         * @param commandBuffer Command buffer receiving bind commands.
         */
        virtual void Bind(const CommandBuffer& commandBuffer) const = 0;

        /** @brief Unbind this pipeline, if supported by the backend. */
        virtual void Unbind() const = 0;

        /**
         * @brief Get descriptor set layout for a set index.
         * @param index Descriptor set number.
         * @return Descriptor set layout associated with @p index.
         */
        [[nodiscard]] const DescriptorSetLayout& descriptorSetLayout(glm::u32 index) const;

        /**
         * @brief Lifetime-tracked reference to the descriptor set layout for @p index.
         *
         * Prefer this over descriptorSetLayout(): a reload can replace the layout, and a raw reference
         * into _setLayouts would be left dangling by that. A ResourceRef notices instead.
         */
        [[nodiscard]] ResourceRef<const DescriptorSetLayout> descriptorSetLayoutRef(glm::u32 index) const;

        /**
         * @brief Get push-constant range by byte offset.
         * @param offset Byte offset into declared push constant ranges.
         * @return Push-constant range covering @p offset.
         */
        [[nodiscard]] const Shader::PushConstant& pushConstantRange(glm::u32 offset) const;

        /**
         * @brief One push constant the pipeline's shaders declare, addressed by name.
         *
         * The offset is the compiler's, absolute within the pipeline's push-constant range, so
         * writing this constant means writing @ref size bytes at @ref offset and nothing else.
         */
        struct KORAL_API PushConstantMember {
            glm::u32 offset = 0;        ///< Byte offset within the pipeline's push-constant range.
            glm::u32 size = 0;          ///< Its size in bytes, as the shader reserves it.
            Flags<Shader::Stage> stages;///< Which stages declare a block containing it.

            glm::u8 scalar = 5;         ///< ValueScalar, as a plain byte; 5 (eOther) for an aggregate.
            glm::u8 rows = 1;           ///< Vector components, or matrix rows.
            glm::u8 columns = 1;        ///< Matrix columns; 1 for scalars and vectors.
            glm::u32 count = 1;         ///< Array elements, or 1.
            glm::u32 arrayStride = 0;   ///< Bytes between array elements, as the shader spaced them.
            glm::u32 matrixStride = 0;  ///< Bytes between matrix columns, likewise.
            bool aggregate = false;     ///< A struct or an array of structs: writable only as raw bytes.
        };

        /**
         * @brief Looks a push constant up by the name its shader gave it.
         * @return The constant, or nullptr when no stage of this pipeline declares one so named.
         *
         * What CommandBuffer::PushConstant uses instead of making the caller work out byte
         * offsets. Names are merged across stages: a constant declared by both the vertex and
         * fragment shader is one entry whose stages are the union of the two.
         */
        [[nodiscard]] const PushConstantMember* findPushConstant(std::string_view name) const;

        /** @brief Every push constant the pipeline declares, by name. For diagnostics. */
        [[nodiscard]] const std::map<std::string, PushConstantMember, std::less<>>& pushConstants() const { return _pushConstants; }

        /** @brief Repository-driven hot reload hook. */
        void automaticUpdate() override;

        /**
         * @brief Whether any shader in this pipeline reaches buffers through raw device addresses.
         *
         * Unioned across stages. When true the automatic barrier resolver cannot see which
         * buffers a draw or dispatch touches, so it reports an unguarded write instead of
         * quietly under-synchronising. See Shader::usesDeviceAddresses.
         */
        [[nodiscard]] bool usesDeviceAddresses() const { return _usesDeviceAddresses; }

    protected:
        Pipeline() = default;

        /**
         * @brief Rebuild @ref _setLayouts and @ref _pushConstantRanges from a set of shaders.
         * @return Empty on success, or the first conflict found — a binding two stages declare
         *         differently, or a push constant they place differently.
         *
         * Merges the memory layouts of @p shaders: descriptors sharing a (set, binding)
         * are unioned across stages, conflicting declarations are reported.
         *
         * @param shaders Shaders making up this pipeline.
         * @return true if the merged layout is consistent, false on a descriptor conflict.
         */
        VoidResult buildLayouts(std::span<const ResourceRef<const Shader>> shaders);

        /** @brief Subscribe to a shader's reload notifications, keyed by stage. */
        void subscribeReload(const ResourceRef<const Shader>& shader);

        /** @brief Drop a previously registered reload subscription for a shader. */
        void unsubscribeReload(const ResourceRef<const Shader>& shader);

        /** @brief Re-validate and recreate backend resources if a shader changed. */
        void Reload();

        /** @brief Create backend pipeline resources from the current state. */
        virtual void Setup() = 0;

        /** @brief Destroy backend pipeline resources. */
        virtual void Teardown() = 0;

        /**
         * @brief Re-validate pipeline state and rebuild descriptor/push-constant layouts.
         * @return success, or a kor::Error describing the first validation failure.
         */
        virtual VoidResult Validate() = 0;

        mutable bool _bound = false;
        bool _shouldReload = false;
        // Keyed by shader pointer (not stage): ray-tracing pipelines may hold several
        // shaders of the same stage (multiple miss / hit-group shaders).
        std::unordered_map<const Shader*, glm::u64> _shaderReloadCallbackIds;

        std::map<glm::u32, Resource<DescriptorSetLayout>> _setLayouts;
        std::map<glm::u32, Shader::PushConstant> _pushConstantRanges;
        /// The same ranges' fields, flattened by name and unioned across stages. @see findPushConstant
        std::map<std::string, PushConstantMember, std::less<>> _pushConstants;
        bool _usesDeviceAddresses = false;
    };
}