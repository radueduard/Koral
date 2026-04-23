//
// Created by radue on 3/17/2026.
//

#include "gui.h"

#include <commandBuffer.h>

#include "context.h"
#include "window.h"
#include "framebuffer.h"
#include "surface.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <image.h>
#include "ogl_err_handling.h"

namespace gfx
{
    void ogl::GUI::Init()
    {
        ImGui_ImplGlfw_InitForOpenGL(*Context::Window(), true);
        ImGui_ImplOpenGL3_Init("#version 450");

        ImGui::StyleColorsDark();
    }

    void ogl::GUI::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void ogl::GUI::Render(gfx::CommandBuffer& commandBuffer, ImDrawData* draw_data)
    {
        commandBuffer.Run([draw_data](gfx::CommandBuffer&)
        {
            if (const bool main_is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f; !main_is_minimized) {
                ImGui_ImplOpenGL3_RenderDrawData(draw_data);
            }
        });
    }

    void ogl::GUI::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
    }

    ogl::GUI_Image::GUI_Image(const gfx::Image& image, const glm::u32 layer, const glm::u32 level) : _image(image)
    {
        setImage(image);
        setLayerAndLevel(layer, level);
    }

    ogl::GUI_Image::~GUI_Image()
    {
        glDeleteTextures(1, reinterpret_cast<const GLuint*>(&_id));
        glCheckError();
    }

    void ogl::GUI_Image::setLayerAndLevel(glm::u32 layer, glm::u32 level)
    {
        const auto& oglImage = dynamic_cast<const gfx::ogl::Image&>(_image.get());

        GLuint srcFramebuffer, dstFramebuffer;
        glGenFramebuffers(1, &srcFramebuffer);
        glGenFramebuffers(1, &dstFramebuffer);

        glCheckError();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFramebuffer);
        if (_image.get().getType() == gfx::Image::Type::e1D && layer == 0) {
            glFramebufferTexture1D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_1D, *oglImage, level);
        } else if (_image.get().getType() == gfx::Image::Type::e1D) {
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *oglImage, layer, level);
        } else if (_image.get().getType() == gfx::Image::Type::e2D && layer == 0) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *oglImage, level);
        } else if (_image.get().getType() == gfx::Image::Type::e2D) {
            glFramebufferTexture3D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_ARRAY, *oglImage, level, layer);
        } else if (_image.get().getType() == gfx::Image::Type::e3D) {
            glFramebufferTexture3D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_3D, *oglImage, level, layer);
        }
        glCheckError();

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFramebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _id, 0);

        glCheckError();

        glBlitFramebuffer(
            0, 0,
            oglImage.getExtent().x, oglImage.getExtent().y,
            0, 0,
            oglImage.getExtent().x, oglImage.getExtent().y,
            GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glCheckError();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &srcFramebuffer);
        glDeleteFramebuffers(1, &dstFramebuffer);

        glCheckError();
    }

    void ogl::GUI_Image::setImage(const gfx::Image& image)
    {
        _image = image;
        const auto& oglImage = dynamic_cast<const gfx::ogl::Image&>(image);
        GLuint textureId;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexStorage2D(GL_TEXTURE_2D, 1, oglImage.getGLFormat(), image.getExtent().x, image.getExtent().y);
        glCheckError();

        if (_id != 0)
        {
            glDeleteTextures(1, reinterpret_cast<const GLuint*>(&_id));
            glCheckError();
        }
        _id = textureId;

        setLayerAndLevel(0, 0);
    }

    ImTextureID ogl::GUI_Image::operator*() const {
        return _id;
    }
}
