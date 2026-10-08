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
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <kmath/matrix.h>

#include "api.h"
#include "descriptorSetLayout.h"
#include "resource.h"
#include "shader.h"

namespace kor
{
    class CommandBuffer;

    /** @brief A specialization constant given to a pipeline builder: by its constant id, or by the name its shader gave it. */
    struct KORAL_API SpecializationValue {
        std::string name;               ///< Empty when given by id.
        kor::u32 id = 0;                ///< The constant id, when given by id.
        std::vector<std::byte> bytes;   ///< The value.
    };

    /**
     * @brief What every pipeline builder takes besides its shaders and its fixed state: where its
     *        descriptors go, and the values of its specialization constants.
     *
     * @code
     * ComputePipeline::Builder{}
     *     .SetComputeShader(blur)
     *     .SetBinding("source", 0, 0)                   // set 0, binding 0, whatever the shader said
     *     .SetBinding("target", 1, 0)
     *     .SetSpecializationConstant("radius", 4)       // by the name the shader gave it
     *     .Build();
     * @endcode
     */
    template<typename Derived>
    struct PipelineSettings {
        Shader::BindingAssignment bindings;                         ///< Descriptors moved, by name. @see SetBinding
        std::vector<SpecializationValue> specializationConstants;   ///< In the order given; a later one for the same constant wins.

        /**
         * @brief Puts the descriptor @p name at @p set, @p binding, overriding what its shader said.
         *
         * @p name is the descriptor's name in the source, or its block's type name for a block declared
         * without an instance name. A shader needs no bindings of its own for this: the pipeline numbers
         * its descriptors, in the SPIR-V it is built from, and the shader itself is untouched — one shader
         * serves pipelines that number it differently. A name no stage declares fails the build; two
         * descriptors put in one place fail it too.
         */
        Derived& SetBinding(std::string name, const kor::u32 set, const kor::u32 binding) {
            bindings[std::move(name)] = Shader::BindingSlot { set, binding };
            return static_cast<Derived&>(*this);
        }

        /** @brief SetBinding for each of @p assignment. */
        Derived& SetBindings(const Shader::BindingAssignment& assignment) {
            for (const auto& [name, slot] : assignment) bindings[name] = slot;
            return static_cast<Derived&>(*this);
        }

        /**
         * @brief Bakes a specialization constant into the pipeline, by the constant id its shader declares.
         * @tparam T The constant's type; trivially copyable, and as wide as the shader's (a bool may be a bool).
         *
         * The shader is compiled with the value as a literal, so branches on it fold away and loops over
         * it can unroll. Stages that declare no constant with this id ignore it.
         */
        template<typename T> requires std::is_trivially_copyable_v<T>
        Derived& SetSpecializationConstant(const kor::u32 id, const T value) {
            specializationConstants.push_back({ {}, id, BytesOf(value) });
            return static_cast<Derived&>(*this);
        }

        /** @brief Bakes a specialization constant into the pipeline, by the name its shader gave it. Fails the build if no stage declares it. */
        template<typename T> requires std::is_trivially_copyable_v<T>
        Derived& SetSpecializationConstant(std::string_view name, const T value) {
            specializationConstants.push_back({ std::string(name), 0, BytesOf(value) });
            return static_cast<Derived&>(*this);
        }

        /** @brief A specialization constant's value as raw bytes, by id or (when @p name is not empty) by name. */
        Derived& SetSpecializationConstantBytes(std::string name, const kor::u32 id, std::vector<std::byte> bytes) {
            specializationConstants.push_back({ std::move(name), id, std::move(bytes) });
            return static_cast<Derived&>(*this);
        }

    private:
        template<typename T>
        static std::vector<std::byte> BytesOf(const T& value) {
            std::vector<std::byte> bytes(sizeof(T));
            std::memcpy(bytes.data(), &value, sizeof(T));
            return bytes;
        }
    };

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
        [[nodiscard]] const DescriptorSetLayout& SetLayout(kor::u32 index) const;

        /**
         * @brief Lifetime-tracked reference to the descriptor set layout for @p index.
         *
         * Prefer this over SetLayout(): a reload can replace the layout, and a raw reference
         * into _setLayouts would be left dangling by that. A ResourceRef notices instead.
         */
        [[nodiscard]] ResourceRef<const DescriptorSetLayout> SetLayoutRef(kor::u32 index) const;

        /**
         * @brief Get push-constant range by byte offset.
         * @param offset Byte offset into declared push constant ranges.
         * @return Push-constant range covering @p offset.
         */
        [[nodiscard]] const Shader::PushConstant& PushConstantRange(kor::u32 offset) const;

        /**
         * @brief One push constant the pipeline's shaders declare, addressed by name.
         *
         * The offset is the compiler's, absolute within the pipeline's push-constant range, so
         * writing this constant means writing @ref size bytes at @ref offset and nothing else.
         */
        struct KORAL_API PushConstantMember {
            kor::u32 offset = 0;        ///< Byte offset within the pipeline's push-constant range.
            kor::u32 size = 0;          ///< Its size in bytes, as the shader reserves it.
            Flags<Shader::Stage> stages;///< Which stages declare a block containing it.

            kor::u8 scalar = 5;         ///< ValueScalar, as a plain byte; 5 (eOther) for an aggregate.
            kor::u8 rows = 1;           ///< Vector components, or matrix rows.
            kor::u8 columns = 1;        ///< Matrix columns; 1 for scalars and vectors.
            kor::u32 count = 1;         ///< Array elements, or 1.
            kor::u32 arrayStride = 0;   ///< Bytes between array elements, as the shader spaced them.
            kor::u32 matrixStride = 0;  ///< Bytes between matrix columns, likewise.
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
        [[nodiscard]] const PushConstantMember* FindPushConstant(std::string_view name) const;

        /** @brief Every push constant the pipeline declares, by name. For diagnostics. */
        [[nodiscard]] const std::map<std::string, PushConstantMember, std::less<>>& PushConstants() const { return _pushConstants; }

        /** @brief Repository-driven hot reload hook. */
        void AutomaticUpdate() override;

        /**
         * @brief Whether any shader in this pipeline reaches buffers through raw device addresses.
         *
         * Unioned across stages. When true the automatic barrier resolver cannot see which
         * buffers a draw or dispatch touches, so it reports an unguarded write instead of
         * quietly under-synchronising. See Shader::UsesDeviceAddresses.
         */
        [[nodiscard]] bool UsesDeviceAddresses() const { return _usesDeviceAddresses; }

        /** @brief Where this pipeline puts its shaders' descriptors, beyond where they put themselves. @see PipelineSettings::SetBinding */
        [[nodiscard]] const Shader::BindingAssignment& Bindings() const { return _bindings; }

    protected:
        Pipeline() = default;

        /** @brief Takes a builder's bindings and specialization constants. */
        template<typename B>
        void TakeSettings(const PipelineSettings<B>& settings) {
            _bindings = settings.bindings;
            _specializationValues = settings.specializationConstants;
        }

        /**
         * @brief Rebuild @ref _setLayouts and @ref _pushConstantRanges from a set of shaders.
         * @return Empty on success, or the first conflict found — a binding two stages declare
         *         differently, or a push constant they place differently.
         *
         * Merges the memory layouts of @p shaders — as moved by the pipeline's bindings — descriptors
         * sharing a (set, binding) are unioned across stages, and conflicting declarations are
         * reported. Resolves the specialization constants too, by name against what the shaders
         * declare, into @ref _specConstantsMetadata and @ref _specConstantsData.
         *
         * @param shaders Shaders making up this pipeline.
         * @return true if the merged layout is consistent, false on a descriptor conflict.
         */
        VoidResult BuildLayouts(std::span<const ResourceRef<const Shader>> shaders);

        /** @brief Subscribe to a shader's reload notifications, keyed by stage. */
        void SubscribeReload(const ResourceRef<const Shader>& shader);

        /** @brief Drop a previously registered reload subscription for a shader. */
        void UnsubscribeReload(const ResourceRef<const Shader>& shader);

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
        std::unordered_map<const Shader*, kor::u64> _shaderReloadCallbackIds;

        std::map<kor::u32, Resource<DescriptorSetLayout>> _setLayouts;
        std::map<kor::u32, Shader::PushConstant> _pushConstantRanges;
        /// The same ranges' fields, flattened by name and unioned across stages. @see findPushConstant
        std::map<std::string, PushConstantMember, std::less<>> _pushConstants;
        bool _usesDeviceAddresses = false;

        Shader::BindingAssignment _bindings;
        std::vector<SpecializationValue> _specializationValues;
        /// The specialization constants as Vulkan takes them: (constant id, offset, size) into the data.
        std::vector<std::tuple<kor::u32, kor::u32, kor::u32>> _specConstantsMetadata;
        std::vector<std::byte> _specConstantsData;
    };
}