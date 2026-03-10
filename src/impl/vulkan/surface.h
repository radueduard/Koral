//
// Created by radue on 3/7/2026.
//

#pragma once
#include "utils/vk_wrapper.h"

#include <vulkan/vulkan.hpp>

namespace gfx::io
{
    class Window;
}

namespace gfx::vk
{
    class Surface final : public Wrapper<::vk::SurfaceKHR>
    {
    public:
        explicit Surface(const gfx::io::Window& window);
        ~Surface() override;

        [[nodiscard]] const std::vector<::vk::SurfaceFormatKHR>& getFormats() const { return _formats; }
        [[nodiscard]] const std::vector<::vk::PresentModeKHR>& getPresentModes() const { return _presentModes; }
        [[nodiscard]] const ::vk::SurfaceCapabilitiesKHR& getCapabilities() const { return _capabilities; }
    private:
        mutable ::vk::SurfaceCapabilitiesKHR _capabilities;
        mutable ::std::vector<::vk::SurfaceFormatKHR> _formats;
        mutable ::std::vector<::vk::PresentModeKHR> _presentModes;
    };
}
