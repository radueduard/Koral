//
// Created by radue on 2/16/2026.
//

#pragma once
#include <filesystem>

namespace gfx {
    class Framebuffer;

    namespace io
    {
        class Window;
    }

    std::filesystem::path asset(const std::filesystem::path& relativePath);

    std::filesystem::path shader(const std::filesystem::path& relativePath);

    enum class API {
        eOpenGL,
        eVulkan,
    };

    class Context
    {
        friend class io::Window;
    public:
        static io::Window& FocusedWindow();

        static io::Window& Window();

        static const gfx::Framebuffer* DefaultFramebuffer();

    private:
        static void setFocusedWindow(io::Window* window);

        inline static io::Window* _focusedWindow = nullptr;
        inline static thread_local io::Window* _currentThreadLinkedWindow = nullptr;
    };
}
