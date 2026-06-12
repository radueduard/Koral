//
// Created by radue on 3/4/2026.
//

module;

#include <stdexcept>

module gfx.descriptorSet;
import gfx.descriptorBinding;
import gfx.structs;
import gfx.log;

namespace gfx {
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
            try {
                auto& _ = descriptor.getBuffer();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for buffer binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for buffer binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid buffer!");
            }
            break;
        case DescriptorType::eCombinedImageSampler:
            try {
                auto& _ = descriptor.getImageView();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for combined image sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for combined image sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid image view!");
            }
            try {
                auto& _ = descriptor.getSampler();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for combined image sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for combined image sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid sampler!");
            }
            break;
        case DescriptorType::eSampledImage:
        case DescriptorType::eStorageImage:
            try {
                auto& _ = descriptor.getImageView();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for image binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for image binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid image view!");
            }
            break;
        case DescriptorType::eSampler:
            try {
                auto& _ = descriptor.getSampler();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid sampler!");
            }
            break;
        default:
            throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(index) + "!");
        }

        writes[binding][index] = descriptor;
        return *this;
    }

    gfx::Resource<DescriptorSet> DescriptorSet::Builder::build()
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::MakeResource<ogl::DescriptorSet>(*this);
            case API::eVulkan:
            return gfx::MakeResource<vk::DescriptorSet>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    DescriptorSet::DescriptorSet(const Builder& builder) : _layout(builder.layout), _writes(builder.writes)
    {
        _isPerFrame = false;
        for (const auto& write : _writes | std::views::values) {
            for (const auto& descriptor : write)
            {
                try {
                    if (const auto& buffer = descriptor.getBuffer(); buffer.isPerFrame()) {
                        _isPerFrame = true;
                        break;
                    }
                } catch (const std::exception&) {
                    // Not a buffer descriptor, ignore
                }
                try {
                    if (const auto imageView = descriptor.getImageView(); imageView.isPerFrame()) {
                        _isPerFrame = true;
                        break;
                    }
                } catch (const std::exception&) {
                    // Not an image view descriptor, ignore
                }
            }
        }
    }
}
