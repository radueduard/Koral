//
// Created by Eduard Andrei Radu on 14.03.2026.
//

module;

#include <memory>
#include <stdexcept>

module gfx.surface;
import vk.surface;
import gfx.context;
import gfx.window;

namespace gfx {
    std::unique_ptr<gfx::Surface> Surface::Create(const gfx::io::Window& window) {
        switch (window.getAPI()) {
            case API::eOpenGL: return std::make_unique<gfx::Surface>(window);
            case API::eVulkan: return std::make_unique<gfx::vk::Surface>(window);
            default: throw std::runtime_error("Unknown API");
        }
    }
}
