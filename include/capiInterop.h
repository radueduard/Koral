//
// For a module's own C interface: what it needs of Koral's handles, which are Koral's to look inside.
//

#pragma once

#include "api.h"
#include "image.h"
#include "koral_c.h"
#include "resource.h"
#include "token.h"

namespace kor::capi
{
    /** @brief The image a KoralImage handle refers to. Throws (std::runtime_error) when it is not one. */
    KORAL_API ResourceRef<const Image> ImageOf(KoralResource* handle);
    /** @brief A borrowed KoralImage handle onto @p image, released with koral_resource_release; null for an empty ref. */
    KORAL_API KoralResource* BorrowImage(const ResourceRef<const Image>& image);
    /** @brief A KoralToken onto @p token, the caller's, freed with koral_token_destroy. */
    KORAL_API KoralToken* MakeToken(const Token& token);
    /** @brief The token a KoralToken handle holds. */
    KORAL_API Token TokenOf(const KoralToken* handle);
}
