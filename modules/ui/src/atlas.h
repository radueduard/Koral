//
// koral-ui: the glyph atlas every font draws from.
//

#pragma once

#include <image.h>
#include <resource.h>

namespace kui::detail
{
    /**
     * @brief The atlas image, with every glyph rendered so far on the GPU (or on its way: the upload is
     *        queued ahead of the frame's work). Made on first use. Main thread.
     */
    kor::ResourceRef<const kor::Image> AtlasImage();

    /** @brief Lets the atlas image go, before the device does. The glyphs stay: a later AtlasImage() uploads them again. */
    void ReleaseAtlas();
}
