//
// Created by radue on 2/21/2026.
//
#include <computePipeline.h>
#include <descriptorSetLayout.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/computePipeline.h"
#include "../backends/vulkan/computePipeline.h"

#include "shader.h"
#include "../../include/window.h"

namespace gfx
{
    ComputePipeline::Builder& ComputePipeline::Builder::setComputeShader(ResourceRef<const Shader> computeShader)
    {
        this->computeShader = std::cref(*computeShader);
        return *this;
    }

    gfx::Resource<ComputePipeline> ComputePipeline::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::MakeResource<ogl::ComputePipeline>(*this);
            case API::eVulkan:
            return gfx::MakeResource<vk::ComputePipeline>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    ComputePipeline::~ComputePipeline() = default;

    void ComputePipeline::Bind(const gfx::CommandBuffer& commandBuffer) const
    {
        _bound = true;
    }

    void ComputePipeline::Unbind() const
    {
        _bound = false;
    }

    const gfx::DescriptorSetLayout& ComputePipeline::getSetLayout(const glm::u32 index) const
    {
        if (!_setLayouts.contains(index))
            throw std::runtime_error("This pipeline does not contain a set with that index!");
        return *_setLayouts.at(index);
    }

    ComputePipeline::ComputePipeline(const Builder& createInfo) : _shader(createInfo.computeShader)
    {
        auto memoryLayout = _shader->get().getMemoryLayout();
        for (const auto& [setIndex, setDescription] : memoryLayout.descriptorSets)
        {
            auto builder = DescriptorSetLayout::Builder();
            for (const auto& [binding, descriptor] : setDescription.descriptors)
            {
                const auto& [type, name, count, stages] = descriptor;
                builder.addBinding(binding, type, count);
            }
            _setLayouts[setIndex] = builder.build();
        }
        for (const auto& [offset, pushConstant] : memoryLayout.pushConstants)
        {
            _pushConstantRanges[offset] = pushConstant;
        }
    }
}
