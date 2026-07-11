//
// Created by Eduard Andrei Radu on 14.03.2026.
//

#include <surface.h>
#include <window.h>
#include "framebuffer.h"
#include "surface.h"

#include "../backends/vulkan/surface.h"
#include <memory>


namespace gfx {
    std::unique_ptr<gfx::Surface> Surface::Create(const gfx::Window& window) {
        switch (window.getAPI()) {
            case API::eOpenGL: return std::make_unique<gfx::Surface>(window);
            case API::eVulkan: return std::make_unique<gfx::vk::Surface>(window);
            default: throw std::runtime_error("Unknown API");
        }
    }
}
