//
// Created by radue on 2/16/2026.
//

#pragma once
#include <filesystem>
#include <thread>
#include <glm/fwd.hpp>


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

        static glm::u32 ThreadId() { return std::hash<std::thread::id>()(_threadId); }
        static glm::u32 MainThreadId() { return std::hash<std::thread::id>()(_mainThreadId); }


    private:
        static void setFocusedWindow(io::Window* window);

        inline static io::Window* _focusedWindow = nullptr;
        inline static thread_local io::Window* _currentThreadLinkedWindow = nullptr;
        inline static thread_local gfx::Scheduler* _scheduler = nullptr;

        inline thread_local static auto _threadId = std::thread::id();
        inline static std::thread::id _mainThreadId = std::this_thread::get_id();

    };
}
