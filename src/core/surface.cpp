//
// Created by Eduard Andrei Radu on 14.03.2026.
//

module;

module gfx;
import :surface;

import :vk_surface;

namespace gfx {
    std::unique_ptr<gfx::Surface> Surface::Create(const gfx::Window& window) {
        switch (window.getAPI()) {
            case API::eOpenGL: return std::make_unique<gfx::Surface>(window);
            case API::eVulkan: return std::make_unique<gfx::vk::Surface>(window);
            default: throw std::runtime_error("Unknown API");
        }
    }
}
