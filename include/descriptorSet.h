//
// Created by radue on 3/4/2026.
//

#pragma once
#include <map>
#include <memory>
#include <vector>
#include <glm/fwd.hpp>

#include "descriptorSetLayout.h"
#include "resource.h"
#include "descriptor.h"
#include "api.h"
#include "builder.h"
#include "error.h"
#include <optional>

namespace kor
{
    class CommandBuffer;
    class Descriptor;
    class Pipeline;

    class KORAL_API DescriptorSet
    {
    public:
        struct KORAL_API Builder : ::Builder
        {
            // Preferred: name the pipeline and the set index, and let the builder fetch the layout.
            // Passing the pipeline (rather than pipeline->getSetLayout(0)) means a *poisoned*
            // pipeline poisons this descriptor set instead of being dereferenced, and the layout is
            // held by a lifetime-tracked ref rather than a raw reference into the pipeline's map.
            Builder(ResourceRef<const Pipeline> pipeline, glm::u32 setIndex);

            // A standalone layout (not owned by a pipeline).
            explicit Builder(ResourceRef<const DescriptorSetLayout> layout);

            // Raw reference. Kept for callers that already hold a layout by reference; prefer one of
            // the above, which can track its lifetime.
            explicit Builder(const DescriptorSetLayout& layout);

            ResourceRef<const DescriptorSetLayout> layout;
            std::map<glm::u32, std::vector<Descriptor>> writes;

            Builder& write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index = 0);
            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<DescriptorSet>> create() const;
            [[nodiscard]] kor::Resource<DescriptorSet> build() const;

        private:
            void initWrites();
            std::optional<Error> _error;
        };

        virtual ~DescriptorSet() = default;

        virtual void bind(const CommandBuffer& commandBuffer, glm::u32 index) const {};
        virtual void Write(glm::u32 binding, const Descriptor& descriptor, glm::u32 index) = 0;

        virtual void DebugPrint() const {};

    protected:
        explicit DescriptorSet(const Builder &builder);
        bool _isPerFrame = false;
        // A ref, not a reference: a shader reload can replace the pipeline's layouts, and a raw
        // reference into that map would be silently left dangling. See Pipeline::buildLayouts.
        kor::ResourceRef<const kor::DescriptorSetLayout> _layout;
        std::map<glm::u32, std::vector<Descriptor>> _writes;
    };
}
