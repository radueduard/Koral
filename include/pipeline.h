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
        [[nodiscard]] const DescriptorSetLayout& getSetLayout(glm::u32 index) const;

        /**
         * @brief Lifetime-tracked reference to the descriptor set layout for @p index.
         *
         * Prefer this over getSetLayout(): a reload can replace the layout, and a raw reference
         * into _setLayouts would be left dangling by that. A ResourceRef notices instead.
         */
        [[nodiscard]] ResourceRef<const DescriptorSetLayout> getSetLayoutRef(glm::u32 index) const;

        /**
         * @brief Get push-constant range by byte offset.
         * @param offset Byte offset into declared push constant ranges.
         * @return Push-constant range covering @p offset.
         */
        [[nodiscard]] const Shader::PushConstant& getPushConstantRange(glm::u32 offset) const;

        /** @brief Repository-driven hot reload hook. */
        void automaticUpdate() override;

    protected:
        Pipeline() = default;

        /**
         * @brief Rebuild @ref _setLayouts and @ref _pushConstantRanges from a set of shaders.
         *
         * Merges the memory layouts of @p shaders: descriptors sharing a (set, binding)
         * are unioned across stages, conflicting declarations are reported.
         *
         * @param shaders Shaders making up this pipeline.
         * @return true if the merged layout is consistent, false on a descriptor conflict.
         */
        bool buildLayouts(std::span<const ResourceRef<const Shader>> shaders);

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
    };
}