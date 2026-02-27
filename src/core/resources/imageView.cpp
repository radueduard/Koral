//
// Created by radue on 2/20/2026.
//

#include "imageView.h"
#include "impl/open_gl/resources/imageView.h"


#include "context.h"
#include "io/window.h"

namespace gfx
{
    std::unique_ptr<ImageView> ImageView::Create(const Image& image, const CreateInfo& createInfo)
    {
        switch (Context::Window().getAPI()) {
            case API::OpenGL:
                return std::make_unique<ogl::ImageView>(dynamic_cast<const ogl::Image&>(image), createInfo);
            case API::Vulkan:
                throw std::runtime_error("The Vulkan API is not yet supported!");
            default:
                throw std::runtime_error("Unknown graphics API!");
        }
    }

    ImageView::ImageView(const CreateInfo& createInfo) :
        _viewType(createInfo.viewType),
        _baseMipLevel(createInfo.baseMipLevel),
        _mipLevelCount(createInfo.mipLevelCount),
        _baseArrayLayer(createInfo.baseArrayLayer),
        _arrayLayerCount(createInfo.arrayLayerCount) {}
}