//
// Created by radue on 3/4/2026.
//

#pragma once
#include <map>
#include <memory>
#include <vector>
#include <glm/fwd.hpp>

namespace gfx
{
    enum class DescriptorType {
        eUniformBuffer,
        eStorageBuffer,
        eCombinedImageSampler,
        eStorageImage,
        eSampler,
    };

    class DescriptorSetLayout
    {
    public:
        class Builder
        {
            friend class DescriptorSetLayout;
        public:
            Builder& addBinding(glm::u32 binding, DescriptorType type, glm::u32 count = 1);
            std::unique_ptr<DescriptorSetLayout> build() const;
        private:
            std::map<glm::u32, std::pair<DescriptorType, glm::u32>> _bindings;
        };

        virtual ~DescriptorSetLayout() = default;

        std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> getBindings() const;
        DescriptorType getBindingType(glm::u32 binding) const;

    protected:
        explicit DescriptorSetLayout(const Builder &builder);

        std::map<glm::u32, std::pair<DescriptorType, glm::u32>> _bindings;
    };
}
