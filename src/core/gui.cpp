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

#include "commandBuffer.h"

void gfx::GUI::DefineStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ChildRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.TabRounding = 0.0f;
    // do not show tab dropdown
    style.DockingSeparatorSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBarOverlineSize = 5.0f;
    style.TabBorderSize = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.DisplaySafeAreaPadding = ImVec2(11.0f, 5.0f);
    style.CellPadding = ImVec2(11.0f, 5.0f);
    style.FramePadding = ImVec2(11.0f, 5.0f);
    style.ItemSpacing = ImVec2(11.0f, 5.0f);

    ImVec4 highlight = ImVec4(static_cast<float>(0x44) / 255.0f, static_cast<float>(0x6D) / 255.0f, static_cast<float>(0xF6) / 255.0f, 1.0f);
    ImVec4 highlightHover = ImVec4(static_cast<float>(0x44) / 255.0f, static_cast<float>(0x6D) / 255.0f, static_cast<float>(0xF6) / 255.0f, 0.5f);
    ImVec4 secondary = ImVec4(static_cast<float>(0xA5) / 255.f, static_cast<float>(0xA6) / 255.f, static_cast<float>(0x1E) / 255.f, 1.0f);
    ImVec4 secondaryHover = ImVec4(static_cast<float>(0xA5) / 255.f, static_cast<float>(0xA6) / 255.f, static_cast<float>(0x1E) / 255.f, 0.5f);

    ImVec4 background = ImVec4(0.048f, 0.048f, 0.048f, 1.0f);
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

    const auto regularFontPath = gfx::assetPath("fonts/Inter_28pt-Regular.ttf");
    const auto boldFontPath = gfx::assetPath("fonts/Inter_28pt-Bold.ttf");
    const auto italicFontPath = gfx::assetPath("fonts/Inter_28pt-Italic.ttf");
    const auto blackFontPath = gfx::assetPath("fonts/Inter_28pt-Black.ttf");
    const auto lightFontPath = gfx::assetPath("fonts/Inter_28pt-Light.ttf");
    ImGuiIO& io = ImGui::GetIO();
    _fonts[Font::Regular] = io.Fonts->AddFontFromFileTTF(regularFontPath.string().c_str(), 28.0f);
    _fonts[Font::Bold] =io.Fonts->AddFontFromFileTTF(boldFontPath.string().c_str(), 32.0f);
    _fonts[Font::Italic] =io.Fonts->AddFontFromFileTTF(italicFontPath.string().c_str(), 28.0f);
    _fonts[Font::Black] =io.Fonts->AddFontFromFileTTF(blackFontPath.string().c_str(), 36.0f);
    _fonts[Font::Light] =io.Fonts->AddFontFromFileTTF(lightFontPath.string().c_str(), 26.0f);
    io.FontDefault = _fonts[Font::Regular];

    // ImGui::SetWindowFontScale(.5f);
    io.FontGlobalScale = .55f;
}

std::unique_ptr<gfx::GUI_Image> gfx::GUI_Image::Create(const gfx::Image& image, glm::u32 layer, glm::u32 level)
{
    switch (Context::Window().getAPI())
    {
    case API::eOpenGL:
        return std::make_unique<gfx::ogl::GUI_Image>(image, layer, level);
    case API::eVulkan:
        return std::make_unique<gfx::vk::GUI_Image>(image, layer, level);
    default:
        throw std::runtime_error("Unsupported graphics API");
    }
}

void gfx::GUI::Init()
{
    Context::_imguiContext = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;


    switch (Context::Window().getAPI())
    {
    case API::eOpenGL:
         ogl::GUI::Init();
        break;
    case API::eVulkan:
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        vk::GUI::Init();
        break;
    default:
        throw std::runtime_error("Unsupported graphics API");
    }

    DefineStyle();
}

void gfx::GUI::Render(gfx::CommandBuffer& commandBuffer, Scene& scene)
{
    auto* context = Context::GetCurrentImGuiContext();

    switch (Context::Window().getAPI())
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

    ImGui::SetCurrentContext(context);
    ImGui::NewFrame();
    constexpr ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    ImGui::Begin("DockSpace", nullptr, window_flags);

    const ImGuiID dockSpaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

    scene.RenderUI(context);

    ImGui::End();
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();
    switch (Context::Window().getAPI())
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
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void gfx::GUI::Shutdown()
{
    switch (Context::Window().getAPI())
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

ImFont* gfx::GUI::GetFont(const Font font)
{
    return _fonts.at(font);
}
