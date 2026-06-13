//
// Created by radue on 3/7/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module gfx:vk_surface;
import :vk_context;
import :surface;
import :window;

namespace gfx::vk
{
    export class Surface final : public gfx::Surface, public Wrapper<::vk::SurfaceKHR>
    {
    public:
        explicit Surface(const gfx::Window& window);
        ~Surface() override;

        [[nodiscard]] const std::vector<::vk::SurfaceFormatKHR>& getFormats() const;

        [[nodiscard]] const std::vector<::vk::PresentModeKHR>& getPresentModes() const;

        [[nodiscard]] const ::vk::SurfaceCapabilitiesKHR& getCapabilities() const;

    private:
        mutable ::vk::SurfaceCapabilitiesKHR _capabilities;
        mutable ::std::vector<::vk::SurfaceFormatKHR> _formats;
        mutable ::std::vector<::vk::PresentModeKHR> _presentModes;
    };


}
