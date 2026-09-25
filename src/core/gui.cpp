//
// Created by radue on 3/17/2026.
//

#include "gui.h"
#include "interface.h"

#include "context.h"
#include "scene.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"

#include "../backends/vulkan/gui.h"
#include "../backends/vulkan/device.h"
#include "../backends/vulkan/surface.h"
#include "../backends/vulkan/vulkanContext.h"

#include <imgui.h>
// #include <imguizmo.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>
#include <GLFW/glfw3.h>
#include <map>
#include <unordered_map>
#include <mutex>

#include "commandBuffer.h"
#include "input.h"
#include "log.h"
#include "module.h"
#include <IconsFontAwesome6.h>

namespace
{
    // Every module that includes gui.h registers its copy of ImGui here as it loads — Koral itself,
    // the runtime, and above all the scene library. See the header for why this exists.
    //
    // The entries hold function pointers *into* those modules, so they are only valid while the
    // module is loaded. Nothing unloads a scene today (LoadLibrary/dlopen hand out a handle that is
    // never closed), and a scene that could be unloaded would have to deregister here first.
    std::vector<kor::detail::ImGuiModule>& imguiModules()
    {
        static std::vector<kor::detail::ImGuiModule> modules;
        return modules;
    }

    // Sharing one ImGuiContext between two separately compiled copies of ImGui is only safe if both
    // agree on its layout. The version string alone does not prove that: the docking branch Koral
    // builds against reports the same "1.91.9" as the stock one while laying ImGuiIO out
    // differently, so a scene that found its own imgui through its own vcpkg would silently scribble
    // over the context. The struct sizes catch that; they are what IMGUI_CHECKVERSION() compares.
    bool layoutMatches(const kor::detail::ImGuiModule& imguiModule)
    {
        return std::string_view(imguiModule.version) == IMGUI_VERSION
            && imguiModule.sizeOfIO == sizeof(ImGuiIO)
            && imguiModule.sizeOfStyle == sizeof(ImGuiStyle)
            && imguiModule.sizeOfVec2 == sizeof(ImVec2)
            && imguiModule.sizeOfVec4 == sizeof(ImVec4)
            && imguiModule.sizeOfDrawVert == sizeof(ImDrawVert)
            && imguiModule.sizeOfDrawIdx == sizeof(ImDrawIdx);
    }

    // Point one module's ImGui globals at our context and allocators. Passing a null context is how
    // Shutdown() takes it back, so nothing keeps a dangling pointer to a destroyed context.
    void bindImGuiModule(const kor::detail::ImGuiModule& imguiModule, ImGuiContext* context)
    {
        if (context)
        {
            if (!layoutMatches(imguiModule))
            {
                kor::log::Error("[gui] a loaded library was built against a different ImGui than Koral "
                                "(it reports {}, ImGuiIO {} bytes; Koral has {}, {} bytes). Its ImGui "
                                "calls are left unbound rather than share a context that would be read "
                                "as a different layout. Build the scene against the ImGui the SDK "
                                "ships (same version *and* the docking feature) and it will bind.",
                                imguiModule.version, imguiModule.sizeOfIO, IMGUI_VERSION, sizeof(ImGuiIO));
                return;
            }

            // Allocators first: from here on the module allocates ImGui objects through the same
            // functions we free them with. Both sides use the process CRT today, so a mismatch is
            // survivable, but only by accident — SetAllocatorFunctions is the guarantee.
            ImGuiMemAllocFunc allocFunc = nullptr;
            ImGuiMemFreeFunc freeFunc = nullptr;
            void* userData = nullptr;
            ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
            imguiModule.setAllocatorFunctions(allocFunc, freeFunc, userData);
        }

        imguiModule.setCurrentContext(context);
    }

    void bindImGuiModules(ImGuiContext* context)
    {
        for (const auto& imguiModule : imguiModules())
            bindImGuiModule(imguiModule, context);
    }
}

namespace
{
    // How many loaded modules registered each copy: on ELF platforms every one shares Koral's, so one
    // unloading must not take Koral's own entry with it.
    std::map<void (*)(ImGuiContext*), int>& imguiModuleReferences()
    {
        static std::map<void (*)(ImGuiContext*), int> references;
        return references;
    }
}

void kor::detail::RegisterImGuiModule(const ImGuiModule& imguiModule)
{
    auto& modules = imguiModules();
    ++imguiModuleReferences()[imguiModule.setCurrentContext];

    // The registrar is an inline variable, so it is constructed once per module — but a module that
    // shares Koral's ImGui (every ELF/Mach-O one) reports the identical pointers, and registering
    // it twice would leave a duplicate entry to bind on every future load.
    const auto sameCopy = [&](const ImGuiModule& known) { return known.setCurrentContext == imguiModule.setCurrentContext; };
    if (std::ranges::any_of(modules, sameCopy))
        return;

    modules.push_back(imguiModule);

    // Registration normally happens while the module loads, long before there is a context to hand
    // out. A module that arrives while an interface is current is bound to it here.
    if (ImGui::GetCurrentContext())
        bindImGuiModule(imguiModule, ImGui::GetCurrentContext());
}

void kor::detail::UnregisterImGuiModule(void (*setCurrentContext)(ImGuiContext*))
{
    auto& references = imguiModuleReferences();
    const auto it = references.find(setCurrentContext);
    if (it == references.end() || --it->second > 0) return;
    references.erase(it);
    std::erase_if(imguiModules(), [&](const ImGuiModule& known) { return known.setCurrentContext == setCurrentContext; });
}

ImFont* AddFont(const std::filesystem::path& path, const float size)
{
    const std::string iconPath = kor::AssetPath(FONT_ICON_FILE_NAME_FAS).string();
    const float iconFontSize = size * 2.0f / 3.0f; // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly

    ImFontConfig config;
    std::ranges::copy(path.filename().string(), config.Name);
    config.MergeMode = false;
    config.PixelSnapH = true;
    ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), size, &config);
    config.MergeMode = true;
    config.GlyphMinAdvanceX = iconFontSize;
    static constexpr ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(iconPath.c_str(), iconFontSize, &config, icons_ranges);
}

void kor::Interface::DefineStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ChildRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.TabRounding = 0.0f;
    style.WindowPadding = ImVec2(11.0f, 5.0f);
    // do not show tab dropdown
    style.DockingSeparatorSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 2.0f;
    style.TabBarOverlineSize = 5.0f;
    style.TabBorderSize = 2.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.DisplaySafeAreaPadding = ImVec2(11.0f, 5.0f);
    style.CellPadding = ImVec2(11.0f, 5.0f);
    style.FramePadding = ImVec2(11.0f, 5.0f);
    style.ItemSpacing = ImVec2(11.0f, 5.0f);

    ImVec4 highlight = ImVec4(static_cast<float>(0x44) / 255.0f, static_cast<float>(0x6D) / 255.0f, static_cast<float>(0xF6) / 255.0f, 1.0f);
    ImVec4 highlightHover = ImVec4(static_cast<float>(0x44) / 255.0f, static_cast<float>(0x6D) / 255.0f, static_cast<float>(0xF6) / 255.0f, 0.5f);
    ImVec4 secondary = ImVec4(static_cast<float>(0xA5) / 255.f, static_cast<float>(0xA6) / 255.f, static_cast<float>(0x1E) / 255.f, 1.0f);
    ImVec4 secondaryHover = ImVec4(static_cast<float>(0xA5) / 255.f, static_cast<float>(0xA6) / 255.f, static_cast<float>(0x1E) / 255.f, 0.5f);

    ImVec4 background = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    ImVec4 unfocused = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    ImVec4 text = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);

    style.Colors[ImGuiCol_WindowBg] = background;
    style.Colors[ImGuiCol_Header] = highlight;
    style.Colors[ImGuiCol_HeaderHovered] = highlightHover;
    style.Colors[ImGuiCol_HeaderActive] = highlightHover;
    style.Colors[ImGuiCol_Button] = unfocused;
    style.Colors[ImGuiCol_ButtonHovered] = highlightHover;
    style.Colors[ImGuiCol_ButtonActive] = highlightHover;
    style.Colors[ImGuiCol_FrameBg] = unfocused;
    style.Colors[ImGuiCol_FrameBgHovered] = highlightHover;
    style.Colors[ImGuiCol_FrameBgActive] = highlightHover;
    style.Colors[ImGuiCol_Tab] = unfocused;
    style.Colors[ImGuiCol_TabActive] = unfocused;
    style.Colors[ImGuiCol_TabHovered] = highlightHover;
    style.Colors[ImGuiCol_TabUnfocused] = unfocused;
    style.Colors[ImGuiCol_TabUnfocusedActive] = unfocused;
    style.Colors[ImGuiCol_TabSelected] = unfocused;
    style.Colors[ImGuiCol_TabSelectedOverline] = highlight;
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = highlightHover;
    style.Colors[ImGuiCol_Border] = background;
    style.Colors[ImGuiCol_SliderGrab] = secondary;
    style.Colors[ImGuiCol_SliderGrabActive] = secondaryHover;
    style.Colors[ImGuiCol_TitleBg] = unfocused;
    style.Colors[ImGuiCol_TitleBgActive] = unfocused;
    style.Colors[ImGuiCol_TitleBgCollapsed] = unfocused;
    style.Colors[ImGuiCol_Text] = text;
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(text.x, text.y, text.z, text.w * 0.5f);
    style.Colors[ImGuiCol_TextSelectedBg] = highlightHover;
    style.Colors[ImGuiCol_PopupBg] = background;
    style.Colors[ImGuiCol_Separator] = highlightHover;
    style.Colors[ImGuiCol_SeparatorHovered] = highlightHover;
    style.Colors[ImGuiCol_SeparatorActive] = highlightHover;
    style.Colors[ImGuiCol_ResizeGrip] = background;
    style.Colors[ImGuiCol_ResizeGripHovered] = highlightHover;
    style.Colors[ImGuiCol_ResizeGripActive] = highlight;
    style.Colors[ImGuiCol_CheckMark] = highlight;
    style.Colors[ImGuiCol_ScrollbarBg] = background;
    style.Colors[ImGuiCol_ScrollbarGrab] = unfocused;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = highlightHover;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = highlightHover;
    style.Colors[ImGuiCol_DockingPreview] = highlightHover;
    style.Colors[ImGuiCol_DockingEmptyBg] = background;
    style.Colors[ImGuiCol_PlotLines] = text;
    style.Colors[ImGuiCol_PlotLinesHovered] = highlightHover;
    style.Colors[ImGuiCol_PlotHistogram] = text;
    style.Colors[ImGuiCol_PlotHistogramHovered] = highlightHover;
    style.Colors[ImGuiCol_BorderShadow] = text;


    auto& io = ImGui::GetIO();
    _fonts[Font::eRegular] = AddFont(kor::AssetPath("fonts/Inter_28pt-Regular.ttf"), 28.0f);
    _fonts[Font::eBold] = AddFont(kor::AssetPath("fonts/Inter_28pt-Bold.ttf"), 32.0f);
    _fonts[Font::eItalic] = AddFont(kor::AssetPath("fonts/Inter_28pt-Italic.ttf"), 28.0f);
    _fonts[Font::eBlack] = AddFont(kor::AssetPath("fonts/Inter_28pt-Black.ttf"), 36.0f);
    _fonts[Font::eLight] = AddFont(kor::AssetPath("fonts/Inter_28pt-Light.ttf"), 26.0f);

    io.FontDefault = _fonts[Font::eRegular];
    io.FontGlobalScale = .55f;

    // When panels can detach into their own OS windows, those windows are real, opaque top-level
    // windows the compositor draws directly — a rounded, semi-transparent window body that looks
    // fine docked reads as a rendering glitch out on the desktop. ImGui's own recommendation for
    // enabling viewports: square the corners and force the window background fully opaque. Left as
    // the docked look when viewports are off (e.g. under Wayland).
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}


namespace
{
    /**
     * @brief Stops the interface chasing a pointer that is not moving.
     *
     * A captured cursor is parked: the OS pointer does not move and only the movement is reported.
     * GLFW still delivers a position, though — an ever-growing virtual one — and the ImGui backend
     * takes it at face value, so within a few frames of aiming a camera the interface believes the
     * pointer has slid off the screen, and whatever keyed on hovering lets the capture go by itself.
     * So while the cursor is captured the interface is told, once per frame, that the pointer is
     * exactly where it was when the capture began.
     */
    void freezePointerWhileCaptured(const kor::Input& input, std::optional<ImVec2>& parked)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (input.CurrentCursorMode() != kor::Input::CursorMode::eCaptured) {
            parked.reset();
            return;
        }
        if (!parked) parked = io.MousePos;
        io.AddMousePosEvent(parked->x, parked->y);
    }
}

kor::Resource<kor::GuiImage> kor::GuiImage::Create(kor::ResourceRef<const kor::Image> image, glm::u32 layer, glm::u32 level)
{
    // A handle is the current interface's texture: without one there is no backend to make it with.
    if (!kor::Interface::Current())
        return kor::Resource<kor::GuiImage>::Failed(kor::Error{.code = kor::ErrorCode::eInvalidArgument,
            .message = "GuiImage::Create needs a scene with an interface (Scene::EnableInterface) to be the current one"});
    kor::Interface::Current()->MakeCurrent();

    auto handle = [&] {
        switch (Context::ActiveAPI())
        {
        case API::eVulkan:
            return kor::MakeBackendResource<kor::GuiImage, kor::vk::GuiImage>(image, layer, level);
        default:
            throw std::runtime_error("Unsupported graphics API");
        }
    }();

    // Registered with the interface it was made in, which brings it up to date each frame. Without
    // this a handle shows whatever its image held at this moment, for ever.
    if (handle) kor::Interface::Current()->Track(kor::ResourceRef<kor::GuiImage>(handle));
    return handle;
}

namespace
{
    // What a scene without an interface leaves current: nothing, so a stray ImGui call fails loudly
    // instead of drawing into another scene's interface.
    thread_local const kor::Interface* g_currentInterface = nullptr;

    // ---- ImGui's own GLFW callbacks, run with the right context --------------------------------------
    //
    // ImGui's GLFW backend keeps its state in the current context. The windows it opens for undocked
    // panels get callbacks of its own (close, move, resize), as does the monitor list, and GLFW runs
    // them while polling events — outside every scene, with no context current. Each is wrapped so it
    // runs with the context of the interface it belongs to.

    struct ContextScope {
        ImGuiContext* previous;
        explicit ContextScope(ImGuiContext* context) : previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
        ~ContextScope() { ImGui::SetCurrentContext(previous); }
    };

    struct PlatformWindowCallbacks {
        ImGuiContext* context = nullptr;
        GLFWwindowclosefun close = nullptr;
        GLFWwindowposfun pos = nullptr;
        GLFWwindowsizefun size = nullptr;
    };

    std::unordered_map<GLFWwindow*, PlatformWindowCallbacks>& platformWindowCallbacks()
    {
        static std::unordered_map<GLFWwindow*, PlatformWindowCallbacks> table;
        return table;
    }

    void platformWindowClosed(GLFWwindow* window)
    {
        const auto it = platformWindowCallbacks().find(window);
        if (it == platformWindowCallbacks().end() || !it->second.close) return;
        const ContextScope scope(it->second.context);
        it->second.close(window);
    }

    void platformWindowMoved(GLFWwindow* window, const int x, const int y)
    {
        const auto it = platformWindowCallbacks().find(window);
        if (it == platformWindowCallbacks().end() || !it->second.pos) return;
        const ContextScope scope(it->second.context);
        it->second.pos(window, x, y);
    }

    void platformWindowResized(GLFWwindow* window, const int width, const int height)
    {
        const auto it = platformWindowCallbacks().find(window);
        if (it == platformWindowCallbacks().end() || !it->second.size) return;
        const ContextScope scope(it->second.context);
        it->second.size(window, width, height);
    }

    // Every live interface's context, and ImGui's monitor callback, which each of them wants run.
    std::vector<ImGuiContext*>& interfaceContexts()
    {
        static std::vector<ImGuiContext*> contexts;
        return contexts;
    }

    GLFWmonitorfun& imguiMonitorCallback()
    {
        static GLFWmonitorfun callback = nullptr;
        return callback;
    }

    void monitorsChanged(GLFWmonitor* monitor, const int event)
    {
        if (!imguiMonitorCallback()) return;
        for (ImGuiContext* context : interfaceContexts()) {
            const ContextScope scope(context);
            imguiMonitorCallback()(monitor, event);
        }
    }

    void claimMonitorCallback()
    {
        // ImGui's GLFW backend installs its own on every Init, and restores the one before on every
        // Shutdown; either way it ends up as the one GLFW calls, outside every context.
        if (const auto previous = glfwSetMonitorCallback(interfaceContexts().empty() ? nullptr : monitorsChanged);
            previous && previous != monitorsChanged)
            imguiMonitorCallback() = previous;
    }
}

kor::Interface* kor::Interface::Current()
{
    const auto* scene = Scene::Current();
    return scene && scene->_interface ? scene->_interface.get() : nullptr;
}

void kor::Interface::MakeCurrent() const
{
    // Koral's own ImGui and every loaded library's — the scene library above all, which on Windows
    // has globals of its own. See the block comment in gui.h.
    if (g_currentInterface == this && ImGui::GetCurrentContext() == _context) return;
    bindImGuiModules(_context);
    ImGui::SetCurrentContext(_context);
    g_currentInterface = this;
}

void kor::Interface::MakeNoneCurrent()
{
    if (!g_currentInterface && !ImGui::GetCurrentContext()) return;
    bindImGuiModules(nullptr);
    ImGui::SetCurrentContext(nullptr);
    g_currentInterface = nullptr;
}

kor::Interface::Interface(Scene& scene, const InterfaceSettings& settings) : _scene(scene)
{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    _context = ImGui::CreateContext();
    ImGui::SetCurrentContext(_context);
    bindImGuiModules(_context);
    g_currentInterface = this;

    ImGuiIO& io = ImGui::GetIO();
    if (settings.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // io.IniFilename keeps the pointer, so it points into this object. ImGui will not create missing
    // directories itself, so the parent is made before it first saves. Empty keeps the layout in memory.
    if (!settings.iniFile.empty()) {
        _iniFile = settings.iniFile.string();
        if (const auto parent = settings.iniFile.parent_path(); !parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }
        io.IniFilename = _iniFile.c_str();
    } else {
        io.IniFilename = nullptr;
    }

    // Panels dragged out into OS windows of their own. Not under Wayland: ImGui has to place those
    // windows at absolute positions, which Wayland denies every client. Set before the backends
    // initialise, which is when they install their viewport hooks.
    if (settings.viewports && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    GLFWwindow* window = *scene.SceneWindow();
    switch (Context::ActiveAPI())
    {
    case API::eVulkan:
        vk::GUI::Init(window, dynamic_cast<const vk::Surface&>(scene.SceneWindow().RenderSurface()).swapChain());
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    DefineStyle();
    scene.SceneInput().SetInterfaceContext(_context);
    interfaceContexts().push_back(_context);
    claimMonitorCallback();

    // Leave things as they were: an interface may be made from inside another scene's hooks.
    if (previous) ImGui::SetCurrentContext(previous);
}

void kor::Interface::Render(kor::CommandBuffer& commandBuffer)
{
    MakeCurrent();
    switch (Context::ActiveAPI())
    {
    case API::eVulkan:
        vk::GUI::NewFrame();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    static thread_local std::map<const Interface*, std::optional<ImVec2>> parked;
    freezePointerWhileCaptured(_scene.SceneInput(), parked[this]);

    ImGui::NewFrame();
    constexpr ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // The host window is a container for the dock space and nothing else: no title bar, no border,
    // no padding — padding would inset every docked window from the edges of the screen.
    window_flags |= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                  | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin(_scene.SceneWindow().Title().c_str(), nullptr, window_flags);
    ImGui::PopStyleVar(2);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        const ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);
    }

    _scene.RenderUI();
    // After the scene's, so a module's own panels layer over the project's interface.
    ModuleHost::RenderUI();

    ImGui::End();
    ImGui::Render();

    // Every handle ImGui may sample, brought up to date in *this* frame's commands — recorded rather
    // than submitted separately, which is what lets the engine's barriers see the read.
    std::erase_if(_images, [](const ResourceRef<GuiImage>& handle) { return !handle.Alive(); });
    for (const auto& handle : _images)
        if (handle.Valid()) const_cast<GuiImage&>(*handle).Refresh(commandBuffer);

    ImDrawData* drawData = ImGui::GetDrawData();
    switch (Context::ActiveAPI())
    {
    case API::eVulkan:
        vk::GUI::Render(commandBuffer, drawData, _scene.SceneWindow().DefaultFramebuffer());
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }
}

void kor::Interface::AttachPlatformWindows()
{
    // The windows ImGui opened for undocked panels feed the scene's input: an undocked panel is a
    // separate OS window, and without this the pointer over one reaches ImGui but never the scene,
    // so a camera driven by the mouse stops the moment its viewport is floating.
    const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    GLFWwindow* own = *_scene.SceneWindow();
    std::vector<GLFWwindow*> present;
    for (ImGuiViewport* viewport : platformIO.Viewports) {
        if (viewport->Flags & ImGuiViewportFlags_IsPlatformWindow && viewport->PlatformHandle != nullptr) {
            auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
            if (window != own) present.push_back(window);
        }
    }
    auto& input = _scene.SceneInput();
    for (auto* window : present) {
        if (std::ranges::find(_platformWindows, window) == _platformWindows.end()) {
            input.AttachTo(window);
            _platformWindows.push_back(window);
            // ImGui's own callbacks on the window, wrapped to run in this interface's context.
            PlatformWindowCallbacks callbacks{.context = _context};
            callbacks.close = glfwSetWindowCloseCallback(window, platformWindowClosed);
            callbacks.pos = glfwSetWindowPosCallback(window, platformWindowMoved);
            callbacks.size = glfwSetWindowSizeCallback(window, platformWindowResized);
            platformWindowCallbacks()[window] = callbacks;
        }
    }
    // Gone: a panel redocked or closed. Detached before ImGui destroys the window.
    std::erase_if(_platformWindows, [&](GLFWwindow* window) {
        if (std::ranges::find(present, window) != present.end()) return false;
        input.DetachFrom(window);
        platformWindowCallbacks().erase(window);
        return true;
    });
}

void kor::Interface::RenderPlatformWindows()
{
    MakeCurrent();
    const ImGuiIO& io = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;

    // After the frame's command buffer has been submitted, and that is the whole point of it being
    // separate: ImGui builds and submits a command buffer of its own for each undocked window, and
    // called from inside the recording it would sample images nothing had transitioned yet.
    GLFWwindow* backupContext = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();
    AttachPlatformWindows();
    {
        std::unique_lock<std::mutex> queueLock;
        if (Context::ActiveAPI() == API::eVulkan) queueLock = vk::Context::Device().lockQueues();
        ImGui::RenderPlatformWindowsDefault();
    }
    // Those submissions bypass the epoch every other one signals, and they draw our images. An epoch
    // marker after them covers them: nothing they used is destroyed until the GPU is past it.
    if (Context::ActiveAPI() == API::eVulkan && ImGui::GetPlatformIO().Viewports.Size > 1) {
        const auto& device = vk::Context::Device();
        device.markEpoch(device.requestQueue(::vk::QueueFlagBits::eGraphics));
    }
    glfwMakeContextCurrent(backupContext);
}

kor::Interface::~Interface()
{
    MakeCurrent();
    auto& input = _scene.SceneInput();
    for (auto* window : _platformWindows) {
        input.DetachFrom(window);
        platformWindowCallbacks().erase(window);
    }
    input.SetInterfaceContext(nullptr);
    switch (Context::ActiveAPI())
    {
    case API::eVulkan:
        vk::GUI::Shutdown();
        break;
    default:
        break;
    }
    // Take the context back from every library before destroying it, so none is left holding a
    // pointer to freed memory.
    bindImGuiModules(nullptr);
    ImGui::DestroyContext(_context);
    ImGui::SetCurrentContext(nullptr);
    g_currentInterface = nullptr;
    std::erase(interfaceContexts(), _context);
    claimMonitorCallback();
}

ImFont* kor::Interface::GetFont(const Font font) const
{
    return _fonts.at(font);
}

ImFont* kor::GUI::GetFont(const Font font)
{
    const auto* interface = Interface::Current();
    if (!interface) throw std::logic_error("GUI::GetFont outside a scene with an interface (Scene::EnableInterface)");
    return interface->GetFont(font);
}
