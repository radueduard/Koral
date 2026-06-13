//
// Created by radue on 3/4/2026.
//

module gfx;
import :descriptorSetLayout;

import :vk_descriptorSetLayout;

namespace gfx
{
    DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::addBinding(glm::u32 binding, DescriptorType type,
        glm::u32 count)
    {
        if (_bindings.contains(binding)) {
            throw std::runtime_error("Binding " + std::to_string(binding) + " already exists in the layout!");
        }
        _bindings[binding] = { type, count };
        return *this;
    }

    std::unique_ptr<DescriptorSetLayout> DescriptorSetLayout::Builder::build() const
    {
        switch (Context::GetWindow().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<DescriptorSetLayout>(*this);
        case API::eVulkan:
            return std::make_unique<vk::DescriptorSetLayout>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> DescriptorSetLayout::getBindings() const
    {
        std::vector<std::tuple<glm::u32, DescriptorType, glm::u32>> bindings;
        for (const auto& [binding, typeAndCount] : _bindings) {
            const auto& [type, count] = typeAndCount;
            bindings.emplace_back(binding, type, count);
        }
        return bindings;
    }

    DescriptorType DescriptorSetLayout::getBindingType(const glm::u32 binding) const
    {
        if (!_bindings.contains(binding)) {
            throw std::runtime_error("Binding " + std::to_string(binding) + " does not exist in the layout!");
        }
        return _bindings.at(binding).first;
    }

    DescriptorSetLayout::DescriptorSetLayout(const Builder& builder) : _bindings(builder._bindings)
    {
    }
}
