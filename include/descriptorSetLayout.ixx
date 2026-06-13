//
// Created by radue on 3/4/2026.
//

module;

#include "api.h"
#include <glm/glm.hpp>

export module gfx:descriptorSetLayout;
import :types;

import std;

namespace gfx
{
    class GFX_API DescriptorSetLayout
    {
    public:
        class GFX_API Builder
        {
            friend class DescriptorSetLayout;
        public:
            Builder& addBinding(glm::u32 binding, DescriptorType type, glm::u32 count = 1);
            [[nodiscard]] std::unique_ptr<DescriptorSetLayout> build() const;
        private:
            std::map<glm::u32, std::pair<DescriptorType, glm::u32>> _bindings;
        };

        explicit DescriptorSetLayout(const Builder &builder);
        virtual ~DescriptorSetLayout() = default;

        DescriptorSetLayout(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

        [[nodiscard]] std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> getBindings() const;
        [[nodiscard]] DescriptorType getBindingType(glm::u32 binding) const;

    protected:
        std::map<glm::u32, std::pair<DescriptorType, glm::u32>> _bindings;
    };
}
