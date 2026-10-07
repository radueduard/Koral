//
// What the device was made with, for Context::Supports and for checking libraries loaded after it.
//

#pragma once

#include "deviceFeatures.h"

namespace kor::detail
{
    /** @brief Called by the backend once the device is made: what the GPU has, and what was enabled of it. */
    void SetDeviceFeatures(Flags<Feature> available, Flags<Feature> enabled);
    /** @brief Called when the device goes: requests are again for the next one. */
    void ClearDeviceFeatures();
    /** @brief Every required and every optional feature asked for, and who asked for each required one. */
    struct WantedFeatures { Flags<Feature> required, optional; std::vector<FeatureRequest> requests; };
    [[nodiscard]] WantedFeatures Wanted();
}
