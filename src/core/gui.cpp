//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include "context.h"
#include "scene.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"

#include "../backends/open_gl/gui.h"
#include "../backends/vulkan/gui.h"

#include <imgui.h>
// #include <imguizmo.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>
#include <GLFW/glfw3.h>
#include <map>

namespace {
    // Process-wide, and deliberately not a static data member of GUI: one copy per loaded
    // module is the bug this exists to avoid. Reached only through kor::GUI's exported
    // members, so every caller — engine or module — lands on this one.
    std::map<kor::Font, ImFont*>& fonts() {
        static std::map<kor::Font, ImFont*> instance;
        return instance;
    }
}

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
                kor::log::error("[gui] a loaded library was built against a different ImGui than Koral "
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

void kor::detail::registerImGuiModule(const ImGuiModule& imguiModule)
{
    auto& modules = imguiModules();

    // The registrar is an inline variable, so it is constructed once per module — but a module that
    // shares Koral's ImGui (every ELF/Mach-O one) reports the identical pointers, and registering
    // it twice would leave a duplicate entry to bind on every future load.
    const auto sameCopy = [&](const ImGuiModule& known) { return known.setCurrentContext == imguiModule.setCurrentContext; };
    if (std::ranges::any_of(modules, sameCopy))
        return;

    modules.push_back(imguiModule);

    // Registration normally happens while the module loads, long before there is a context to hand
    // out; GUI::Init() picks these up. A module that arrives after the GUI is up is bound here.
    if (ImGui::GetCurrentContext())
        bindImGuiModule(imguiModule, ImGui::GetCurrentContext());
}

ImFont* AddFont(const std::filesystem::path& path, const float size)
{
    const std::string iconPath = kor::assetPath(FONT_ICON_FILE_NAME_FAS).string();
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

void kor::GUI::DefineStyle()
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
    fonts()[Font::eRegular] = AddFont(kor::assetPath("fonts/Inter_28pt-Regular.ttf"), 28.0f);
    fonts()[Font::eBold] = AddFont(kor::assetPath("fonts/Inter_28pt-Bold.ttf"), 32.0f);
    fonts()[Font::eItalic] = AddFont(kor::assetPath("fonts/Inter_28pt-Italic.ttf"), 28.0f);
    fonts()[Font::eBlack] = AddFont(kor::assetPath("fonts/Inter_28pt-Black.ttf"), 36.0f);
    fonts()[Font::eLight] = AddFont(kor::assetPath("fonts/Inter_28pt-Light.ttf"), 26.0f);

    io.FontDefault = fonts()[Font::eRegular];
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
    // Every handle that has been made and not yet destroyed.
    //
    // Refs, not owners: a handle belongs to whoever created it, and one that is dropped disappears
    // from here on the next frame. Process-wide and in this translation unit rather than inline in
    // the header, so a handle created by a scene or a module lands in the same list the GUI walks.
    // @see kor::GuiImage::refresh
    std::vector<kor::ResourceRef<kor::GuiImage>>& liveImages()
    {
        static std::vector<kor::ResourceRef<kor::GuiImage>> images;
        return images;
    }

    /**
     * @brief Stops the interface chasing a pointer that is not moving.
     *
     * A captured cursor is parked: the OS pointer does not move and only the movement is reported.
     * GLFW still delivers a position, though — an ever-growing virtual one — and the ImGui backend
     * takes it at face value, so within a few frames of aiming a camera the interface believes the
     * pointer has slid off the screen. Whatever the scene keyed on hovering, including the viewport
     * that captured the mouse in the first place, then stops being hovered and hands the cursor
     * back: the capture lets go by itself, and the faster the mouse moves the sooner it happens.
     *
     * So while the cursor is captured the interface is told, once per frame, that the pointer is
     * exactly where it was when the capture began. Queued as an event rather than written to
     * io.MousePos, so ImGui's own NewFrame derives a zero delta from it as it would from any other.
     */
    void freezePointerWhileCaptured()
    {
        // Where the pointer was when the capture began; empty while nothing is captured.
        static std::optional<ImVec2> parked;

        ImGuiIO& io = ImGui::GetIO();
        if (kor::Input::cursorMode() != kor::Input::CursorMode::eCaptured) {
            parked.reset();
            return;
        }

        if (!parked) parked = io.MousePos;
        io.AddMousePosEvent(parked->x, parked->y);
    }
}

kor::Resource<kor::GuiImage> kor::GuiImage::Create(kor::ResourceRef<const kor::Image> image, glm::u32 layer, glm::u32 level)
{
    auto handle = [&] {
        switch (Context::activeAPI())
        {
        case API::eOpenGL:
            return kor::MakeBackendResource<kor::GuiImage, kor::ogl::GuiImage>(image, layer, level);
        case API::eVulkan:
            return kor::MakeBackendResource<kor::GuiImage, kor::vk::GuiImage>(image, layer, level);
        default:
            throw std::runtime_error("Unsupported graphics API");
        }
    }();

    // Registered so the GUI can bring it up to date each frame. Without this a handle shows whatever
    // its image held at this moment, for ever.
    if (handle) liveImages().emplace_back(kor::ResourceRef<kor::GuiImage>(handle));
    return handle;
}

void kor::GUI::Init()
{
    ImGui::CreateContext();
    // ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

    // Before anything else touches ImGui: hand the context and its allocators to every module that
    // registered a copy of ImGui as it loaded — the scene library above all, which on Windows has
    // its own null GImGui until this runs. See the block comment in gui.h.
    bindImGuiModules(ImGui::GetCurrentContext());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Point ImGui at the layout file the config resolved (by default beside koral.json). io.IniFilename
    // holds the pointer rather than copying, so it must reference storage that outlives the context —
    // the window's own string does. An empty path leaves ImGui's default (imgui.ini in the CWD) in
    // place. ImGui will not create missing directories itself, so make the parent before it saves.
    if (const std::string& iniPath = Context::Window().imguiIniPath(); !iniPath.empty()) {
        if (const auto parent = std::filesystem::path(iniPath).parent_path(); !parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }
        io.IniFilename = iniPath.c_str();
    }

    // Multi-viewport: let panels be dragged out of the main window into their own OS windows.
    // Enabled on every platform except Wayland, and set *before* the backend Init below so the
    // GLFW/Vulkan/GL backends install their viewport hooks (they check this flag at init time).
    //
    // Wayland is the exception, and it is not a matter of decorations or a transparent framebuffer:
    // ImGui viewports require the platform to place a window at an absolute screen position
    // (Platform_SetWindowPos), and Wayland deliberately denies clients any global coordinate — GLFW's
    // Wayland backend returns GLFW_FEATURE_UNAVAILABLE from glfwSetWindowPos (the same reason the
    // custom title-bar drag in Render() cannot move the window there). Secondary viewports would all
    // pile up wherever the compositor decides, so under Wayland they stay off and panels dock inside
    // the main window as before. Switch the window to X11/XWayland (--platform x11) to get viewports.
    const bool viewportsSupported = glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
    if (viewportsSupported)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    switch (Context::activeAPI())
    {
    case API::eOpenGL:
         ogl::GUI::Init();
        break;
    case API::eVulkan:
        vk::GUI::Init();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    DefineStyle();
}

void kor::GUI::Render(kor::CommandBuffer& commandBuffer, Scene& scene)
{
    switch (Context::activeAPI())
    {
    case API::eOpenGL:
        ogl::GUI::NewFrame();
        break;
    case API::eVulkan:
        vk::GUI::NewFrame();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    freezePointerWhileCaptured();

    ImGui::NewFrame();
    // ImGuizmo::BeginFrame();
    constexpr ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // The host window is a container for the dock space and nothing else, so it has no chrome of its
    // own: no title bar, no border, and no padding — padding here would inset every docked window from
    // the edges of the screen and leave a frame of background around the whole interface.
    //
    // No title bar even when the window is undecorated. An undecorated window used to get a *drawn*
    // one here, with its own close/maximise/minimise buttons and drag handling; that is gone, so an
    // undecorated window is exactly what it says — bare. Moving and closing it is then the
    // application's business (kor::Window::close, glfwSetWindowPos), which is where those decisions
    // belong.
    window_flags |= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                  | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin(Context::Window().title().c_str(), nullptr, window_flags);
    ImGui::PopStyleVar(2);

    const ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

    scene.RenderUI();
    // After the scene's, so a module's own panels (a camera inspector, a physics debug window)
    // layer over the project's interface rather than under it.
    ModuleHost::RenderUI();

    ImGui::End();
    ImGui::Render();

    // Every handle that ImGui may sample, brought up to date in *this* frame's commands: after the
    // interface has been built (so a handle created during RenderUI is included) and before the draws
    // that read it are recorded. Recorded rather than submitted separately, which is what lets the
    // engine's barrier resolution see the read and transition the image the handle copies from.
    //
    // Dead handles are dropped here rather than anywhere else — nothing else walks this list, and a
    // scene that creates and drops handles as it runs would otherwise grow it without bound.
    std::erase_if(liveImages(), [](const ResourceRef<GuiImage>& handle) { return !handle.alive(); });
    for (const auto& handle : liveImages()) {
        if (handle.valid()) const_cast<GuiImage&>(*handle).refresh(commandBuffer);
    }

    ImDrawData* draw_data = ImGui::GetDrawData();
    switch (Context::activeAPI())
    {
    case API::eOpenGL:
        ogl::GUI::Render(commandBuffer, draw_data);
        break;
    case API::eVulkan:
        vk::GUI::Render(commandBuffer, draw_data);
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    // The secondary platform windows are deliberately *not* rendered here. @see RenderPlatformWindows
}

namespace
{
    // Which of ImGui's windows Input has been told about, so each is attached once.
    std::vector<GLFWwindow*> g_attachedPlatformWindows;

    void attachPlatformWindowsToInput()
    {
        const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

        std::vector<GLFWwindow*> present;
        for (ImGuiViewport* viewport : platformIO.Viewports) {
            // The main viewport's window is the engine's own, already attached.
            if (viewport->Flags & ImGuiViewportFlags_IsPlatformWindow && viewport->PlatformHandle != nullptr) {
                auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
                if (window == *kor::Context::Window()) continue;
                present.push_back(window);
            }
        }

        for (auto* window : present) {
            if (std::ranges::find(g_attachedPlatformWindows, window) == g_attachedPlatformWindows.end()) {
                kor::Input::attachTo(window);
                g_attachedPlatformWindows.push_back(window);
            }
        }

        // Gone: a panel redocked or closed. Told to Input before ImGui destroys the window.
        std::erase_if(g_attachedPlatformWindows, [&present](GLFWwindow* window) {
            if (std::ranges::find(present, window) != present.end()) return false;
            kor::Input::detachFrom(window);
            return true;
        });
    }
}

void kor::GUI::RenderPlatformWindows()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;

    // Called after the frame's command buffer has been submitted, and that is the whole point of it
    // being a separate function.
    //
    // ImGui::RenderPlatformWindowsDefault() does not record into our command buffer — it builds command
    // buffers of its own, for each undocked window's own swap chain, and *submits them there and then*.
    // Called from inside Render() it therefore ran before the frame's barriers had even been emitted
    // (they are resolved at CommandBuffer::End(), after the recording callback returns), so a panel
    // floating outside the main window sampled an image that nothing had transitioned yet: the layout
    // was still eUndefined and every frame of a drag produced a validation error and a grey window.
    //
    // Nothing about a docked panel showed the problem, because a docked one is drawn by the main
    // window's own draw list — inside our command buffer, after our barriers, in order.
    GLFWwindow* backup_ctx = glfwGetCurrentContext();
    ImGui::UpdatePlatformWindows();

    // The windows ImGui just created or destroyed, handed to Input.
    //
    // An undocked panel is a *separate OS window*, and GLFW delivers events to the window they happen
    // over — so without this the pointer moving across an undocked viewport reaches ImGui (which
    // installs its own callbacks on those windows) but never reaches kor::Input, and a camera driven
    // by the mouse simply stops responding the moment its viewport is floating. Done right after
    // UpdatePlatformWindows, which is what creates and destroys them.
    attachPlatformWindowsToInput();

    ImGui::RenderPlatformWindowsDefault();
    glfwMakeContextCurrent(backup_ctx);
}

void kor::GUI::Shutdown()
{
    switch (Context::activeAPI())
    {
    case API::eOpenGL:
        ogl::GUI::Shutdown();
        break;
    case API::eVulkan:
        vk::GUI::Shutdown();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    // Take the context back before destroying it, so no module is left holding a pointer to freed
    // memory. A scene's RenderUI() is not called after shutdown, but its destructor still runs.
    // Named explicitly in the call below because clearing it includes clearing our own GImGui, and
    // the no-argument DestroyContext() destroys whatever that points at.
    ImGuiContext* context = ImGui::GetCurrentContext();
    bindImGuiModules(nullptr);
    ImGui::DestroyContext(context);
}

ImFont* kor::GUI::GetFont(const Font font)
{
    return fonts().at(font);
}
