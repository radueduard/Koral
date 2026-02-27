//
// Created by radue on 2/18/2026.
//

#include "image.h"
#include "impl/open_gl/resources/image.h"

#include "context.h"
#include "io/window.h"

namespace gfx
{
    std::unique_ptr<Image> Image::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Image>(*this);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Image::Image(const Builder& createInfo) :
        type(createInfo.type),
        format(createInfo.format),
        extent(createInfo.extent),
        mipLevels(createInfo.mipLevels),
        arrayLayers(createInfo.arrayLayers),
        msaa(createInfo.msaa),
        usage(createInfo.usage) {}

    bool IsDepthStencilFormat(const Image::Format format)
    {
        switch (format)
        {
        case Image::Format::eD16_UNORM:
        case Image::Format::eD24_UNORM_S8_UINT:
        case Image::Format::eD32_SFLOAT:
        case Image::Format::eD32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }
} // gfx