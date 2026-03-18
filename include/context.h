//
// Created by radue on 2/16/2026.
//

#pragma once
#include "api.h"
#include <filesystem>
#include <thread>
#include <glm/fwd.hpp>

struct ImGuiContext;

namespace gfx {
    class GUI;
    class Scheduler;
    class Framebuffer;

    namespace io
    {
        class Window;
    }

    GFX_API std::filesystem::path assetPath(const std::filesystem::path& relativePath);
    GFX_API std::filesystem::path shaderPath(const std::filesystem::path& relativePath);
    GFX_API std::filesystem::path scenePath(const std::filesystem::path& relativePath);
    GFX_API std::filesystem::path templatePath(const std::filesystem::path& relativePath);

    enum class API {
        eOpenGL,
        eVulkan,
    };

    class Context
    {
        friend class io::Window;
        friend class gfx::Scheduler;
        friend class gfx::GUI;
    public:
        static GFX_API io::Window& FocusedWindow();

        static GFX_API io::Window& Window();
        static GFX_API const gfx::Scheduler& Scheduler();

        static GFX_API const gfx::Framebuffer& DefaultFramebuffer();

        static GFX_API void InitScheduler();
        static GFX_API void DestroyScheduler();

        static GFX_API glm::u32 ThreadId() { return std::hash<std::thread::id>()(_threadId); }
        static GFX_API glm::u32 MainThreadId() { return std::hash<std::thread::id>()(_mainThreadId); }
        static GFX_API ImGuiContext* GetCurrentImGuiContext();


    private:
        static GFX_API void setFocusedWindow(io::Window* window);

        inline static io::Window* _focusedWindow = nullptr;
        inline static thread_local io::Window* _currentThreadLinkedWindow = nullptr;
        inline static thread_local gfx::Scheduler* _scheduler = nullptr;
        inline static thread_local ImGuiContext* _imguiContext = nullptr;

        static thread_local std::thread::id _threadId;
        inline static std::thread::id _mainThreadId = std::this_thread::get_id();

    };
}
