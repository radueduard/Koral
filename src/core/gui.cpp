//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include "context.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"

#include "../backends/open_gl/gui.h"
#include "../backends/vulkan/gui.h"

#include <imgui.h>
// #include <imguizmo.h>

#include <iostream>
#include <GLFW/glfw3.h>

#include "commandBuffer.h"
#include <IconsFontAwesome6.h>

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
    _fonts[Font::Regular] = AddFont(kor::assetPath("fonts/Inter_28pt-Regular.ttf"), 28.0f);
    _fonts[Font::Bold] = AddFont(kor::assetPath("fonts/Inter_28pt-Bold.ttf"), 32.0f);
    _fonts[Font::Italic] = AddFont(kor::assetPath("fonts/Inter_28pt-Italic.ttf"), 28.0f);
    _fonts[Font::Black] = AddFont(kor::assetPath("fonts/Inter_28pt-Black.ttf"), 36.0f);
    _fonts[Font::Light] = AddFont(kor::assetPath("fonts/Inter_28pt-Light.ttf"), 26.0f);

    io.FontDefault = _fonts[Font::Regular];
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


kor::Resource<kor::GUI_Image> kor::GUI_Image::Create(kor::ResourceRef<const kor::Image> image, glm::u32 layer, glm::u32 level)
{
    switch (Context::activeAPI())
    {
        case API::eOpenGL:
        return kor::MakeBackendResource<kor::GUI_Image, kor::ogl::GUI_Image>(image, layer, level);
    case API::eVulkan:
        return kor::MakeBackendResource<kor::GUI_Image, kor::vk::GUI_Image>(image, layer, level);
    default:
        throw std::runtime_error("Unsupported graphics API");
    }
}

void kor::GUI::Init()
{
    ImGui::CreateContext();
    // ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Point ImGui at the layout file the config resolved (by default beside koral.json). io.IniFilename
    // holds the pointer rather than copying, so it must reference storage that outlives the context —
    // the window's own string does. An empty path leaves ImGui's default (imgui.ini in the CWD) in
    // place. ImGui will not create missing directories itself, so make the parent before it saves.
    if (const std::string& iniPath = Context::Window().getImguiIniPath(); !iniPath.empty()) {
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

    ImGui::NewFrame();
    // ImGuizmo::BeginFrame();
    constexpr ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    window_flags |= ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;

    if (Context::Window().isDecorated())
    {
        window_flags |= ImGuiWindowFlags_NoTitleBar;
    }

    ImGui::PushFont(GetFont(Font::Bold));

    ImGui::Begin(Context::Window().getTitle().c_str(), nullptr, window_flags);

    if (!Context::Window().isDecorated())
    {
        ImVec2 windowPos = ImGui::GetWindowPos();
        float titleBarHeight = ImGui::GetFrameHeight();
        float buttonSize = titleBarHeight - 8.0f;
        float padding = 4.0f;
        float buttonY = windowPos.y + padding;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        struct TitleButton {
            ImVec2 min, max;
            const char* label;
            ImU32 hoverColor;
        };

        float closeX    = windowPos.x + ImGui::GetWindowWidth() - buttonSize - padding;
        float maximizeX = closeX - buttonSize - padding;
        float minimizeX = maximizeX - buttonSize - padding;

        TitleButton buttons[] = {
            { ImVec2(closeX,    buttonY), ImVec2(closeX    + buttonSize, buttonY + buttonSize), ICON_FA_X,  IM_COL32(220, 50,  50,  255) },
            { ImVec2(maximizeX, buttonY), ImVec2(maximizeX + buttonSize, buttonY + buttonSize), ICON_FA_SQUARE,  IM_COL32(80,  80,  80,  255) },
            { ImVec2(minimizeX, buttonY), ImVec2(minimizeX + buttonSize, buttonY + buttonSize), ICON_FA_MINUS, IM_COL32(80,  80,  80,  255) }
        };

        for (auto& btn : buttons)
        {
            bool isHovered = ImGui::IsMouseHoveringRect(btn.min, btn.max, false);
            bool isClicked = isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

            ImU32 bgColor = isHovered ? btn.hoverColor : IM_COL32(0, 0, 0, 0);
            drawList->AddRectFilled(btn.min, btn.max, bgColor, buttonSize * 0.5f);

            ImVec2 textSize = ImGui::CalcTextSize(btn.label);
            ImVec2 btnCenter = ImVec2(
                btn.min.x + buttonSize * 0.5f,
                btn.min.y + buttonSize * 0.5f
            );
            auto textPos = ImVec2(
                btnCenter.x - textSize.x * 0.5f + 0.5f,  // +0.5 for pixel alignment
                btnCenter.y - textSize.y * 0.5f - 0.5f
            );
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), btn.label);

            if (isClicked)
            {
                if (btn.label[0] == 'X')
                {
                    Context::Window().close();
                }
                else if (btn.label[0] == ICON_FA_SQUARE[0] && btn.label[1] == ICON_FA_SQUARE[1])
                {
                    GLFWwindow* win = *Context::Window();
                    if (glfwGetWindowAttrib(win, GLFW_MAXIMIZED))
                        glfwRestoreWindow(win);
                    else
                        glfwMaximizeWindow(win);
                }
                else if (btn.label[0] == ICON_FA_MINUS[0] && btn.label[1] == ICON_FA_MINUS[1])
                {
                    glfwIconifyWindow(*Context::Window());
                }
            }
        }
    }
    ImGui::PopFont();

    if (!Context::Window().isDecorated())
    {
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 titleBarMin = windowPos;
        const auto titleBarMax = ImVec2(windowPos.x + ImGui::GetWindowWidth(),
                                    windowPos.y + ImGui::GetFrameHeight());

        static bool isDragging = false;
        static ImVec2 dragStartMousePos;
        static ImVec2 dragStartWindowPos;

        if (bool isHoveringTitleBar = ImGui::IsMouseHoveringRect(titleBarMin, titleBarMax, false); isHoveringTitleBar && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            isDragging = true;
            dragStartMousePos = ImGui::GetMousePos();
            int wx, wy;
            glfwGetWindowPos(*Context::Window(), &wx, &wy);
            dragStartWindowPos = ImVec2(static_cast<float>(wx), static_cast<float>(wy));
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDragging = false;
        }

        if (isDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const ImVec2 currentMousePos = ImGui::GetMousePos();
            const float dx = currentMousePos.x - dragStartMousePos.x;
            const float dy = currentMousePos.y - dragStartMousePos.y;

            glfwSetWindowPos(
                *Context::Window(),
                static_cast<int>(dragStartWindowPos.x + dx),
                static_cast<int>(dragStartWindowPos.y + dy)
            );
        }
    }


    const ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

    scene.RenderUI();

    ImGui::End();
    ImGui::Render();

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

    if (const ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backup_ctx = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_ctx);
    }
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
    ImGui::DestroyContext();
}

ImFont* kor::GUI::GetFont(const Font font)
{
    return _fonts.at(font);
}
