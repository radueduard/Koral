//
// Created by radue on 2/20/2026.
//

#include "imageView.h"

namespace gfx::ogl
{
    ImageView::ImageView(const Builder& createInfo) :
        gfx::ImageView(createInfo),
        _image(dynamic_cast<const ogl::Image&>(createInfo.image)) {}
}
