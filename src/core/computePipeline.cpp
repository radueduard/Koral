//
// Created by radue on 2/21/2026.
//
#include "computePipeline.h"
#include "descriptorSetLayout.h"
#include "impl/open_gl/computePipeline.h"

#include "io/window.h"

namespace gfx
{
    std::unique_ptr<ComputePipeline> ComputePipeline::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::ComputePipeline>(*this);
        case API::eVulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    const gfx::DescriptorSetLayout& ComputePipeline::getSetLayout(const glm::u32 index) const
    {
        if (!_setLayouts.contains(index))
            throw std::runtime_error("This pipeline does not contain a set with that index!");
        return *_setLayouts.at(index);
    }

    ComputePipeline::ComputePipeline(const Builder& createInfo) : _shader(createInfo.computeShader)
    {
        for (auto memoryLayout = _shader->get().getMemoryLayout();
            const auto& [setIndex, setDescription] : memoryLayout.descriptorSets)
        {
            auto builder = DescriptorSetLayout::Builder();
            for (const auto& [binding, descriptor] : setDescription.descriptors)
            {
                const auto& [type, name, count] = descriptor;
                builder.addBinding(binding, type, count);
            }
            _setLayouts[setIndex] = builder.build();
        }
    }
}
