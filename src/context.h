//
// Created by radue on 2/16/2026.
//

#pragma once
#include <filesystem>


namespace gfx {
    class Scheduler;
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
        friend class Scheduler;
    public:
        static io::Window& FocusedWindow();

        static io::Window& Window();
        static const Scheduler& Scheduler();

        static const gfx::Framebuffer& DefaultFramebuffer();

        static void InitScheduler();
        static void DestroyScheduler();

    private:
        static void setFocusedWindow(io::Window* window);

        inline static io::Window* _focusedWindow = nullptr;
        inline static thread_local io::Window* _currentThreadLinkedWindow = nullptr;
        inline static thread_local gfx::Scheduler* _scheduler = nullptr;
    };
}
