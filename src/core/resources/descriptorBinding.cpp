//
// Created by radue on 2/20/2026.
//

#include "descriptorBinding.h"
#include "impl/open_gl/resources/descriptorBinding.h"

#include "io/window.h"

namespace gfx
{
    std::unique_ptr<Descriptor> Descriptor::Create(const CreateInfo& createInfo)
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Descriptor>(createInfo);
        case API::Vulkan:
            throw std::runtime_error("vulkan is not supported");
        default:
            throw std::runtime_error("Unknown API");
        }
    }

    Descriptor::Descriptor(const CreateInfo& createInfo): _type(createInfo.type)
    {
        switch (createInfo.type) {
        case Type::eUniformBuffer:
        case Type::eStorageBuffer:
            _buffer = createInfo.buffer;
            _offset = createInfo.offset;
            _range = createInfo.range;
            break;
        case Type::eCombinedImageSampler:
        case Type::eStorageImage:
        case Type::eSampler:
            _imageView = createInfo.imageView;
            _sampler = createInfo.sampler;
            break;
        default:
            throw std::runtime_error("Unsupported descriptor type!");
        }
    }
}
