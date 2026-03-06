//
// Created by radue on 3/4/2026.
//

#include "descriptorSet.h"
#include "impl/open_gl/descriptorSet.h"

#include "descriptorBinding.h"
#include "descriptorSetLayout.h"
#include "io/window.h"

namespace gfx
{
    DescriptorSet::Builder::Builder(const DescriptorSetLayout& layout) : layout(layout)
    {
        for (const auto& [binding, type, count] : layout.getBindings()) {
            writes[binding] = std::vector<Descriptor>();
            writes[binding].resize(count);
        }
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding, const Descriptor& descriptor, const glm::u32 index)
    {
        if (!writes.contains(binding)) {
            throw std::runtime_error("Binding " + std::to_string(binding) + " does not exist in the layout!");
        }
        if (index >= writes[binding].size()) {
            throw std::runtime_error("Index " + std::to_string(index) + " is out of bounds for binding " + std::to_string(binding) + " which has count " + std::to_string(writes[binding].size()) + "!");
        }
        if (writes[binding][index].isValid()) {
            throw std::runtime_error("Descriptor at binding " + std::to_string(binding) + " index " + std::to_string(index) + " is already written!");
        }
        if (!descriptor.isValid()) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        switch (layout.getBindingType(binding))
        {
        case DescriptorType::eUniformBuffer:
        case DescriptorType::eStorageBuffer:
            if (descriptor._buffer == nullptr)
            {
                throw std::runtime_error("Descriptor for buffer binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid buffer!");
            }
            break;
        case DescriptorType::eCombinedImageSampler:
            if (descriptor._imageView == nullptr) {
                throw std::runtime_error("Descriptor for image binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid image view!");
            }
            if (descriptor._sampler == nullptr)
            {
                throw std::runtime_error("Descriptor for image binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid sampler!");
            }
            break;
        case DescriptorType::eStorageImage:
            if (descriptor._imageView == nullptr) {
                throw std::runtime_error("Descriptor for image binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid image view!");
            }
            break;
        case DescriptorType::eSampler:
            if (descriptor._sampler == nullptr)            {
                throw std::runtime_error("Descriptor for sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid sampler!");
            }
            break;
        default:
            throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(index) + "!");
        }


        writes[binding][index] = descriptor;
        return *this;
    }

    std::unique_ptr<DescriptorSet> DescriptorSet::Builder::build()
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::DescriptorSet>(*this);
        case API::eVulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    DescriptorSet::DescriptorSet(const Builder& builder) : _layout(builder.layout), _writes(builder.writes)
    {

    }
}
