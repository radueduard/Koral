//
// For a module's own C interface: what it needs of Koral's handles, which are Koral's to look inside.
//

#pragma once

#include "api.h"
#include "image.h"
#include "koral_c.h"
#include "resource.h"

namespace kor::capi
{
    /** @brief The image a KoralImage handle refers to. Throws (std::runtime_error) when it is not one. */
    KORAL_API ResourceRef<const Image> ImageOf(KoralResource* handle);
    /** @brief A borrowed KoralImage handle onto @p image, released with koral_resource_release; null for an empty ref. */
    KORAL_API KoralResource* BorrowImage(const ResourceRef<const Image>& image);
}
