#pragma once

#include <span>
#include <string_view>

namespace kui::detail
{
    /** @brief One of the Material icons' SVGs, as icons/material/<style>/<name>.svg has it. */
    struct EmbeddedIcon {
        std::string_view style;
        std::string_view name;
        std::string_view svg;
    };

    /** @brief Every one of them, compiled in: generated at build time from icons/material by cmake/EmbedIcons.cmake. */
    std::span<const EmbeddedIcon> MaterialIconData();
}
