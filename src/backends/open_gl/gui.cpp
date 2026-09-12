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

namespace kor
{
    void ogl::GUI::Init()
    {
        ImGui_ImplGlfw_InitForOpenGL(*Context::Window(), false);
        ImGui_ImplOpenGL3_Init("#version 450");

        ImGui::StyleColorsDark();
    }

    void ogl::GUI::NewFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void ogl::GUI::Render(kor::CommandBuffer& commandBuffer, ImDrawData* draw_data)
    {
        commandBuffer.Run([draw_data](kor::CommandBuffer&)
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

    ogl::GuiImage::GuiImage(kor::ResourceRef<const kor::Image> image, const glm::u32 layer, const glm::u32 level) : _image(image)
    {
        _generation = image->generation();
        setImage(image);
        setLayerAndLevel(layer, level);
    }

    ogl::GuiImage::~GuiImage()
    {
        glDeleteTextures(1, reinterpret_cast<const GLuint*>(&_id));
        glCheckError();
    }

    void ogl::GuiImage::refresh(kor::CommandBuffer&)
    {
        // No command buffer is used: unlike Vulkan, where the blit has to be recorded into the
        // frame so the engine's barriers can see the read, GL's blit is immediate.
        if (const auto generation = _image->generation(); generation != _generation)
        {
            // The source was recreated at a new extent (@see Image::doResize). This handle's copy
            // has immutable storage at the old one, so it has to be reallocated before the blit —
            // otherwise the viewport keeps showing a copy the size the window used to be.
            _generation = generation;
            const auto layer = _layer, level = _level;
            setImage(_image);
            setLayerAndLevel(layer, level);
            return;
        }
        setLayerAndLevel(_layer, _level);
    }

    void ogl::GuiImage::setLayerAndLevel(glm::u32 layer, glm::u32 level)
    {
        _layer = layer;
        _level = level;
        const auto& oglImage = dynamic_cast<const kor::ogl::Image&>(*_image);

        GLuint srcFramebuffer, dstFramebuffer;
        glGenFramebuffers(1, &srcFramebuffer);
        glGenFramebuffers(1, &dstFramebuffer);

        glCheckError();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFramebuffer);
        if (_image->type() == kor::Image::Type::e1D && layer == 0) {
            glFramebufferTexture1D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_1D, *oglImage, level);
        } else if (_image->type() == kor::Image::Type::e1D) {
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *oglImage, layer, level);
        } else if (_image->type() == kor::Image::Type::e2D && layer == 0) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *oglImage, level);
        } else if (_image->type() == kor::Image::Type::e2D) {
            glFramebufferTexture3D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_ARRAY, *oglImage, level, layer);
        } else if (_image->type() == kor::Image::Type::e3D) {
            glFramebufferTexture3D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_3D, *oglImage, level, layer);
        }
        glCheckError();

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFramebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _id, 0);

        glCheckError();

        // Flipped vertically on the way in, for a source that was *rendered* into.
        //
        // Koral rasterizes Vulkan's Y-down clip space on GL through glClipControl(GL_UPPER_LEFT),
        // which leaves an offscreen target's rows in memory bottom-up relative to Vulkan's. A
        // render → sample → present chain never notices, because every stage is mirrored alike —
        // but ImGui is not part of that chain. It samples this handle with the same UVs it samples
        // its font atlas with, top row at v=0, so a rendered target reaches the screen upside down
        // while an uploaded texture reaches it the right way up. Undoing the mirror here, in a
        // blit that already happens, costs nothing and keeps `ImGui::Image(**handle, …)` meaning
        // the same thing on both backends. @see ogl::Scheduler::Initialize
        //
        // Which images were rendered into is read off their usage. An image only written by a
        // compute imageStore is already top-down — glClipControl moves the rasterizer, not the
        // shader — so one carrying eColorAttachment it never actually rendered with would come out
        // flipped. Sampling one of those through ImGui is not a thing any scene here does.
        const bool rendered = (_image->usage() & kor::Image::Usage::eColorAttachment)
                           || (_image->usage() & kor::Image::Usage::eDepthStencilAttachment);
        const auto width = static_cast<GLint>(oglImage.extent().x);
        const auto height = static_cast<GLint>(oglImage.extent().y);

        glBlitFramebuffer(
            0, 0, width, height,
            0, rendered ? height : 0,
            width, rendered ? 0 : height,
            GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glCheckError();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &srcFramebuffer);
        glDeleteFramebuffers(1, &dstFramebuffer);

        glCheckError();
    }

    void ogl::GuiImage::setImage(kor::ResourceRef<const kor::Image> image)
    {
        _image = image;
        const auto& oglImage = dynamic_cast<const kor::ogl::Image&>(*image);
        GLuint textureId;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexStorage2D(GL_TEXTURE_2D, 1, oglImage.getGLFormat(), image->extent().x, image->extent().y);
        glCheckError();

        if (_id != 0)
        {
            glDeleteTextures(1, reinterpret_cast<const GLuint*>(&_id));
            glCheckError();
        }
        _id = textureId;

        setLayerAndLevel(0, 0);
    }

    ImTextureID ogl::GuiImage::operator*() const {
        return _id;
    }
}
