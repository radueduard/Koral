//
// Created by radue on 2/20/2026.
//

#include "imageView.h"

namespace gfx::ogl
{
    ImageView::ImageView(const Image& image, const CreateInfo& createInfo) :
        gfx::ImageView(createInfo), _image(image) {}
}
